"""Original affine/renaming controls; native tests require a private input file."""
import contextlib
import io
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_fragment_atlas_probe as reader


def mapping():
    return {"schema": reader.MAPPING_SCHEMA, "scope": reader.MAPPING_SCOPE,
        "W": [2., 1., .1, -.5, 1.5, .2], "color": [.4, .1, .05, 0., .6, .2],
        "coverage": [.3, -.05, .55, .08, .5, .1]}


def packed_mapping():
    original = mapping()
    return {"schema": reader.PACKED_MAPPING_SCHEMA, "scope": reader.PACKED_MAPPING_SCOPE,
            "B": original["W"], "color": original["color"], "coverage": original["coverage"]}


def apply(matrix, point):
    return [matrix[0]*point[0]+matrix[1]*point[1]+matrix[2], matrix[3]*point[0]+matrix[4]*point[1]+matrix[5]]


class OwnedControls(unittest.TestCase):
    def test_packed_chart_is_distinct_and_conjugates_original_u(self):
        definition = packed_mapping(); pc, pa = reader.packed_mapping_contract(definition)
        b = [reader.f32(v) for v in definition["B"]]
        for point in ((0., 0.), (.2, .7), (1., 1.), (-.8, 2.)):
            x, y = point[0]-b[2], point[1]-b[5]
            det = b[0]*b[4]-b[1]*b[3]
            source = [(b[4]*x-b[1]*y)/det, (b[0]*y-b[3]*x)/det]
            for panel, matrix in (("color", pc), ("coverage", pa)):
                expected = apply(b, apply([reader.f32(v) for v in definition[panel]], source))
                for actual, value in zip(apply(matrix, point), expected):
                    self.assertAlmostEqual(actual, value, delta=2e-7)
        with self.assertRaises(reader.probe.ProbeError): reader.mapping_contract(definition)
        with self.assertRaises(reader.probe.ProbeError): reader.packed_mapping_contract(mapping())
        for name, value in (("scope", reader.MAPPING_SCOPE), ("B", [0.]*6), ("color", [True]*6),
                            ("coverage", [1., 0., math.nan, 0., 1., 0.])):
            invalid = packed_mapping(); invalid[name] = value
            with self.subTest(name=name), self.assertRaises(reader.probe.ProbeError): reader.packed_mapping_contract(invalid)
        with self.assertRaises(reader.probe.ProbeError): reader.transform_packed(b"", definition, 1)

    def test_packed_and_q_routes_equivalent_for_different_same_invocation_r1(self):
        # Independent chart equation N(q)=R1.min+R1.span*q. This is algebra
        # testing, not a CPU metadata join or a native mapping assertion.
        original = mapping(); mc, ma = reader.mapping_contract(original)
        for r1 in ((.125, .25, .875, 1.), (.25, .0625, 1., .9375), (-.5, .125, 1.5, .625)):
            n = [r1[2]-r1[0], 0., r1[0], 0., r1[3]-r1[1], r1[1]]
            packed = packed_mapping(); packed["B"] = reader.compose(n, original["W"])
            pc, pa = reader.packed_mapping_contract(packed)
            for q in ((.2, .7), (.91, .03), (-.1, 1.1)):
                u = apply(n, q)
                for mapped_u, mapped_q in ((pc, mc), (pa, ma)):
                    for a, b in zip(apply(mapped_u, u), apply(n, apply(mapped_q, q))):
                        self.assertAlmostEqual(a, b, delta=1e-6)

    def test_conjugation_against_independent_pointwise_solve(self):
        definition = mapping(); mc, ma = reader.mapping_contract(definition)
        w = [reader.f32(value) for value in definition["W"]]
        for point in ((0., 0.), (.2, .7), (1., 1.), (-.8, 2.)):
            x, y = point[0]-w[2], point[1]-w[5]
            determinant = w[0]*w[4]-w[1]*w[3]
            original = [(w[4]*x-w[1]*y)/determinant, (w[0]*y-w[3]*x)/determinant]
            for name, result in (("color", mc), ("coverage", ma)):
                expected = apply(w, apply([reader.f32(v) for v in definition[name]], original))
                for a, b in zip(apply(result, point), expected): self.assertAlmostEqual(a, b, delta=2e-7)
        self.assertNotEqual(mc, definition["color"])
        self.assertNotEqual(mc, ma)

    def test_explicit_bounded_nondegenerate_mapping_only(self):
        for field, value in (("scope", "FITTED_NATIVE_AUTHORITY"), ("schema", "future"), ("W", [0.]*6),
                             ("color", [1., 0., 0., 0., 0., 0.]), ("coverage", [1.]*5)):
            changed = mapping(); changed[field] = value
            with self.subTest(field=field), self.assertRaises(reader.probe.ProbeError): reader.mapping_contract(changed)
        for value in (True, math.inf, math.nan, 1e200, 10**1000, "1"):
            changed = mapping(); changed["W"][0] = value
            with self.subTest(type=type(value).__name__), self.assertRaises(reader.probe.ProbeError): reader.mapping_contract(changed)
        for changed in (None, {}, dict(mapping(), extra=True)):
            with self.assertRaises(reader.probe.ProbeError): reader.mapping_contract(changed)
        with self.assertRaises(reader.probe.ProbeError): reader.transform(b"", mapping(), 1)

    def test_id_renaming_preserves_literal_indices_and_ext_instruction_numbers(self):
        instruction = reader.ssa.Instruction(79, (9, 100, 11, 12, 0, 1, 2, 3))
        renamed = reader.renamed(instruction, {100: 200, 11: 111, 12: 112, 3: 333})
        self.assertEqual(renamed.operands, (9, 200, 111, 112, 0, 1, 2, 3))
        ext = reader.ssa.Instruction(12, (8, 100, 1, 43, 11, 12))
        self.assertEqual(reader.renamed(ext, {100: 200, 43: 444, 11: 111}).operands, (8, 200, 1, 43, 111, 12))
        with self.assertRaises(reader.probe.ProbeError): reader.renamed(reader.ssa.Instruction(61, (8, 100, 11)), {})


class PrivateControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get("FRAME_PRIVATE_ORIGINAL_PS")
        if not name: raise unittest.SkipTest("explicit private source absent; no native module tracked")
        cls.original = reader.recovery.read_owned(Path(name), reader.ssa.MAX_BYTES)
        if reader.probe.digest(cls.original) != reader.ssa.ORIGINAL_SHA256: raise AssertionError("wrong private source")

    def test_original_stream_reconstructible_without_new_native_loads(self):
        patched, record = reader.transform(self.original, mapping())
        _, original = reader.ssa.parse_module(self.original)
        _, changed = reader.ssa.parse_module(patched)
        cursor = 0
        for item in changed:
            if item.opcode == 15: item = reader.ssa.Instruction(15, item.operands[:-4])
            if cursor < len(original) and item == original[cursor]: cursor += 1
        self.assertEqual(cursor, len(original))
        for opcode in (61, 65):
            self.assertEqual(sum(item.opcode == opcode for item in changed), sum(item.opcode == opcode for item in original))
        self.assertEqual(sum(item.opcode == 87 for item in changed), 6)
        self.assertEqual(sum(item.opcode == 62 and item.operands[0] == 504 for item in changed), 1)
        self.assertEqual(record["new_ssa_ids"]["original_aperture"], 899)
        self.assertNotEqual(record["new_ssa_ids"]["color_weight"], record["new_ssa_ids"]["mask_weight"])
        self.assertEqual(record["coverage_meaning"], "raw sampled RGB_IDENTITY component-G reconstructed with its own Ma weight; no decoder, tint or aperture; not proven linear game alpha")
        for key in ("runtime_or_pipeline_activation", "native_draw_coherence_proven", "native_frame_association_proven",
                    "application_ray_mapping_proven", "physical_eye_proven", "native_sampler_equivalence_proven",
                    "linear_game_alpha_proven", "alpha_composition_authorized", "passthrough_authorized"):
            self.assertIs(record[key], False)

    def test_disabled_changes_only_output_one_selection_and_enabled_code(self):
        active, active_record = reader.transform(self.original, mapping())
        disabled, record = reader.transform(self.original, mapping(), False)
        self.assertIs(record["enabled"], False)
        header, _ = reader.ssa.parse_module(self.original)
        _, instructions = reader.ssa.parse_module(disabled)
        output1var = next(item.operands[0] for item in instructions if item.opcode == 71
                          and item.operands[0] >= header[3] and item.operands[1:] == (30, 1))
        self.assertTrue(any(item == reader.ssa.Instruction(62, (output1var, 994)) for item in instructions),
                        "disabled own MRT1 must store the exact original output value")
        self.assertNotEqual(active, disabled)
        self.assertEqual(active_record["Mc"], record["Mc"]); self.assertEqual(active_record["Ma"], record["Ma"])
        altered = bytearray(self.original); altered[8] ^= 1
        with self.assertRaisesRegex(reader.probe.ProbeError, "unknown original"): reader.transform(bytes(altered), mapping())
        with self.assertRaisesRegex(reader.probe.ProbeError, "unknown original"): reader.transform(active, mapping())

    def test_actual_private_validation_cli_and_safe_publication(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary)
            original, definition, output, record = (root/name for name in ("private.spv", "mapping.json", "probe.spv", "record.json"))
            original.write_bytes(self.original); definition.write_text(json.dumps(mapping()))
            args = ["--original", str(original), "--mapping", str(definition), "--output", str(output), "--record", str(record)]
            with mock.patch.object(reader.subprocess, "run") as validator:
                validator.return_value.returncode = 1
                self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists()); self.assertFalse(record.exists())
            self.assertEqual(reader.main(args), 0)
            saved = (output.read_bytes(), record.read_bytes())
            self.assertTrue(json.loads(saved[1])["spirv_val_passed"])
            self.assertEqual(reader.main(args), 2); self.assertEqual((output.read_bytes(), record.read_bytes()), saved)
            output.unlink(); record.unlink()
            self.assertEqual(reader.main(args+["--disabled"]), 0)
            output.unlink(); record.unlink()
            original.unlink(); original.symlink_to(definition)
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())
            original.unlink(); original.write_bytes(self.original)
            linked = root/"linked"; linked.symlink_to(root, target_is_directory=True)
            args[1] = str(linked/original.name)
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())

    def test_packed_original_stream_and_direct_u_dependency(self):
        patched, record = reader.transform_packed(self.original, packed_mapping())
        header, original = reader.ssa.parse_module(self.original)
        _, changed = reader.ssa.parse_module(patched)
        cursor = 0
        for item in changed:
            if item.opcode == 15: item = reader.ssa.Instruction(15, item.operands[:-4])
            if cursor < len(original) and item == original[cursor]: cursor += 1
        self.assertEqual(cursor, len(original))
        for opcode in (61, 65):
            self.assertEqual(sum(i.opcode == opcode for i in changed), sum(i.opcode == opcode for i in original))
        added = [i for i in changed if reader.result_id(i) is not None and reader.result_id(i) >= header[3]]
        extracts = [i.operands[2:] for i in added if i.opcode == 81]
        self.assertIn((538, 0), extracts); self.assertIn((538, 1), extracts)
        self.assertNotIn((883, 0), extracts); self.assertNotIn((883, 1), extracts)
        self.assertEqual(sum(i.opcode == 87 for i in changed), 6)
        ids = record["new_ssa_ids"]
        for u in (ids["mapped_uc"], ids["mapped_ua"]):
            self.assertTrue(any(i.opcode == 80 and i.operands[1] == u for i in added))
        self.assertFalse(record["copied_r1_or_cpu_uniform_join_used"])
        self.assertNotIn("Mc", record); self.assertNotIn("Ma", record)
        self.assertEqual(record["schema"], "native-fragment-packed-atlas-probe-v1")
        self.assertEqual(record["Pc"], reader.packed_mapping_contract(packed_mapping())[0])
        for key in ("runtime_or_pipeline_activation", "native_draw_coherence_proven", "native_frame_association_proven",
                    "application_ray_mapping_proven", "physical_eye_proven", "native_sampler_equivalence_proven",
                    "linear_game_alpha_proven", "alpha_composition_authorized", "passthrough_authorized"):
            self.assertIs(record[key], False)
        disabled, _ = reader.transform_packed(self.original, packed_mapping(), False)
        _, instructions = reader.ssa.parse_module(disabled)
        output = next(i.operands[0] for i in instructions if i.opcode == 71 and i.operands[0] >= header[3] and i.operands[1:] == (30, 1))
        self.assertIn(reader.ssa.Instruction(62, (output, 994)), instructions)

    def test_packed_cli_requires_explicit_route_and_validates(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary)
            original, definition, output, record = (root/name for name in ("private.spv", "mapping.json", "probe.spv", "record.json"))
            original.write_bytes(self.original); definition.write_text(json.dumps(packed_mapping()))
            args = ["--original", str(original), "--mapping", str(definition), "--output", str(output), "--record", str(record)]
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())
            self.assertEqual(reader.main(args+["--packed"]), 0)
            self.assertEqual(json.loads(record.read_bytes())["schema"], "native-fragment-packed-atlas-probe-v1")
            output.unlink(); record.unlink()
            self.assertEqual(reader.main(args+["--packed", "--disabled"]), 0)


if __name__ == "__main__":
    unittest.main()
