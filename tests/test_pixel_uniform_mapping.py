"""Original asymmetric offline controls; no Valve shader code or device access."""
import contextlib
import copy
import io
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pixel_uniform_mapping as mapping
import test_pixel_uniform_recovery as original

uniform = mapping.uniform


def fixture(current=1, fallback=2, check=37, expected=37, coherent=True, aliases=False):
    data, metadata = original.fixture(eye=1)
    slices = metadata["native_uniform_control"]["slices"]
    parts = {item["binding"]: bytearray(data[item["file_offset"]:item["file_offset"] + item["byte_length"]]) for item in slices}
    struct.pack_into("<I", parts[2], 0, current)
    if current < 3:
        struct.pack_into("<I", parts[2], 24 + 928 * current, check)
    struct.pack_into("<II", parts[3], 384, fallback, expected)
    for slot in range(3):
        for eye in range(2):
            # Asymmetric columns make row/column swaps and eye/slot confusion
            # visible. Integer flag bits may be a NaN as FLOAT and stay raw.
            values = [.2 + slot * .03, .05 + slot * .01, 8., 0.,
                      .3 + eye * .02, .1 + eye * .02, 9., 0.,
                      .7 + slot * .03, .95 + slot * .01, 10., .07,
                      .8 + eye * .02, .9 + eye * .02, 11., .41]
            base = 432 + 928 * slot + 448 * eye
            struct.pack_into("<16f", parts[2], base, *values)
            struct.pack_into("<I", parts[2], base + 12, 0x7fc00001)
    selected = current if check == expected else fallback
    if aliases:
        parts[6][:] = parts[2]
        for key in ("buffer_handle", "buffer_generation", "buffer_size", "buffer_usage", "allocation_handle",
                    "allocation_generation", "allocation_size", "memory_offset", "descriptor_offset",
                    "descriptor_range", "dynamic_offset", "effective_offset", "effective_range"):
            slices[4][key] = slices[1][key]
    else:
        # Default PS record0 has its own asymmetric rectangle. A coherent
        # fixture copies only the selected full matrix, not a presumed K0.
        struct.pack_into("<16f", parts[6], 880, .1, .01, 8., 0., .2, .02, 9., 0., .8, .99, 10., .1, .9, .98, 11., .2)
        if coherent and selected < 3:
            base = 880 + 928 * selected
            parts[6][880:944] = parts[2][base:base + 64]
    data = b"".join(bytes(parts[item["binding"]]) for item in slices)
    return data, original.rehash(data, metadata)


def mutate(data, metadata, binding, offset, value, fmt="<I"):
    changed = bytearray(data)
    item = next(item for item in metadata["native_uniform_control"]["slices"] if item["binding"] == binding)
    struct.pack_into(fmt, changed, item["file_offset"] + offset, value)
    return bytes(changed), original.rehash(changed, metadata)


