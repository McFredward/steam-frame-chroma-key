#!/usr/bin/env python3
"""Inert offscreen dual-panel probe; explicit diagnostic W is never inferred.

The privately supplied exact native dependency cone is cloned at execution time.
No native instruction body is redistributed, no runtime override is installed,
and raw reconstructed component-G is not claimed to be linear game opacity.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import sys

import native_fragment_ssa_probe as ssa

probe, recovery, require = ssa.probe, ssa.recovery, ssa.require
MAPPING_SCHEMA = "native-fragment-atlas-mapping-v1"
MAPPING_SCOPE = "EXPLICIT_DIAGNOSTIC_AFFINES_ONLY"
PACKED_MAPPING_SCHEMA = "native-fragment-packed-atlas-mapping-v1"
PACKED_MAPPING_SCOPE = "EXPLICIT_DIAGNOSTIC_PACKED_AFFINES_ONLY"
OUTPUTS = [
    {"location": 0, "format": "R32G32B32A32_SFLOAT", "meaning": "unchanged original RGBA"},
    {"location": 1, "format": "R32G32B32A32_SFLOAT", "meaning": "retargeted native decoded RGB times original-q aperture/tint once; independently reconstructed raw component-G"},
    {"location": 2, "format": "R32G32B32A32_SFLOAT", "meaning": ["qc.x", "qc.y", "qa.x", "qa.y"]},
    {"location": 3, "format": "R32G32B32A32_SFLOAT", "meaning": ["color inner weight", "coverage inner weight", "original-q aperture", "enabled"]},
    {"location": 4, "format": "R32G32B32A32_UINT", "meaning": ["tint.r IEEE32 bits", "tint.g IEEE32 bits", "tint.b IEEE32 bits", "raw flags uint32"]}]


def f32(value):
    try: valid = type(value) in (int, float) and math.isfinite(value)
    except OverflowError: valid = False
    require(valid, "finite non-boolean affine coefficient required")
    try: result = struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error): recovery.fail("affine coefficient exceeds binary32")
    require(math.isfinite(result), "finite binary32 affine coefficient required")
    return result


def affine(value):
    require(type(value) is list and len(value) == 6, "six row-major affine coefficients required")
    result = [f32(item) for item in value]
    determinant = result[0] * result[4] - result[1] * result[3]
    require(math.isfinite(determinant) and determinant != 0., "nondegenerate finite affine required")
    return result


def compose(a, b):
    return [a[0]*b[0]+a[1]*b[3], a[0]*b[1]+a[1]*b[4], a[0]*b[2]+a[1]*b[5]+a[2],
            a[3]*b[0]+a[4]*b[3], a[3]*b[1]+a[4]*b[4], a[3]*b[2]+a[4]*b[5]+a[5]]


def mapping_contract(mapping):
    require(type(mapping) is dict and set(mapping) == {"schema", "scope", "W", "color", "coverage"}
            and mapping["schema"] == MAPPING_SCHEMA and mapping["scope"] == MAPPING_SCOPE,
            "explicit pinned diagnostic affine schema required")
    return conjugated_panels(mapping, "W")


def packed_mapping_contract(mapping):
    """B is an explicit chart into original packed-u, never a discovered W."""
    require(type(mapping) is dict and set(mapping) == {"schema", "scope", "B", "color", "coverage"}
            and mapping["schema"] == PACKED_MAPPING_SCHEMA and mapping["scope"] == PACKED_MAPPING_SCOPE,
            "explicit pinned diagnostic packed affine schema required")
    return conjugated_panels(mapping, "B")


def conjugated_panels(mapping, chart):
    w, color, coverage = (affine(mapping[key]) for key in (chart, "color", "coverage"))
    determinant = w[0]*w[4]-w[1]*w[3]
    inverse = [w[4]/determinant, -w[1]/determinant, (w[1]*w[5]-w[4]*w[2])/determinant,
               -w[3]/determinant, w[0]/determinant, (w[3]*w[2]-w[0]*w[5])/determinant]
    require(all(math.isfinite(item) for item in inverse), "finite inverse W required")
    return affine(compose(compose(w, color), inverse)), affine(compose(compose(w, coverage), inverse))


def result_id(item):
    return item.operands[0] if item.opcode in ssa.FIRST_RESULT else item.operands[1] if item.opcode in ssa.TYPED_RESULT else None


def source_cone(header, instructions, store_index):
    start = next(i for i, item in enumerate(instructions) if item.opcode == 248 and item.operands == (703,))
    cone = instructions[start:store_index]
    require(cone and cone[0].opcode == 248 and all(item.opcode not in (54, 56, 62, 253) for item in cone),
            "unsupported native dependency-cone boundary")
    definitions, blocks, successors, block = {}, {}, {}, None
    for index, item in enumerate(instructions):
        if item.opcode == 248: block = item.operands[0]; successors[block] = []
        ident = result_id(item)
        if ident is not None: definitions[ident] = item; blocks[ident] = block
        if item.opcode in (249, 250): successors[block] = list(item.operands if item.opcode == 249 else item.operands[1:])
    dom = ssa.dominators(successors, 5)
    loads = [item.operands[1] for item in cone if item.opcode == 61]
    require(set(loads) == {599, 600, 625, 626, 906}, "native descriptor-load envelope differs")
    require(all(blocks[ident] in dom[661] for ident in loads + [1001, 538, 883, 871, 907, 899, 709]),
            "reused native SSA does not dominate splice")
    require(all(definitions[ident].opcode == 87 for ident in (603, 629))
            and definitions[1008].opcode == 169 and definitions[687].opcode == 129,
            "native sample/weight/reconstruction anchors differ")
    require(header[3] < 65536, "native result-ID envelope exhausted")
    return cone, loads


def renamed(item, replacements):
    """Remap IDs only; literal vector indices and operation codes stay literals."""
    op, operands = item.opcode, list(item.operands)
    require(op not in (61, 65), "native descriptor loads/access chains must be reused, never cloned")
    if op == 248: indices = [0]
    elif op == 247: indices = [0]
    elif op in (249, 250): indices = list(range(len(operands)))
    elif op in ssa.TYPED_RESULT:
        indices = [1]
        if op == 79: indices += [2, 3]
        elif op == 81: indices += [2]
        elif op == 82: indices += [2, 3]
        elif op == 12: indices += list(range(4, len(operands)))
        elif op == 87:
            require(len(operands) == 4, "unsupported implicit sample operands"); indices += [2, 3]
        else: indices += list(range(2, len(operands)))
    else: recovery.fail("unsupported cloned native opcode")
    for index in indices: operands[index] = replacements.get(operands[index], operands[index])
    return ssa.Instruction(op, tuple(operands))


def transform(original, mapping, enabled=True):
    return _transform(original, mapping, enabled, packed=False)


def transform_packed(original, mapping, enabled=True):
    """Retarget original u538 directly, using only same-invocation metadata."""
    return _transform(original, mapping, enabled, packed=True)


def _transform(original, mapping, enabled, packed):
    require(type(enabled) is bool, "enabled must be an explicit boolean")
    mc, ma = (packed_mapping_contract if packed else mapping_contract)(mapping)
    header, instructions = ssa.parse_module(original)
    require(len(original) == ssa.ORIGINAL_BYTES and probe.digest(original) == ssa.ORIGINAL_SHA256,
            "unknown original native shader profile")
    entry, store_index = ssa.profile_structure(header, instructions)
    cone, reused_loads = source_cone(header, instructions, store_index)
    next_id = header[3]
    def allocate():
        nonlocal next_id
        value = next_id; next_id += 1; return value
    make = lambda op, *a: ssa.Instruction(op, tuple(a))
    declarations, prefix = [], []
    constants = {}
    def constant(value):
        bits = struct.unpack("<I", struct.pack("<f", f32(value)))[0]
        if bits not in constants:
            constants[bits] = allocate(); declarations.append(make(43, 8, constants[bits], bits))
        return constants[bits]
    def emit(target, op, typ, *values):
        ident = allocate(); target.append(make(op, typ, ident, *values)); return ident
    source = 538 if packed else 883
    qx = emit(prefix, 81, 8, source, 0); qy = emit(prefix, 81, 8, source, 1)
    minimum = emit(prefix, 79, 39, 871, 871, 0, 1)
    maximum = emit(prefix, 79, 39, 871, 871, 2, 3)
    span = emit(prefix, 131, 39, maximum, minimum)
    def target_coordinate(matrix):
        xy = []
        for row in (0, 3):
            x = emit(prefix, 133, 8, constant(matrix[row]), qx)
            y = emit(prefix, 133, 8, constant(matrix[row+1]), qy)
            xy.append(emit(prefix, 129, 8, emit(prefix, 129, 8, x, y), constant(matrix[row+2])))
        mapped = emit(prefix, 80, 39, *xy)
        if packed:
            u = mapped
            # Diagnostic normalization only. Both cloned native cones consume
            # mapped u directly; R1 is the original value in this invocation.
            q = emit(prefix, 136, 39, emit(prefix, 131, 39, u, minimum), span)
        else:
            q = mapped
            u = emit(prefix, 129, 39, minimum, emit(prefix, 133, 39, span, q))
        return q, u
    qc, uc = target_coordinate(mc); qa, ua = target_coordinate(ma)
    def clone(u):
        substitutions = {538: u}
        for item in cone:
            ident = result_id(item)
            if ident is not None and item.opcode not in (61, 65) and ident != 1001:
                substitutions[ident] = allocate()
        body = [renamed(item, substitutions) for item in cone
                if item.opcode not in (61, 65) and result_id(item) != 1001]
        require(not any(item.opcode == 61 for item in body), "clone introduced a native descriptor load")
        return substitutions, body
    color, color_body = clone(uc); mask, mask_body = clone(ua)
    continuation = allocate()
    first_type = next(i for i, item in enumerate(instructions) if item.opcode in ssa.TYPE_OPS)
    first_function = next(i for i, item in enumerate(instructions) if item.opcode == 54)
    u3, u4, uptr = allocate(), allocate(), allocate()
    variables = [allocate() for _ in range(4)]
    annotations = [make(71, variable, 30, location) for location, variable in enumerate(variables, 1)]
    declarations += [make(23, u3, 6, 3), make(23, u4, 6, 4), make(32, uptr, 3, u4)]
    declarations += [make(59, 503, variable, 3) for variable in variables[:3]] + [make(59, uptr, variables[3], 3)]
    suffix = []
    color_aperture = emit(suffix, 142, 30, color[687], 899)
    color_rgb = emit(suffix, 133, 30, color_aperture, 907)
    first_g = emit(suffix, 81, 8, mask[603], 1); second_g = emit(suffix, 81, 8, mask[629], 1)
    inverse_weight = emit(suffix, 131, 8, constant(1.), mask[1008])
    raw_g = emit(suffix, 129, 8, emit(suffix, 133, 8, first_g, inverse_weight), emit(suffix, 133, 8, second_g, mask[1008]))
    output1 = emit(suffix, 80, 9, color_rgb, raw_g)
    coordinates = emit(suffix, 79, 9, qc, qa, 0, 1, 2, 3)
    weights = emit(suffix, 80, 9, color[1008], mask[1008], 899, constant(1. if enabled else 0.))
    tint_bits = emit(suffix, 124, u3, 907)
    tint_flags = emit(suffix, 80, u4, tint_bits, 709)
    suffix += [make(62, variable, value) for variable, value in zip(variables,
        (output1 if enabled else 994, coordinates, weights, tint_flags))]
    injection = prefix + [make(249, color[703])] + color_body + [make(249, mask[703])] + mask_body
    injection += [make(249, continuation), make(248, continuation)] + suffix
    patched_instructions = []
    for index, item in enumerate(instructions):
        if index == first_type: patched_instructions.extend(annotations)
        if index == first_function: patched_instructions.extend(declarations)
        if index == store_index: patched_instructions.extend(injection)
        patched_instructions.append(make(15, *item.operands, *variables) if item is entry else item)
    require(next_id <= 65536, "instrumented ID bound exceeded")
    words = [*header[:3], next_id, header[4]]
    words.extend(word for item in patched_instructions for word in item.words())
    patched = struct.pack("<%dI" % len(words), *words)
    require(len(patched) <= ssa.MAX_BYTES, "instrumented byte bound exceeded")
    record = {"schema": "native-fragment-atlas-probe-v1", "scope": "INERT_OFFLINE_EXPLICIT_AFFINE_PROBE_ONLY",
        "original_sha256": ssa.ORIGINAL_SHA256, "instrumented_sha256": probe.digest(patched),
        "original_bytes": len(original), "instrumented_bytes": len(patched), "entry": "PS_DISTORT",
        "mapping": mapping, "Mc": mc, "Ma": ma, "affine_composition": "binary64 on host then constants rounded to binary32; GPU arithmetic binary32",
        "enabled": enabled, "disabled_retains_diagnostic_coordinates_and_weights": True, "outputs": OUTPUTS,
        "coverage_meaning": "raw sampled RGB_IDENTITY component-G reconstructed with its own Ma weight; no decoder, tint or aperture; not proven linear game alpha",
        "color_policy": "native decoded Mc reconstruction multiplied by ORIGINAL-q aperture and ORIGINAL tint exactly once",
        "original_instruction_stream_reconstructible": True, "original_output_zero_preserved": True,
        "added_native_ubo_or_sampler_loads": 0, "reused_original_load_ssa_ids": reused_loads,
        "new_ssa_ids": {"mapped_qc": qc, "mapped_qa": qa, "color_weight": color[1008], "mask_weight": mask[1008],
            "original_aperture": 899, "combined_color_rgb": color_rgb, "raw_component_g": raw_g,
            "output1": output1, "diagnostic_weight_vector": weights},
        "runtime_or_pipeline_activation": False, "native_draw_coherence_proven": False,
        "native_frame_association_proven": False, "application_ray_mapping_proven": False,
        "physical_eye_proven": False, "native_sampler_equivalence_proven": False,
        "linear_game_alpha_proven": False, "alpha_composition_authorized": False,
        "opaque_fallback_required": True, "passthrough_authorized": False}
    if packed:
        record.update(schema="native-fragment-packed-atlas-probe-v1",
            scope="INERT_OFFLINE_EXPLICIT_PACKED_AFFINE_PROBE_ONLY",
            Pc=record.pop("Mc"), Pa=record.pop("Ma"),
            coordinate_policy="Pc=B color B^-1 and Pa=B coverage B^-1 transform original u538 directly; target-q is diagnostic only",
            coverage_meaning="raw sampled RGB_IDENTITY component-G reconstructed with its own Pa weight; no decoder, tint or aperture; not proven linear game alpha",
            color_policy="native decoded Pc reconstruction multiplied by ORIGINAL-q aperture and ORIGINAL tint exactly once",
            copied_r1_or_cpu_uniform_join_used=False)
        record["new_ssa_ids"].update(mapped_uc=uc, mapped_ua=ua)
    return patched, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "mapping", "output", "record"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--packed", action="store_true", help="require the distinct explicit packed-B schema")
    args = parser.parse_args(argv)
    try:
        mapping, mapping_sha = recovery.load_json(args.mapping)
        builder = transform_packed if args.packed else transform
        patched, record = builder(recovery.read_owned(args.original, ssa.MAX_BYTES), mapping, not args.disabled)
        with tempfile.TemporaryDirectory(prefix="private-native-atlas-") as temporary:
            candidate = Path(temporary)/"instrumented.spv"; candidate.write_bytes(patched)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(candidate)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=False)
            require(result.returncode == 0, "SPIR-V validation failed; no output published")
        record.update(spirv_val_passed=True, mapping_json_sha256=mapping_sha)
        recovery.write_owned(args.output, patched)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True, allow_nan=False)+"\n").encode())
        print(json.dumps({"scope": record["scope"], "instrumented_sha256": record["instrumented_sha256"], "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError, struct.error, ZeroDivisionError, subprocess.SubprocessError) as error:
        print("native fragment atlas probe: "+str(error), file=sys.stderr); return 2


if __name__ == "__main__":
    raise SystemExit(main())
