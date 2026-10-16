"""Separate count1 SSBO grammar, exact source/access and artifact contracts."""
import contextlib
import io
import json
import os
import re
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import native_fragment_ssbo_profile as tool


class PublicControls(unittest.TestCase):
    def test_distinct_bounded_grammar_never_broadens_original_parser(self):
        data = struct.pack("<7I", 0x07230203, 0x10000, 0, 2, 0, (2 << 16)|29, 1)
        self.assertEqual(tool.parse_ssbo_module(data)[1], [tool.ssa.Instruction(29, (1,))])
        with self.assertRaises(tool.ssbo.probe.ProbeError): tool.ssa.parse_module(data)
        for candidate in (data[:-1], b"", data[:20]+struct.pack("<I", (6 << 16)|230),
                          data[:20]+struct.pack("<I", (1 << 16)|65535)):
            with self.assertRaises(tool.ssbo.probe.ProbeError): tool.parse_ssbo_module(candidate)
        self.assertNotIn(29, tool.ssa.ALLOWED)
        self.assertNotIn(109, tool.ssa.ALLOWED)
        for bad in (data, bytes(tool.ssa.ORIGINAL_BYTES)):
            with self.assertRaisesRegex(tool.ssbo.probe.ProbeError, "exact original"): tool.transform(bad)


