"""Owned offline fixtures; no headset, native receiver, network or image scraping."""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

TOOLS = Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("pixel_payload_recovery", TOOLS / "pixel_payload_recovery.py")
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)
probe = recovery.probe
SESSION = "0123456789abcdef0123456789abcdef"


def snapshot(width=2, height=2, y=64, cb=512, cr=512, model="ycbcr_709", range_="itu_narrow"):
    raw = struct.pack("<" + "H" * (width * height * 3 // 2),
                      *([y << 6] * (width * height)
                        + [cb << 6, cr << 6] * (width * height // 4)))
    y_size = width * height * 2
    metadata = {"schema": recovery.SCHEMA, "format": recovery.FORMAT,
                "width": width, "height": height, "byte_length": len(raw),
                "immutable": True, "sha256": probe.digest(raw),
                "generation": {"capture_id": "owned_fixture", "image_generation": 1,
                               "acquisition_id": 0, "conversion_generation": 1},
                "planes": [{"name": "Y", "offset": 0, "length": y_size, "sha256": probe.digest(raw[:y_size])},
                           {"name": "UV", "offset": y_size, "length": len(raw) - y_size,
                            "sha256": probe.digest(raw[y_size:])}],
                "conversion": {"model": model, "range": range_, "x_chroma_offset": "midpoint",
                               "y_chroma_offset": "midpoint", "chroma_filter": "linear",
                               "components": {"r": "r", "g": "g", "b": "b", "a": "a"}}}
    return raw, metadata


def rehash(raw, metadata):
    metadata = copy.deepcopy(metadata)
    metadata["sha256"] = probe.digest(raw)
    for plane in metadata["planes"]:
        plane["sha256"] = probe.digest(raw[plane["offset"]:plane["offset"] + plane["length"]])
    return metadata


class P010ConversionTests(unittest.TestCase):
    def assertOpaque(self, report):
        self.assertFalse(report["native_frame_association_proven"])
        self.assertIsNone(report["native_spatial_mapping"])
        self.assertTrue(report["opaque_fallback_required"])
        self.assertFalse(report["passthrough_authorized"])

    def test_bt709_narrow_grayscale_known_codes(self):
        for y, level in ((0, 0), (64, 0), (283, 64), (502, 128), (721, 191), (940, 255), (1023, 255)):
            with self.subTest(y=y):
                image, report = recovery.convert_p010(*snapshot(y=y))
                self.assertEqual(image.rgb, bytes((level,)) * 12)
                self.assertTrue(report["rgb_converted"])
                self.assertOpaque(report)

    def test_bt709_full_grayscale_known_codes(self):
        for y, level in ((0, 0), (256, 64), (512, 128), (767, 191), (1023, 255)):
            with self.subTest(y=y):
                image, report = recovery.convert_p010(*snapshot(y=y, range_="itu_full"))
                self.assertEqual(image.rgb, bytes((level,)) * 12)
                self.assertOpaque(report)

    def test_bt709_narrow_chroma_known_red_green_blue(self):
        # BT.709 10-bit narrow encodings rounded to the nearest stored code.
        for codes, wanted in (((250, 409, 960), (255, 0, 0)),
                              ((691, 167, 105), (0, 255, 0)),
                              ((127, 960, 471), (0, 0, 255))):
            with self.subTest(codes=codes):
                image, _ = recovery.convert_p010(*snapshot(y=codes[0], cb=codes[1], cr=codes[2]))
                self.assertEqual(tuple(image.rgb[:3]), wanted)

    def test_bt709_full_known_red_and_blue(self):
        for codes, wanted in (((217, 395, 1023), (255, 0, 0)),
                              ((74, 1023, 465), (0, 0, 255))):
            with self.subTest(codes=codes):
                image, _ = recovery.convert_p010(*snapshot(y=codes[0], cb=codes[1], cr=codes[2], range_="itu_full"))
                self.assertEqual(tuple(image.rgb[:3]), wanted)

    def test_models_and_explicit_filter_are_not_ignored(self):
        colors = [recovery.convert_p010(*snapshot(y=500, cb=700, cr=300, model=model))[0].rgb
                  for model in ("ycbcr_709", "ycbcr_601", "ycbcr_2020")]
        self.assertEqual(len(set(colors)), 3)
        raw, metadata = snapshot(width=4, height=4, y=500)
        values = [500 << 6] * 16 + [64 << 6, 512 << 6, 940 << 6, 512 << 6] * 2
        raw = struct.pack("<" + "H" * len(values), *values)
        metadata = rehash(raw, metadata)
        linear, _ = recovery.convert_p010(raw, metadata)
        metadata["conversion"]["chroma_filter"] = "nearest"
        nearest, _ = recovery.convert_p010(raw, metadata)
        self.assertNotEqual(linear.rgb, nearest.rgb)
        metadata["conversion"]["chroma_filter"] = "linear"
        metadata["conversion"]["x_chroma_offset"] = "cosited_even"
        cosited, _ = recovery.convert_p010(raw, metadata)
        self.assertNotEqual(linear.rgb, cosited.rgb)

    def test_unknown_or_missing_conversion_retains_raw_provenance_only(self):
        for field, value in (("model", "unknown"), ("model", []), ("range", None), ("components", {}),
                              ("x_chroma_offset", None), ("y_chroma_offset", "unknown"),
                              ("chroma_filter", "cubic")):
            raw, metadata = snapshot()
            metadata["conversion"][field] = value
            with self.subTest(field=field):
                image, report = recovery.convert_p010(raw, metadata)
                self.assertIsNone(image)
                self.assertTrue(report["raw_snapshot_valid"])
                self.assertFalse(report["rgb_converted"])
                self.assertEqual(report["snapshot_sha256"], probe.digest(raw))
                self.assertOpaque(report)
        raw, metadata = snapshot()
        del metadata["conversion"]
        self.assertIsNone(recovery.convert_p010(raw, metadata)[0])

    def test_explicit_stored_y_diagnostic_does_not_guess_native_rgb_identity_shader(self):
        raw, metadata = snapshot(y=512)
        metadata["conversion"]["model"] = "rgb_identity"
        self.assertIsNone(recovery.convert_p010(raw, metadata)[0])
        image, report = recovery.convert_p010(raw, metadata, luma_only=True)
        self.assertEqual(image.rgb, bytes((128,)) * 12)
        self.assertFalse(report["rgb_converted"])
        self.assertTrue(report["color_conversion_unknown"])
        self.assertFalse(report["color_conversion_applied"])
        self.assertTrue(report["luma_diagnostic_generated"])
        self.assertOpaque(report)
        for y, expected in ((0, 0), (64, 16), (940, 234), (1023, 255)):
            with self.subTest(y=y):
                image, _ = recovery.convert_p010(*snapshot(y=y), luma_only=True)
                self.assertEqual(image.rgb, bytes((expected,)) * 12)

    def test_bad_extent_format_generation_padding_or_digests_refuse(self):
        for field, value in (("width", 3), ("height", 8194), ("width", True), ("format", 1),
                              ("immutable", False), ("byte_length", 0), ("sha256", "0" * 64),
                              ("generation", {}), ("planes", [])):
            raw, metadata = snapshot()
            metadata[field] = value
            with self.subTest(field=field), self.assertRaises(probe.ProbeError):
                recovery.convert_p010(raw, metadata)
        raw, metadata = snapshot()
        raw = bytes((raw[0] | 1,)) + raw[1:]
        with self.assertRaisesRegex(probe.ProbeError, "padding"):
            recovery.convert_p010(raw, rehash(raw, metadata))
        raw, metadata = snapshot()
        for field, value in (("offset", 1), ("length", 2), ("sha256", "0" * 64), ("name", "VU")):
            changed = copy.deepcopy(metadata)
            changed["planes"][1][field] = value
            with self.subTest(field=field), self.assertRaises(probe.ProbeError):
                recovery.convert_p010(raw, changed)
        for invalid in (raw[:-2], raw + b"\0\0"):
            with self.assertRaises(probe.ProbeError):
                recovery.convert_p010(invalid, metadata)


class MappedHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        color, coverage = probe.fixture_sources(48, 40, "left")
        cls.reference, cls.manifest = probe.render(48, 40, color, coverage, SESSION, 7, "left")
        cls.mapping = {"schema": recovery.MAP_SCHEMA, "kind": "axis_aligned_pixel_centers",
                       "source_canvas": [cls.reference.width, cls.reference.height],
                       "receiver_extent": [cls.reference.width, cls.reference.height],
                       "scale": [[1, 1], [1, 1]], "offset": [[0, 1], [0, 1]],
                       "recorded_provenance": "owned test fixture, no native mapping claim"}

    def recover(self, **change):
        args = dict(candidate=self.reference, reference=self.reference, manifest=self.manifest,
                    mapping=self.mapping, session=SESSION, content_id=7, eye="left")
        args.update(change)
        return recovery.recover_header(**args)

    def test_all_three_exact_headers_recover_without_native_authority(self):
        report = self.recover()
        self.assertTrue(report["header_recovered"])
        self.assertEqual(report["header_copies_verified"], 3)
        self.assertFalse(report["plane_fidelity_measured"])
        self.assertFalse(report["native_frame_association_proven"])
        self.assertTrue(report["opaque_fallback_required"])
        self.assertFalse(report["passthrough_authorized"])

    def test_explicit_upscale_and_crop_offset_sample_headers_only(self):
        source = self.reference
        width, height = source.width * 2 + 8, source.height * 2 + 6
        rgb = bytearray(width * height * 3)
        for y in range(source.height * 2):
            for x in range(source.width * 2):
                i = ((y + 2) * width + x + 3) * 3
                j = ((y // 2) * source.width + x // 2) * 3
                rgb[i:i + 3] = source.rgb[j:j + 3]
        image = probe.Image(width, height, bytes(rgb))
        mapping = dict(self.mapping, receiver_extent=[width, height],
                       scale=[[2, 1], [2, 1]], offset=[[3, 1], [2, 1]])
        self.assertTrue(self.recover(candidate=image, mapping=mapping)["header_recovered"])
        # Existing original measurement validation is intentionally untouched.
        report = probe.measure_candidate(image, source, SESSION, 7, "left", (48, 40),
                                         (self.manifest["color_sha256"], self.manifest["coverage_sha256"]))
        self.assertFalse(report["measurement_valid"])

    def test_each_header_copy_must_agree_and_all_copy_corruption_must_pass_crc(self):
        for copies in ((0,), (0, 1, 2)):
            data = bytearray(self.reference.rgb)
            for copy in copies:
                y0 = probe.MARGIN + copy * (probe.HEADER_HEIGHT + 8)
                for y in range(y0, y0 + probe.CELL):
                    for x in range(probe.MARGIN, probe.MARGIN + probe.CELL * 2):
                        pos = (y * self.reference.width + x) * 3
                        data[pos:pos + 3] = bytes(255 - value for value in data[pos:pos + 3])
            report = self.recover(candidate=probe.Image(self.reference.width, self.reference.height, bytes(data)))
            self.assertFalse(report["header_recovered"])
            self.assertIn("disagreement" if len(copies) == 1 else "CRC", report["reason"])

    def test_wrong_expected_identity_exact_manifest_or_trusted_reference_refuses(self):
        for change in (dict(eye="right"), dict(content_id=8), dict(session="f" * 32),
                       dict(manifest=dict(self.manifest, canvas_rgb_sha256="0" * 64)),
                       dict(reference=probe.Image(2, 2, b"\0" * 12))):
            with self.subTest(change=change):
                self.assertFalse(self.recover(**change)["header_recovered"])
        color, coverage = probe.fixture_sources(48, 40, "right")
        other, _ = probe.render(48, 40, color, coverage, SESSION, 7, "right")
        rejected = self.recover(candidate=other)
        self.assertFalse(rejected["header_recovered"])
        self.assertIn("trusted source identity", rejected["reason"])

    def test_unsupported_ambiguous_or_clipped_mapping_refuses(self):
        for field, value in (("kind", "foveated"), ("scale", [[-1, 1], [1, 1]]),
                              ("scale", [[1, 20], [1, 1]]), ("scale", [[1, 0], [1, 1]]),
                              ("offset", [[-5000, 1], [0, 1]]), ("recorded_provenance", ""),
                              ("scale", [1.0, 1.0]), ("receiver_extent", [1, 1])):
            with self.subTest(field=field):
                self.assertFalse(self.recover(mapping=dict(self.mapping, **{field: value}))["header_recovered"])
        self.assertFalse(self.recover(mapping=dict(self.mapping, nonlinear=True))["header_recovered"])


class SafeCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="frame-pixel-recovery-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.raw, self.metadata = snapshot()
        (self.root / "snapshot.raw").write_bytes(self.raw)
        (self.root / "metadata.json").write_text(json.dumps(self.metadata))
        self.args = ["--snapshot", str(self.root / "snapshot.raw"), "--metadata", str(self.root / "metadata.json"),
                     "--png", str(self.root / "out.png"), "--record", str(self.root / "out.json")]

    def run_cli(self, args=None):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return recovery.main(self.args if args is None else args)

    def test_cli_converts_png_and_verifiable_provenance_without_overwrite(self):
        self.assertEqual(self.run_cli(), 0)
        png = (self.root / "out.png").read_bytes()
        report = json.loads((self.root / "out.json").read_text())
        self.assertEqual(probe.decode_png(png), probe.Image(2, 2, b"\0" * 12))
        self.assertEqual(report["png_sha256"], probe.digest(png))
        self.assertEqual(self.run_cli(), 2)
        self.assertEqual((self.root / "out.png").read_bytes(), png)

    def test_cli_unknown_conversion_emits_raw_report_without_any_png(self):
        del self.metadata["conversion"]
        (self.root / "metadata.json").write_text(json.dumps(self.metadata))
        self.assertEqual(self.run_cli(), 2)
        self.assertFalse((self.root / "out.png").exists())
        report = json.loads((self.root / "out.json").read_text())
        self.assertTrue(report["raw_snapshot_valid"])
        self.assertFalse(report["rgb_converted"])
        self.assertIn("missing", report["reason"])

    def test_cli_stored_y_diagnostic_retains_unknown_color_and_no_rgb_conversion_claim(self):
        self.metadata["conversion"]["model"] = "rgb_identity"
        (self.root / "metadata.json").write_text(json.dumps(self.metadata))
        self.assertEqual(self.run_cli(self.args + ["--luma-diagnostic"]), 0)
        report = json.loads((self.root / "out.json").read_text())
        self.assertTrue(report["luma_diagnostic_generated"])
        self.assertFalse(report["rgb_converted"])
        self.assertTrue(report["color_conversion_unknown"])
        self.assertTrue((self.root / "out.png").is_file())

    def test_cli_end_to_end_stored_y_header_recovery_requires_explicit_expected_identity(self):
        color, coverage = probe.fixture_sources(48, 40, "left")
        reference, manifest = probe.render(48, 40, color, coverage, SESSION, 7, "left")
        _, metadata = snapshot(width=reference.width, height=reference.height)
        values = [((value * 876 + 127) // 255 + 64) << 6 for value in reference.rgb[::3]]
        values += [512 << 6] * (reference.width * reference.height // 2)
        raw = struct.pack("<" + "H" * len(values), *values)
        metadata = rehash(raw, metadata)
        metadata["conversion"]["model"] = "rgb_identity"
        mapping = {"schema": recovery.MAP_SCHEMA, "kind": "axis_aligned_pixel_centers",
                   "source_canvas": [reference.width, reference.height],
                   "receiver_extent": [reference.width, reference.height],
                   "scale": [[1, 1], [1, 1]], "offset": [[0, 1], [0, 1]],
                   "recorded_provenance": "owned end-to-end fixture only"}
        (self.root / "snapshot.raw").write_bytes(raw)
        (self.root / "metadata.json").write_text(json.dumps(metadata))
        (self.root / "source.json").write_text(json.dumps(manifest))
        (self.root / "source.png").write_bytes(probe.encode_png(reference))
        (self.root / "mapping.json").write_text(json.dumps(mapping))
        args = self.args + ["--luma-diagnostic", "--source-manifest", str(self.root / "source.json"),
                            "--reference-png", str(self.root / "source.png"),
                            "--mapping", str(self.root / "mapping.json"), "--expected-session", SESSION,
                            "--expected-content-id", "7", "--expected-eye", "left"]
        self.assertEqual(self.run_cli(args), 0)
        report = json.loads((self.root / "out.json").read_text())
        self.assertTrue(report["header_recovered"])
        self.assertFalse(report["rgb_converted"])
        self.assertTrue(report["color_conversion_unknown"])
        self.assertFalse(report["source_frame_association_proven"])
        self.assertFalse(report["private_resource_association_proven"])
        self.assertFalse(report["header_recovery"]["plane_fidelity_measured"])

    def test_cli_rejects_partial_recovery_arguments(self):
        self.assertEqual(self.run_cli(self.args + ["--expected-eye", "left"]), 2)
        self.assertFalse((self.root / "out.png").exists())

    def test_inputs_and_outputs_cannot_follow_links_in_any_component(self):
        (self.root / "link").symlink_to(self.root / "snapshot.raw")
        (self.root / "parent").symlink_to(self.root, target_is_directory=True)
        for path in (self.root / "link", self.root / "parent/snapshot.raw"):
            with self.subTest(path=path), self.assertRaises(OSError):
                recovery.read_owned(path, 100)
        for path in (self.root / "link", self.root / "parent/new"):
            with self.subTest(path=path), self.assertRaises(OSError):
                recovery.write_owned(path, b"new")
        self.assertEqual((self.root / "snapshot.raw").read_bytes(), self.raw)

    def test_read_rejects_fifo_bounded_size_and_json_duplicate_or_nonfinite(self):
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        for path, limit in ((fifo, 100), (self.root / "snapshot.raw", 1)):
            with self.assertRaises(probe.ProbeError):
                recovery.read_owned(path, limit)
        for text in ('{"width":2,"width":4}', '{"width":NaN}', '[]'):
            (self.root / "bad.json").write_text(text)
            with self.assertRaises(probe.ProbeError):
                recovery.load_json(self.root / "bad.json")


if __name__ == "__main__":
    unittest.main()
