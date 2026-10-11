"""Layout rejection and real SPIR-V assembly/validation regression tests.

GPU regression belongs to the headless Vulkan test. These checks deliberately
do not substitute a Python reimplementation of shader math for execution.
"""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_mask_spv", ROOT / "tools/gen_mask_spv.py")
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)
FIXTURE = ROOT / "tests/fixtures/mask_scene.spvasm"
SEPARATE_SAMPLER_FIXTURE = ROOT / "tests/fixtures/mask_scene_separate_sampler.spvasm"


def evaluate_mask_store(assembly, color, alpha):
    """Execute only the inserted arithmetic to check its declared convention.

    This bounded CPU instruction check supplements, rather than replaces, the
    actual SPIR-V validator and headless Vulkan regression.
    """
    values = {"%911": color, "%883": (0.5, 0.5), "%smask_lod_zero": 0.0,
              "%smask_texture": "same-frame-sampler"}
    active = False
    for _, tokens in PATCHER._instructions(assembly):
        if tokens[:2] == ["%smask_sampler", "="]:
            active = True
        if not active:
            continue
        if tokens[:2] == ["OpStore", "%504"]:
            return values[tokens[2]]
        target, _, operation, _result_type, *operands = tokens
        if operation == "OpLoad":
            values[target] = values[operands[0]]
        elif operation == "OpImageSampleExplicitLod":
            values[target] = (alpha, 0.0, 0.0, 1.0)
        elif operation == "OpCompositeExtract":
            values[target] = values[operands[0]][int(operands[1])]
        elif operation == "OpVectorTimesScalar":
            values[target] = tuple(channel * values[operands[1]] for channel in values[operands[0]])
        elif operation == "OpFOrdGreaterThan":
            values[target] = values[operands[0]] > values[operands[1]]
        elif operation == "OpSelect":
            values[target] = values[operands[1] if values[operands[0]] else operands[2]]
        elif operation == "OpCompositeConstruct":
            values[target] = tuple(values[operand] for operand in operands)
        else:
            raise AssertionError("unexpected inserted arithmetic instruction " + operation)
    raise AssertionError("mask output store absent")


