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
SPARSE32_PROFILE = "EXACT_ORIGINAL_CONSTANT_ZERO_B9_COUNT1_SSBO_FIRST_ELIGIBLE_GRID32_SET1_V1"
MIRROR_PROFILE = "EXACT_BOTH_STAGES_UNUSED_BINDING8_COUNT1_SSBO_FIRST_ELIGIBLE_GRID32_SET0_MIRROR_V1"
VERTEX_BYTES = 9460
VERTEX_SHA256 = "94abc0c24124e8b4a301e7f1df5a4c8c0e8b0eb50e4c6ea78f3521370a6897c5"
MIRROR_BINDINGS = (0, 2, 3, 4, 6, 9, 25)
MIRROR_RECORD_BINDING = 8
# A finite instrumentation grammar, not a change to ssa.ALLOWED. These extra
# opcodes are required by this exact SSBO transformer and are not original code.
SSBO_ALLOWED = ssa.ALLOWED | {29, 109, 167, 184, 190, 230}


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


def election_mode(first_covered=False, election=None):
    require(type(first_covered) is bool, "explicit SSBO selection mode required")
    if election is None: return "first-covered" if first_covered else "pixel0"
    require(type(election) is str and election in ("pixel0", "first-covered", "sparse32") and not first_covered,
            "one explicit SSBO election mode required")
    return election


def canonical_ssbo(original, first_covered=False, election=None):
    mode = election_mode(first_covered, election)
    binary, _ = ssbo.transform(original, canonical_admission(), (0, 0))
    if mode == "pixel0": return binary
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
    if mode == "first-covered": blocks[body_index] = body[claim_index+1:-2]
    else:
        # Every32nd integer pixel on both axes may contend. This is the first
        # ELIGIBLE grid invocation, not necessarily the first covered fragment.
        # A mesh/scissor with no eligible coverage leaves claim0 and is refused.
        # No guessed center, native UBO read, subgroup feature or shared read is
        # introduced. Original fragment output/accesses remain intact.
        frag = body[0];require(frag.opcode == 61 and frag.operands[0] == 9 and frag.operands[2] == 487, "exact own fragment-coordinate load")
        components = [next(item for item in body[:merge] if item.opcode == 81 and item.operands[2:] == (frag.operands[1], axis)) for axis in (0, 1)]
        ids = list(range(header[3], header[3]+9));zero, mask, x, y, mx, my, ex, ey, selected = ids
        boolean = next(item.operands[0] for item in native if item.opcode == 20)
        make = lambda op, *a: ssa.Instruction(op, tuple(a))
        blocks[sorted(blocks)[1]] += [make(43, 6, zero, 0), make(43, 6, mask, 31)]
        prefix = [frag, *components, make(109, 6, x, components[0].operands[1]), make(109, 6, y, components[1].operands[1]),
                  make(199, 6, mx, x, mask), make(199, 6, my, y, mask), make(170, boolean, ex, mx, zero),
                  make(170, boolean, ey, my, zero), make(167, boolean, selected, ex, ey)]
        blocks[body_index] = prefix+[body[merge], make(250, selected, *branch.operands[1:])]+body[merge+2:]
        header = (*header[:3], ids[-1]+1, header[4])
    result = []
    for index, item in enumerate(native): result.extend(blocks.get(index, [])); result.append(item)
    words = list(header)+[word for item in result for word in item.words()]
    return struct.pack("<%dI" % len(words), *words)