class SelectionTests(unittest.TestCase):
    def test_selected_current_record_one_is_not_assumed_zero(self):
        data, metadata = fixture()
        report = mapping.inspect_mapping(data, metadata)
        selected = report["selection"]
        self.assertEqual(selected["current_slot"], 1)
        self.assertEqual(selected["selected_slot"], 1)
        self.assertEqual(selected["record_check_offset"], 952)
        self.assertFalse(selected["fallback_used"])
        self.assertTrue(report["conditional_r1_cancellation"])
        self.assertEqual(report["vertex_selected"]["matrix_byte_offset"], 1808)
        self.assertEqual(report["fragment_record_zero"]["matrix_byte_offset"], 880)
        self.assertTrue(report["binding_relation"]["selected_matrix_bits_equal"])
        self.assertFalse(report["binding_relation"]["same_buffer_allocation_identity"])
        self.assertEqual(report["vertex_selected"]["flags_u32"], 0x7fc00001)
        self.assertTrue(math.isnan(struct.unpack("<f", struct.pack("<I", 0x7fc00001))[0]))
        for field, wanted in (("R0", (.23, .32, .73, .82)), ("R1", (.06, .12, .96, .92)), ("patch_origin", (.07, .41))):
            for actual, expected in zip(report["vertex_selected"][field], wanted):
                self.assertAlmostEqual(actual, expected, places=6)
        for slot in (0, 1, 2):
            payload, description = fixture(current=slot)
            result = mapping.inspect_mapping(payload, description)
            self.assertEqual(result["selection"]["selected_slot"], slot)
            self.assertEqual(result["vertex_selected"]["matrix_byte_offset"], 880 + 928 * slot)

    def test_mismatch_uses_captured_fallback_even_when_current_is_zero(self):
        for current, fallback in ((0, 2), (2, 1), (0, 0)):
            with self.subTest(current=current, fallback=fallback):
                data, metadata = fixture(current=current, fallback=fallback, check=184686, expected=184685)
                result = mapping.inspect_mapping(data, metadata)["selection"]
                self.assertTrue(result["fallback_used"])
                self.assertEqual(result["current_record_consistency"], 184686)
                self.assertEqual(result["expected_consistency"], 184685)
                self.assertEqual(result["selected_slot"], fallback)
                self.assertFalse(result["selector_words_are_source_frame_ids"])

    def test_matching_current_does_not_validate_unused_bad_fallback(self):
        data, metadata = fixture(fallback=0xffffffff)
        self.assertEqual(mapping.inspect_mapping(data, metadata)["selection"]["selected_slot"], 1)

    def test_actual_alias_shape_and_offsets_are_retained_not_guessed(self):
        data, metadata = fixture(current=0, check=11, expected=12, fallback=0, aliases=True)
        report = mapping.inspect_mapping(data, metadata)
        self.assertTrue(report["binding_relation"]["same_effective_base"])
        self.assertTrue(report["binding_relation"]["same_selected_matrix_location"])
        self.assertTrue(report["binding_relation"]["selected_matrix_bits_equal"])
        self.assertEqual(report["binding_relation"]["absolute_buffer_matrix_offsets"], [1136, 1136])
        changed = copy.deepcopy(metadata)
        item = changed["native_uniform_control"]["slices"][1]
        item.update(descriptor_type=8, descriptor_offset=128, dynamic_offset=128)
        report = mapping.inspect_mapping(data, changed)
        self.assertEqual(report["binding_relation"]["descriptor_dynamic_offsets"], [128, 0])
        self.assertEqual(report["binding_relation"]["descriptor_effective_offsets"], [256, 256])

    def test_invalid_current_fallback_matrix_bounds_and_profile_fail(self):
        for current, fallback, check, expected in ((3, 0, 1, 2), (0xffffffff, 0, 1, 2), (0, 3, 1, 2), (0, 0xffffffff, 1, 2)):
            data, metadata = fixture(current=current, fallback=fallback, check=check, expected=expected)
            with self.subTest(current=current, fallback=fallback), self.assertRaisesRegex(uniform.probe.ProbeError, "slot"):
                mapping.inspect_mapping(data, metadata)
        data, metadata = fixture(current=0)
        changed = bytearray(data)
        item = metadata["native_uniform_control"]["slices"][1]
        # Reduce the first metadata buffer to a valid complete944-byte profile,
        # while selecting K2: no truncation or invented bytes may rescue it.
        at, size = item["file_offset"], item["byte_length"]
        changed = changed[:at + 944] + changed[at + size:]
        delta = size - 944
        item.update(buffer_size=1200, descriptor_range=944, effective_range=944, byte_length=944)
        for other in metadata["native_uniform_control"]["slices"][2:]:
            other["file_offset"] -= delta
        metadata["native_uniform_control"]["byte_length"] -= delta
        struct.pack_into("<I", changed, at + 24, 8)
        struct.pack_into("<II", changed, metadata["native_uniform_control"]["slices"][2]["file_offset"] + 384, 2, 9)
        with self.assertRaisesRegex(uniform.probe.ProbeError, "matrix outside"):
            mapping.inspect_mapping(changed, original.rehash(changed, metadata))
        data, metadata = fixture()
        metadata["native_uniform_control"]["stages"][0]["fnv1a64"] = "0" * 16
        with self.assertRaisesRegex(uniform.probe.ProbeError, "stage profile"):
            mapping.inspect_mapping(data, metadata)
        with self.assertRaisesRegex(uniform.probe.ProbeError, "SHA256"):
            mapping.verify_modules(b"x" * 9460, b"x" * 6620)

    def test_nondegenerate_finite_and_selector_checks(self):
        data, metadata = fixture()
        base = 880 + 928
        for offset, value in ((base + 4, float("nan")), (base + 20, float("inf")), (base + 36, .06)):
            changed, description = mutate(data, metadata, 2, offset, value, "<f")
            with self.subTest(offset=offset), self.assertRaises(uniform.probe.ProbeError):
                mapping.inspect_mapping(changed, description)
        for binding in (0, 4):
            changed, description = mutate(data, metadata, binding, 256, 2)
            with self.assertRaisesRegex(uniform.probe.ProbeError, "selector"):
                mapping.inspect_mapping(changed, description)
        with self.assertRaisesRegex(uniform.probe.ProbeError, "digest"):
            mapping.inspect_mapping(data[:-1] + b"x", metadata)

    def test_different_vertex_rectangles_do_not_fake_cancellation(self):
        data, metadata = fixture(coherent=False)
        report = mapping.inspect_mapping(data, metadata, points=[(.25, .75)])
        self.assertFalse(report["conditional_r1_cancellation"])
        self.assertFalse(report["binding_relation"]["selected_r1_bits_equal"])
        point = report["lookup_references"][0]
        vertex = report["vertex_selected"]["R1"]
        expected = [vertex[i] + (vertex[i + 2] - vertex[i]) * q for i, q in enumerate((.25, .75))]
        for actual, value in zip(point["first_packed_lookup"], expected):
            self.assertAlmostEqual(actual, value, places=6)
        self.assertNotEqual(point["normalized_fragment_coordinate"], [.25, .75])