@unittest.skipUnless(os.environ.get("FRAME_NATIVE_PS"), "private native source is explicit and not distributed")
class PrivateControls(unittest.TestCase):
    def setUp(self): self.original = Path(os.environ["FRAME_NATIVE_PS"]).read_bytes()

    def test_exact_profile_preserves_original_and_ssbo_except_owned_variable_type(self):
        active, record = tool.transform(self.original)
        header, changed = tool.parse_ssbo_module(active)
        canonical, _ = tool.ssbo.transform(self.original, tool.canonical_admission())
        old_header, old = tool.parse_ssbo_module(canonical)
        fresh = record["new_interface_ids"]
        self.assertEqual(fresh, list(range(old_header[3], old_header[3]+3)))
        self.assertEqual(header[3], old_header[3]+3)
        stripped = [item for item in changed if item not in (
            tool.ssa.Instruction(43, (6, fresh[0], 1)), tool.ssa.Instruction(28, (fresh[1], 286, fresh[0])),
            tool.ssa.Instruction(32, (fresh[2], 0, fresh[1])))]
        stripped = [tool.ssa.Instruction(59, (288, 289, 0)) if item == tool.ssa.Instruction(59, (fresh[2], 289, 0)) else item for item in stripped]
        self.assertEqual(stripped, old)  # Every original output/access and added SSBO opcode.
        self.assertEqual(record["output_sha256"], "6d666c261114f93436e48c1f52793fe18afbce9552e07b53d4212654c4560020")
        self.assertEqual(len(active), 9864)
        self.assertIn(tool.ssa.Instruction(43, (6, 102, 3)), changed)  # Shared constant remains3.
        self.assertEqual(sum(i.opcode == 230 for i in changed), 1)
        self.assertEqual(record["original_b9_access_indices"], [0, 0])
        for key in ("native_descriptor_layout_modified", "native_creation_proven", "native_same_draw_eye_frame_binding_proven",
                    "runtime_activation_authorized", "passthrough_authorized"):
            self.assertIs(record[key], False)
        # Reject a changed instrumentation access even when its header is valid.
        wrong = bytearray(canonical); wrong[-16] ^= 1
        with self.assertRaisesRegex(tool.ssbo.probe.ProbeError, "exact canonical"): tool.specialize(self.original, bytes(wrong))
        other, _ = tool.ssbo.transform(self.original, tool.canonical_admission(), (1, 0))
        with self.assertRaisesRegex(tool.ssbo.probe.ProbeError, "exact canonical"): tool.specialize(self.original, other)
        wrong = bytearray(self.original); wrong[8] ^= 1
        with self.assertRaises(tool.ssbo.probe.ProbeError): tool.transform(bytes(wrong))

    def test_cli_validates_and_exclusive_output(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(directory); source, output, record = [root/name for name in ("source.spv", "owned.spv", "record.json")]
            source.write_bytes(self.original)
            args = ["--original", str(source), "--output", str(output), "--record", str(record)]
            self.assertEqual(tool.main(args), 0)
            self.assertTrue(json.loads(record.read_bytes())["spirv_val_passed"])
            previous = output.read_bytes(), record.read_bytes()
            self.assertEqual(tool.main(args), 2)
            self.assertEqual((output.read_bytes(), record.read_bytes()), previous)

    def test_first_covered_is_distinct_and_cpp_tables_contain_only_owned_additions(self):
        active, record = tool.transform(self.original, True)
        self.assertEqual(record["output_sha256"], "e76f8832c87229138e4c6b21bcab7b11e63d98bc016d42f5afb8dd7098c01bcd")
        self.assertEqual(len(active), 9616)
        self.assertIsNone(record["selected_fragment"])
        self.assertTrue(record["first_covered_invocation"])
        self.assertEqual(record["profile"], tool.FIRST_COVERED_PROFILE)
        _, instructions = tool.parse_ssbo_module(active)
        original_header, original = tool.ssa.parse_module(self.original)
        # First-covered adds no coordinate/input load; both original sampling
        # streams and original output remain the canonical same invocation.
        self.assertEqual([i for i in instructions if i.opcode == 61], [i for i in original if i.opcode == 61])
        self.assertEqual(sum(i.opcode == 230 for i in instructions), 1)
        self.assertEqual(sum(i.opcode == 247 for i in instructions), sum(i.opcode == 247 for i in original)+1)
        with self.assertRaisesRegex(tool.ssbo.probe.ProbeError, "exact canonical"): tool.specialize(self.original, active, False)
        tables = tool.cpp_template_data(self.original)
        header = (Path(__file__).resolve().parents[1]/"layer/pixel_ssbo_profile_transform.h").read_text()
        for name, data in tables.items():
            if isinstance(data, list):
                declaration = re.search(r"\b"+name+r"\[\]=\{(.*?)\};", header, re.S)
                self.assertIsNotNone(declaration)
                self.assertEqual([int(value) for value in re.findall(r"(\d+)u", declaration[1])], data)
            else: self.assertIn(name+"="+str(data)+"u;", header)
        # Generated body output uses original IDs, while newly defined result
        # IDs live strictly beyond the exact original bound.
        self.assertEqual(tables["original_bound"], original_header[3])

    def test_sparse32_is_first_eligible_only_and_preserves_original_stream(self):
        active, record = tool.transform(self.original, election="sparse32")
        self.assertEqual(record["output_sha256"], "eb242f9252a51393e0794e12afed36a90de3e1059d45310d8b4c70d543019efd")
        self.assertEqual(len(active), 9888)
        self.assertEqual(record["profile"], tool.SPARSE32_PROFILE)
        self.assertTrue(record["sparse_grid32"])
        self.assertFalse(record["first_covered_invocation"])
        self.assertIn("safe incomplete/refusal", record["sparse_empty_claim_means"])
        self.assertIsNone(record["selected_fragment"])
        _, instructions = tool.parse_ssbo_module(active)
        _, original = tool.ssa.parse_module(self.original)
        self.assertEqual(sum(item.opcode == 109 for item in instructions), 2)
        self.assertEqual(sum(item.opcode == 230 for item in instructions), 1)
        self.assertEqual([item for item in instructions if item.opcode == 62 and item.operands[0] == 504],
                         [item for item in original if item.opcode == 62 and item.operands[0] == 504])
        self.assertEqual([item for item in instructions if item.opcode == 65 and item.operands[2] == 289],
                         [item for item in original if item.opcode == 65 and item.operands[2] == 289])
        for bad in (True, 32, "sparse16"):
            with self.assertRaises(tool.ssbo.probe.ProbeError): tool.transform(self.original, election=bad)
        with self.assertRaises(tool.ssbo.probe.ProbeError): tool.transform(self.original, True, "sparse32")


if __name__ == "__main__": unittest.main()
