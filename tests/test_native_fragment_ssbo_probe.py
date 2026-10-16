"""Bounded owned SSBO contracts; exact native controls need explicit private input."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import native_fragment_ssbo_probe as tool


def admission():
    return {"schema": tool.ADMISSION_SCHEMA, "scope": tool.ADMISSION_SCOPE,
        "fragment_stores_and_atomics_enabled": True, "own_layout_compatible": True,
        "descriptor_set": 1, "max_bound_descriptor_sets": 8, "max_per_stage_storage_buffers": 8,
        "max_storage_buffer_range": 1 << 27, "descriptor_range": 128}


def instructions(binary):
    words = struct.unpack("<%dI" % (len(binary)//4), binary); cursor = 5; result = []
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 65535
        if not 0 < size <= len(words)-cursor: raise ValueError("invalid bounded instruction")
        result.append(tool.ssa.Instruction(opcode, words[cursor+1:cursor+size])); cursor += size
    return result


class OwnedControls(unittest.TestCase):
    def test_only_explicit_enabled_owned_device_and_layout_can_admit(self):
        self.assertEqual(tool.admission_contract(admission()), 1)
        for key, value in (("scope", "NATIVE_READY"), ("schema", "future"),
                ("fragment_stores_and_atomics_enabled", False), ("fragment_stores_and_atomics_enabled", 1),
                ("own_layout_compatible", False), ("own_layout_compatible", "true"),
                ("descriptor_set", 0), ("descriptor_set", 8), ("descriptor_set", True),
                ("max_bound_descriptor_sets", 1), ("max_bound_descriptor_sets", 33),
                ("max_per_stage_storage_buffers", 0), ("max_storage_buffer_range", 127),
                ("descriptor_range", 127), ("descriptor_range", 256), ("max_storage_buffer_range", -1)):
            bad = admission(); bad[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(tool.probe.ProbeError): tool.admission_contract(bad)
        for invalid in ({}, None, dict(admission(), supported_instead_of_enabled=True)):
            with self.assertRaises(tool.probe.ProbeError): tool.admission_contract(invalid)
        for disabled in (False, None, 0, 1):
            with self.subTest(disabled=disabled), self.assertRaises(tool.probe.ProbeError): tool.admission_contract(admission(), disabled)

    def test_record_is_complete_nonoverlapping_and_finite(self):
        spans = sorted((at, at+count) for at, count in tool.FIELDS.values())
        self.assertEqual(spans[0][0], 0); self.assertEqual(spans[-1][1], 32)
        self.assertTrue(all(a[1] == b[0] for a, b in zip(spans, spans[1:])))
        self.assertEqual(tool.RECORD_BYTES, 4*tool.RECORD_WORDS)
        for coordinate in ((-1, 0), (0, 16384), (True, 0), (0,), "00", [0, 0, 0]):
            with self.subTest(coordinate=coordinate), self.assertRaisesRegex(tool.probe.ProbeError, "coordinates"):
                tool.transform(b"", admission(), coordinate)


class PrivateControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get("FRAME_PRIVATE_ORIGINAL_PS")
        if not name: raise unittest.SkipTest("explicit private module absent")
        cls.original = tool.recovery.read_owned(Path(name), tool.ssa.MAX_BYTES)
        if tool.probe.digest(cls.original) != tool.ssa.ORIGINAL_SHA256: raise AssertionError("wrong private source")

    def test_original_instruction_stream_and_exact_input_loads_preserved(self):
        binary, record = tool.transform(self.original, admission())
        changed = instructions(binary); original = instructions(self.original); cursor = 0
        for item in changed:
            if cursor < len(original) and item == original[cursor]: cursor += 1
        self.assertEqual(cursor, len(original))
        self.assertEqual([i for i in original if i.opcode == 15], [i for i in changed if i.opcode == 15])
        native_loads = [i for i in original if i.opcode == 61]
        self.assertEqual([i for i in changed if i.opcode == 61 and i.operands[2] != 487], native_loads)
        self.assertEqual(sum(i.opcode == 61 for i in changed), len(native_loads)+1)
        self.assertEqual([i for i in original if i.opcode == 17], [i for i in changed if i.opcode == 17])
        self.assertEqual([i for i in original if i.opcode == 62 and i.operands[0] == 504],
                         [i for i in changed if i.opcode == 62 and i.operands[0] == 504])
        old_bound = struct.unpack_from("<I", self.original, 12)[0]
        self.assertFalse(any(i.opcode == 59 and i.operands[1] >= old_bound and i.operands[2] == 3 for i in changed))
        self.assertEqual(sum(i.opcode == 230 for i in changed), 1)
        self.assertEqual(sum(i.opcode == 62 for i in changed), sum(i.opcode == 62 for i in original)+31)
        variable = record["new_ssa_ids"]["variable"]
        self.assertIn(tool.ssa.Instruction(71, (variable, 34, 1)), changed)
        self.assertEqual(record["added_input_loads"], 1)
        self.assertEqual(record["added_native_ubo_or_sampler_loads"], 0)
        self.assertEqual(record["original_descriptor_sets"], [0])
        for key in ("caller_facts_are_native_observations", "runtime_or_pipeline_activation", "native_feature_enabled_proven",
                "native_layout_compatible_proven", "native_output_lifetime_proven", "native_frame_association_proven",
                "physical_eye_proven", "application_ray_mapping_proven", "linear_game_alpha_proven",
                "alpha_composition_authorized", "passthrough_authorized"):
            self.assertIs(record[key], False)
        mutated = bytearray(self.original); mutated[8] ^= 1
        with self.assertRaisesRegex(tool.probe.ProbeError, "unknown original"): tool.transform(bytes(mutated), admission())

    def test_cli_spirv_validation_exclusive_output_and_no_feature_refusal(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary); original, definition, output, record = [root/name for name in ("original.spv", "admission.json", "private.spv", "record.json")]
            original.write_bytes(self.original); definition.write_text(json.dumps(admission()))
            args = ["--original", str(original), "--admission", str(definition), "--output", str(output), "--record", str(record)]
            self.assertEqual(tool.main(args+["--disabled"]), 2); self.assertFalse(output.exists())
            self.assertEqual(tool.main(args), 0)
            self.assertTrue(json.loads(record.read_bytes())["spirv_val_passed"])
            previous = output.read_bytes(), record.read_bytes()
            self.assertEqual(tool.main(args), 2); self.assertEqual((output.read_bytes(), record.read_bytes()), previous)
            output.unlink(); record.unlink()
            definition.write_text(json.dumps({**admission(), "fragment_stores_and_atomics_enabled": False}))
            self.assertEqual(tool.main(args), 2); self.assertFalse(output.exists()); self.assertFalse(record.exists())
            definition.write_text(json.dumps(admission())); original.unlink(); original.symlink_to(definition)
            self.assertEqual(tool.main(args), 2); self.assertFalse(output.exists())


if __name__ == "__main__": unittest.main()
