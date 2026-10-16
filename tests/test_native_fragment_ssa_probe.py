"""Owned parser/control fixtures; native checks need an explicit private file."""
import contextlib
from dataclasses import replace
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_fragment_ssa_probe as reader


def module(instructions, version=0x10000, bound=100):
    words = [0x07230203, version, 0, bound, 0]
    words.extend(word for item in instructions for word in item.words())
    return struct.pack("<%dI" % len(words), *words)


class OwnedControlTests(unittest.TestCase):
    def test_bounded_instruction_framing_and_supported_versions(self):
        # Original tiny instruction-framing fixture, not a native shader clone.
        owned = module([reader.Instruction(17, (1,)), reader.Instruction(19, (2,))])
        header, parsed = reader.parse_module(owned)
        self.assertEqual(header[3], 100); self.assertEqual(len(parsed), 2)
        for version in (0x10000, 0x10600): self.assertEqual(reader.parse_module(module([], version))[0][1], version)
        malformed = [b"", owned[:-1], b"\0" * 20, module([], 0x10700), module([], 0x20000),
                     module([], bound=0), module([], bound=65537), b"\0" * (reader.MAX_BYTES + 4),
                     module([]) + struct.pack("<I", 0), module([]) + struct.pack("<I", 3 << 16 | 17),
                     module([reader.Instruction(65535, ())])]
        for value in malformed:
            with self.subTest(length=len(value)), self.assertRaises(reader.probe.ProbeError): reader.parse_module(value)
        with self.assertRaises(reader.probe.ProbeError): reader.parse_module(bytearray(owned))
        with self.assertRaisesRegex(reader.probe.ProbeError, "unknown original"):
            reader.transform(owned)

    def test_control_flow_dominance_diamond_loop_and_unknown_target(self):
        diamond = {1: [2, 3], 2: [4], 3: [4], 4: []}
        result = reader.dominators(diamond, 1)
        self.assertEqual(result[4], {1, 4})
        self.assertNotIn(2, result[4]); self.assertNotIn(3, result[4])
        self.assertEqual(reader.dominators({1: [2], 2: [2, 3], 3: []}, 1)[3], {1, 2, 3})
        for graph, entry in (({1: [9]}, 1), ({1: [], 2: []}, 1), ({1: []}, 9)):
            with self.assertRaises(reader.probe.ProbeError): reader.dominators(graph, entry)

    def test_value_operand_rules_do_not_treat_shuffle_indices_as_ids(self):
        self.assertEqual(reader.value_inputs(reader.Instruction(79, (9, 100, 11, 12, 0, 1, 2, 3))), (11, 12))
        self.assertEqual(reader.value_inputs(reader.Instruction(81, (8, 100, 11, 3, 0))), (11,))
        self.assertEqual(reader.value_inputs(reader.Instruction(245, (8, 100, 11, 22, 12, 23))), (11, 12))


class PrivateOriginalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get("FRAME_PRIVATE_ORIGINAL_PS")
        if not name: raise unittest.SkipTest("explicit private original absent; no native fixture is tracked")
        cls.original = reader.recovery.read_owned(Path(name), reader.MAX_BYTES)
        if reader.probe.digest(cls.original) != reader.ORIGINAL_SHA256:
            raise AssertionError("supplied private original does not match the pinned profile")

    def test_exact_original_bodies_and_output_zero_survive_transform(self):
        patched, record = reader.transform(self.original)
        before, original = reader.parse_module(self.original)
        after, modified = reader.parse_module(patched)
        self.assertEqual(after[:3], before[:3]); self.assertEqual(after[3], before[3] + 10)
        restored, additions = [], []
        for item in modified:
            a, op = item.operands, item.opcode
            new_id = a[0] if op in reader.FIRST_RESULT else a[1] if op in reader.TYPED_RESULT else None
            if ((new_id is not None and new_id >= before[3])
                    or (op == 71 and a[0] >= before[3]) or (op == 62 and a[0] >= before[3])):
                additions.append(item)
            else: restored.append(replace(item, operands=a[:-4]) if op == 15 else item)
        self.assertEqual(restored, original)
        self.assertEqual(sum(item.opcode == 61 for item in additions), 0)
        self.assertEqual(sum(item.opcode == 62 for item in additions), 4)
        self.assertFalse(any(item.opcode == 71 and item.operands[1] in (33, 34) for item in additions))
        self.assertEqual(record["outputs"][4]["format"], "R32G32B32A32_UINT")
        self.assertEqual(record["instrumented_sha256"], reader.probe.digest(patched))
        for key in ("runtime_or_pipeline_activation", "native_draw_coherence_proven", "native_frame_association_proven",
                    "application_ray_mapping_proven", "physical_eye_proven", "native_sampler_equivalence_proven",
                    "alpha_composition_authorized", "passthrough_authorized"):
            self.assertIs(record[key], False)

    def test_type_and_dominance_checks_are_not_only_a_hash_label(self):
        header, original = reader.parse_module(self.original)
        for role, (ident, _) in reader.ANCHORS.items():
            changed = [replace(item, operands=(header[3] - 1, *item.operands[1:]))
                       if item.opcode in reader.TYPED_RESULT and item.operands[1] == ident else item for item in original]
            with self.subTest(role=role), self.assertRaisesRegex(reader.probe.ProbeError, "SSA type/definition"):
                reader.profile_structure(header, changed)
        # Move u into a single eye branch. Its type stays correct, but it no
        # longer dominates the shared output; never repair this by reloading u.
        target = next(item for item in original if item.opcode in reader.TYPED_RESULT and item.operands[1] == reader.ANCHORS["u"][0])
        changed = [item for item in original if item is not target]
        labels = [i for i, item in enumerate(changed) if item.opcode == 248]
        changed.insert(labels[1] + 1, target)
        with self.assertRaisesRegex(reader.probe.ProbeError, "does not dominate"):
            reader.profile_structure(header, changed)
        changed_bytes = bytearray(self.original); changed_bytes[8] ^= 1
        with self.assertRaisesRegex(reader.probe.ProbeError, "unknown original"): reader.transform(bytes(changed_bytes))
        patched, _ = reader.transform(self.original)
        with self.assertRaisesRegex(reader.probe.ProbeError, "unknown original"): reader.transform(patched)

    def test_private_cli_validation_fail_closed_and_safe_exclusive_files(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary)
            original, output, record = (root / name for name in ("private-original.spv", "private-output.spv", "record.json"))
            original.write_bytes(self.original)
            args = ["--original", str(original), "--output", str(output), "--record", str(record)]
            with mock.patch.object(reader.subprocess, "run") as validator:
                validator.return_value.returncode = 1
                self.assertEqual(reader.main(args), 2)
                self.assertFalse(output.exists()); self.assertFalse(record.exists())
            self.assertEqual(reader.main(args), 0)
            output_bytes, record_bytes = output.read_bytes(), record.read_bytes()
            self.assertEqual(json.loads(record_bytes)["spirv_val_passed"], True)
            self.assertEqual(reader.main(args), 2)
            self.assertEqual(output.read_bytes(), output_bytes); self.assertEqual(record.read_bytes(), record_bytes)
            original.unlink(); original.symlink_to(output)
            self.assertEqual(reader.main(args), 2)
            original.unlink(); original.write_bytes(self.original)
            link = root / "link"; link.symlink_to(root, target_is_directory=True)
            args[1] = str(link / original.name)
            self.assertEqual(reader.main(args), 2)


if __name__ == "__main__":
    unittest.main()
