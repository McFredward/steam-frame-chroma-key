"""Bounded original paired fixtures; no Frame connection or inferred mapping."""
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pixel_sampler_recovery as sampler

probe, recovery = sampler.probe, sampler.recovery
SESSION = "0123456789abcdef0123456789abcdef"


def fixture(width=2, height=2, levels=None):
    count = width * height
    if levels is None:
        levels = [0, 256, 512, 1023] * (count // 4)
    raw = struct.pack("<" + "H" * (count * 3 // 2),
                      *([value << 6 for value in levels] + [256 << 6, 768 << 6] * (count // 4)))
    sampled = b"".join(bytes((191, (value * 255 + 511) // 1023, 64, 255)) for value in levels)
    split = count * 2
    metadata = {"schema": recovery.SCHEMA, "format": recovery.FORMAT,
                "width": width, "height": height, "byte_length": len(raw), "immutable": True,
                "sha256": probe.digest(raw),
                "generation": {"capture_id": "owned_pair", "image_generation": 2, "acquisition_id": 1, "conversion_generation": 3},
                "planes": [{"name": "Y", "offset": 0, "length": split, "sha256": probe.digest(raw[:split])},
                           {"name": "UV", "offset": split, "length": len(raw) - split, "sha256": probe.digest(raw[split:])}],
                "conversion": {"model": "rgb_identity", "range": "itu_full", "components": {"r": "r", "g": "g", "b": "b", "a": "a"},
                               "x_chroma_offset": "midpoint", "y_chroma_offset": "midpoint", "chroma_filter": "linear"},
                "observed_vulkan_conversion": {"complete": True, "has_view_conversion": True, "format": recovery.FORMAT,
                    "force_explicit_reconstruction": 0, "model": 0, "range": 0, "components": [0, 0, 0, 0], "chroma_offsets": [1, 1], "chroma_filter": 1},
                "acquisition": {"path": "same_primary_after_distortion_renderpass_gpu_plane_copy", "owned_fence_complete": True,
                    "one_time_submit": True, "retirement_required": False, "generation_retired": False,
                    "command_generation": 4, "view_generation": 5, "queue_family": 0},
                "sampler_control": {"schema": sampler.CONTROL_SCHEMA, "file": sampler.SAMPLER_FILE,
                    "capture_id": "owned_pair", "image_generation": 2, "conversion_generation": 3, "command_generation": 4, "view_generation": 5,
                    "width": width, "height": height, "byte_length": len(sampled), "sha256": probe.digest(sampled),
                    "encoding": sampler.ENCODING, "sample_positions": sampler.POSITIONS,
                    "same_command_generation": True, "same_image_view_generation": True, "graphics_state_untouched": True,
                    "no_prior_compute_state": True, "shared_storage_capacity": count * 7, "buffer_offset": count * 3, "total_paired_bytes": count * 7},
                "source_frame_associated": False, "uv_owned": False, "passthrough_authorized": False}
    return raw, sampled, metadata


def rehash(raw, metadata):
    result = copy.deepcopy(metadata)
    result["sha256"] = probe.digest(raw)
    for plane in result["planes"]:
        plane["sha256"] = probe.digest(raw[plane["offset"]:plane["offset"] + plane["length"]])
    return result


class PairTests(unittest.TestCase):
    def test_original_components_and_gray_y_codes_are_distinct(self):
        raw, sampled, metadata = fixture()
        component, green, report = sampler.inspect_pair(raw, sampled, metadata)
        self.assertEqual(component.rgb, b"".join(sampled[i:i + 3] for i in range(0, len(sampled), 4)))
        self.assertEqual(green.rgb, bytes((0,)) * 3 + bytes((64,)) * 3 + bytes((128,)) * 3 + bytes((255,)) * 3)
        self.assertEqual(report["comparison"]["mean_absolute_error_8bit"], 0)
        self.assertEqual(report["comparison"]["exact_pixels"], 4)
        for key in ("rgb_converted", "header_recovered", "native_sampler_equivalence_proven",
                    "native_frame_association_proven", "source_frame_association_proven", "eye_placement_proven", "passthrough_authorized"):
            self.assertIs(report[key], False)
        self.assertIs(report["opaque_fallback_required"], True)
        self.assertIsNone(report["native_spatial_mapping"])

    def test_real_code_differences_are_measured_not_rejected_or_converted(self):
        raw, original, metadata = fixture()
        sampled = bytearray(original)
        sampled[5] += 1
        sampled[9] += 12
        sampled[15] = 0
        metadata["sampler_control"]["sha256"] = probe.digest(sampled)
        _, _, report = sampler.inspect_pair(raw, sampled, metadata)
        self.assertEqual(report["comparison"]["mean_absolute_error_8bit"], 3.25)
        self.assertEqual(report["comparison"]["maximum_absolute_error_8bit"], 12)
        self.assertEqual(report["comparison"]["within_one_code_pixels"], 3)
        self.assertEqual(report["comparison"]["over_one_code_pixels"], 1)
        self.assertEqual(report["nonopaque_sampler_alpha_pixels"], 1)

    def test_one_time_generation_may_have_already_retired_before_background_read(self):
        raw, sampled, metadata = fixture()
        metadata["acquisition"]["generation_retired"] = True
        self.assertTrue(sampler.inspect_pair(raw, sampled, metadata)[2]["generation_retired"])

    def test_reusable_generation_requires_actual_retirement_and_fence(self):
        raw, sampled, metadata = fixture()
        metadata["acquisition"].update(one_time_submit=False, retirement_required=True, generation_retired=True)
        report = sampler.inspect_pair(raw, sampled, metadata)[2]
        self.assertTrue(report["pair_valid"])
        self.assertTrue(report["generation_retired"])
        self.assertFalse(report["one_time_submit"])
        self.assertTrue(report["retirement_required"])
        self.assertFalse(report["native_frame_association_proven"])

    def test_incomplete_or_nonboolean_reusable_retirement_refuses(self):
        raw, sampled, metadata = fixture()
        metadata["acquisition"].update(one_time_submit=False, retirement_required=True, generation_retired=True)
        for field, value in (("owned_fence_complete", False), ("retirement_required", False),
                             ("retirement_required", None), ("generation_retired", False),
                             ("generation_retired", None), ("generation_retired", 1),
                             ("retirement_required", 1), ("one_time_submit", 0)):
            changed = copy.deepcopy(metadata)
            changed["acquisition"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(probe.ProbeError):
                sampler.inspect_pair(raw, sampled, changed)

    def test_wrong_raw_or_sampler_size_hash_plane_or_padding_refuses(self):
        raw, sampled, metadata = fixture()
        cases = [(raw[:-2], sampled, metadata), (raw, sampled[:-1], metadata), (raw, sampled + b"\0", metadata)]
        for field in ("sha256", "byte_length", "buffer_offset", "total_paired_bytes", "shared_storage_capacity"):
            changed = copy.deepcopy(metadata)
            changed["sampler_control"][field] = "0" * 64 if field == "sha256" else 0
            cases.append((raw, sampled, changed))
        changed = copy.deepcopy(metadata)
        changed["planes"][1]["sha256"] = "0" * 64
        cases.append((raw, sampled, changed))
        bad_padding = bytes((raw[0] | 1,)) + raw[1:]
        cases.append((bad_padding, sampled, rehash(bad_padding, metadata)))
        for index, case in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(probe.ProbeError):
                sampler.inspect_pair(*case)

    def test_wrong_capture_generation_extent_and_schema_refuse(self):
        raw, sampled, metadata = fixture()
        fields = {"schema": "future-v2", "file": "other.rgba8", "capture_id": "older_capture",
                  "image_generation": 7, "conversion_generation": 8, "command_generation": 9,
                  "view_generation": 10, "width": 4, "height": True, "encoding": "true_scene_RGB",
                  "sample_positions": "latest approximate timestamp", "same_command_generation": False,
                  "same_image_view_generation": False, "graphics_state_untouched": False, "no_prior_compute_state": False}
        for field, value in fields.items():
            changed = copy.deepcopy(metadata)
            changed["sampler_control"][field] = value
            with self.subTest(field=field), self.assertRaises(probe.ProbeError):
                sampler.inspect_pair(raw, sampled, changed)
        changed = copy.deepcopy(metadata)
        changed["sampler_control"]["timestamp"] = 12345
        with self.assertRaisesRegex(probe.ProbeError, "timestamps"):
            sampler.inspect_pair(raw, sampled, changed)
        del metadata["sampler_control"]
        with self.assertRaisesRegex(probe.ProbeError, "schema"):
            sampler.inspect_pair(raw, sampled, metadata)

    def test_unsafe_acquisition_or_untrusted_numeric_profile_refuses(self):
        raw, sampled, metadata = fixture()
        cases = []
        for field, value in (("owned_fence_complete", False), ("one_time_submit", False), ("retirement_required", True),
                             ("generation_retired", 1), ("command_generation", 0), ("view_generation", True)):
            changed = copy.deepcopy(metadata)
            changed["acquisition"][field] = value
            cases.append(changed)
        for field, value in (("model", False), ("force_explicit_reconstruction", False), ("components", [False, 0, 0, 0]),
                             ("has_view_conversion", False), ("chroma_offsets", [0, 0]), ("complete", 1)):
            changed = copy.deepcopy(metadata)
            changed["observed_vulkan_conversion"][field] = value
            cases.append(changed)
        for field, value in (("schema", "different"), ("source_frame_associated", True), ("uv_owned", True),
                             ("passthrough_authorized", True), ("width", 8192), ("height", 8192)):
            changed = copy.deepcopy(metadata)
            changed[field] = value
            cases.append(changed)
        changed = copy.deepcopy(metadata)
        changed["conversion"]["model"] = "ycbcr_709"
        cases.append(changed)
        changed = copy.deepcopy(metadata)
        changed["width"] = changed["height"] = 8192
        cases.append(changed)
        changed = copy.deepcopy(metadata)
        changed["sampler_control"]["shared_storage_capacity"] = recovery.MAX_RAW_BYTES + 1
        cases.append(changed)
        for index, changed in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(probe.ProbeError):
                sampler.inspect_pair(raw, sampled, changed)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="frame-sampler-recovery-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        raw, sampled, metadata = fixture()
        self.write(raw, sampled, metadata)

    def write(self, raw, sampled, metadata):
        (self.path / "snapshot.p010").write_bytes(raw)
        (self.path / sampler.SAMPLER_FILE).write_bytes(sampled)
        (self.path / "snapshot.json").write_text(json.dumps(metadata))

    def args(self):
        return ["--snapshot", str(self.path / "snapshot.p010"), "--sampler", str(self.path / sampler.SAMPLER_FILE),
                "--metadata", str(self.path / "snapshot.json"), "--png", str(self.path / "components.png"),
                "--green-png", str(self.path / "green.png"), "--record", str(self.path / "report.json")]

    def run_cli(self, extra=()):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return sampler.main(self.args() + list(extra))

    def test_cli_writes_two_explicit_component_previews_and_report(self):
        self.assertEqual(self.run_cli(), 0)
        report = json.loads((self.path / "report.json").read_text())
        self.assertTrue(report["pair_valid"])
        for filename, field in (("components.png", "png_sha256"), ("green.png", "green_png_sha256")):
            content = (self.path / filename).read_bytes()
            self.assertEqual(probe.digest(content), report[field])
            self.assertEqual(probe.decode_png(content).width, 2)
        self.assertFalse(report["passthrough_authorized"])

    def test_output_overwrite_and_output_alias_refuse_before_writing(self):
        existing = self.path / "report.json"
        existing.write_bytes(b"keep")
        self.assertEqual(self.run_cli(), 2)
        self.assertEqual(existing.read_bytes(), b"keep")
        self.assertFalse((self.path / "components.png").exists())
        existing.unlink()
        args = self.args()
        args[-1] = str(self.path / "components.png")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(sampler.main(args), 2)
        self.assertFalse((self.path / "components.png").exists())

    def test_symlink_inputs_output_and_parent_refuse(self):
        raw = self.path / "snapshot.p010"
        original = self.path / "original.p010"
        raw.rename(original)
        raw.symlink_to(original)
        self.assertEqual(self.run_cli(), 2)
        raw.unlink()
        original.rename(raw)
        outside = self.path / "outside"
        outside.write_bytes(b"keep")
        output = self.path / "components.png"
        output.symlink_to(outside)
        self.assertEqual(self.run_cli(), 2)
        self.assertEqual(outside.read_bytes(), b"keep")
        output.unlink()
        folder = self.path / "real"
        folder.mkdir()
        link = self.path / "linked"
        link.symlink_to(folder, target_is_directory=True)
        args = self.args()
        args[args.index("--png") + 1] = str(link / "components.png")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(sampler.main(args), 2)
        self.assertFalse((folder / "components.png").exists())

    def test_duplicate_nonfinite_json_or_partial_header_group_refuses(self):
        metadata = self.path / "snapshot.json"
        for text in ('{"width":2,"width":2}', '{"width":NaN}'):
            metadata.write_text(text)
            self.assertEqual(self.run_cli(), 2)
        self.write(*fixture())
        self.assertEqual(self.run_cli(("--expected-eye", "left")), 2)

    def test_all_three_headers_recover_only_with_explicit_originals_and_mapping(self):
        color, coverage = probe.fixture_sources(48, 40, "left")
        reference, manifest = probe.render(48, 40, color, coverage, SESSION, 7, "left")
        levels = [(value * 1023 + 127) // 255 for value in reference.rgb[1::3]]
        self.write(*fixture(reference.width, reference.height, levels))
        (self.path / "source.png").write_bytes(probe.encode_png(reference))
        (self.path / "source.json").write_text(json.dumps(manifest))
        mapping = {"schema": recovery.MAP_SCHEMA, "kind": "axis_aligned_pixel_centers",
                   "source_canvas": [reference.width, reference.height], "receiver_extent": [reference.width, reference.height],
                   "scale": [[1, 1], [1, 1]], "offset": [[0, 1], [0, 1]],
                   "recorded_provenance": "original synthetic source coordinate fixture; no native mapping"}
        (self.path / "mapping.json").write_text(json.dumps(mapping))
        extra = ["--reference-png", str(self.path / "source.png"), "--source-manifest", str(self.path / "source.json"),
                 "--mapping", str(self.path / "mapping.json"), "--expected-session", SESSION,
                 "--expected-content-id", "7", "--expected-eye", "left"]
        self.assertEqual(self.run_cli(extra), 0)
        report = json.loads((self.path / "report.json").read_text())
        self.assertTrue(report["header_recovered"])
        self.assertEqual(report["header_recovery"]["header_copies_verified"], 3)
        self.assertFalse(report["native_frame_association_proven"])
        self.assertFalse(report["header_recovery"]["native_frame_association_proven"])
        self.assertFalse(report["passthrough_authorized"])
        for file in ("components.png", "green.png", "report.json"):
            (self.path / file).unlink()
        extra[-1] = "right"
        self.assertEqual(self.run_cli(extra), 2)
        self.assertFalse(json.loads((self.path / "report.json").read_text())["header_recovered"])


if __name__ == "__main__":
    unittest.main()
