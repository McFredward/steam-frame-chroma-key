"""Frozen-format guard tests; native shader inputs remain explicit/private."""
import contextlib
import copy
import io
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import native_fragment_guarded_presenter as guard


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def definitions(instructions):
    return {guard.result_id(i): i for i in instructions if guard.result_id(i) is not None}


def evaluate(value, defs, seeded):
    """Independent evaluator for the small predicate graph, never texture code."""
    if value in seeded:
        return seeded[value]
    item = defs[value]
    op, args = item.opcode, item.operands
    if op == 43:
        return struct.unpack("<f", struct.pack("<I", args[2]))[0]
    if op == 81:
        return evaluate(args[2], defs, seeded)[args[3]]
    left, right = (evaluate(v, defs, seeded) for v in args[2:])
    if op == 131:
        return f32(left - right)
    if op == 167:
        return left and right
    if op == 186:
        return left > right
    if op == 188:
        return left <= right
    if op == 190:
        return left >= right
    raise AssertionError("unexpected predicate opcode")


def blocks_and_owners(instructions):
    blocks, owners, current = {}, {}, None
    for item in instructions:
        if item.opcode == 248:
            current = item.operands[0]
            blocks[current] = []
        if current is not None:
            blocks[current].append(item)
            if guard.result_id(item) is not None:
                owners[guard.result_id(item)] = current
    return blocks, owners


def assert_guard_structure(test, instructions, record):
    ids = record["guard_ssa"]
    defs = definitions(instructions)
    blocks, owners = blocks_and_owners(instructions)
    geometry = blocks[owners[ids["geometry_valid"]]]
    test.assertIn(guard.ssa.Instruction(250, (ids["geometry_valid"], ids["signature_header"], ids["invalid_geometry"])), geometry)
    test.assertIn(guard.ssa.Instruction(250, (ids["first_signature_valid"], ids["remaining_header"], ids["invalid_first_signature"])), blocks[ids["signature_header"]])
    test.assertIn(guard.ssa.Instruction(250, (ids["signature_valid"], ids["panel_header"], ids["invalid_signature"])), blocks[ids["remaining_header"]])
    for label in ("invalid_geometry", "invalid_first_signature", "invalid_signature"):
        test.assertEqual([i for i in blocks[ids[label]] if i.opcode == 62], [guard.ssa.Instruction(62, (504, 994))])
    test.assertEqual(sum(i.opcode == 88 for i in blocks[ids["signature_header"]]), 1)
    test.assertEqual(sum(i.opcode == 88 for i in blocks[ids["remaining_header"]]), 11)
    for cell in ids["cells"]:
        for value, opcode, bound in ((cell["lower"], 190, cell["low"]), (cell["upper"], 188, cell["high"])):
            test.assertEqual(defs[value], guard.ssa.Instruction(opcode, (96, value, cell["g"], bound)))
        code = cell["code"]
        for name, value in (("low", (code - 2) / 255.), ("high", (code + 2) / 255.)):
            test.assertEqual(defs[cell[name]].operands[2], struct.unpack("<I", struct.pack("<f", value))[0])
        test.assertEqual(defs[cell["g"]], guard.ssa.Instruction(81, (8, cell["g"], cell["sample"], 1)))
        sample = defs[cell["sample"]]
        test.assertEqual(sample.opcode, 88)
        test.assertEqual(sample.operands[3:], (cell["coordinate"], 2, ids["zero"]))
        sampled = defs[sample.operands[2]]
        test.assertEqual(sampled.operands[2:], (599, 600))
        test.assertEqual(owners[sampled.operands[1]], owners[cell["sample"]])


