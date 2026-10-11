#!/usr/bin/env python3
"""Patch a supported scene shader to sample a separate, explicit opacity mask.

The caller must bind an R8_UNORM mask texture for the *same source video frame*,
with opaque sampler borders, at the selected combined-image-sampler binding.
This tool neither changes Vulkan pipeline layouts nor associates video frames.
The meaning of the native %883 source coordinates must be checked against the
actual SteamVR shader before enabling a replacement on a headset.

Only the final opaque store is replaced. Native color conversion, source
reprojection, tinting and distortion remain unchanged. There is no color key,
spill suppression, threshold or opacity inference from RGB.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


class ShaderLayoutError(ValueError):
    """The input is not the narrow, supported native shader layout."""


TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|[^\s;]+')
MASK_PREFIX = "%smask_"


def _instructions(assembly):
    result = []
    for index, line in enumerate(assembly.splitlines()):
        # A semicolon in an OpName string is part of the string, not a comment.
        tokens = []
        pos = 0
        while pos < len(line):
            if line[pos].isspace():
                pos += 1
            elif line[pos] == ";":
                break
            else:
                match = TOKEN.match(line, pos)
                if not match:
                    raise ShaderLayoutError("cannot parse assembly line {}".format(index + 1))
                tokens.append(match.group(0))
                pos = match.end()
        if tokens:
            result.append((index, tokens))
    return result


def _require(condition, detail):
    if not condition:
        raise ShaderLayoutError("unsupported shader layout: " + detail)


def patch_assembly(assembly, descriptor_set=0, binding=31):
    """Return patched assembly, refusing unknown anchors or binding collisions."""
    for value, label in ((descriptor_set, "descriptor set"), (binding, "binding")):
        if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFFFFFF:
            raise ShaderLayoutError(label + " must be an unsigned 32-bit integer")

    instructions = _instructions(assembly)
    definitions = {}
    functions = {}
    current_function = None
    for line, tokens in instructions:
        _require(not any(token.startswith(MASK_PREFIX) for token in tokens),
                 "reserved mask identifiers already present")
        if len(tokens) > 2 and tokens[1] == "=":
            ident = tokens[0]
            _require(ident not in definitions, "duplicate result identifier " + ident)
            definitions[ident] = (line, tokens[2:])
            if tokens[2] == "OpFunction":
                current_function = ident
            functions[ident] = current_function
        if tokens[0] == "OpFunctionEnd":
            current_function = None

    def definition(ident, expected):
        _require(ident in definitions, "missing " + ident)
        _require(definitions[ident][1] == expected, "unexpected declaration for " + ident)

    definition("%float", ["OpTypeFloat", "32"])
    definition("%v2float", ["OpTypeVector", "%float", "2"])
    definition("%v3float", ["OpTypeVector", "%float", "3"])
    definition("%v4float", ["OpTypeVector", "%float", "4"])
    definition("%void", ["OpTypeVoid"])
    definition("%3", ["OpTypeFunction", "%void"])
    definition("%4", ["OpFunction", "%void", "None", "%3"])

    entries = [tokens for _, tokens in instructions if tokens[0] == "OpEntryPoint"]
    _require(len(entries) == 1 and entries[0][1:3] == ["Fragment", "%4"],
             "expected one Fragment entry point %4")
    _require("%504" in entries[0][4:], "scene output absent from entry point")
    _require(any(tokens == ["OpCapability", "Shader"] for _, tokens in instructions),
             "Shader capability absent")

    stores = [(line, tokens) for line, tokens in instructions
              if tokens[:2] == ["OpStore", "%504"]]
    _require(len(stores) == 1 and stores[0][1] == ["OpStore", "%504", "%994"],
             "expected exactly one opaque scene store OpStore %504 %994")
    store_line = stores[0][0]
    main_line = definitions["%4"][0]
    first_function = min(line for line, tokens in instructions
                         if len(tokens) >= 3 and tokens[1:3] == ["=", "OpFunction"])
    main_end = next((line for line, tokens in instructions
                     if line > main_line and tokens == ["OpFunctionEnd"]), None)
    _require(main_end is not None and main_line < store_line < main_end,
             "scene store is outside the expected entry point")

    for ident, result_type in (("%883", "%v2float"), ("%911", "%v3float"),
                               ("%994", "%v4float")):
        _require(ident in definitions and len(definitions[ident][1]) >= 2
                 and definitions[ident][1][1] == result_type,
                 ident + " has an unexpected result type")
        _require(functions.get(ident) == "%4"
                 and main_line < definitions[ident][0] < store_line,
                 ident + " must precede the scene store in the entry point")

    output = definitions.get("%504", (None, []))[1]
    _require(len(output) == 3 and output[0] == "OpVariable" and output[2] == "Output",
             "%504 is not an Output variable")
    definition(output[1], ["OpTypePointer", "Output", "%v4float"])

    # Decoration groups could conceal a descriptor collision. Refuse them rather
    # than guessing at the descriptor layout of a changed compositor shader.
    _require(not any(tokens[0] in ("OpGroupDecorate", "OpGroupMemberDecorate", "OpDecorateId")
                     for _, tokens in instructions), "unsupported grouped/id decorations")
    descriptors = {}
    for _, tokens in instructions:
        if len(tokens) == 4 and tokens[0] == "OpDecorate" and tokens[2] in ("DescriptorSet", "Binding"):
            try:
                value = int(tokens[3], 0)
            except ValueError as error:
                raise ShaderLayoutError("non-literal descriptor decoration") from error
            pair = descriptors.setdefault(tokens[1], {})
            _require(tokens[2] not in pair, "duplicate descriptor decoration")
            pair[tokens[2]] = value
    _require(not any(pair.get("DescriptorSet") == descriptor_set and pair.get("Binding") == binding
                     for pair in descriptors.values()), "requested descriptor set/binding is occupied")

    type_lines = [line for line, tokens in instructions
                  if len(tokens) >= 3 and tokens[1] == "=" and tokens[2].startswith("OpType")]
    _require(bool(type_lines), "type declarations absent")
    first_type = min(type_lines)
    annotations = [
        "OpDecorate %smask_texture DescriptorSet {}".format(descriptor_set),
        "OpDecorate %smask_texture Binding {}".format(binding),
    ]
    declarations = [
        "%smask_image_type = OpTypeImage %float 2D 0 0 0 1 Unknown",
        "%smask_sampled_type = OpTypeSampledImage %smask_image_type",
        "%smask_pointer_type = OpTypePointer UniformConstant %smask_sampled_type",
        "%smask_texture = OpVariable %smask_pointer_type UniformConstant",
        "%smask_lod_zero = OpConstant %float 0",
    ]
    code = [
        "%smask_sampler = OpLoad %smask_sampled_type %smask_texture",
        "%smask_sample = OpImageSampleExplicitLod %v4float %smask_sampler %883 Lod %smask_lod_zero",
        "%smask_alpha = OpCompositeExtract %float %smask_sample 0",
        "%smask_rgb = OpVectorTimesScalar %v3float %911 %smask_alpha",
        "%smask_r = OpCompositeExtract %float %smask_rgb 0",
        "%smask_g = OpCompositeExtract %float %smask_rgb 1",
        "%smask_b = OpCompositeExtract %float %smask_rgb 2",
        "%smask_output = OpCompositeConstruct %v4float %smask_r %smask_g %smask_b %smask_alpha",
        "OpStore %504 %smask_output",
    ]

    result = []
    for line, text in enumerate(assembly.splitlines()):
        if line == first_type:
            result.extend(annotations)
        if line == first_function:
            result.extend(declarations)
        if line == store_line:
            result.extend(code)
        else:
            result.append(text)
    return "\n".join(result) + "\n"


def write_shader(source, destination, descriptor_set=0, binding=31):
    """Assemble and validate in isolation before atomically replacing output."""
    assembly = patch_assembly(Path(source).read_text(encoding="utf-8"), descriptor_set, binding)
    destination = Path(destination)
    with tempfile.TemporaryDirectory(prefix=".mask-shader-", dir=destination.parent) as directory:
        assembly_path = Path(directory) / "mask.spvasm"
        binary_path = Path(directory) / "mask.spv"
        assembly_path.write_text(assembly, encoding="utf-8")
        for command in (["spirv-as", "--target-env", "vulkan1.1", str(assembly_path), "-o", str(binary_path)],
                        ["spirv-val", "--target-env", "vulkan1.1", str(binary_path)]):
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise ShaderLayoutError("{} rejected the shader: {}".format(
                    command[0], (result.stderr or result.stdout).strip()[:8000]))
        os.replace(binary_path, destination)


def _uint32(value):
    try:
        integer = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected an unsigned integer") from error
    if not 0 <= integer <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("expected an unsigned 32-bit integer")
    return integer


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="unmodified compositor shader disassembly")
    parser.add_argument("output", help="validated output SPIR-V module")
    parser.add_argument("--set", type=_uint32, default=0, dest="descriptor_set")
    parser.add_argument("--binding", type=_uint32, default=31)
    args = parser.parse_args(argv)
    try:
        write_shader(args.source, args.output, args.descriptor_set, args.binding)
    except (ShaderLayoutError, OSError) as error:
        print("mask shader: {}".format(error), file=sys.stderr)
        return 1
    print("wrote validated explicit-mask shader: {}".format(args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