class AlgebraTests(unittest.TestCase):
    def lookup(self, q, flags=0, **kwargs):
        return mapping.lookup_reference([.2, .3, .7, .8], [.05, .1, .95, .9], [.07, .41], flags, q, **kwargs)

    def test_both_packed_lookups_are_asymmetric_and_r1_cancels(self):
        result = self.lookup([.25, .75])
        for actual, expected in zip(result["first_packed_lookup"], [.275, .7]):
            self.assertAlmostEqual(actual, expected, places=6)
        for actual, expected in zip(result["second_packed_lookup"], [.205, 1.05]):
            self.assertAlmostEqual(actual, expected, places=6)
        for actual, expected in zip(result["normalized_fragment_coordinate"], [.25, .75]):
            self.assertAlmostEqual(actual, expected, places=6)
        self.assertNotEqual(result["first_packed_lookup"], result["second_packed_lookup"])
        self.assertEqual(result["outer_attenuation"], 1.)

    def test_inner_and_outer_ramps_flags_and_endpoints(self):
        def at_inner(distance):
            x = .2 + .5 * ((distance + 1) / 2)
            return self.lookup([(x - .05) / .9, (.55 - .1) / .8])
        self.assertAlmostEqual(at_inner(.8)["second_sample_weight"], 1., places=5)
        self.assertAlmostEqual(at_inner(.895)["second_sample_weight"], .5, places=5)
        self.assertAlmostEqual(at_inner(.99)["second_sample_weight"], 0., places=5)
        self.assertEqual(at_inner(1.2)["second_sample_weight"], 0.)
        for distance, expected in ((.95, 1.), (.97, .5), (.99, 0.), (1.2, 0.)):
            self.assertAlmostEqual(self.lookup([(distance + 1) / 2, .5])["outer_attenuation"], expected, places=5)
        center = self.lookup([(.45 - .05) / .9, (.55 - .1) / .8])
        self.assertEqual(center["second_sample_weight"], 1.)
        disabled = self.lookup([(.45 - .05) / .9, (.55 - .1) / .8], flags=2 << 16)
        self.assertEqual(disabled["second_sample_weight"], 0.)
        self.assertTrue(disabled["inner_reconstruction_disabled"])

    def test_two_decoders_blend_order_and_tint(self):
        first, second = [.62, .3, .54], [.2, .8, .9]
        q = [(.2 + .5 * .9475 - .05) / .9, (.55 - .1) / .8]
        for flags in (0, 4 << 16):
            result = self.lookup(q, flags, first_components=first, second_components=second, tint=[.8, .6, .4])
            self.assertAlmostEqual(result["second_sample_weight"], .5, places=5)
            self.assertTrue(result["component_values_are_explicit_inputs"])
            self.assertFalse(result["native_sampler_equivalence_proven"])
            r, g, b = first
            if flags:
                expected_first = [g + (2 * r - 1) - (2 * b - 1), g + (2 * b - 1), g - (2 * r - 1) - (2 * b - 1)]
            else:
                expected_first = [g + 1.5748 * (r - .501960814), g - .1873 * (b - .501960814) - .4681 * (r - .501960814), g + 1.8556 * (b - .501960814)]
            for actual, expected in zip(result["first_decoded_rgb"], expected_first):
                self.assertAlmostEqual(actual, expected, places=6)
            for i in range(3):
                expected = result["second_decoded_rgb"][i] * .5 + result["first_decoded_rgb"][i] * .5
                self.assertAlmostEqual(result["blended_rgb"][i], expected, places=5)
                self.assertAlmostEqual(result["attenuated_tinted_rgb"][i], expected * [.8, .6, .4][i], places=5)
            disabled = self.lookup(q, flags | (2 << 16), first_components=first, second_components=second)
            self.assertEqual(disabled["blended_rgb"], disabled["first_decoded_rgb"])

    def test_invalid_numeric_profiles_and_unpaired_components_refuse(self):
        for bad in (float("nan"), float("inf"), True, "1"):
            with self.subTest(bad=bad), self.assertRaises(uniform.probe.ProbeError):
                self.lookup([bad, .5])
        for bad in (-1, 2**32, True, 1.):
            with self.assertRaises(uniform.probe.ProbeError):
                self.lookup([.5, .5], flags=bad)
        for kwargs in ({"first_components": [0., 0., 0.]}, {"tint": [1., 1., 1.]},
                       {"first_components": [2., 0., 0.], "second_components": [0., 0., 0.]}):
            with self.assertRaises(uniform.probe.ProbeError):
                self.lookup([.5, .5], **kwargs)
        with self.assertRaisesRegex(uniform.probe.ProbeError, "overflow"):
            mapping.f32(1e39)
        with self.assertRaisesRegex(uniform.probe.ProbeError, "nondegenerate"):
            mapping.lookup_reference([.2, .3, .2, .8], [.05, .1, .95, .9], [.07, .41], 0, [.5, .5])


