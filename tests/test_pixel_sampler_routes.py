#!/usr/bin/env python3
"""Negative controls for the finite sampler API-route envelope."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sampler_routes", ROOT / "tools/generate_pixel_sampler_routes.py")
routes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(routes)


def registry(command, *, extension="VK_TEST_route", kind="device", supported="vulkan"):
    return f'''<registry><enums name="VkStructureType"><enum name="VK_STRUCTURE_TYPE_OWN_NODE" value="123"/></enums>
    <types><type category="struct" name="VkOwnNode" structextends="VkDeviceCreateInfo">
    <member values="VK_STRUCTURE_TYPE_OWN_NODE"><type>VkStructureType</type><name>sType</name></member></type></types>
    <extensions><extension name="VK_TEST_base" number="100" type="device" supported="vulkan"><require/></extension>
    <extension name="{extension}" number="101" type="{kind}" supported="{supported}">
    <require><command name="{command}"/></require></extension></extensions></registry>'''.encode()


class RouteEnvelope(unittest.TestCase):
    def test_unknown_binding_and_execution_commands_are_excluded(self):
        for command in ("vkCmdBindPipeline2FUTURE", "vkCmdBindDescriptorHeapFUTURE", "vkCmdExecuteGeneratedCommandsEXT",
                        "vkCmdDispatchGraphAMDX", "vkCmdCudaLaunchKernelNV", "vkCmdBindSamplerHeapEXT",
                        "vkCmdBindResourceHeapEXT", "vkCmdPushDataEXT", "vkCmdDispatchTileQCOM",
                        "vkCmdBindFutureStateTEST", "vkCmdDispatchFutureTEST"):
            with self.subTest(command=command):
                text, excluded = routes.generate(registry(command), "a" * 40)
                self.assertEqual(excluded, [("VK_TEST_route", [command])])
                self.assertNotIn('    "VK_TEST_route",', text)

    def test_observed_aliases_and_independent_graphics_state_are_retained(self):
        for command in ("vkCmdBindPipeline", "vkCmdBindDescriptorSets2", "vkCmdPushDescriptorSet2KHR",
                        "vkCmdBindShadersEXT", "vkCmdDispatchBaseKHR", "vkCmdSetViewport",
                        "vkCmdBindVertexBuffers2EXT", "vkCmdBindIndexBuffer2KHR", "vkCmdSetFragmentShadingRateKHR"):
            text, excluded = routes.generate(registry(command), "a" * 40)
            self.assertEqual(excluded, [])
            self.assertIn('    "VK_TEST_route",', text)

    def test_instance_and_disabled_extensions_never_become_device_routes(self):
        for kwargs in ({"kind": "instance"}, {"supported": "disabled"}):
            text, _ = routes.generate(registry("vkCmdSetViewport", **kwargs), "a" * 40)
            self.assertNotIn('    "VK_TEST_route",', text)

    def test_device_chain_uses_public_enum_values_and_loader_exception(self):
        text, _ = routes.generate(registry("vkCmdSetViewport"), "b" * 40)
        self.assertIn("static_cast<VkStructureType>(123)", text)
        self.assertEqual(text.count("    VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,"), 1)

    def test_generated_production_envelope_refuses_unobserved_routes(self):
        text = (ROOT / "layer/pixel_sampler_routes.h").read_text()
        for name in ("VK_NV_device_generated_commands", "VK_EXT_device_generated_commands", "VK_ARM_data_graph",
                     "VK_EXT_descriptor_heap", "VK_QCOM_tile_shading", "VK_EXT_conditional_rendering",
                     "VK_NV_command_buffer_inheritance"):
            self.assertNotIn('    "' + name + '",', text)
        self.assertIn('    "VK_EXT_descriptor_buffer",', text)
        self.assertIn('    "VK_KHR_sampler_ycbcr_conversion",', text)

    def test_feature_only_unsafe_extensions_are_refused(self):
        for name in routes.EXCLUDED_EXTENSIONS:
            text, excluded = routes.generate(registry("vkCmdSetViewport", extension=name), "a" * 40)
            self.assertEqual(len(excluded), 1)
            self.assertNotIn('    "' + name + '",', text)


if __name__ == "__main__":
    unittest.main()
