#!/usr/bin/env python3
"""Generate a finite reviewed API envelope from a pinned public Vulkan registry.

The sampler changes only COMPUTE pipeline/descriptor bindings and writes no
push constants or graphics state. Commands that can replace those bindings
must be observed or their device extension is excluded. Unknown future device
extensions/chain types never inherit this envelope. No network or Frame access.
"""
import argparse
import hashlib
from pathlib import Path
import re
import xml.etree.ElementTree as ET

OBSERVED = frozenset((
    "vkCmdBindPipeline", "vkCmdBindDescriptorSets", "vkCmdBindShadersEXT",
    "vkCmdPushDescriptorSetKHR", "vkCmdPushDescriptorSet",
    "vkCmdPushDescriptorSetWithTemplateKHR", "vkCmdPushDescriptorSetWithTemplate",
    "vkCmdBindDescriptorBuffersEXT", "vkCmdSetDescriptorBufferOffsetsEXT",
    "vkCmdBindDescriptorBufferEmbeddedSamplersEXT", "vkCmdBindDescriptorSets2KHR",
    "vkCmdBindDescriptorSets2", "vkCmdPushDescriptorSet2KHR", "vkCmdPushDescriptorSet2",
    "vkCmdPushDescriptorSetWithTemplate2KHR", "vkCmdPushDescriptorSetWithTemplate2",
    "vkCmdSetDescriptorBufferOffsets2EXT", "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT",
    "vkCmdExecuteCommands",
    "vkCmdDispatch", "vkCmdDispatchIndirect", "vkCmdDispatchBase", "vkCmdDispatchBaseKHR",
))

# These bindings are independent of COMPUTE pipeline/descriptor state. New
# binding names do not inherit that assumption merely by being public Vulkan.
INDEPENDENT_BINDINGS = frozenset((
    "vkCmdBindIndexBuffer", "vkCmdBindIndexBuffer2", "vkCmdBindIndexBuffer2KHR",
    "vkCmdBindVertexBuffers", "vkCmdBindVertexBuffers2", "vkCmdBindVertexBuffers2EXT",
    "vkCmdBindTransformFeedbackBuffersEXT",
))

# Public features can affect inserted commands without introducing a binding
# function. Conditional scopes may suppress dispatch; command-buffer inheritance
# invalidates the clean COMPUTE baseline. This temporary experiment supports
# neither and must not silently admit them through a name-only command scan.
EXCLUDED_EXTENSIONS = {
    "VK_EXT_conditional_rendering": "unobserved conditional dispatch scope",
    "VK_NV_command_buffer_inheritance": "inherited command-buffer state",
}


def affects_compute_bindings(name):
    if name in INDEPENDENT_BINDINGS:
        return False
    return bool(re.match(r"vkCmd(?:Bind|Dispatch|PushData|SetDescriptor|PushDescriptor|Execute)", name)
                or re.search(r"(?:Cuda|CuLaunch|DispatchGraph|DataGraph)", name))


def generate(raw, revision):
    root = ET.fromstring(raw)
    values = {}
    for enum in root.findall(".//enum"):
        if "value" in enum.attrib or "alias" in enum.attrib or ("offset" in enum.attrib and "extnumber" in enum.attrib):
            values.setdefault(enum.get("name"), dict(enum.attrib))
    for extension in root.findall("./extensions/extension"):
        for enum in extension.findall(".//enum"):
            if enum.get("extends") == "VkStructureType" and "offset" in enum.attrib:
                values[enum.get("name")] = {**enum.attrib, "extnumber": enum.get("extnumber", extension.get("number"))}

    def number(name, seen=()):
        if name in seen:
            raise ValueError("Cyclic enum alias")
        node = values[name]
        if "alias" in node:
            return number(node["alias"], (*seen, name))
        if "offset" in node:
            value = 1000000000 + (int(node["extnumber"]) - 1) * 1000 + int(node["offset"])
            return -value if node.get("dir") == "-" else value
        return int(node["value"], 0)

    allowed, excluded = [], []
    for extension in root.findall("./extensions/extension"):
        if extension.get("type") != "device" or "vulkan" not in extension.get("supported", "").split(","):
            continue
        commands = {c.get("name") for c in extension.findall("./require/command")}
        unseen = sorted(c for c in commands if affects_compute_bindings(c) and c not in OBSERVED)
        if extension.get("name") in EXCLUDED_EXTENSIONS:
            unseen.append(EXCLUDED_EXTENSIONS[extension.get("name")])
        if unseen:
            excluded.append((extension.get("name"), unseen))
        else:
            allowed.append(extension.get("name"))
    # Chain nodes are only skipped, never read as guessed private structures.
    # The enabled YCbCr feature itself is parsed separately by admission code.
    nodes = {}
    for structure in root.findall("./types/type"):
        parents = set(structure.get("structextends", "").split(","))
        if not parents.intersection(("VkDeviceCreateInfo", "VkPhysicalDeviceFeatures2")):
            continue
        member = next((m for m in structure.findall("member") if m.findtext("name") == "sType"), None)
        if member is not None and member.get("values"):
            symbol = member.get("values")
            nodes[number(symbol)] = symbol
    assert 0 < len(allowed) <= 1024 and 0 < len(nodes) < 1024
    digest = hashlib.sha256(raw).hexdigest()
    lines = ["#pragma once", "// Generated by tools/generate_pixel_sampler_routes.py; do not hand-edit.",
             "// Public Vulkan-Headers revision: " + revision, "// Registry SHA-256: " + digest,
             "// Unknown future extensions/types are refused. Excluded state routes:"]
    lines += ["// " + name + ": " + ", ".join(commands) for name, commands in sorted(excluded)]
    lines += ["#include <vulkan/vulkan.h>", "namespace frame_pixel_sampler_routes {",
              "inline constexpr const char* DeviceExtensions[] = {"]
    lines += ['    "' + name + '",' for name in sorted(allowed)]
    lines += ["};", "inline constexpr VkStructureType DeviceChainTypes[] = {",
              "    VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,"]
    lines += [f"    static_cast<VkStructureType>({value}), // {symbol}" for value, symbol in sorted(nodes.items())]
    lines += ["};", "} // namespace frame_pixel_sampler_routes", ""]
    return "\n".join(lines), excluded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry", type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        parser.error("Use the exact pinned public registry commit")
    raw = args.registry.read_bytes()
    if len(raw) > 5 * 1024 * 1024:
        parser.error("Registry exceeds bounded input")
    text, excluded = generate(raw, args.revision)
    args.output.write_text(text)
    print("Generated finite API envelope; excluded unsupported state routes:", excluded)


if __name__ == "__main__":
    main()
