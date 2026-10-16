#!/usr/bin/env python3
"""Inert original-invocation SSA export into one caller-owned bounded SSBO.

Admission fields describe an OWNED HOST fixture, not an observed native device.
No receiver shader, pipeline or descriptor layout is activated by this tool.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_ssa_probe as ssa

require, recovery, probe = ssa.require, ssa.recovery, ssa.probe
ADMISSION_SCHEMA = "native-fragment-ssbo-owned-admission-v1"
ADMISSION_SCOPE = "EXPLICIT_OWNED_OFFSCREEN_FACTS_ONLY"
RECORD_WORDS, RECORD_BYTES, MAGIC = 32, 128, 0x5353424f
FIELDS = {"claim": [0, 1], "magic": [1, 1], "version": [2, 1], "words": [3, 1],
          "u": [4, 2], "R1": [6, 4], "R0": [10, 4], "patch": [14, 2], "flags": [16, 1],
          "tint": [17, 3], "first_sample": [20, 4], "second_sample": [24, 4], "original_rgba": [28, 4]}


def admission_contract(admission, enabled=True):
    keys = {"schema", "scope", "fragment_stores_and_atomics_enabled", "own_layout_compatible",
            "descriptor_set", "max_bound_descriptor_sets", "max_per_stage_storage_buffers",
            "max_storage_buffer_range", "descriptor_range"}
    require(type(enabled) is bool and enabled, "disabled SSBO export must not admit a pipeline")
    require(type(admission) is dict and set(admission) == keys and admission["schema"] == ADMISSION_SCHEMA
            and admission["scope"] == ADMISSION_SCOPE, "explicit owned SSBO admission schema required")
    require(admission["fragment_stores_and_atomics_enabled"] is True,
            "actually enabled fragmentStoresAndAtomics required, not physical support alone")
    require(admission["own_layout_compatible"] is True, "explicit caller-owned compatible layout required")
    for name in ("descriptor_set", "max_bound_descriptor_sets", "max_per_stage_storage_buffers", "max_storage_buffer_range", "descriptor_range"):
        require(type(admission[name]) is int and 0 <= admission[name] <= 0xffffffff, "bounded integer admission limit required")
    require(1 <= admission["descriptor_set"] < admission["max_bound_descriptor_sets"] <= 32,
            "distinct available owned descriptor set required")
    require(admission["max_per_stage_storage_buffers"] >= 1 and admission["max_storage_buffer_range"] >= RECORD_BYTES
            and admission["descriptor_range"] == RECORD_BYTES, "complete exact128-byte SSBO range and stage limits required")
    return admission["descriptor_set"]


def transform(original, admission, selected_fragment=(0, 0), enabled=True):
    own_set = admission_contract(admission, enabled)
    require(type(selected_fragment) in (tuple, list) and len(selected_fragment) == 2
            and all(type(v) is int and 0 <= v < 16384 for v in selected_fragment),
            "two bounded nonnegative fragment coordinates required")
    header, original_instructions = ssa.parse_module(original)
    require(len(original) == ssa.ORIGINAL_BYTES and probe.digest(original) == ssa.ORIGINAL_SHA256,
            "unknown original native shader profile")
    _, store_index = ssa.profile_structure(header, original_instructions)
    sets = {i.operands[2] for i in original_instructions if i.opcode == 71 and i.operands[1] == 34}
    require(sets == {0} and own_set not in sets, "original descriptor sets must be observed and disjoint")
    require(any(i.opcode == 71 and i.operands == (487, 11, 15) for i in original_instructions)
            and any(i.opcode == 59 and i.operands == (486, 487, 1) for i in original_instructions),
            "exact original fragment-coordinate input required")
    bool_type = next(i.operands[0] for i in original_instructions if i.opcode == 20)
    next_id = header[3]
    def alloc():
        nonlocal next_id
        result = next_id; next_id += 1; return result
    make = lambda op, *a: ssa.Instruction(op, tuple(a))
    declarations, body, constants = [], [], {}
    def constant(value, floating=False):
        bits = struct.unpack("<I", struct.pack("<f", value))[0] if floating else value
        key = floating, bits
        if key not in constants:
            constants[key] = alloc(); declarations.append(make(43, 8 if floating else 6, constants[key], bits))
        return constants[key]
    def emit(op, typ, *a):
        ident = alloc(); body.append(make(op, typ, ident, *a)); return ident
    # Uniform+BufferBlock is the public SPIR-V1.0 SSBO representation. Preserve
    # the exact original module version/capabilities; no extension is invented.
    array, structure, struct_pointer, word_pointer, variable = [alloc() for _ in range(5)]
    annotations = [make(71, array, 6, 4), make(71, structure, 3), make(72, structure, 0, 35, 0),
                   make(71, variable, 34, own_set), make(71, variable, 33, 0)]
    declarations += [make(29, array, 6), make(30, structure, array), make(32, struct_pointer, 2, structure),
                     make(32, word_pointer, 2, 6), make(59, struct_pointer, variable, 2)]
    frag = emit(61, 9, 487)  # Input read only, never an additional native UBO load.
    conditions = []
    for axis, coordinate in enumerate(selected_fragment):
        value = emit(81, 8, frag, axis)
        lower = emit(190, bool_type, value, constant(float(coordinate), True))
        upper = emit(184, bool_type, value, constant(float(coordinate+1), True))
        conditions.append(emit(167, bool_type, lower, upper))
    selected = emit(167, bool_type, *conditions)
    claim_label, write_label, claim_end, final = [alloc() for _ in range(4)]
    body += [make(247, final, 0), make(250, selected, claim_label, final), make(248, claim_label)]
    zero, one = constant(0), constant(1)
    claim_pointer = emit(65, word_pointer, variable, zero, zero)
    # Device scope, Relaxed success/failure semantics. This publishes CLAIMED,
    # not READY; only the owner's completed GPU fence permits reading the body.
    previous = emit(230, 6, claim_pointer, one, zero, zero, one, zero)
    won = emit(170, bool_type, previous, zero)
    body += [make(247, claim_end, 0), make(250, won, write_label, claim_end), make(248, write_label)]
    words = [None, constant(MAGIC), one, constant(RECORD_WORDS)]
    fields = [(538, 2), (871, 4), (777, 4), (797, 2), (709, 0), (907, 3), (603, 4), (629, 4), (994, 4)]
    for source, count in fields:
        if count == 0: words.append(source)
        else:
            for component in range(count):
                words.append(emit(124, 6, emit(81, 8, source, component)))
    require(len(words) == RECORD_WORDS, "fixed complete SSBO record required")
    store_pointers = []
    for index, value in enumerate(words[1:], 1):
        pointer = emit(65, word_pointer, variable, zero, constant(index))
        body.append(make(62, pointer, value)); store_pointers.append(pointer)
    body += [make(249, claim_end), make(248, claim_end), make(249, final), make(248, final)]
    first_type = next(i for i, item in enumerate(original_instructions) if item.opcode in ssa.TYPE_OPS)
    first_function = next(i for i, item in enumerate(original_instructions) if item.opcode == 54)
    changed = []
    for index, item in enumerate(original_instructions):
        if index == first_type: changed += annotations
        if index == first_function: changed += declarations
        if index == store_index: changed += body
        changed.append(item)
    require(next_id <= 65536, "instrumented result ID bound exceeded")
    result = [*header[:3], next_id, header[4]]
    result += [word for instruction in changed for word in instruction.words()]
    binary = struct.pack("<%dI" % len(result), *result)
    require(len(binary) <= ssa.MAX_BYTES, "bounded instrumented module required")
    record = {"schema": "native-fragment-ssbo-probe-v1", "scope": "INERT_OWNED_HOST_SSBO_ONLY",
        "admission": admission, "caller_facts_are_native_observations": False,
        "original_sha256": ssa.ORIGINAL_SHA256, "original_bytes": len(original),
        "instrumented_sha256": probe.digest(binary), "instrumented_bytes": len(binary),
        "entry": "PS_DISTORT", "original_descriptor_sets": sorted(sets), "own_descriptor_set": own_set,
        "own_descriptor_binding": 0, "descriptor_range": RECORD_BYTES, "record_words": RECORD_WORDS,
        "record_fields": FIELDS, "record_magic": MAGIC, "selected_fragment": list(selected_fragment),
        "claim_one_means": "claimed, not ready; complete only after owned GPU visibility barrier and fence",
        "added_native_ubo_or_sampler_loads": 0, "added_input_loads": 1,
        "added_color_output_locations": 0, "original_instruction_stream_preserved": True,
        "original_output_zero_store_preserved": True,
        "new_ssa_ids": {"variable": variable, "claim_pointer": claim_pointer, "claim_previous": previous,
            "word_pointers": store_pointers, "flags_word_source": 709, "last_store_pointer": store_pointers[-1]},
        "runtime_or_pipeline_activation": False, "native_feature_enabled_proven": False,
        "native_layout_compatible_proven": False, "native_output_lifetime_proven": False,
        "native_frame_association_proven": False, "physical_eye_proven": False,
        "application_ray_mapping_proven": False, "linear_game_alpha_proven": False,
        "alpha_composition_authorized": False, "opaque_fallback_required": True, "passthrough_authorized": False}
    return binary, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "admission", "output", "record"): parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--pixel", nargs=2, type=int, default=(0, 0))
    parser.add_argument("--disabled", action="store_true")
    args = parser.parse_args(argv)
    try:
        admission, admission_sha = recovery.load_json(args.admission)
        binary, record = transform(recovery.read_owned(args.original, ssa.MAX_BYTES), admission, args.pixel, not args.disabled)
        with tempfile.TemporaryDirectory(prefix="native-ssbo-private-") as temporary:
            candidate = Path(temporary)/"probe.spv"; candidate.write_bytes(binary)
            validation = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(candidate)],
                capture_output=True, timeout=15, check=False)
            require(validation.returncode == 0, "SPIR-V validation failed; no output published")
        record.update(spirv_val_passed=True, admission_json_sha256=admission_sha)
        recovery.write_owned(args.output, binary)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True, allow_nan=False)+"\n").encode())
        print(json.dumps({"scope": record["scope"], "instrumented_sha256": record["instrumented_sha256"], "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError, StopIteration, struct.error, subprocess.SubprocessError) as error:
        print("native fragment SSBO probe: "+str(error), file=sys.stderr); return 2


if __name__ == "__main__": raise SystemExit(main())