def specialize(original, instrumented, first_covered=False, election=None):
    mode = election_mode(first_covered, election)
    variable, accesses = original_interface(original)
    canonical = canonical_ssbo(original, election=mode)
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
    record = {"schema": "native-fragment-count1-ssbo-profile-v1", "profile": {"first-covered": FIRST_COVERED_PROFILE, "pixel0": PROFILE, "sparse32": SPARSE32_PROFILE}[mode],
        "original_sha256": ssa.ORIGINAL_SHA256, "original_bytes": len(original),
        "canonical_array3_ssbo_sha256": ssbo.probe.digest(canonical),
        "output_sha256": ssbo.probe.digest(binary), "output_bytes": len(binary),
        "original_b9_access_count": 2, "original_b9_access_indices": [0, 0],
        "new_interface_ids": [one, array, pointer], "shared_original_array_length_mutated": False,
        "original_executable_accesses_preserved": True, "original_output_store_preserved": True,
        "instrumentation": "original-invocation 128-byte Set1 SSBO; single color attachment",
        "selected_fragment": [0, 0] if mode == "pixel0" else None, "first_covered_invocation": mode == "first-covered",
        "sparse_grid32": mode == "sparse32", "election": mode,
        "sparse_empty_claim_means": "safe incomplete/refusal, no eligible covered grid fragment" if mode == "sparse32" else None,
        "own_b9_descriptor_count": 1,
        "native_descriptor_layout_modified": False, "native_creation_proven": False,
        "native_same_draw_eye_frame_binding_proven": False, "runtime_activation_authorized": False,
        "passthrough_authorized": False}
    return binary, record


def transform(original, first_covered=False, election=None):
    original_interface(original)
    mode = election_mode(first_covered, election)
    return specialize(original, canonical_ssbo(original, election=mode), election=mode)


