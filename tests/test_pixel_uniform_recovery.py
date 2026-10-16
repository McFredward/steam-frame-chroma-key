"""Original bounded UBO fixtures; no Valve instructions, device or pixel scan."""
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
import pixel_uniform_recovery as uniform

probe = uniform.probe


def fixture(eye=1):
    pixels = 4
    raw, sampled = b"\0" * (pixels * 3), b"\0\0\0\xff" * pixels
    pair = {"schema": uniform.sampler.CONTROL_SCHEMA, "file": uniform.sampler.SAMPLER_FILE,
        "capture_id": "owned_uniform", "image_generation": 2, "conversion_generation": 3,
        "command_generation": 4, "view_generation": 5, "width": 2, "height": 2,
        "byte_length": len(sampled), "sha256": probe.digest(sampled),
        "encoding": uniform.sampler.ENCODING, "sample_positions": uniform.sampler.POSITIONS,
        "same_command_generation": True, "same_image_view_generation": True,
        "graphics_state_untouched": True, "no_prior_compute_state": True,
        "shared_storage_capacity": 65536, "buffer_offset": pixels * 3, "total_paired_bytes": pixels * 7}
    metadata = {"schema": uniform.recovery.SCHEMA, "format": uniform.recovery.FORMAT,
        "width": 2, "height": 2, "byte_length": len(raw), "sha256": probe.digest(raw), "immutable": True,
        "generation": {"capture_id": "owned_uniform", "image_generation": 2, "conversion_generation": 3, "acquisition_id": 1},
        "acquisition": {"path": "same_primary_after_distortion_renderpass_gpu_plane_copy", "owned_fence_complete": True,
            "one_time_submit": False, "retirement_required": True, "generation_retired": True,
            "command_generation": 4, "view_generation": 5, "queue_family": 0},
        "planes": [{"name": "Y", "offset": 0, "length": 8, "sha256": probe.digest(raw[:8])},
                   {"name": "UV", "offset": 8, "length": 4, "sha256": probe.digest(raw[8:])}],
        "conversion": {"model": "rgb_identity", "range": "itu_full", "components": {"r": "r", "g": "g", "b": "b", "a": "a"},
            "x_chroma_offset": "midpoint", "y_chroma_offset": "midpoint", "chroma_filter": "linear"},
        "observed_vulkan_conversion": {"complete": True, "has_view_conversion": True, "format": uniform.recovery.FORMAT,
            "force_explicit_reconstruction": 0, "model": 0, "range": 0, "components": [0, 0, 0, 0], "chroma_offsets": [1, 1], "chroma_filter": 1},
        "source_frame_associated": False, "uv_owned": False, "passthrough_authorized": False, "sampler_control": pair}
    contents = [bytearray(size) for size in (272, 2800, 400, 272, 2800)]
    for index in (0, 3):
        struct.pack_into("<I", contents[index], 256, eye)
    struct.pack_into("<I", contents[0], 264, 0x12345678)
    struct.pack_into("<I", contents[1], 0, 0xffffffff)  # May select fallback; never infer selectedK.
    struct.pack_into("<III", contents[2], 384, 2, 7, 0x3f800000)
    for selector in (0, 1):
        # Deliberately asymmetric columns; the flags word is a NaN FLOAT bit
        # pattern which the native code treats as UINT, so it must stay raw.
        values = [.1, .11 + selector / 100, 8., 0., .2, .22 + selector / 100, 9., 0.,
                  .8, .77 + selector / 100, 10., .31, .9, .88 + selector / 100, 11., .42]
        base = 432 + 448 * selector
        struct.pack_into("<16f", contents[4], base, *values)
        struct.pack_into("<I", contents[4], base + 12, 0x7fc00001)
    slices, offset = [], 0
    for index, (binding, content) in enumerate(zip(uniform.BINDINGS, contents)):
        size = len(content)
        slices.append({"binding": binding, "descriptor_type": 6, "descriptor_stages": 0x7fffffff,
            "descriptor_revision": 20 + index, "buffer_handle": f"{100 + index:x}", "buffer_generation": 30 + index,
            "buffer_size": size + 256, "buffer_usage": 19, "allocation_handle": f"{200 + index:x}",
            "allocation_generation": 40 + index, "allocation_size": size + 512, "memory_offset": 128,
            "descriptor_offset": 256, "descriptor_range": size, "dynamic_offset": 0,
            "effective_offset": 256, "effective_range": size, "file_offset": offset,
            "byte_length": size, "sha256": probe.digest(content)})
        offset += size
    data = b"".join(contents)
    metadata["native_uniform_control"] = {"schema": uniform.SCHEMA, "file": uniform.FILE,
        "capture_id": "owned_uniform", "command_generation": 4, "image_generation": 2, "view_generation": 5,
        "pipeline": {"handle": "abcd", "generation": 9, "layout_handle": "abcd1", "layout_generation": 10},
        "stages": [{"stage": stage, "module": f"{300 + i:x}", "generation": 50 + i,
                    "fnv1a64": fnv, "bytes": size, "entry": entry}
                   for i, (stage, fnv, size, entry) in enumerate(uniform.PROFILE)],
        "selected_draw_ordinal": 2,
        "set": {"handle": "abcd2", "generation": 11, "layout_handle": "abcd3", "layout_generation": 12,
                "revision": 100, "command_binding_revision": 101},
        "reobserved_set_revision": 100, "reobserved_command_binding_revision": 101,
        "byte_length": len(data), "sha256": probe.digest(data), "staging_offset": 28,
        "shared_storage_capacity": pair["shared_storage_capacity"],
        "guards": dict.fromkeys(uniform.GUARDS, True), "slices": slices}
    return data, metadata


