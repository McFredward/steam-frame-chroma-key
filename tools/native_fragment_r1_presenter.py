#!/usr/bin/env python3
"""Inert single-output retargeting through CURRENT original q/u/R1.

Only an explicitly owned synthetic straight-RGB/linear-coverage source is
accepted. This creates a private candidate, not a native override or a live
source-validity guard. No physical-eye table or copied CPU uniform is used.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_atlas_probe as atlas
import native_fragment_packed_output as packed_output

ssa, recovery, require = atlas.ssa, atlas.recovery, atlas.require
SCHEMA = "native-fragment-r1-presenter-mapping-v1"
SCOPE = "INERT_OFFLINE_CURRENT_R1_SYNTHETIC_SOURCE_ONLY"
CHART = "ORIGINAL_CURRENT_R1_NORMALIZED_CANVAS"
ENCODING = "OWNED_LINEAR_RGBA32F_COMPONENT_G_COVERAGE"
MARKER = "MRMASK/2 inert-current-r1-presenter source-frame-binding-required"
IDENTITY = [1., 0., 0., 0., 1., 0.]


def _panel(value):
    panel = atlas.affine(value)
    # This first profile describes two ordinary rectangular source panels. It
    # does not infer a crop, flip, shear or view projection from their extents.
    require(panel[1] == panel[3] == 0. and panel[0] > 0. and panel[4] > 0.
            and panel[2] >= 0. and panel[5] >= 0.
            and panel[0] + panel[2] <= 1. and panel[4] + panel[5] <= 1.,
            "positive axis-aligned panels inside the unit canvas required")
    return panel


def mapping_contract(mapping):
    require(type(mapping) is dict and set(mapping) == {
        "schema", "scope", "chart", "source_encoding", "rgb_convention", "color", "coverage"}
        and mapping["schema"] == SCHEMA and mapping["scope"] == SCOPE
        and mapping["chart"] == CHART and mapping["source_encoding"] == ENCODING
        and mapping["rgb_convention"] == "straight",
        "explicit current-R1 synthetic straight-RGB/linear-coverage schema required")
    color, coverage = (_panel(mapping[key]) for key in ("color", "coverage"))
    require(color[2] + color[0] <= coverage[2]
            or coverage[2] + coverage[0] <= color[2]
            or color[5] + color[4] <= coverage[5]
            or coverage[5] + coverage[4] <= color[5], "disjoint color and coverage panels required")
    return {"schema": atlas.MAPPING_SCHEMA, "scope": atlas.MAPPING_SCOPE,
            "W": IDENTITY.copy(), "color": color, "coverage": coverage}


def _marked_module(binary):
    header, instructions = ssa.parse_module(binary)
    encoded = MARKER.encode("ascii") + b"\0"
    encoded += b"\0" * (-len(encoded) % 4)
    marker = ssa.Instruction(330, struct.unpack("<%dI" % (len(encoded) // 4), encoded))
    # OpModuleProcessed belongs to debug information, before annotations/types.
    first_annotation = next(i for i, item in enumerate(instructions) if item.opcode in (71, 72))
    instructions.insert(first_annotation, marker)
    old_version = header[1]
    version = max(old_version, 0x00010100)  # OpModuleProcessed requires SPIR-V 1.1.
    words = [header[0], version, *header[2:]]
    words.extend(word for item in instructions for word in item.words())
    result = struct.pack("<%dI" % len(words), *words)
    require(len(result) <= ssa.MAX_BYTES, "bounded marked candidate required")
    return result, old_version, version


def transform(original, mapping, enabled=True, descriptor_count=1):
    require(type(enabled) is bool, "enabled must be an explicit boolean")
    require(type(descriptor_count) is int and descriptor_count in (1, 3),
            "explicit owned descriptor count1 or3 required")
    definition = mapping_contract(mapping)
    # The original q883 is normalized by THIS invocation's original R1. The
    # existing q route maps Tc(q)/Ta(q) back through that same R1; W is identity,
    # never a caller chart, a host-copied R1 or an inferred physical-eye index.
    intermediate, proof = atlas.transform(original, definition)
    record = {
        "schema": "native-fragment-r1-presenter-v1", "scope": SCOPE,
        "original_sha256": ssa.ORIGINAL_SHA256, "original_bytes": len(original),
        "mapping": mapping, "enabled": enabled,
        "coordinate_policy": "current original q883; panel q -> current original R1 -> original packed reconstruction",
        "color": proof["Mc"], "coverage": proof["Ma"],
        "color_policy": "native decoded color times ORIGINAL aperture/tint once times clamped raw numeric coverage",
        "alpha_policy": "clamped raw numeric coverage times ORIGINAL aperture once",
        "output_convention": "premultiplied RGBA contribution from straight synthetic RGB",
        "coverage_source_contract": ENCODING,
        "coverage_contract_is_caller_supplied_not_discovered": True,
        "source_rgb_convention": "straight", "zero_coverage_rgb_is_zero": True,
        "additive_or_refraction_contract": False, "color_attachment_count": 1,
        "owned_b9_descriptor_count": descriptor_count if enabled else 3,
        "owned_count1_interface_specialization": enabled and descriptor_count == 1,
        "native_descriptor_layout_modified": False, "new_descriptor_bindings": 0,
        "added_native_ubo_or_sampler_loads": 0, "host_chart_used": False,
        "copied_r1_or_cpu_uniform_join_used": False, "physical_eye_table_used": False,
        "disabled_returns_original_module_byte_for_byte": True,
        "runtime_or_pipeline_activation": False, "native_spatial_mapping_proven": False,
        "native_draw_coherence_proven": False, "native_frame_association_proven": False,
        "physical_eye_proven": False, "native_sampler_equivalence_proven": False,
        "linear_game_alpha_proven": False, "alpha_composition_authorized": False,
        "live_source_validity_guard_implemented": False, "opaque_fallback_required": True,
        "passthrough_authorized": False}
    if not enabled:
        record.update(output_sha256=atlas.probe.digest(original), output_bytes=len(original), new_ssa_ids={},
                      candidate_marker=None, spirv_version_original=struct.unpack_from("<I", original, 4)[0],
                      spirv_version_output=struct.unpack_from("<I", original, 4)[0], spirv_version_changed=False)
        return original, record
    header, instructions = ssa.parse_module(intermediate)
    native_header, _ = ssa.parse_module(original)
    variables = {i.operands[0] for i in instructions if i.opcode == 71
                 and i.operands[0] >= native_header[3] and i.operands[1] == 30}
    require(len(variables) == 4 and {i.operands[2] for i in instructions
            if i.opcode == 71 and i.operands[0] in variables} == {1, 2, 3, 4},
            "exact four inherited diagnostic outputs required")
    bound = header[3]
    def fresh():
        nonlocal bound
        value = bound
        bound += 1
        return value
    make = lambda op, *a: ssa.Instruction(op, tuple(a))
    zero, one, coverage, rgb, alpha, output = [fresh() for _ in range(6)]
    ids = proof["new_ssa_ids"]
    declarations = [make(43, 8, zero, 0), make(43, 8, one, 0x3f800000)]
    composition = [make(12, 8, coverage, 1, 43, ids["raw_component_g"], zero, one),
                   make(142, 30, rgb, ids["combined_color_rgb"], coverage),
                   make(133, 8, alpha, coverage, ids["original_aperture"]),
                   make(80, 9, output, rgb, alpha)]
    result, stores, replaced = [], 0, 0
    for item in instructions:
        op, args = item.opcode, item.operands
        if op == 54 and not any(i.opcode == 54 for i in result):
            result.extend(declarations)
        if op in (71, 59) and (args[0] if op == 71 else args[1]) in variables:
            continue
        if op == 62 and args[0] in variables:
            stores += 1
            continue
        if op == 15:
            require(set(args[-4:]) == variables, "exact inherited entry interface required")
            item = make(15, *args[:-4])
        if op == 62 and args[0] == 504:
            require(args == (504, 994), "exact original output relation required")
            result.extend(composition)
            item = make(62, 504, output)
            replaced += 1
        result.append(item)
    require(stores == 4 and replaced == 1 and bound <= 65536, "single bounded output replacement required")
    words = [*header[:3], bound, header[4]]
    words.extend(word for item in result for word in item.words())
    binary = struct.pack("<%dI" % len(words), *words)
    if descriptor_count == 1:
        binary = packed_output.specialize_owned_binding9_count1(binary, original)
    binary, old_version, version = _marked_module(binary)
    record.update(output_sha256=atlas.probe.digest(binary), output_bytes=len(binary),
                  new_ssa_ids={**ids, "clamped_coverage": coverage, "premultiplied_rgb": rgb,
                               "output_alpha": alpha, "output": output},
                  diagnostic_outputs_removed=4, candidate_marker=MARKER,
                  spirv_version_original=old_version, spirv_version_output=version,
                  spirv_version_changed=old_version != version)
    return binary, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "mapping", "output", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--owned-descriptor-count", type=int, choices=(1, 3), default=1)
    args = parser.parse_args(argv)
    try:
        paths = (args.original, args.mapping, args.output, args.record)
        require(len({p.absolute() for p in paths}) == 4, "distinct input/output/record paths required")
        require(not any(path.exists() or path.is_symlink() for path in (args.output, args.record)),
                "exclusive output and record paths required")
        mapping, mapping_sha = recovery.load_json(args.mapping)
        binary, record = transform(recovery.read_owned(args.original, ssa.MAX_BYTES), mapping,
                                   not args.disabled, args.owned_descriptor_count)
        with tempfile.TemporaryDirectory(prefix="private-r1-presenter-") as directory:
            path = Path(directory) / "candidate.spv"
            path.write_bytes(binary)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=False)
            require(result.returncode == 0, "SPIR-V validation failed; no output published")
        record.update(spirv_val_passed=True, mapping_json_sha256=mapping_sha)
        recovery.write_owned(args.output, binary)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"scope": SCOPE, "output_sha256": record["output_sha256"], "passthrough_authorized": False}))
        return 0
    except (atlas.probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError,
            struct.error, ZeroDivisionError, subprocess.SubprocessError) as error:
        print("native fragment R1 presenter: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
