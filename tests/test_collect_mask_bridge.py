"""Read-only collector contracts; fixtures contain no Valve shader payload."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "collector", Path(__file__).parents[1] / "tools" / "collect_mask_bridge.py")
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)

# Reflection-only synthetic fixture. The instruction bodies are deliberately
# incomplete: the collector must not claim validation/UV mapping from metadata.
ASSEMBLY = '''
; Synthetic test shader
%1 = OpExtInstImport "GLSL.std.450"
OpEntryPoint Fragment %4 "main" %uv %504
OpName %uv "PRIVATE_SHADER_STRING_DO_NOT_SHARE"
%s = OpString "PRIVATE_SHADER_STRING_DO_NOT_SHARE"
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 7
OpDecorate %uv Location 0
OpDecorate %504 Location 0
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%image = OpTypeImage %float 2D 0 0 0 1 Unknown
%sampled = OpTypeSampledImage %image
%texture_ptr = OpTypePointer UniformConstant %sampled
%uv_ptr = OpTypePointer Input %float
%out_ptr = OpTypePointer Output %v4float
%pc_ptr = OpTypePointer PushConstant %struct
%texture = OpVariable %texture_ptr UniformConstant
%uv = OpVariable %uv_ptr Input
%504 = OpVariable %out_ptr Output
%pc = OpVariable %pc_ptr PushConstant
%4 = OpFunction %void None %3
%sample = OpImageSampleImplicitLod %v4float %loaded_texture %883
OpStore %504 %994
'''


class ReflectionTests(unittest.TestCase):
    def test_reflection_records_interfaces_without_disclosing_strings(self):
        summary = collector.summarize_shader(ASSEMBLY)
        self.assertEqual(summary["entry_points"], [{"execution_model": "Fragment", "id": "%4"}])
        self.assertEqual(len(summary["descriptors"]), 1)
        descriptor = summary["descriptors"][0]
        self.assertEqual((descriptor["DescriptorSet"], descriptor["Binding"]), (2, 7))
        self.assertEqual(descriptor["type"]["pointee_kind"], "OpTypeSampledImage")
        self.assertEqual(descriptor["type"]["image_type_id"], "%image")
        self.assertEqual({entry["storage_class"] for entry in summary["interfaces"]},
                         {"Input", "Output", "PushConstant"})
        self.assertEqual(summary["sample_operations"][0]["coordinate_id"], "%883")
        self.assertNotIn("PRIVATE_SHADER_STRING", json.dumps(summary))
        self.assertFalse(summary["coordinate_mapping_proven"])
        self.assertFalse(summary["decoded_frame_identity_proven"])

    def test_marker_changes_are_detected_without_patching_anything(self):
        summary = collector.summarize_shader(ASSEMBLY.replace("OpStore %504 %994", "OpStore %504 %998"))
        self.assertFalse(summary["upstream_patch_markers"]["original_store"])
        duplicate = collector.summarize_shader(ASSEMBLY + "\nOpStore %504 %994\n")
        self.assertFalse(duplicate["upstream_patch_markers"]["original_store"])

    def test_reflection_records_are_bounded(self):
        fixture = ASSEMBLY + "\n" + "\n".join(
            f"%sample{i} = OpImageSampleImplicitLod %v4float %tex %uv" for i in range(300))
        summary = collector.summarize_shader(fixture)
        self.assertEqual(len(summary["sample_operations"]), collector.MAX_RECORDS)
        self.assertTrue(summary["truncated"])


class CollectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.steamvr = self.root / "steamvr"
        self.shader = self.steamvr / collector.DEFAULT_SHADER
        self.shader.parent.mkdir(parents=True)
        self.shader.write_bytes(struct.pack("<5I", 0x07230203, 0x00010300, 0, 100, 0))
        self.output = self.root / "output"
        self.icd = self.root / "icd"
        self.icd.mkdir()
        self.os_release = self.root / "os-release"
        self.os_release.write_text('ID=steamos\nVERSION_ID="3.8"\nBUILD_ID=20261007\n'
                                   'SECRET="DO_NOT_SHARE_CREDENTIAL_CANARY"\n')

    def perform_collect(self, **changes):
        arguments = {"icd_dirs": [self.icd], "os_release": self.os_release, **changes}
        with mock.patch.object(collector.shutil, "which", return_value="/mock/spirv-dis"):
            with mock.patch.object(collector, "run_bounded", return_value=(0, ASSEMBLY.encode())):
                return collector.collect(self.output, self.steamvr, **arguments)

    def test_report_is_metadata_only_and_shader_stays_private(self):
        # A credential-bearing adjacent config must never be opened/copied.
        config = self.steamvr / "config" / "steamvr.vrsettings"
        config.parent.mkdir()
        config.write_text('password=DO_NOT_SHARE_CREDENTIAL_CANARY')
        (self.icd / "test.json").write_text(json.dumps({
            "ICD": {"library_path": "/private/username/libvulkan_test.so", "api_version": "1.4.309"},
            "credentials": "DO_NOT_SHARE_CREDENTIAL_CANARY"}))
        report = self.perform_collect()
        public = (self.output / "report.json").read_text()
        self.assertNotIn("DO_NOT_SHARE", public)
        self.assertNotIn("PRIVATE_SHADER_STRING", public)
        self.assertNotIn(str(self.root), public)
        self.assertNotIn("/private/username", public)
        self.assertNotIn("OpStore", public)
        self.assertEqual(report["os"], {"ID": "steamos", "VERSION_ID": "3.8", "BUILD_ID": "20261007"})
        self.assertEqual(report["vulkan_icds"], [
            {"file": "test.json", "api_version": "1.4.309", "library": "libvulkan_test.so"}])
        self.assertFalse(report["bridge_status"]["ready_for_stream_mask_binding"])
        private = self.output / "private" / "distort_ps_achromatic_se2.spvasm"
        self.assertEqual(private.read_text(), ASSEMBLY)
        self.assertEqual(private.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.output / ".gitignore").read_text(), "*\n")

    def test_missing_shader_is_reported_and_does_not_create_an_assembly(self):
        self.shader.unlink()
        report = self.perform_collect()
        self.assertFalse(report["shader"]["present"])
        self.assertTrue(report["warnings"])
        self.assertEqual(list((self.output / "private").iterdir()), [])

    def test_bad_spirv_is_not_sent_to_tool(self):
        self.shader.write_bytes(b"not a spir-v module")
        with mock.patch.object(collector, "run_bounded") as tool:
            report = collector.collect(self.output, self.steamvr, icd_dirs=[self.icd],
                                       os_release=self.os_release)
        tool.assert_not_called()
        self.assertTrue(report["warnings"])
        self.assertNotIn("interface", report["shader"])

    def test_nonempty_output_is_rejected_without_deleting_anything(self):
        self.output.mkdir()
        retained = self.output / "retain.txt"
        retained.write_text("must be kept")
        with self.assertRaises(collector.CollectionError):
            self.perform_collect()
        self.assertEqual(retained.read_text(), "must be kept")

    def test_symlink_output_is_rejected(self):
        real = self.root / "real"
        real.mkdir()
        self.output.symlink_to(real, target_is_directory=True)
        with self.assertRaises(collector.CollectionError):
            self.perform_collect()
        self.assertEqual(list(real.iterdir()), [])

    def test_missing_disassembler_has_actionable_warning(self):
        with mock.patch.object(collector.shutil, "which", return_value=None):
            report = collector.collect(self.output, self.steamvr, icd_dirs=[self.icd],
                                       os_release=self.os_release)
        self.assertIn("spirv-dis is missing", report["warnings"][0])
        self.assertEqual(list((self.output / "private").iterdir()), [])

    def test_tool_failure_does_not_publish_tool_output(self):
        with mock.patch.object(collector.shutil, "which", return_value="/mock/spirv-dis"):
            with mock.patch.object(collector, "run_bounded", return_value=(1, b"secret tool output")):
                report = collector.collect(self.output, self.steamvr, icd_dirs=[self.icd],
                                           os_release=self.os_release)
        self.assertNotIn("secret", json.dumps(report))
        self.assertIn("rejected", report["warnings"][0])

    def test_oversized_input_is_rejected(self):
        source = self.root / "oversized"
        source.write_bytes(b"12345")
        with self.assertRaises(collector.CollectionError):
            collector.limited_read(source, 4)

    @unittest.skipUnless(shutil.which("spirv-as") and shutil.which("spirv-dis"), "spirv-tools required")
    def test_collects_real_tool_disassembly_of_our_own_shader(self):
        # Compile our own tiny shader so this exercises real command execution,
        # header checking, SPIR-V disassembly and reflection end to end.
        assembly = self.root / "ours.spvasm"
        assembly.write_text('''OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %color
OpExecutionMode %main OriginUpperLeft
OpDecorate %color Location 0
%void = OpTypeVoid
%function = OpTypeFunction %void
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%pointer = OpTypePointer Output %v4float
%zero = OpConstant %float 0
%rgba = OpConstantComposite %v4float %zero %zero %zero %zero
%color = OpVariable %pointer Output
%main = OpFunction %void None %function
%label = OpLabel
OpStore %color %rgba
OpReturn
OpFunctionEnd
''')
        subprocess.run(["spirv-as", "--target-env", "vulkan1.1", str(assembly), "-o", str(self.shader)],
                       check=True, capture_output=True)
        report = collector.collect(self.output, self.steamvr, icd_dirs=[self.icd],
                                   os_release=self.os_release)
        self.assertEqual(report["warnings"], [])
        self.assertEqual(len(report["shader"]["interface"]["interfaces"]), 1)
        self.assertEqual(report["shader"]["interface"]["interfaces"][0]["storage_class"], "Output")
        self.assertTrue((self.output / "private" / "distort_ps_achromatic_se2.spvasm").is_file())


class ToolBoundaryTests(unittest.TestCase):
    def test_read_only_subprocess_has_no_shell_interpolation(self):
        code, data = collector.run_bounded(
            [sys.executable, "-c", "import sys; print(sys.argv[1])", "$(must_not_execute)"],
            output_limit=4096)
        self.assertEqual(code, 0)
        self.assertEqual(data.strip(), b"$(must_not_execute)")

    def test_subprocess_timeout_is_bounded(self):
        with self.assertRaisesRegex(collector.CollectionError, "time limit"):
            collector.run_bounded([sys.executable, "-c", "import time; time.sleep(10)"], timeout=0.05)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux resource boundary")
    def test_subprocess_cannot_fill_disk_with_output(self):
        code, data = collector.run_bounded(
            [sys.executable, "-c", "import os; os.write(1, b'x' * 1000000); os.write(1, b'x')"],
            output_limit=4096)
        self.assertNotEqual(code, 0)
        self.assertLessEqual(len(data), 4096)


if __name__ == "__main__":
    unittest.main()