def rehash(data, metadata):
    metadata = copy.deepcopy(metadata)
    control = metadata["native_uniform_control"]
    control["sha256"] = probe.digest(data)
    for item in control["slices"]:
        item["sha256"] = probe.digest(data[item["file_offset"]:item["file_offset"] + item["byte_length"]])
    return metadata


class UniformTests(unittest.TestCase):
    def test_asymmetric_original_columns_and_raw_flags_have_no_mapping_authority(self):
        for selector in (0, 1):
            data, metadata = fixture(selector)
            report = uniform.inspect_uniforms(data, metadata, "owned_uniform")
            matrix = report["fragment_record_zero"]
            self.assertEqual(matrix["matrix_byte_offset"], 432 + 448 * selector)
            for actual, expected in zip(matrix["R1"], (.11, .22, .77, .88)):
                self.assertAlmostEqual(actual, expected + selector / 100, places=6)
            self.assertEqual(matrix["flags_u32"], 0x7fc00001)
            self.assertEqual(matrix["raw_u32_hex"][3], "7fc00001")
            self.assertEqual(report["raw_words"]["projection"], 0x12345678)
            self.assertEqual(report["raw_words"]["current_metadata_slot"], 0xffffffff)
            self.assertIsNone(report["selected_metadata_slot"])
            self.assertIsNone(report["physical_eye"])
            self.assertIsNone(report["native_spatial_mapping"])
            for key in ("paired_pixel_bytes_validated", "native_draw_coherence_proven", "native_frame_association_proven",
                        "source_frame_association_proven", "native_eye_placement_proven", "passthrough_authorized"):
                self.assertIs(report[key], False)

    def test_only_native_ps_record_zero_is_derived_and_selectors_can_disagree(self):
        data, metadata = fixture()
        changed = bytearray(data)
        struct.pack_into("<I", changed, 256, 0)
        report = uniform.inspect_uniforms(changed, rehash(changed, metadata))
        self.assertFalse(report["selectors_agree"])
        self.assertEqual(report["fragment_record_zero"]["matrix_byte_offset"], 880)
        self.assertFalse(report["selected_metadata_slot_resolved"])

    def test_one_time_and_dynamic_whole_size_rules(self):
        data, metadata = fixture()
        metadata["acquisition"].update(one_time_submit=True, retirement_required=False, generation_retired=False)
        item = metadata["native_uniform_control"]["slices"][1]
        item.update(descriptor_type=8, descriptor_range=2**64 - 1)
        self.assertTrue(uniform.inspect_uniforms(data, metadata)["uniform_bytes_valid"])
        item.update(dynamic_offset=4, effective_offset=260)
        with self.assertRaisesRegex(probe.ProbeError, "WHOLE_SIZE"):
            uniform.inspect_uniforms(data, metadata)

    def test_missing_or_extended_control_and_original_schema_fail(self):
        data, metadata = fixture()
        for where, field, value in (((), "schema", "other"), (("native_uniform_control",), "schema", "future"),
            (("native_uniform_control",), "timestamp", 123), (("native_uniform_control", "guards"), "extra_guard", True),
            (("native_uniform_control",), "file", "../foreign")):
            changed = copy.deepcopy(metadata)
            target = changed
            for key in where:
                target = target[key]
            target[field] = value
            with self.subTest(where=where, field=field), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(data, changed)
        del metadata["native_uniform_control"]
        with self.assertRaises(probe.ProbeError):
            uniform.inspect_uniforms(data, metadata)

    def test_capture_generation_fence_and_producer_claim_failures(self):
        data, metadata = fixture()
        cases = []
        for key, value in (("capture_id", "stale"), ("command_generation", 99), ("image_generation", 99),
                           ("view_generation", 99), ("selected_draw_ordinal", 0), ("reobserved_set_revision", 99),
                           ("reobserved_command_binding_revision", 99)):
            changed = copy.deepcopy(metadata); changed["native_uniform_control"][key] = value; cases.append(changed)
        for key, value in (("owned_fence_complete", False), ("generation_retired", False), ("one_time_submit", 1), ("view_generation", True)):
            changed = copy.deepcopy(metadata); changed["acquisition"][key] = value; cases.append(changed)
        for key in uniform.GUARDS:
            for value in (False, 1):
                changed = copy.deepcopy(metadata); changed["native_uniform_control"]["guards"][key] = value; cases.append(changed)
        for key in ("source_frame_associated", "uv_owned", "passthrough_authorized"):
            changed = copy.deepcopy(metadata); changed[key] = True; cases.append(changed)
        for changed in cases:
            with self.subTest(changed=changed["native_uniform_control"].get("capture_id")), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(data, changed)
        with self.assertRaisesRegex(probe.ProbeError, "unexpected capture"):
            uniform.inspect_uniforms(data, metadata, "wrong_expected_capture")

    def test_size_hash_limits_and_staging_disjointness(self):
        data, metadata = fixture()
        for payload in (data[:-1], data + b"\0", b"\0" * (uniform.MAX_BYTES + 4)):
            with self.subTest(length=len(payload)), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(payload, metadata)
        for key, value in (("sha256", "0" * 64), ("byte_length", True), ("byte_length", 20484),
                           ("staging_offset", 24), ("staging_offset", 29), ("staging_offset", 65536),
                           ("shared_storage_capacity", 65540)):
            changed = copy.deepcopy(metadata); changed["native_uniform_control"][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(data, changed)

    def test_stage_and_handle_profiles_are_strict(self):
        data, metadata = fixture()
        for section, field, value in (("pipeline", "handle", "0"), ("pipeline", "generation", True),
            ("set", "layout_handle", "0xabc"), ("set", "revision", 0),
            (0, "fnv1a64", "0" * 16), (0, "entry", "main"), (1, "bytes", 9460), (1, "stage", True)):
            changed = copy.deepcopy(metadata); control = changed["native_uniform_control"]
            (control["stages"][section] if type(section) is int else control[section])[field] = value
            with self.subTest(section=section, field=field), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(data, changed)

    def test_exact_maximum_file_and_arithmetic_overflow(self):
        data, metadata = fixture()
        control = metadata["native_uniform_control"]
        expanded = bytearray()
        for index, item in enumerate(control["slices"]):
            content = data[item["file_offset"]:item["file_offset"] + item["byte_length"]]
            expanded.extend(content + b"\0" * (4096 - len(content)))
            item.update(buffer_size=4352, allocation_size=4608, descriptor_range=4096,
                        effective_range=4096, byte_length=4096, file_offset=index * 4096)
        control["byte_length"] = len(expanded)
        metadata = rehash(expanded, metadata)
        self.assertEqual(uniform.inspect_uniforms(expanded, metadata)["byte_length"], uniform.MAX_BYTES)
        bad = copy.deepcopy(metadata)
        item = bad["native_uniform_control"]["slices"][1]
        item.update(descriptor_type=8, buffer_size=2**64 - 1, allocation_size=2**64 - 1,
                    memory_offset=0, descriptor_offset=2**64 - 128, dynamic_offset=256, effective_offset=128)
        with self.assertRaisesRegex(probe.ProbeError, "allocation/offset"):
            uniform.inspect_uniforms(expanded, bad)

    def test_slice_range_generation_digest_and_order_failures(self):
        data, metadata = fixture()
        for index in range(5):
            for key, value in (("buffer_generation", 0), ("allocation_handle", "F00D"), ("buffer_usage", 16),
                ("descriptor_stages", 0), ("descriptor_stages", 0x80000011), ("descriptor_revision", 101),
                ("descriptor_type", 7), ("effective_offset", 260), ("effective_range", 4),
                ("file_offset", 4), ("byte_length", 0), ("memory_offset", 2**64 - 1), ("sha256", "0" * 64)):
                changed = copy.deepcopy(metadata); changed["native_uniform_control"]["slices"][index][key] = value
                with self.subTest(index=index, key=key), self.assertRaises(probe.ProbeError):
                    uniform.inspect_uniforms(data, changed)
        changed = copy.deepcopy(metadata); changed["native_uniform_control"]["slices"].reverse()
        with self.assertRaises(probe.ProbeError):
            uniform.inspect_uniforms(data, changed)

    def test_invalid_selector_or_used_float_refuses_but_nan_flag_bits_are_raw(self):
        data, metadata = fixture()
        for offset, value in ((256, 2), (metadata["native_uniform_control"]["slices"][3]["file_offset"] + 256, 7),
            (metadata["native_uniform_control"]["slices"][4]["file_offset"] + 880 + 20, 0x7f800000)):
            changed = bytearray(data); struct.pack_into("<I", changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(changed, rehash(changed, metadata))

    def test_metadata_conversion_is_not_guessed_or_boolean_coerced(self):
        data, metadata = fixture()
        for section, field, value in (("observed_vulkan_conversion", "model", False),
            ("observed_vulkan_conversion", "components", [False, 0, 0, 0]), ("conversion", "model", "ycbcr_709"),
            ("sampler_control", "view_generation", 6), ("generation", "conversion_generation", 0)):
            changed = copy.deepcopy(metadata); changed[section][field] = value
            with self.subTest(section=section, field=field), self.assertRaises(probe.ProbeError):
                uniform.inspect_uniforms(data, changed)

    def test_cli_safe_io_duplicate_keys_oversized_inputs_and_no_overwrite(self):
        data, metadata = fixture()
        with tempfile.TemporaryDirectory(prefix="uniform-reader-") as directory:
            folder = Path(directory); payload = folder / uniform.FILE; meta = folder / "snapshot.json"; report = folder / "report.json"
            payload.write_bytes(data); meta.write_text(json.dumps(metadata))
            args = ["--uniforms", str(payload), "--metadata", str(meta), "--record", str(report)]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(uniform.main(args), 0)
                original = report.read_bytes()
                self.assertEqual(uniform.main(args), 2)
                self.assertEqual(report.read_bytes(), original)
                meta.write_text('{"schema":1,"schema":2}')
                self.assertEqual(uniform.main(args), 2)
                meta.write_text(json.dumps(metadata)); payload.write_bytes(b"\0" * (uniform.MAX_BYTES + 1))
                self.assertEqual(uniform.main(args), 2)
                payload.unlink(); payload.symlink_to(meta)
                self.assertEqual(uniform.main(args), 2)
                payload.unlink(); payload.write_bytes(data)
                alias = folder / "alias"; alias.symlink_to(folder, target_is_directory=True)
                self.assertEqual(uniform.main(["--uniforms", str(alias / uniform.FILE), "--metadata", str(meta), "--record", str(folder / "other.json")]), 2)
            parsed = json.loads(original)
            self.assertIn("metadata_sha256", parsed)
            self.assertFalse(parsed["paired_pixel_bytes_validated"])


if __name__ == "__main__":
    unittest.main()