def descriptor_census(data):
    """Independent finite metadata walk; never broadens the executable parser.

    Complete decorated-variable sets are checked only AFTER exact stage pins by
    transform_mirror. This helper alone creates no checked profile/authority.
    """
    require(type(data) is bytes and 20 <= len(data) <= ssa.MAX_BYTES and len(data)%4 == 0,
            "bounded descriptor metadata module required")
    words = struct.unpack("<%dI" % (len(data)//4), data)
    require(words[0] == 0x07230203 and words[4] == 0 and 0 < words[3] < 65536,
            "ordinary bounded SPIR-V descriptor metadata header required")
    decorations, variables, at = {}, {}, 5
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 65535
        require(0 < count <= len(words)-at, "complete bounded descriptor metadata instruction")
        operands = words[at+1:at+count]
        require(op not in (73, 74, 75, 332), "indirect/group descriptor decorations are not attested")
        if op == 71 and len(operands) >= 2 and operands[1] in (33, 34):
            require(len(operands) == 3 and operands[0] < words[3], "literal descriptor decoration required")
            target = decorations.setdefault(operands[0], {})
            require(operands[1] not in target, "duplicate descriptor decoration")
            target[operands[1]] = operands[2]
        if op == 59:
            require(len(operands) >= 3 and operands[1] not in variables, "unique descriptor variable definition")
            variables[operands[1]] = operands[2]
        at += count
    result = []
    for target, values in decorations.items():
        require(set(values) == {33, 34} and target in variables, "complete literal decorated variable required")
        result.append((values[34], values[33], variables[target]))
    require(len(result) == len(set(result)), "unique native descriptor bindings required")
    return tuple(sorted(result))


def mirror_original_interfaces(vertex, fragment):
    require(len(vertex) == VERTEX_BYTES and ssbo.probe.digest(vertex) == VERTEX_SHA256,
            "exact original vertex mirror pin required")
    original_interface(fragment)
    vs = descriptor_census(vertex); ps = descriptor_census(fragment)
    require(vs == ((0, 0, 2), (0, 2, 2), (0, 3, 2)) and
            ps == ((0, 4, 2), (0, 6, 2), (0, 9, 0), (0, 25, 0)),
            "complete pinned original descriptor census required; binding8 collision refused")
    require(all(binding != MIRROR_RECORD_BINDING for _, binding, _ in (*vs, *ps)),
            "original stages must not statically declare the mirror record binding")
    return vs, ps


def transform_mirror(vertex, fragment):
    """Exact original one-set interface, separate from all legacy Set1 modes."""
    vs, ps = mirror_original_interfaces(vertex, fragment)
    legacy, record = transform(fragment, election="sparse32")
    header, instructions = parse_ssbo_module(legacy)
    old = {ssa.Instruction(71, (1014, 34, 1)): ssa.Instruction(71, (1014, 34, 0)),
           ssa.Instruction(71, (1014, 33, 0)): ssa.Instruction(71, (1014, 33, MIRROR_RECORD_BINDING))}
    require(all(instructions.count(item) == 1 for item in old), "exact owned SSBO descriptor decorations required")
    changed = [old.get(item, item) for item in instructions]
    words = list(header)+[word for item in changed for word in item.words()]
    binary = struct.pack("<%dI" % len(words), *words)
    record.update(profile=MIRROR_PROFILE, original_vertex_sha256=VERTEX_SHA256,
        original_vertex_bytes=len(vertex), original_vertex_descriptors=vs, original_fragment_descriptors=ps,
        record_descriptor_set=0, record_descriptor_binding=MIRROR_RECORD_BINDING,
        required_original_copy_bindings=list(MIRROR_BINDINGS), compatible_original_set0_mirror=True,
        additional_set1=False, original_binding8_unused=True, native_descriptor_set_written=False,
        output_sha256=ssbo.probe.digest(binary), output_bytes=len(binary),
        instrumentation="original-invocation 128-byte owned Set0 binding8 SSBO; unchanged original one-set layout")
    return binary, record


def cpp_template_data(original):
    """Owned additions and scalar patch offsets only; no original instructions."""
    original_interface(original)
    header, native = ssa.parse_module(original)
    offsets, cursor = [], 5
    for item in native: offsets.append(cursor); cursor += len(item.words())
    _, blocks = insertion_blocks(original, canonical_ssbo(original))
    _, first_blocks = insertion_blocks(original, canonical_ssbo(original, True))
    sparse_header, _ = parse_ssbo_module(canonical_ssbo(original, election="sparse32"))
    _, sparse_blocks = insertion_blocks(original, canonical_ssbo(original, election="sparse32"))
    points = sorted(blocks)
    instrumented_header, _ = parse_ssbo_module(canonical_ssbo(original))
    one, array, pointer = range(instrumented_header[3], instrumented_header[3]+3)
    flatten = lambda items: [word for item in items for word in item.words()]
    sparse_one, sparse_array, sparse_pointer = range(sparse_header[3], sparse_header[3]+3)
    return {"annotations": flatten(blocks[points[0]]), "globals": flatten(blocks[points[1]]),
        "pixel_body": flatten(blocks[points[2]]), "first_body": flatten(first_blocks[points[2]]),
        "sparse_globals": flatten(sparse_blocks[points[1]]), "sparse_body": flatten(sparse_blocks[points[2]]),
        "interface_types": flatten([ssa.Instruction(43, (6, one, 1)),
            ssa.Instruction(28, (array, 286, one)), ssa.Instruction(32, (pointer, 0, array))]),
        "interface_variable": flatten([ssa.Instruction(59, (pointer, 289, 0))]),
        "sparse_interface_types": flatten([ssa.Instruction(43, (6, sparse_one, 1)),
            ssa.Instruction(28, (sparse_array, 286, sparse_one)), ssa.Instruction(32, (sparse_pointer, 0, sparse_array))]),
        "sparse_interface_variable": flatten([ssa.Instruction(59, (sparse_pointer, 289, 0))]),
        "insertion_offsets": [offsets[index] for index in points],
        "variable_offset": offsets[native.index(ssa.Instruction(59, (288, 289, 0)))],
        "output_bound": pointer+1, "sparse_output_bound": sparse_pointer+1, "original_bound": header[3]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "output", "record"): parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--first-covered", action="store_true")
    parser.add_argument("--mirror-set0", action="store_true")
    parser.add_argument("--vertex", type=Path)
    parser.add_argument("--election", choices=("pixel0", "first-covered", "sparse32"))
    args = parser.parse_args(argv)
    try:
        require(args.output.absolute() != args.record.absolute(), "distinct private output paths required")
        require(not any(path.exists() or path.is_symlink() for path in (args.output, args.record)), "exclusive output paths required")
        original = ssbo.recovery.read_owned(args.original, ssa.MAX_BYTES)
        if args.mirror_set0:
            require(args.vertex is not None and not args.first_covered and args.election is None,
                    "mirror requires both original stages and its distinct fixed sparse32 mode")
            binary, record = transform_mirror(ssbo.recovery.read_owned(args.vertex, ssa.MAX_BYTES), original)
        else:
            require(args.vertex is None, "vertex input is exclusive to the checked mirror mode")
            binary, record = transform(original, args.first_covered, args.election)
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