class OwnedControls(unittest.TestCase):
    def test_exact_published_contract_and_no_caller_overrides(self):
        profile = guard.load_profile()
        self.assertEqual(profile["canvas"], [688, 548])
        self.assertEqual(profile["guard"]["codes"], [111, 153, 155, 51, 111, 82, 92, 219, 102, 227, 121, 189])
        for key, value in (("profile_revision", 2.0), ("authenticates_body", 0),
                           ("scope", "LIVE_NATIVE_AUTHORITY"), ("B", [1, 0, 0, 0, 1, 0]),
                           ("rgb_convention", "premultiplied")):
            changed = copy.deepcopy(profile)
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(guard.presenter.atlas.probe.ProbeError):
                guard.profile_contract(changed)
        for name in ("canvas", "color", "coverage"):
            changed = copy.deepcopy(profile)
            changed[name][0] += 1
            with self.subTest(name=name), self.assertRaises(guard.presenter.atlas.probe.ProbeError):
                guard.profile_contract(changed)
        sealed = guard.profile_contract(profile)
        profile["guard"]["codes"][0] = 0
        self.assertEqual(sealed["guard"]["codes"][0], 111)

    def test_signature_geometry_tolerance_and_types_are_frozen(self):
        for key, value in (("code_tolerance", 3), ("cells", 11), ("origin", [16, 465]),
                           ("stride", [51, 0]), ("size", [30, 30]), ("signature_sha256", "0" * 64)):
            changed = guard.load_profile()
            changed["guard"][key] = value
            with self.subTest(key=key), self.assertRaises(guard.presenter.atlas.probe.ProbeError):
                guard.profile_contract(changed)
        for value in (None, {}, {"NaN": float("nan")}, {"unsupported": object()}):
            with self.assertRaises(guard.presenter.atlas.probe.ProbeError):
                guard.profile_contract(value)
        panels = guard.panel_mapping(guard.load_profile())
        self.assertEqual(panels["color"], [320 / 688, 0., 16 / 688, 0., 192 / 548, 244 / 548])
        self.assertEqual(panels["coverage"], [320 / 688, 0., 352 / 688, 0., 192 / 548, 244 / 548])

    def test_invalid_switch_and_descriptor_types_refuse_before_shader_access(self):
        for enabled in (None, 0, 1, "ON"):
            with self.assertRaisesRegex(guard.presenter.atlas.probe.ProbeError, "explicit boolean"):
                guard.transform(b"", guard.load_profile(), enabled)
        for count in (None, True, 0, 2, 4):
            with self.assertRaisesRegex(guard.presenter.atlas.probe.ProbeError, "count1 or3"):
                guard.transform(b"", guard.load_profile(), descriptor_count=count)


class PrivateControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get("FRAME_PRIVATE_ORIGINAL_PS")
        if not name:
            raise unittest.SkipTest("explicit private original shader absent")
        cls.original = guard.recovery.read_owned(Path(name), guard.ssa.MAX_BYTES)
        cls.binary, cls.record = guard.transform(cls.original, guard.load_profile())
        cls.header, cls.instructions = guard.parse_module(cls.binary)

    def validate(self, binary):
        with tempfile.TemporaryDirectory(prefix="guard-val-") as directory:
            path = Path(directory) / "private.spv"
            path.write_bytes(binary)
            process = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                                     capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_original_function_reads_and_exact_fallback_survive(self):
        native_header, native = guard.ssa.parse_module(self.original)
        _, store = guard.ssa.profile_structure(native_header, native)
        original_start = next(i for i, item in enumerate(native) if item.opcode == 54)
        start = next(i for i, item in enumerate(self.instructions) if item.opcode == 54)
        prefix = native[original_start:store]
        self.assertTrue(self.instructions[start:start + len(prefix)] == prefix,
                        "original entire SSA994 prefix must be unchanged")
        for opcode in (61, 65, 87):
            self.assertTrue([i for i in self.instructions if i.opcode == opcode] == [i for i in native if i.opcode == opcode],
                            "original loads/accesses/implicit samples must be unchanged")
        self.assertEqual(sum(i.opcode == 87 for i in self.instructions), 2)
        self.assertEqual(sum(i.opcode == 88 for i in self.instructions), 16)
        self.assertEqual([i.operands[1] for i in self.instructions if i.opcode == 59 and i.operands[2] == 3], [504])
        self.assertEqual(sum(i.opcode == 62 and i.operands == (504, 994) for i in self.instructions), 3)
        self.assertEqual(sum(i.opcode == 62 for i in self.instructions), 4)
        self.assertTrue([i for i in self.instructions if i.opcode == 71 and i.operands[1] in (33, 34)] ==
                        [i for i in native if i.opcode == 71 and i.operands[1] in (33, 34)])
        assert_guard_structure(self, self.instructions, self.record)
        self.validate(self.binary)
        self.validate(guard.transform(self.original, guard.load_profile(), descriptor_count=3)[0])

    def test_predicates_reject_nonfinite_outside_and_every_bad_code(self):
        defs, ids = definitions(self.instructions), self.record["guard_ssa"]
        valid_r1 = (0., .25, 1., .5)
        for r1, u in ((valid_r1, (.5, .375)), ((.1, .11, .8, .9), (.4, .5)),
                      ((0., .75, 1., 1.), (0., .75))):
            self.assertTrue(evaluate(ids["geometry_valid"], defs, {871: r1, 538: u}))
        invalid = [((-.1, .25, 1., .5), (.5, .375)), ((0., .25, 1.1, .5), (.5, .375)),
                   ((0., .5, 1., .5), (.5, .5)), ((0., .6, 1., .5), (.5, .55)),
                   (valid_r1, (.5, .249)), (valid_r1, (1.01, .4))]
        for value in (float("nan"), float("inf"), -float("inf")):
            for index in range(4):
                r1 = list(valid_r1)
                r1[index] = value
                invalid.append((r1, (.5, .375)))
            invalid.append((valid_r1, (value, .375)))
        for r1, u in invalid:
            self.assertFalse(evaluate(ids["geometry_valid"], defs, {871: r1, 538: u}))
        good = {cell["g"]: f32(cell["code"] / 255.) for cell in ids["cells"]}
        self.assertTrue(evaluate(ids["signature_valid"], defs, good))
        for cell in ids["cells"]:
            low = evaluate(cell["low"], defs, {})
            high = evaluate(cell["high"], defs, {})
            for boundary in (low, high):
                seeded = dict(good, **{})
                seeded[cell["g"]] = boundary
                self.assertTrue(evaluate(ids["signature_valid"], defs, seeded))
            low_bits, high_bits = (struct.unpack("<I", struct.pack("<f", value))[0] for value in (low, high))
            bad = [struct.unpack("<f", struct.pack("<I", low_bits - 1))[0],
                   struct.unpack("<f", struct.pack("<I", high_bits + 1))[0],
                   float("nan"), float("inf"), -float("inf")]
            for value in bad:
                seeded = dict(good)
                seeded[cell["g"]] = value
                self.assertFalse(evaluate(ids["signature_valid"], defs, seeded))

    def test_branches_precede_all_new_reads_and_first_mismatch_is_cheap(self):
        ids = self.record["guard_ssa"]
        blocks, owners = blocks_and_owners(self.instructions)
        successors = {}
        for label, body in blocks.items():
            jump = next((i for i in body if i.opcode in (249, 250, 253)), None)
            self.assertIsNotNone(jump)
            successors[label] = list(jump.operands if jump.opcode == 249 else jump.operands[1:] if jump.opcode == 250 else ())
        dom = guard.ssa.dominators(successors, 5)
        guard_samples = {cell["sample"] for cell in ids["cells"]}
        for item in self.instructions:
            if item.opcode == 88:
                block = owners[item.operands[1]]
                self.assertIn(owners[ids["geometry_valid"]], dom[block])
                if item.operands[1] not in guard_samples:
                    self.assertIn(ids["panel_header"], dom[block])
                    self.assertIn(ids["remaining_header"], dom[block])
        for invalid in ("invalid_geometry", "invalid_first_signature", "invalid_signature"):
            reachable, todo = set(), [ids[invalid]]
            while todo:
                current = todo.pop()
                if current not in reachable:
                    reachable.add(current)
                    todo.extend(successors[current])
            self.assertTrue(all(item.opcode != 88 for block in reachable for item in blocks[block]))
            self.assertNotIn(ids["panel_header"], reachable)
        self.assertEqual(sum(i.opcode == 88 for i in blocks[ids["signature_header"]]), 1)
        self.assertTrue(self.record["first_cell_mismatch_skips_remaining_samples"])

    def test_valid_mutants_fail_control_and_window_proof(self):
        ids = self.record["guard_ssa"]
        first = guard.ssa.Instruction(250, (ids["first_signature_valid"], ids["remaining_header"], ids["invalid_first_signature"]))
        bypass_first = [guard.ssa.Instruction(250, (ids["geometry_valid"], *i.operands[1:])) if i == first else i for i in self.instructions]
        last = ids["cells"][-1]
        bad_window = [guard.ssa.Instruction(190, (96, last["lower"], last["g"], last["high"]))
                      if i.opcode == 190 and i.operands[1] == last["lower"] else i for i in self.instructions]
        # The original has no global vec4 constant. A new ordinary constant is a
        # legal but deliberately incorrect fallback, independent of native IDs.
        vector_constant = self.header[3]
        bad_fallback = [guard.ssa.Instruction(62, (504, vector_constant)) if i == guard.ssa.Instruction(62, (504, 994)) else i
                        for i in self.instructions]
        first_function = next(i for i, item in enumerate(bad_fallback) if item.opcode == 54)
        bad_fallback.insert(first_function, guard.ssa.Instruction(44, (9, vector_constant, 185, 185, 185, 185)))
        for name, changed, bound in (("bypass_first", bypass_first, self.header[3]),
                                     ("bad_window", bad_window, self.header[3]),
                                     ("wrong_fallback", bad_fallback, self.header[3] + 1)):
            with self.subTest(mutant=name):
                self.assertTrue(changed != self.instructions)
                self.validate(guard.pack_module([*self.header[:3], bound, self.header[4]], changed))
                with self.assertRaises(AssertionError):
                    assert_guard_structure(self, changed, self.record)

    def test_marker_legacy_refusal_and_real_off(self):
        markers = [i for i in self.instructions if i.opcode == 330]
        self.assertEqual(len(markers), 1)
        text = struct.pack("<%dI" % len(markers[0].operands), *markers[0].operands).split(b"\0", 1)[0]
        self.assertEqual(text, guard.MARKER.encode("ascii"))
        for count in (1, 3):
            off, record = guard.transform(self.original, guard.load_profile(), False, count)
            self.assertEqual(off, self.original)
            self.assertFalse(record["synthetic_format_guard_implemented"])
            self.assertEqual(record["guard_ssa"], {})
        for key in ("runtime_or_pipeline_activation", "authenticates_body", "proves_liveness", "linear_game_alpha_proven",
                    "native_spatial_mapping_proven", "native_frame_association_proven", "native_sampler_equivalence_proven",
                    "physical_eye_proven", "live_source_validity_guard_implemented", "passthrough_authorized"):
            self.assertIs(self.record[key], False, key)
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ unavailable for unchanged actual legacy-guard call-through")
        with tempfile.TemporaryDirectory(prefix="fragment-guard-legacy-") as directory:
            root = Path(directory)
            source = root / "guard.cpp"
            source.write_text('''#include "layer/mask_module_guard.h"
#include <fstream>
#include <iterator>
#include <vector>
int main(int n,char**v){if(n!=2)return 2;std::ifstream f(v[1],std::ios::binary);
std::vector<char>b{std::istreambuf_iterator<char>(f),{}};
return mr::mask::legacyOverrideAllowed(b.data(),b.size())?1:0;}
''')
            module, executable = root / "private.spv", root / "check"
            module.write_bytes(self.binary)
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(ROOT), str(source), "-o", str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertEqual(subprocess.run([str(executable), str(module)], check=False, timeout=10).returncode, 0)

    def test_cli_validates_before_exclusive_publication(self):
        with tempfile.TemporaryDirectory(prefix="fragment-guard-cli-") as directory, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(directory)
            original, module, record = (root / name for name in ("original.spv", "output.spv", "record.json"))
            original.write_bytes(self.original)
            args = ["--original", str(original), "--output", str(module), "--record", str(record)]
            with mock.patch.object(guard.subprocess, "run") as validator:
                validator.return_value.returncode = 1
                self.assertEqual(guard.main(args), 2)
                self.assertFalse(module.exists())
                self.assertFalse(record.exists())
            self.assertEqual(guard.main(args), 0)
            self.assertTrue(json.loads(record.read_text())["spirv_val_passed"])
            saved = module.read_bytes(), record.read_bytes()
            self.assertEqual(guard.main(args), 2)
            self.assertEqual(saved, (module.read_bytes(), record.read_bytes()))
            module.unlink()
            record.unlink()
            self.assertEqual(guard.main(args + ["--disabled"]), 0)
            self.assertEqual(module.read_bytes(), self.original)


if __name__ == "__main__":
    unittest.main()
