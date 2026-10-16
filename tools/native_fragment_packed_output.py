#!/usr/bin/env python3
"""Inert single-attachment output from an explicitly owned packed source.

This is an offline integration boundary, never a native shader override. The
caller supplies both the chart and a source whose sampled component-G is known
linear coverage; no real video transfer or source-frame association is inferred.
The exact native dependency cones are reused from the approved atlas probe.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_atlas_probe as atlas

ssa, recovery, require = atlas.ssa, atlas.recovery, atlas.require
SCHEMA = "native-fragment-packed-output-mapping-v1"
SCOPE = "EXPLICIT_OWNED_OFFSCREEN_PACKED_SOURCE_ONLY"
ENCODING = "OWNED_LINEAR_RGBA32F_COMPONENT_G_COVERAGE"


def mapping_contract(mapping):
    require(type(mapping) is dict and set(mapping) == {
        "schema", "scope", "source_encoding", "rgb_convention", "B", "color", "coverage"}
        and mapping["schema"] == SCHEMA and mapping["scope"] == SCOPE
        and mapping["source_encoding"] == ENCODING
        and mapping["rgb_convention"] in ("straight", "premultiplied"),
        "explicit owned straight or premultiplied color/linear-coverage mapping required")
    packed = {key: mapping[key] for key in ("B", "color", "coverage")}
    packed.update(schema=atlas.PACKED_MAPPING_SCHEMA, scope=atlas.PACKED_MAPPING_SCOPE)
    atlas.packed_mapping_contract(packed)
    return packed


def specialize_owned_binding9_count1(binary, original):
    """Fresh owned interface types; never rewrite a native layout or its shader.

    The pinned executable can address only descriptor0. This separate offline
    variant declares that exact extent instead of borrowing a native count3
    interface or asserting that a count1 original pipeline would be valid.
    """
    require(atlas.probe.digest(original) == ssa.ORIGINAL_SHA256, "exact original specialization profile required")
    _, native = ssa.parse_module(original)
    definitions = {atlas.result_id(i): i for i in native if atlas.result_id(i) is not None}
    require(definitions[289] == ssa.Instruction(59, (288, 289, 0))
            and definitions[288] == ssa.Instruction(32, (288, 0, 287))
            and definitions[287] == ssa.Instruction(28, (287, 286, 102))
            and definitions[102].opcode == 43 and definitions[102].operands[2] == 3
            and definitions[110] == ssa.Instruction(43, (90, 110, 0)), "pinned image-array type/length/index profile differs")
    accesses = [i for i in native if 289 in ssa.value_inputs(i)]
    require(accesses == [ssa.Instruction(65, (290, 598, 289, 110)),
                         ssa.Instruction(65, (290, 624, 289, 110))], "all native B9 accesses must be proven constant0")
    require({i.operands for i in native if i.opcode == 71 and i.operands[0] == 289}
            == {(289, 33, 9), (289, 34, 0)}, "exact B9 descriptor decorations required")
    header, instructions = ssa.parse_module(binary)
    require([i for i in instructions if 289 in ssa.value_inputs(i)] == accesses,
            "composed module added a B9 descriptor access")
    one, array, pointer = range(header[3], header[3]+3)
    declarations = [ssa.Instruction(43, (definitions[102].operands[0], one, 1)),
        ssa.Instruction(28, (array, 286, one)), ssa.Instruction(32, (pointer, 0, array))]
    result, replacements = [], 0
    for item in instructions:
        if item == definitions[289]:
            result.extend(declarations); item = ssa.Instruction(59, (pointer, 289, 0)); replacements += 1
        result.append(item)
    require(replacements == 1 and pointer < 65536, "single owned descriptor-array specialization required")
    words = [*header[:3], pointer+1, header[4]]+[word for i in result for word in i.words()]
    return struct.pack("<%dI" % len(words), *words)


def transform(original, mapping, enabled=True, descriptor_count=3):
    require(type(enabled) is bool, "enabled must be an explicit boolean")
    require(type(descriptor_count) is int and descriptor_count in (1, 3), "explicit owned descriptor count1 or3 required")
    packed = mapping_contract(mapping)
    # Always run the exact original profile/SSA checks, including disabled mode.
    intermediate, proof = atlas.transform_packed(original, packed)
    record = {
        "schema": "native-fragment-packed-output-v1", "scope": SCOPE,
        "original_sha256": ssa.ORIGINAL_SHA256, "original_bytes": len(original),
        "mapping": mapping, "enabled": enabled,
        "color_policy": ("straight native decoded Pc color times tint, linear coverage and ORIGINAL-q aperture once"
            if mapping["rgb_convention"] == "straight" else
            "premultiplied native decoded Pc color times tint and ORIGINAL-q aperture once; alpha0 emission retained"),
        "alpha_policy": "clamp(raw Pa sampled component-G,0,1) times ORIGINAL-q aperture once",
        "output_convention": "premultiplied RGBA", "color_attachment_count": 1,
        "owned_b9_descriptor_count": descriptor_count if enabled else 3,
        "owned_count1_interface_specialization": enabled and descriptor_count == 1,
        "native_descriptor_layout_modified": False,
        "Pc": proof["Pc"], "Pa": proof["Pa"],
        "added_native_ubo_or_sampler_loads": 0,
        "copied_r1_or_cpu_uniform_join_used": False,
        "coverage_source_contract": ENCODING,
        "coverage_contract_is_caller_supplied_not_discovered": True,
        "disabled_returns_original_module_byte_for_byte": True,
        "runtime_or_pipeline_activation": False, "native_spatial_mapping_proven": False,
        "native_frame_association_proven": False, "native_draw_coherence_proven": False,
        "physical_eye_proven": False, "native_sampler_equivalence_proven": False,
        "linear_game_alpha_proven": False, "alpha_composition_authorized": False,
        "opaque_fallback_required": True, "passthrough_authorized": False}
    if not enabled:
        record.update(output_sha256=atlas.probe.digest(original), output_bytes=len(original), new_ssa_ids={})
        return original, record
    header, instructions = ssa.parse_module(intermediate)
    original_header, _ = ssa.parse_module(original)
    variables = {i.operands[0] for i in instructions if i.opcode == 71
                 and i.operands[0] >= original_header[3] and i.operands[1] == 30}
    require(len(variables) == 4, "exact four diagnostic outputs required")
    require({i.operands[2] for i in instructions if i.opcode == 71 and i.operands[0] in variables} == {1, 2, 3, 4},
            "exact diagnostic attachment locations required")
    bound = header[3]
    def fresh():
        nonlocal bound
        value = bound; bound += 1; return value
    make = lambda op, *a: ssa.Instruction(op, tuple(a))
    zero, one, coverage, rgb, alpha, output = [fresh() for _ in range(6)]
    ids = proof["new_ssa_ids"]
    declarations = [make(43, 8, zero, 0), make(43, 8, one, 0x3f800000)]
    # GLSL.std.450 FClamp is a value-range guard, never a key-color operation.
    # combined_color_rgb already contains the original aperture and tint. A
    # premultiplied source must keep its RGB even at coverage0 (e.g. emission).
    composition = [make(12, 8, coverage, 1, 43, ids["raw_component_g"], zero, one),
        make(142, 30, rgb, ids["combined_color_rgb"],
             coverage if mapping["rgb_convention"] == "straight" else one),
        make(133, 8, alpha, coverage, ids["original_aperture"]),
        make(80, 9, output, rgb, alpha)]
    result, removed_stores, replaced = [], 0, 0
    for item in instructions:
        op, a = item.opcode, item.operands
        if op == 54 and not any(i.opcode == 54 for i in result): result.extend(declarations)
        if op == 71 and a[0] in variables: continue
        if op == 59 and a[1] in variables: continue
        if op == 62 and a[0] in variables:
            removed_stores += 1; continue
        if op == 15:
            require(set(a[-4:]) == variables, "exact appended entry interface required")
            item = make(15, *a[:-4])
        if op == 62 and a[0] == 504:
            require(a == (504, 994), "exact original output relation required")
            result.extend(composition); item = make(62, 504, output); replaced += 1
        result.append(item)
    require(removed_stores == 4 and replaced == 1 and bound <= 65536, "single bounded output replacement required")
    words = [*header[:3], bound, header[4]]
    words.extend(word for item in result for word in item.words())
    binary = struct.pack("<%dI" % len(words), *words)
    if descriptor_count == 1: binary = specialize_owned_binding9_count1(binary, original)
    require(len(binary) <= ssa.MAX_BYTES, "bounded composed native module required")
    record.update(output_sha256=atlas.probe.digest(binary), output_bytes=len(binary),
        new_ssa_ids={**ids, "clamped_coverage": coverage, "premultiplied_rgb": rgb,
                     "output_alpha": alpha, "output": output},
        diagnostic_outputs_removed=4, native_aperture_id=899, native_tint_id=907)
    return binary, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "mapping", "output", "record"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--owned-descriptor-count", type=int, choices=(1, 3), default=3)
    args = parser.parse_args(argv)
    try:
        require(args.output.absolute() != args.record.absolute(), "distinct output and record paths required")
        require(not any(path.exists() or path.is_symlink() for path in (args.output, args.record)),
                "exclusive output and record paths required")
        mapping, mapping_sha = recovery.load_json(args.mapping)
        binary, record = transform(recovery.read_owned(args.original, ssa.MAX_BYTES), mapping, not args.disabled, args.owned_descriptor_count)
        with tempfile.TemporaryDirectory(prefix="private-packed-output-") as directory:
            path = Path(directory)/"output.spv"; path.write_bytes(binary)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=False)
            require(result.returncode == 0, "SPIR-V validation failed; no output published")
        record.update(spirv_val_passed=True, mapping_json_sha256=mapping_sha)
        recovery.write_owned(args.output, binary)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True, allow_nan=False)+"\n").encode())
        print(json.dumps({"scope": SCOPE, "output_sha256": record["output_sha256"], "passthrough_authorized": False}))
        return 0
    except (atlas.probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError,
            struct.error, ZeroDivisionError, subprocess.SubprocessError) as error:
        print("native fragment packed output: "+str(error), file=sys.stderr); return 2


if __name__ == "__main__": raise SystemExit(main())
