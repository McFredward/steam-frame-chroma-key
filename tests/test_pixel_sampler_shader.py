"""Validate the embedded original compute diagnostic and its source provenance."""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SamplerShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "layer/pixel_sampler_probe_spv.h").read_text()
        match = re.search(r"PixelSamplerProbeSpirv\[\]\s*=\s*\{([^}]*)\}", cls.header)
        if not match:
            raise AssertionError("missing bounded embedded sampler SPIR-V")
        words = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{8})u", match.group(1))]
        if not 5 <= len(words) <= 4096:
            raise AssertionError("shader extent must be 20..16384 bytes")
        cls.binary = struct.pack("<" + "I" * len(words), *words)

    def test_exact_source_and_binary_digests(self):
        source = (ROOT / "shaders/pixel_sampler_probe.comp").read_bytes()
        self.assertIn("GLSL SHA-256: " + hashlib.sha256(source).hexdigest(), self.header)
        self.assertIn("SPIR-V SHA-256: " + hashlib.sha256(self.binary).hexdigest(), self.header)
        self.assertEqual(struct.unpack_from("<I", self.binary)[0], 0x07230203)

    def test_embedded_shader_validates(self):
        validator = shutil.which("spirv-val")
        self.assertIsNotNone(validator, "spirv-val is required for shader validation")
        with tempfile.TemporaryDirectory(prefix="frame-sampler-spirv-") as temporary:
            shader = Path(temporary) / "sampler.spv"
            shader.write_bytes(self.binary)
            result = subprocess.run([validator, "--target-env", "vulkan1.0", str(shader)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_recompiled_source_validates_and_matches(self):
        compiler, validator = shutil.which("glslc"), shutil.which("spirv-val")
        self.assertIsNotNone(compiler, "glslc is required to verify generated shader provenance")
        self.assertIsNotNone(validator, "spirv-val is required for shader validation")
        with tempfile.TemporaryDirectory(prefix="frame-sampler-source-") as temporary:
            shader = Path(temporary) / "sampler.spv"
            result = subprocess.run([compiler, "-O", "--target-env=vulkan1.0",
                                     str(ROOT / "shaders/pixel_sampler_probe.comp"), "-o", str(shader)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([validator, "--target-env", "vulkan1.0", str(shader)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            # Compiler version changes may legitimately change the instruction
            # stream. Keep source/hash/validation mandatory, while comparing the
            # essential compiled resource/operation contract independently below.
            self.assert_compiled_contract(shader.read_bytes())
            self.assert_compiled_contract(self.binary)

    def assert_compiled_contract(self, binary):
        words = struct.unpack("<" + "I" * (len(binary) // 4), binary)
        offset, instructions = 5, []
        while offset < len(words):
            count, opcode = words[offset] >> 16, words[offset] & 0xffff
            self.assertGreater(count, 0)
            self.assertLessEqual(offset + count, len(words))
            instructions.append((opcode, words[offset + 1:offset + count]))
            offset += count
        self.assertEqual(offset, len(words))
        # OpExecutionMode LocalSize8x8x1; one explicitly-LOD image sample; no
        # image writes, barriers, atomics or push-constant storage declaration.
        self.assertTrue(any(op == 16 and args[1:] == (17, 8, 8, 1) for op, args in instructions))
        self.assertEqual(sum(op == 88 for op, _ in instructions), 1)  # OpImageSampleExplicitLod
        self.assertFalse(any(op == 99 for op, _ in instructions))  # OpImageWrite
        self.assertFalse(any(op == 59 and len(args) >= 3 and args[2] == 9 for op, args in instructions))
        bindings = {args[0]: args[2] for op, args in instructions if op == 71 and len(args) == 3 and args[1] == 33}
        sets = {args[0]: args[2] for op, args in instructions if op == 71 and len(args) == 3 and args[1] == 34}
        self.assertEqual(sorted(bindings.values()), [0, 1])
        self.assertTrue(all(sets.get(identifier) == 0 for identifier in bindings))


if __name__ == "__main__":
    unittest.main()
