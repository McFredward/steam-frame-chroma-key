#!/usr/bin/env python3
"""Offline, exact-pin count1 interface for the inert original-invocation SSBO.

This is deliberately separate from the original array3 parser/profile. Only the
owned module's B9 variable type is specialized; its original executable accesses,
output store and all SSBO instrumentation remain unchanged. Native layouts and
pipelines are never rewritten or activated here.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_ssbo_probe as ssbo

ssa, require = ssbo.ssa, ssbo.require
PROFILE = "EXACT_ORIGINAL_CONSTANT_ZERO_B9_COUNT1_SSBO_PIXEL_0_0_SET1_V1"
FIRST_COVERED_PROFILE = "EXACT_ORIGINAL_CONSTANT_ZERO_B9_COUNT1_SSBO_FIRST_COVERED_SET1_V1"
# A finite instrumentation grammar, not a change to ssa.ALLOWED. These extra
# opcodes are required by this exact SSBO transformer and are not original code.
SSBO_ALLOWED = ssa.ALLOWED | {29, 167, 184, 190, 230}


def parse_ssbo_module(data):
    require(type(data) is bytes and 20 <= len(data) <= ssa.MAX_BYTES and len(data) % 4 == 0,
            "bounded SSBO whole-word module required")
    words = struct.unpack("<%dI" % (len(data)//4), data)
    # Reuse only the established header admission, without widening its grammar.
    ssa.parse_module(data[:20])
    result, cursor = [], 5
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 65535
        require(0 < count <= len(words)-cursor and opcode in SSBO_ALLOWED,
                "unsupported separate SSBO profile instruction")
        result.append(ssa.Instruction(opcode, words[cursor+1:cursor+count])); cursor += count
    return words[:5], result


def original_interface(original):
    require(len(original) == ssa.ORIGINAL_BYTES and ssbo.probe.digest(original) == ssa.ORIGINAL_SHA256,
            "exact original specialization pin required")
    header, instructions = ssa.parse_module(original)
    ssa.profile_structure(header, instructions)
    expected = [ssa.Instruction(59, (288, 289, 0)), ssa.Instruction(32, (288, 0, 287)),
                ssa.Instruction(28, (287, 286, 102)), ssa.Instruction(43, (6, 102, 3)),
                ssa.Instruction(43, (90, 110, 0))]
    require(all(instructions.count(item) == 1 for item in expected), "exact original B9 type/index graph required")
    accesses = [i for i in instructions if 289 in ssa.value_inputs(i)]
    require(accesses == [ssa.Instruction(65, (290, 598, 289, 110)),
                         ssa.Instruction(65, (290, 624, 289, 110))],
            "exhaustive original B9 accesses must be the two constant0 chains")
    require({i.operands for i in instructions if i.opcode == 71 and i.operands[0] == 289}
            == {(289, 33, 9), (289, 34, 0)}, "exact original B9 decorations required")
    return expected[0], accesses


def canonical_admission():
    return {"schema": ssbo.ADMISSION_SCHEMA, "scope": ssbo.ADMISSION_SCOPE,
        "fragment_stores_and_atomics_enabled": True, "own_layout_compatible": True,
        "descriptor_set": 1, "max_bound_descriptor_sets": 2,
        "max_per_stage_storage_buffers": 1, "max_storage_buffer_range": 128, "descriptor_range": 128}


def insertion_blocks(original, instrumented):
    """Separate only transformer-owned additions from the pinned original."""
    _, native = ssa.parse_module(original)
    _, changed = parse_ssbo_module(instrumented)
    anchors = {next(i for i, item in enumerate(native) if item.opcode in ssa.TYPE_OPS),
               next(i for i, item in enumerate(native) if item.opcode == 54),
               next(i for i, item in enumerate(native) if item.opcode == 62 and item.operands == (504, 994))}
    blocks, cursor = {}, 0
    for index, item in enumerate(native):
        added = []
        while cursor < len(changed) and changed[cursor] != item:
            added.append(changed[cursor]); cursor += 1
        require(cursor < len(changed) and (not added or index in anchors), "only the three known owned insertion sites are allowed")
        if added: blocks[index] = added
        cursor += 1
    require(cursor == len(changed) and len(blocks) == 3, "complete original instruction stream retained")
    return native, blocks


def canonical_ssbo(original, first_covered=False):
    require(type(first_covered) is bool, "explicit SSBO selection mode required")
    binary, _ = ssbo.transform(original, canonical_admission(), (0, 0))
    if not first_covered: return binary
    # Retain the existing atomic first claimant, removing only the coordinate
    # predicate around it. This chooses an actually rasterized invocation; no
    # guessed screen center, private eye/frame claim or extra resource read.
    header, _ = parse_ssbo_module(binary)
    native, blocks = insertion_blocks(original, binary)
    body_index = max(blocks); body = blocks[body_index]
    merge = next(i for i, item in enumerate(body) if item.opcode == 247)
    final = body[merge].operands[0]
    branch = body[merge+1];require(branch.opcode == 250, "exact outer coordinate predicate")
    claim = ssa.Instruction(248, (branch.operands[1],))
    claim_index = body.index(claim)
    require(body[-2:] == [ssa.Instruction(249, (final,)), ssa.Instruction(248, (final,))], "exact outer predicate tail")
    blocks[body_index] = body[claim_index+1:-2]
    result = []
    for index, item in enumerate(native): result.extend(blocks.get(index, [])); result.append(item)
    words = list(header)+[word for item in result for word in item.words()]
    return struct.pack("<%dI" % len(words), *words)


def specialize(original, instrumented, first_covered=False):
    variable, accesses = original_interface(original)
    canonical = canonical_ssbo(original, first_covered)
    # Do not accept caller claims that an arbitrary instrumented module preserves
    # accesses. Byte equality proves this exact original SSA export before the
    # independent interface-only rewrite (including every added SSBO operation).
    require(instrumented == canonical, "exact canonical Set1 pixel0,0 SSBO transform required")
    header, instructions = parse_ssbo_module(instrumented)
    require([i for i in instructions if i.opcode == 65 and len(i.operands) >= 3 and i.operands[2] == 289] == accesses,
            "SSBO must preserve both original constant0 B9 accesses")
    one, array, pointer = range(header[3], header[3]+3)
    declarations = [ssa.Instruction(43, (6, one, 1)), ssa.Instruction(28, (array, 286, one)),
                    ssa.Instruction(32, (pointer, 0, array))]
    result, replacements = [], 0
    for item in instructions:
        if item == variable:
            result.extend(declarations); item = ssa.Instruction(59, (pointer, 289, 0)); replacements += 1
        result.append(item)
    require(replacements == 1 and pointer < 65536, "single bounded owned interface specialization required")
    words = [*header[:3], pointer+1, header[4]]+[word for item in result for word in item.words()]
    binary = struct.pack("<%dI" % len(words), *words)
    record = {"schema": "native-fragment-count1-ssbo-profile-v1", "profile": FIRST_COVERED_PROFILE if first_covered else PROFILE,
        "original_sha256": ssa.ORIGINAL_SHA256, "original_bytes": len(original),
        "canonical_array3_ssbo_sha256": ssbo.probe.digest(canonical),
        "output_sha256": ssbo.probe.digest(binary), "output_bytes": len(binary),
        "original_b9_access_count": 2, "original_b9_access_indices": [0, 0],
        "new_interface_ids": [one, array, pointer], "shared_original_array_length_mutated": False,
        "original_executable_accesses_preserved": True, "original_output_store_preserved": True,
        "instrumentation": "original-invocation 128-byte Set1 SSBO; single color attachment",
        "selected_fragment": None if first_covered else [0, 0], "first_covered_invocation": first_covered,
        "own_b9_descriptor_count": 1,
        "native_descriptor_layout_modified": False, "native_creation_proven": False,
        "native_same_draw_eye_frame_binding_proven": False, "runtime_activation_authorized": False,
        "passthrough_authorized": False}
    return binary, record


def transform(original, first_covered=False):
    original_interface(original)
    return specialize(original, canonical_ssbo(original, first_covered), first_covered)


def cpp_template_data(original):
    """Owned additions and scalar patch offsets only; no original instructions."""
    original_interface(original)
    header, native = ssa.parse_module(original)
    offsets, cursor = [], 5
    for item in native: offsets.append(cursor); cursor += len(item.words())
    _, blocks = insertion_blocks(original, canonical_ssbo(original))
    _, first_blocks = insertion_blocks(original, canonical_ssbo(original, True))
    points = sorted(blocks)
    instrumented_header, _ = parse_ssbo_module(canonical_ssbo(original))
    one, array, pointer = range(instrumented_header[3], instrumented_header[3]+3)
    flatten = lambda items: [word for item in items for word in item.words()]
    return {"annotations": flatten(blocks[points[0]]), "globals": flatten(blocks[points[1]]),
        "pixel_body": flatten(blocks[points[2]]), "first_body": flatten(first_blocks[points[2]]),
        "interface_types": flatten([ssa.Instruction(43, (6, one, 1)),
            ssa.Instruction(28, (array, 286, one)), ssa.Instruction(32, (pointer, 0, array))]),
        "interface_variable": flatten([ssa.Instruction(59, (pointer, 289, 0))]),
        "insertion_offsets": [offsets[index] for index in points],
        "variable_offset": offsets[native.index(ssa.Instruction(59, (288, 289, 0)))],
        "output_bound": pointer+1, "original_bound": header[3]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "output", "record"): parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--first-covered", action="store_true")
    args = parser.parse_args(argv)
    try:
        require(args.output.absolute() != args.record.absolute(), "distinct private output paths required")
        require(not any(path.exists() or path.is_symlink() for path in (args.output, args.record)), "exclusive output paths required")
        binary, record = transform(ssbo.recovery.read_owned(args.original, ssa.MAX_BYTES), args.first_covered)
        with tempfile.TemporaryDirectory(prefix="owned-count1-ssbo-") as temporary:
            path = Path(temporary)/"probe.spv"; path.write_bytes(binary)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                capture_output=True, timeout=15, check=False)
            require(result.returncode == 0, "specialized SSBO SPIR-V validation failed; no output published")
        record["spirv_val_passed"] = True
        ssbo.recovery.write_owned(args.output, binary)
        ssbo.recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True)+"\n").encode())
        print(json.dumps({"profile": record["profile"], "output_sha256": record["output_sha256"], "passthrough_authorized": False}))
        return 0
    except (ssbo.probe.ProbeError, OSError, ValueError, KeyError, struct.error, subprocess.SubprocessError) as error:
        print("native count1 SSBO profile: "+str(error), file=sys.stderr); return 2


if __name__ == "__main__": raise SystemExit(main())