class CliTests(unittest.TestCase):
    def test_bounded_points_and_cli_no_authority_roundtrip(self):
        data, metadata = fixture()
        for points in ([], [[0., 0.]] * 65, [[0., 0., 0.]], [[float("nan"), .5]]):
            with self.assertRaises(uniform.probe.ProbeError):
                mapping.inspect_mapping(data, metadata, points=points)
        self.assertEqual(len(mapping.inspect_mapping(data, metadata, points=[[.5, .5]] * 64)["lookup_references"]), 64)
        for text in ("nan,.5", ".5,inf", "1,2,3", "x,.5", "1" * 129 + ",.5"):
            with self.assertRaises((uniform.probe.ProbeError, ValueError)):
                mapping.parse_point(text)
        with tempfile.TemporaryDirectory(prefix="uniform-map-") as name:
            root = Path(name)
            (root / "uniforms.bin").write_bytes(data)
            (root / "snapshot.json").write_text(json.dumps(metadata))
            args = ["--uniforms", str(root / "uniforms.bin"), "--metadata", str(root / "snapshot.json"),
                    "--record", str(root / "mapping.json"), "--q", ".25,.75", "--expected-capture-id", "owned_uniform"]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(mapping.main(args), 0)
                # Existing outputs, missing module pair, excessive points, and
                # tampered capture IDs never overwrite an existing report.
                before = (root / "mapping.json").read_bytes()
                self.assertEqual(mapping.main(args), 2)
                self.assertEqual(mapping.main(args + ["--vertex-module", "missing"]), 2)
                self.assertEqual(mapping.main(args + ["--q", "0,0"] * 65), 2)
                self.assertEqual(mapping.main(args[:-1] + ["foreign"]), 2)
            self.assertEqual((root / "mapping.json").read_bytes(), before)
            report = json.loads(before)
            self.assertEqual(report["metadata_sha256"], uniform.probe.digest((root / "snapshot.json").read_bytes()))
            self.assertEqual(len(report["lookup_references"]), 1)
            self.assertFalse(report["semantic_profile"]["original_module_files_verified"])
            self.assertTrue(report["semantic_profile"]["profile_is_diagnostic_not_authentication"])
            for key in ("paired_pixel_bytes_validated", "native_sampler_equivalence_proven", "native_draw_coherence_proven",
                        "native_frame_association_proven", "source_frame_association_proven", "native_eye_placement_proven",
                        "application_ray_mapping_proven", "alpha_composition_authorized", "passthrough_authorized"):
                self.assertIs(report[key], False)
            self.assertIsNone(report["native_spatial_mapping"])
            self.assertIsNone(report["physical_eye"])
            self.assertTrue(report["opaque_fallback_required"])


if __name__ == "__main__":
    unittest.main()