class MaskLayoutTests(unittest.TestCase):
    def setUp(self):
        self.original = FIXTURE.read_text(encoding="utf-8")

    def rejected(self, source, message):
        with self.assertRaisesRegex(PATCHER.ShaderLayoutError, message):
            PATCHER.patch_assembly(source)

    def existing_sampler(self, descriptor_set, binding):
        source = self.original.replace("OpDecorate %source_uv Location 0", "\n".join([
            "OpDecorate %collision DescriptorSet {}".format(descriptor_set),
            "OpDecorate %collision Binding {}".format(binding),
            "OpDecorate %source_uv Location 0"]))
        return source.replace("%4 = OpFunction %void None %3", "\n".join([
            "%collision_image = OpTypeImage %float 2D 0 0 0 1 Unknown",
            "%collision_sampled = OpTypeSampledImage %collision_image",
            "%collision_pointer = OpTypePointer UniformConstant %collision_sampled",
            "%collision = OpVariable %collision_pointer UniformConstant",
            "%4 = OpFunction %void None %3"]))

    def test_native_uv_color_and_processing_are_preserved(self):
        result = PATCHER.patch_assembly(self.original)
        for line in self.original.splitlines():
            if line.strip() != "OpStore %504 %994":
                self.assertIn(line + "\n", result)
        self.assertIn("OpImageSampleExplicitLod %v4float %smask_sampler %883 Lod %smask_lod_zero", result)
        self.assertEqual(result.count("OpVectorTimesScalar %v3float %911 %smask_alpha"), 1)
        self.assertIn("OpCompositeConstruct %v4float %smask_r %smask_g %smask_b %smask_alpha", result)
        self.assertNotIn("SmoothStep", result)
        self.assertNotIn("Distance", result)
        self.assertNotIn("%mk_", result)

    def test_configurable_descriptor(self):
        result = PATCHER.patch_assembly(self.original, 2, 7)
        self.assertIn("OpDecorate %smask_texture DescriptorSet 2", result)
        self.assertIn("OpDecorate %smask_texture Binding 7", result)

    def test_explicit_straight_mode_is_unchanged_default(self):
        self.assertEqual(PATCHER.patch_assembly(self.original),
                         PATCHER.patch_assembly(self.original, rgb_convention="straight"))

    def test_unknown_rgb_convention_rejected(self):
        with self.assertRaisesRegex(PATCHER.ShaderLayoutError, "RGB convention"):
            PATCHER.patch_assembly(self.original, rgb_convention="infer")

    def test_premultiplied_rgb_is_not_multiplied_twice(self):
        result = PATCHER.patch_assembly(self.original, rgb_convention="premultiplied")
        self.assertNotIn("OpVectorTimesScalar", result)
        self.assertEqual(result.count("OpSelect %float %smask_visible"), 3)
        self.assertIn("OpFOrdGreaterThan %smask_bool_type %smask_alpha %smask_lod_zero", result)
        self.assertIn('OpModuleProcessed "MRMASK/1 source-frame-binding-required"', result)

    def test_straight_partial_alpha_and_color_independence(self):
        result = PATCHER.patch_assembly(self.original)
        for color in ((0.0, 1.0, 0.0), (1.0, 0.0, 1.0), (0.2, 0.4, 0.8)):
            for alpha in (0.0, 0.25, 0.5, 1.0):
                with self.subTest(color=color, alpha=alpha):
                    self.assertEqual(evaluate_mask_store(result, color, alpha),
                                     tuple(channel * alpha for channel in color) + (alpha,))

    def test_premultiplied_partial_alpha_and_background_noise_clear(self):
        result = PATCHER.patch_assembly(self.original, rgb_convention="premultiplied")
        for alpha, native_color, expected in (
                (0.0, (0.8, 0.1, 0.6), (0.0, 0.0, 0.0, 0.0)),
                (0.25, (0.1, 0.2, 0.05), (0.1, 0.2, 0.05, 0.25)),
                (0.5, (0.2, 0.4, 0.1), (0.2, 0.4, 0.1, 0.5)),
                (1.0, (0.4, 0.8, 0.2), (0.4, 0.8, 0.2, 1.0))):
            with self.subTest(alpha=alpha):
                self.assertEqual(evaluate_mask_store(result, native_color, alpha), expected)

    def test_frame_binding_marker_precedes_annotations(self):
        result = PATCHER.patch_assembly(self.original)
        marker = 'OpModuleProcessed "MRMASK/1 source-frame-binding-required"'
        self.assertEqual(result.count(marker), 1)
        self.assertLess(result.index(marker), result.index("OpDecorate"))

    def test_existing_frame_binding_marker_rejected(self):
        source = self.original.replace("OpDecorate %source_uv Location 0", '\n'.join([
            'OpModuleProcessed "MRMASK/1 source-frame-binding-required"',
            "OpDecorate %source_uv Location 0"]))
        self.rejected(source, "mask binding marker already present")

    def test_duplicate_scene_store_rejected(self):
        self.rejected(self.original.replace("OpReturn", "OpStore %504 %994\nOpReturn"),
                      "exactly one opaque scene store")

    def test_changed_store_rejected(self):
        self.rejected(self.original.replace("OpStore %504 %994", "OpStore %504 %native_output"),
                      "opaque scene store")

    def test_duplicate_result_identifier_rejected(self):
        self.rejected(self.original.replace("OpReturn", "%883 = OpLoad %v2float %source_uv\nOpReturn"),
                      "duplicate result identifier")

    def test_changed_function_rejected(self):
        self.rejected(self.original.replace("%4 = OpFunction %void None %3", "%4 = OpFunction %void Inline %3"),
                      "unexpected declaration for %4")

    def test_unsupported_uv_type_rejected(self):
        self.rejected(self.original.replace("%883 = OpLoad %v2float", "%883 = OpLoad %v3float"),
                      "%883 has an unexpected result type")

    def test_unsupported_rgb_type_rejected(self):
        self.rejected(self.original.replace("%911 = OpLoad %v3float", "%911 = OpLoad %v4float"),
                      "%911 has an unexpected result type")

    def test_unsupported_float_precision_rejected(self):
        self.rejected(self.original.replace("%float = OpTypeFloat 32", "%float = OpTypeFloat 16"),
                      "unexpected declaration for %float")

    def test_wrong_output_type_rejected(self):
        self.rejected(self.original.replace("OpTypePointer Output %v4float", "OpTypePointer Output %v3float"),
                      "unexpected declaration for %pointer_output")

    def test_compute_entry_point_rejected(self):
        self.rejected(self.original.replace("OpEntryPoint Fragment", "OpEntryPoint GLCompute"),
                      "Fragment entry point")

    def test_multiple_entry_points_rejected(self):
        self.rejected(self.original.replace("OpExecutionMode", 'OpEntryPoint Fragment %4 "other" %504\nOpExecutionMode'),
                      "Fragment entry point")

    def test_missing_output_interface_rejected(self):
        self.rejected(self.original.replace('"main" %source_uv %source_rgb %504', '"main" %source_uv %source_rgb'),
                      "output absent")

    def test_anchor_declared_after_store_rejected(self):
        late = self.original.replace("%883 = OpLoad %v2float %source_uv\n", "")
        late = late.replace("OpReturn", "%883 = OpLoad %v2float %source_uv\nOpReturn")
        self.rejected(late, "%883 must precede")

    def test_existing_descriptor_collision_rejected(self):
        collision = self.existing_sampler(0, 31)
        self.rejected(collision, "set/binding is occupied")

    def test_same_binding_in_different_set_is_not_collision(self):
        source = self.existing_sampler(1, 31)
        self.assertIn("%smask_texture Binding 31", PATCHER.patch_assembly(source))

    def test_grouped_decorations_rejected(self):
        source = self.original.replace("OpDecorate %source_uv Location 0", "\n".join([
            "%group = OpDecorationGroup", "OpGroupDecorate %group %source_uv",
            "OpDecorate %source_uv Location 0"]))
        self.rejected(source, "grouped/id decorations")

    def test_double_patch_rejected(self):
        self.rejected(PATCHER.patch_assembly(self.original), "mask binding marker already present")

    def test_reserved_ids_rejected_without_marker(self):
        self.rejected(PATCHER.patch_assembly(self.original).replace(
            'OpModuleProcessed "MRMASK/1 source-frame-binding-required"\n', ""),
            "reserved mask identifiers")

    def test_invalid_descriptor_numbers_rejected(self):
        for value in (-1, 0x100000000, 1.5, True):
            with self.subTest(value=value):
                with self.assertRaisesRegex(PATCHER.ShaderLayoutError, "unsigned 32-bit"):
                    PATCHER.patch_assembly(self.original, value, 31)
                with self.assertRaisesRegex(PATCHER.ShaderLayoutError, "unsigned 32-bit"):
                    PATCHER.patch_assembly(self.original, 0, value)

    def test_separate_native_sampler_types_are_reused_exactly(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8")
        for convention in ("straight", "premultiplied"):
            with self.subTest(convention=convention):
                result = PATCHER.patch_assembly(source, rgb_convention=convention)
                self.assertEqual(result.count(" = OpTypeImage "), 1)
                self.assertEqual(result.count(" = OpTypeSampledImage "), 1)
                self.assertEqual(result.count(" = OpTypePointer UniformConstant %native_combined_type"), 1)
                self.assertIn("%smask_texture = OpVariable %native_pointer_combined UniformConstant", result)
                self.assertIn("%smask_sampler = OpLoad %native_combined_type %smask_texture", result)

    def test_separate_native_sampler_without_combined_pointer_gets_one(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8").replace(
            "%native_pointer_combined = OpTypePointer UniformConstant %native_combined_type\n", "")
        result = PATCHER.patch_assembly(source)
        self.assertEqual(result.count(" = OpTypeImage "), 1)
        self.assertEqual(result.count(" = OpTypeSampledImage "), 1)
        self.assertIn("%smask_pointer_type = OpTypePointer UniformConstant %native_combined_type", result)

    def test_incompatible_existing_image_type_rejected(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8").replace(
            "%native_image_type = OpTypeImage %float 2D 0 0 0 1 Unknown",
            "%native_image_type = OpTypeImage %float Cube 0 0 0 1 Unknown")
        source = source.replace("%scene_sample = OpImageSampleImplicitLod %v4float %scene_combined %883\n", "")
        self.rejected(source, "image declarations are incompatible")

    def test_ambiguous_native_image_type_rejected(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8").replace(
            "%4 = OpFunction %void None %3",
            "%duplicate_image = OpTypeImage %float 2D 0 0 0 1 Unknown\n%4 = OpFunction %void None %3")
        self.rejected(source, "ambiguous native OpTypeImage")

    def test_ambiguous_native_combined_type_rejected(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8").replace(
            "%4 = OpFunction %void None %3",
            "%duplicate_combined = OpTypeSampledImage %native_image_type\n%4 = OpFunction %void None %3")
        self.rejected(source, "ambiguous native OpTypeSampledImage")

    def test_ambiguous_native_combined_pointer_type_rejected(self):
        source = SEPARATE_SAMPLER_FIXTURE.read_text(encoding="utf-8").replace(
            "%4 = OpFunction %void None %3",
            "%duplicate_pointer = OpTypePointer UniformConstant %native_combined_type\n%4 = OpFunction %void None %3")
        self.rejected(source, "ambiguous native OpTypePointer")

    def test_semicolon_in_debug_string_is_not_comment(self):
        source = self.original.replace("OpDecorate %source_uv Location 0", 'OpName %source_uv "uv;native"\nOpDecorate %source_uv Location 0')
        self.assertIn('"uv;native"', PATCHER.patch_assembly(source))


class MaskBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for command in ("spirv-as", "spirv-val", "spirv-dis"):
            if not shutil.which(command):
                raise RuntimeError("SPIR-V validation requires " + command)

    def test_original_fixture_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "original.spv"
            subprocess.run(["spirv-as", "--target-env", "vulkan1.1", str(FIXTURE), "-o", str(binary)], check=True, capture_output=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(binary)], check=True, capture_output=True)

    def test_patched_fixture_is_assembled_and_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "patched.spv"
            PATCHER.write_shader(FIXTURE, binary)
            result = subprocess.run(["spirv-dis", str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("OpImageSampleExplicitLod", result.stdout)
            self.assertIn("Binding 31", result.stdout)
            self.assertIn('OpModuleProcessed "MRMASK/1 source-frame-binding-required"', result.stdout)
            self.assertEqual(result.stdout.count("OpVectorTimesScalar"), 1)
            self.assertFalse(list(Path(directory).glob(".mask-shader-*")))

    def test_premultiplied_shader_is_assembled_and_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "premultiplied.spv"
            PATCHER.write_shader(FIXTURE, binary, rgb_convention="premultiplied")
            result = subprocess.run(["spirv-dis", str(binary)], check=True, capture_output=True, text=True)
            self.assertNotIn("OpVectorTimesScalar", result.stdout)
            self.assertEqual(result.stdout.count("OpSelect"), 3)

    def test_premultiplied_shader_reuses_native_bool_type(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "native-bool.spvasm"
            source.write_text(FIXTURE.read_text().replace("%float = OpTypeFloat 32", "%bool = OpTypeBool\n%float = OpTypeFloat 32"))
            binary = Path(directory) / "premultiplied.spv"
            PATCHER.write_shader(source, binary, rgb_convention="premultiplied")
            result = subprocess.run(["spirv-dis", str(binary)], check=True, capture_output=True, text=True)
            self.assertEqual(result.stdout.count("OpTypeBool"), 1)

    def test_separate_sampler_fixture_and_both_mask_modes_validate(self):
        with tempfile.TemporaryDirectory() as directory:
            original_binary = Path(directory) / "native-separate.spv"
            subprocess.run(["spirv-as", "--target-env", "vulkan1.1", str(SEPARATE_SAMPLER_FIXTURE),
                            "-o", str(original_binary)], check=True, capture_output=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(original_binary)],
                           check=True, capture_output=True)
            for convention in ("straight", "premultiplied"):
                with self.subTest(convention=convention):
                    binary = Path(directory) / (convention + ".spv")
                    PATCHER.write_shader(SEPARATE_SAMPLER_FIXTURE, binary, rgb_convention=convention)
                    result = subprocess.run(["spirv-dis", str(binary)], check=True, capture_output=True, text=True)
                    self.assertEqual(result.stdout.count(" = OpTypeImage "), 1)
                    self.assertEqual(result.stdout.count(" = OpTypeSampledImage "), 1)

    def test_incompatible_native_type_failure_preserves_known_good_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "native-incompatible.spvasm"
            source.write_text(SEPARATE_SAMPLER_FIXTURE.read_text().replace(
                "%native_image_type = OpTypeImage %float 2D 0 0 0 1 Unknown",
                "%native_image_type = OpTypeImage %float Cube 0 0 0 1 Unknown").replace(
                    "%scene_sample = OpImageSampleImplicitLod %v4float %scene_combined %883\n", ""))
            unmodified_binary = Path(directory) / "valid-native-cube.spv"
            subprocess.run(["spirv-as", "--target-env", "vulkan1.1", str(source),
                            "-o", str(unmodified_binary)], check=True, capture_output=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(unmodified_binary)],
                           check=True, capture_output=True)
            binary = Path(directory) / "keep.spv"
            binary.write_bytes(b"previous known-good shader")
            with self.assertRaisesRegex(PATCHER.ShaderLayoutError, "image declarations are incompatible"):
                PATCHER.write_shader(source, binary)
            self.assertEqual(binary.read_bytes(), b"previous known-good shader")

    def test_cli_default_straight_and_premultiplied_choices(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            binaries = {}
            for convention in (None, "straight", "premultiplied"):
                binary = directory / ((convention or "default") + ".spv")
                command = [sys.executable, str(ROOT / "tools/gen_mask_spv.py"), str(FIXTURE), str(binary)]
                if convention:
                    command += ["--rgb-convention", convention]
                subprocess.run(command, check=True, capture_output=True, text=True)
                binaries[convention] = binary.read_bytes()
            self.assertEqual(binaries[None], binaries["straight"])
            self.assertNotEqual(binaries[None], binaries["premultiplied"])

    def test_layout_failure_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.spvasm"
            source.write_text(FIXTURE.read_text().replace("%883 = OpLoad %v2float", "%883 = OpLoad %v3float"))
            binary = Path(directory) / "keep.spv"
            binary.write_bytes(b"previous known-good shader")
            with self.assertRaises(PATCHER.ShaderLayoutError):
                PATCHER.write_shader(source, binary)
            self.assertEqual(binary.read_bytes(), b"previous known-good shader")

    def test_actual_validator_failure_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.spvasm"
            source.write_text(FIXTURE.read_text().replace("%native_r = OpCompositeExtract %float %911 0", "%native_r = OpCompositeExtract %float %911 5"))
            binary = Path(directory) / "keep.spv"
            binary.write_bytes(b"previous known-good shader")
            with self.assertRaisesRegex(PATCHER.ShaderLayoutError, "spirv-val rejected"):
                PATCHER.write_shader(source, binary)
            self.assertEqual(binary.read_bytes(), b"previous known-good shader")
            self.assertFalse(list(Path(directory).glob(".mask-shader-*")))

    def test_reference_shader_compiles_when_glslc_available(self):
        if not shutil.which("glslc"):
            self.skipTest("glslc is not installed")
        with tempfile.TemporaryDirectory() as directory:
            for define in ([], ["-DMASK_RGB_PREMULTIPLIED=1"]):
                binary = Path(directory) / "reference.spv"
                subprocess.run(["glslc", "--target-env=vulkan1.1"] + define
                               + [str(ROOT / "layer/mask_reference.frag"), "-o", str(binary)],
                               check=True, capture_output=True)
                subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(binary)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
