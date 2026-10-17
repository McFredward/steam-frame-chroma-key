"""Focused current-R1 candidate controls; private originals are never bundled."""
import contextlib
import io
import json
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
import native_fragment_r1_presenter as output


def mapping():
    return {"schema": output.SCHEMA, "scope": output.SCOPE, "chart": output.CHART,
            "source_encoding": output.ENCODING, "rgb_convention": "straight",
            "color": [.4, 0., .03, 0., .6, .2],
            "coverage": [.4, 0., .56, 0., .6, .2]}


def unpack_candidate(binary):
    words = struct.unpack("<%dI" % (len(binary) // 4), binary)
    header, instructions, markers, at = list(words[:5]), [], [], 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        if not count or at + count > len(words):
            raise AssertionError("invalid candidate instruction bounds")
        item = output.ssa.Instruction(opcode, words[at + 1:at + count])
        (markers if opcode == 330 else instructions).append(item)
        at += count
    # Reuse the strict inherited profile parser after removing only the new
    # module marker. This is framing analysis, not general SPIR-V validation.
    encoded = struct.pack("<%dI" % (5 + sum(len(i.words()) for i in instructions)),
                          *header, *(w for i in instructions for w in i.words()))
    output.ssa.parse_module(encoded)
    return header, instructions, markers


def repack(header, instructions, markers):
    values = list(instructions)
    index = next(i for i, item in enumerate(values) if item.opcode in (71, 72))
    values[index:index] = markers
    words = [*header, *(w for i in values for w in i.words())]
    return struct.pack("<%dI" % len(words), *words)


def definitions(instructions):
    return {output.atlas.result_id(i): i for i in instructions if output.atlas.result_id(i) is not None}


def assert_causal_structure(test, instructions, record):
    """Independent narrow dataflow checks, also used against valid mutants."""
    defs, ids = definitions(instructions), record["new_ssa_ids"]
    def ancestors(value):
        result, todo = set(), [value]
        while todo:
            current = todo.pop()
            if current in result:
                continue
            result.add(current)
            if current in defs:
                todo.extend(output.ssa.value_inputs(defs[current]))
        return result
    for key, panel in (("mapped_qc", record["color"]), ("mapped_qa", record["coverage"])):
        vector = defs[ids[key]]
        test.assertEqual(vector.opcode, 80)
        test.assertEqual(len(vector.operands), 4)
        # Identity chart: each q' component is sx*q883.x + sy*q883.y + t.
        for value, offset in zip(vector.operands[2:], (0, 3)):
            last = defs[value]
            test.assertEqual(last.opcode, 129)
            first, translation = (defs[v] for v in last.operands[2:])
            test.assertEqual(translation.opcode, 43)
            test.assertEqual(translation.operands[2], struct.unpack("<I", struct.pack("<f", panel[offset + 2]))[0])
            test.assertEqual(first.opcode, 129)
            for multiply, coefficient, component in zip((defs[v] for v in first.operands[2:]), panel[offset:offset + 2], (0, 1)):
                test.assertEqual(multiply.opcode, 133)
                constant, extract = (defs[v] for v in multiply.operands[2:])
                test.assertEqual(constant.operands[2], struct.unpack("<I", struct.pack("<f", coefficient))[0])
                test.assertEqual(extract, output.ssa.Instruction(81, (8, extract.operands[1], 883, component)))
    test.assertNotEqual(ids["mask_weight"], ids["color_weight"])
    raw_dependencies = ancestors(ids["raw_component_g"])
    test.assertIn(ids["mask_weight"], raw_dependencies)
    test.assertNotIn(ids["color_weight"], raw_dependencies)
    # The final RGB contribution and alpha each consume coverage/aperture at
    # the specifically intended stage; a second aperture cannot hide here.
    test.assertEqual(defs[ids["premultiplied_rgb"]], output.ssa.Instruction(142,
        (30, ids["premultiplied_rgb"], ids["combined_color_rgb"], ids["clamped_coverage"])))
    test.assertEqual(defs[ids["output_alpha"]], output.ssa.Instruction(133,
        (8, ids["output_alpha"], ids["clamped_coverage"], 899)))
    combined = defs[ids["combined_color_rgb"]]
    test.assertEqual(combined.opcode, 133)
    test.assertEqual(combined.operands[3], 907)
    aperture = defs[combined.operands[2]]
    test.assertEqual(aperture.opcode, 142)
    test.assertEqual(aperture.operands[3], 899)


class OwnedControls(unittest.TestCase):
    def test_strict_current_chart_and_no_host_chart(self):
        result = output.mapping_contract(mapping())
        self.assertEqual(result["W"], [1., 0., 0., 0., 1., 0.])
        for name in ("B", "W", "eye", "copied_R1"):
            with self.subTest(name=name), self.assertRaises(output.atlas.probe.ProbeError):
                output.mapping_contract(dict(mapping(), **{name: [1., 0., 0., 0., 1., 0.]}))
        for key, value in (("scope", "NATIVE_ACTIVATION"), ("chart", "HOST_FITTED_B"),
                           ("source_encoding", "NATIVE_VIDEO_ALPHA"), ("rgb_convention", "premultiplied"),
                           ("schema", output.atlas.MAPPING_SCHEMA)):
            changed = mapping()
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(output.atlas.probe.ProbeError):
                output.mapping_contract(changed)
        for value in (None, {}, dict(mapping(), extra=True)):
            with self.assertRaises(output.atlas.probe.ProbeError):
                output.mapping_contract(value)

    def test_panels_are_bounded_disjoint_explicit_rectangles(self):
        for panel in ([True] * 6, [1., 0., 0., 0., float("nan"), 0.], [0.] * 6,
                      [.4, .1, .03, 0., .6, .2], [-.4, 0., .5, 0., .6, .2],
                      [.4, 0., -.1, 0., .6, .2], [.4, 0., .8, 0., .6, .2],
                      [.4, 0., .03, 0., .6, .8], [.4, 0., .56, 0., .6, .2]):
            changed = mapping()
            changed["color"] = panel
            with self.subTest(panel=panel), self.assertRaises(output.atlas.probe.ProbeError):
                output.mapping_contract(changed)
        # The actual producer's normalized panel rectangles are admitted, with
        # no reference to an observed native eye or historical fitted chart.
        current = mapping()
        current.update(color=[320 / 688, 0., 16 / 688, 0., 192 / 548, 244 / 548],
                       coverage=[320 / 688, 0., 352 / 688, 0., 192 / 548, 244 / 548])
        output.mapping_contract(current)

    def test_explicit_switch_and_interface_types_before_private_access(self):
        for enabled in (1, 0, None, "true"):
            with self.assertRaisesRegex(output.atlas.probe.ProbeError, "explicit boolean"):
                output.transform(b"", mapping(), enabled)
        for count in (True, 0, 2, 4, None):
            with self.assertRaisesRegex(output.atlas.probe.ProbeError, "count1 or3"):
                output.transform(b"", mapping(), descriptor_count=count)

    def test_marker_requires_spirv11_and_precedes_annotations(self):
        # Minimal framing-only inputs, not claimed valid executable modules.
        for version in (0x00010000, 0x00010100, 0x00010300):
            words = (0x07230203, version, 0, 2, 0, 4 << 16 | 71, 1, 30, 0)
            binary = struct.pack("<9I", *words)
            marked, original, result = output._marked_module(binary)
            self.assertEqual(original, version)
            self.assertEqual(result, max(version, 0x00010100))
            parsed = struct.unpack("<%dI" % (len(marked) // 4), marked)
            self.assertEqual(parsed[5] & 65535, 330)
            length = parsed[5] >> 16
            text = struct.pack("<%dI" % (length - 1), *parsed[6:5 + length]).split(b"\0", 1)[0]
            self.assertEqual(text, output.MARKER.encode("ascii"))
            self.assertEqual(parsed[5 + length] & 65535, 71)


class PrivateControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get("FRAME_PRIVATE_ORIGINAL_PS")
        if not name:
            raise unittest.SkipTest("explicit private native shader absent")
        cls.original = output.recovery.read_owned(Path(name), output.ssa.MAX_BYTES)
        if output.atlas.probe.digest(cls.original) != output.ssa.ORIGINAL_SHA256:
            raise AssertionError("wrong original input")

    def validate(self, binary):
        with tempfile.TemporaryDirectory(prefix="r1-presenter-val-") as directory:
            path = Path(directory) / "private.spv"
            path.write_bytes(binary)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                                    capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_current_r1_path_single_output_no_new_native_reads(self):
        binary, record = output.transform(self.original, mapping(), descriptor_count=3)
        header, instructions, markers = unpack_candidate(binary)
        original_header, native = output.ssa.parse_module(self.original)
        self.assertEqual(len(markers), 1)
        self.assertEqual(header[1], 0x00010100)
        self.assertEqual(original_header[1], 0x00010000)
        self.assertTrue(record["spirv_version_changed"])
        self.assertEqual([i for i in instructions if i.opcode == 15], [i for i in native if i.opcode == 15])
        self.assertEqual([i.operands[1] for i in instructions if i.opcode == 59 and i.operands[2] == 3], [504])
        self.assertEqual([i for i in instructions if i.opcode == 62],
                         [output.ssa.Instruction(62, (504, record["new_ssa_ids"]["output"]))])
        for opcode in (61, 65):
            self.assertEqual([i for i in instructions if i.opcode == opcode], [i for i in native if i.opcode == opcode])
        self.assertEqual([i for i in instructions if i.opcode == 59 and i.operands[2] != 3],
                         [i for i in native if i.opcode == 59 and i.operands[2] != 3])
        self.assertEqual([i for i in instructions if i.opcode == 71 and i.operands[1] in (33, 34)],
                         [i for i in native if i.opcode == 71 and i.operands[1] in (33, 34)])
        self.assertEqual(sum(i.opcode == 87 for i in instructions), 6)
        self.assertEqual(record["new_descriptor_bindings"], 0)
        assert_causal_structure(self, instructions, record)
        self.validate(binary)

    def test_count1_specialization_only_fresh_owned_interface_ids(self):
        regular, _ = output.transform(self.original, mapping(), descriptor_count=3)
        specialized, record = output.transform(self.original, mapping())
        _, old, old_markers = unpack_candidate(regular)
        _, new, new_markers = unpack_candidate(specialized)
        defs = definitions(new)
        variable = defs[289]
        pointer = defs[variable.operands[0]]
        array = defs[pointer.operands[2]]
        self.assertEqual(array.opcode, 28)
        self.assertEqual(defs[array.operands[2]].operands[2], 1)
        self.assertEqual(defs[102].operands[2], 3)  # Shared original constant unchanged.
        fresh = {pointer.operands[0], array.operands[0], array.operands[2]}
        restored = [output.ssa.Instruction(59, (288, 289, 0)) if i == variable else i
                    for i in new if output.atlas.result_id(i) not in fresh]
        self.assertEqual(restored, old)
        self.assertEqual(old_markers, new_markers)
        self.assertTrue(record["owned_count1_interface_specialization"])
        self.assertFalse(record["native_descriptor_layout_modified"])
        self.validate(specialized)

    def test_real_off_exact_original_and_all_live_authorities_false(self):
        for count in (1, 3):
            off, record = output.transform(self.original, mapping(), False, count)
            self.assertEqual(off, self.original)
            self.assertIsNone(record["candidate_marker"])
            self.assertFalse(record["spirv_version_changed"])
            self.assertEqual(record["new_ssa_ids"], {})
            self.assertEqual(record["owned_b9_descriptor_count"], 3)
        _, record = output.transform(self.original, mapping())
        for key in ("host_chart_used", "copied_r1_or_cpu_uniform_join_used", "physical_eye_table_used",
                    "runtime_or_pipeline_activation", "native_spatial_mapping_proven", "native_draw_coherence_proven",
                    "native_frame_association_proven", "physical_eye_proven", "native_sampler_equivalence_proven",
                    "linear_game_alpha_proven", "alpha_composition_authorized", "live_source_validity_guard_implemented",
                    "passthrough_authorized", "additive_or_refraction_contract"):
            self.assertIs(record[key], False, key)
        corrupted = bytearray(self.original)
        corrupted[8] ^= 1
        for source in (b"", bytes(corrupted), output.transform(self.original, mapping())[0]):
            for enabled in (False, True):
                with self.assertRaises(output.atlas.probe.ProbeError):
                    output.transform(source, mapping(), enabled)

    def test_valid_causal_mutants_fail_independent_structure_checks(self):
        binary, record = output.transform(self.original, mapping(), descriptor_count=3)
        header, instructions, markers = unpack_candidate(binary)
        ids = record["new_ssa_ids"]
        native_bound = output.ssa.parse_module(self.original)[0][3]
        # Wrong chart remains legal SPIR-V but bypasses original normalized q.
        wrong_chart = [output.ssa.Instruction(81, (*i.operands[:2], 538, i.operands[3]))
                       if i.opcode == 81 and i.operands[2] == 883 and i.operands[1] >= native_bound else i
                       for i in instructions]
        shared_weight = []
        for item in instructions:
            if item.opcode in (131, 133) and ids["mask_weight"] in item.operands[2:] and item.operands[1] >= native_bound:
                args = (*item.operands[:2], *(ids["color_weight"] if v == ids["mask_weight"] else v for v in item.operands[2:]))
                item = output.ssa.Instruction(item.opcode, args)
            shared_weight.append(item)
        doubled = []
        squared = header[3]
        for item in instructions:
            if item.opcode == 133 and item.operands[1] == ids["output_alpha"]:
                doubled.append(output.ssa.Instruction(133, (8, squared, 899, 899)))
                item = output.ssa.Instruction(133, (8, ids["output_alpha"], ids["clamped_coverage"], squared))
            doubled.append(item)
        for name, altered, bound in (("wrong_chart", wrong_chart, header[3]),
                                     ("shared_weight", shared_weight, header[3]),
                                     ("double_aperture", doubled, header[3] + 1)):
            with self.subTest(mutant=name):
                self.assertTrue(altered != instructions, "mutant must actually alter the candidate")
                mutant_header = [*header[:3], bound, header[4]]
                self.validate(repack(mutant_header, altered, markers))
                with self.assertRaises(AssertionError):
                    assert_causal_structure(self, altered, record)

    def test_actual_legacy_guard_refuses_marked_candidate(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ unavailable for actual legacy-guard call-through")
        with tempfile.TemporaryDirectory(prefix="r1-presenter-guard-") as directory:
            root = Path(directory)
            source = root / "guard.cpp"
            source.write_text('''#include "layer/mask_module_guard.h"
#include <fstream>
#include <iterator>
#include <vector>
std::vector<char> read(const char* p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){if(argc!=3)return 2;const auto a=read(argv[1]),b=read(argv[2]);
return !mr::mask::legacyOverrideAllowed(a.data(),a.size()) ||
mr::mask::legacyOverrideAllowed(b.data(),b.size()) ||
mr::mask::legacyOverrideAllowed(b.data(),b.size(),a.data(),a.size());}
''')
            executable, original, candidate = (root / name for name in ("guard", "original.spv", "candidate.spv"))
            original.write_bytes(self.original)
            candidate.write_bytes(output.transform(self.original, mapping())[0])
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                                    "-I", str(ROOT), str(source), "-o", str(executable)],
                                   capture_output=True, text=True, timeout=30, check=False)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable), str(original), str(candidate)],
                                 capture_output=True, text=True, timeout=10, check=False)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_cli_validates_before_exclusive_output_and_publishes_real_off(self):
        with tempfile.TemporaryDirectory(prefix="r1-presenter-cli-") as directory, \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(directory)
            source, definition, module, record = (root / name for name in ("original.spv", "mapping.json", "output.spv", "record.json"))
            source.write_bytes(self.original)
            definition.write_text(json.dumps(mapping()))
            args = ["--original", str(source), "--mapping", str(definition), "--output", str(module), "--record", str(record)]
            with mock.patch.object(output.subprocess, "run") as validator:
                validator.return_value.returncode = 1
                self.assertEqual(output.main(args), 2)
                self.assertFalse(module.exists())
                self.assertFalse(record.exists())
            self.assertEqual(output.main(args), 0)
            saved = module.read_bytes(), record.read_bytes()
            self.assertTrue(json.loads(saved[1])["spirv_val_passed"])
            self.assertEqual(output.main(args), 2)
            self.assertEqual((module.read_bytes(), record.read_bytes()), saved)
            module.unlink()
            record.unlink()
            self.assertEqual(output.main(args + ["--disabled"]), 0)
            self.assertEqual(module.read_bytes(), self.original)
            module.unlink()
            record.unlink()
            record.write_text("existing evidence")
            self.assertEqual(output.main(args), 2)
            self.assertFalse(module.exists())
            self.assertEqual(record.read_text(), "existing evidence")
            record.unlink()
            source.unlink()
            source.symlink_to(definition)
            self.assertEqual(output.main(args), 2)
            self.assertFalse(module.exists())


if __name__ == "__main__":
    unittest.main()
