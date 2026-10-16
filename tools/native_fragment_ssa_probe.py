#!/usr/bin/env python3
"""Inert, offline-only MRT exports of already-consumed original fragment SSA.

Only one privately supplied, exact native module is accepted. This changes no
runtime, installs no override and establishes no source frame, W, eye or alpha
binding. Generated native modules must remain private and untracked.
"""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import pixel_payload_recovery as recovery

probe = recovery.probe
ORIGINAL_SHA256 = "f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2"
ORIGINAL_BYTES = 6620
MAX_BYTES = 65536
TYPE_OPS = frozenset((19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 32, 33))
FIRST_RESULT = TYPE_OPS | {11, 248}
TYPED_RESULT = frozenset((12, 43, 44, 46, 54, 59, 61, 65, 79, 80, 81, 82, 86, 87,
    124, 129, 131, 133, 136, 142, 145, 169, 170, 171, 194, 199, 245))
ALLOWED = FIRST_RESULT | TYPED_RESULT | {14, 15, 16, 17, 56, 62, 71, 72, 247, 249, 250, 253}
# Numeric IDs and interface metadata only; no native instruction bodies.
ANCHORS = {"u": (538, 39), "patch": (797, 39), "R0": (777, 9), "R1": (871, 9),
           "tint": (907, 30), "flags": (709, 6), "q": (883, 39)}
OUTPUTS = [
    {"location": 0, "format": "R32G32B32A32_SFLOAT", "meaning": "unchanged original output"},
    {"location": 1, "format": "R32G32B32A32_SFLOAT", "meaning": ["u.x", "u.y", "patch.x", "patch.y"]},
    {"location": 2, "format": "R32G32B32A32_SFLOAT", "meaning": ["R0.min.x", "R0.min.y", "R0.max.x", "R0.max.y"]},
    {"location": 3, "format": "R32G32B32A32_SFLOAT", "meaning": ["R1.min.x", "R1.min.y", "R1.max.x", "R1.max.y"]},
    {"location": 4, "format": "R32G32B32A32_UINT", "meaning": ["tint.r IEEE32 bits", "tint.g IEEE32 bits", "tint.b IEEE32 bits", "raw flags uint32"]}]


def require(condition, message):
    if not condition:
        recovery.fail(message)


@dataclass(frozen=True)
class Instruction:
    opcode: int
    operands: tuple

    def words(self):
        return ((len(self.operands) + 1) << 16 | self.opcode, *self.operands)


def parse_module(data):
    """Bound instruction framing; this is not a general SPIR-V validator."""
    require(type(data) is bytes and 20 <= len(data) <= MAX_BYTES and len(data) % 4 == 0,
            "bounded whole-word SPIR-V bytes required")
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    magic, version, _, bound, reserved = words[:5]
    require(magic == 0x07230203 and version >> 16 == 1 and (version >> 8 & 255) <= 6
            and version & 255 == 0 and reserved == 0 and 0 < bound <= 65536,
            "unsupported SPIR-V header")
    instructions, cursor = [], 5
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 65535
        require(count > 0 and count <= len(words) - cursor and opcode in ALLOWED,
                "invalid instruction framing or unsupported profile opcode")
        instructions.append(Instruction(opcode, words[cursor + 1:cursor + count]))
        cursor += count
    return words[:5], instructions


def value_inputs(instruction):
    """Value IDs for the bounded original opcode profile, excluding literals."""
    op, a = instruction.opcode, instruction.operands
    if op == 61: return a[2:3]
    if op == 65: return a[2:]
    if op == 79: return a[2:4]
    if op == 81: return a[2:3]
    if op == 82: return a[2:4]
    if op == 87: return a[2:4]
    if op == 12: return a[4:]
    if op == 245: return a[2::2]
    if op in (43, 44, 46, 54, 59): return a[2:] if op == 44 else ()
    if op in TYPED_RESULT: return a[2:]
    if op == 62: return a[:2]
    if op == 250: return a[:1]
    return ()


def dominators(successors, entry):
    require(entry in successors and all(target in successors for targets in successors.values() for target in targets),
            "unknown control-flow target")
    reachable, todo = set(), [entry]
    while todo:
        block = todo.pop()
        if block not in reachable:
            reachable.add(block); todo.extend(successors[block])
    require(reachable == set(successors), "unreachable or foreign function block")
    result = {block: ({entry} if block == entry else set(reachable)) for block in reachable}
    changed = True
    while changed:
        changed = False
        for block in reachable - {entry}:
            predecessors = [parent for parent, targets in successors.items() if block in targets]
            require(bool(predecessors), "block has no predecessor")
            new = {block} | set.intersection(*(result[parent] for parent in predecessors))
            if result[block] != new: result[block] = new; changed = True
    return result


def profile_structure(header, instructions):
    definitions, positions, block_of, successors, decorations = {}, {}, {}, {}, {}
    function = block = entry = None
    entries, stores, function_count = [], [], 0
    for index, instruction in enumerate(instructions):
        op, a = instruction.opcode, instruction.operands
        result = None
        if op in FIRST_RESULT:
            require(len(a) >= 1, "truncated result declaration"); result = a[0]
        elif op in TYPED_RESULT:
            require(len(a) >= 2, "truncated typed declaration"); result = a[1]
        if result is not None:
            require(0 < result < header[3] and result not in definitions, "duplicate or out-of-bound result ID")
            definitions[result] = instruction; positions[result] = index; block_of[result] = block
        if op == 15: entries.append(instruction)
        if op == 71:
            require(len(a) >= 2, "truncated decoration")
            if a[1] == 30:
                require(len(a) == 3 and a[0] not in decorations, "ambiguous location decoration")
                decorations[a[0]] = a[2]
        if op == 54:
            require(function is None and len(a) == 4, "ambiguous function"); function = a[1]; function_count += 1
        if op == 248:
            require(function == 4 and block is None and len(a) == 1, "invalid block boundary")
            block = a[0]; successors[block] = []; block_of[block] = block
            if entry is None: entry = block
        if result is not None and block is not None: block_of[result] = block
        if op == 62 and a[:1] == (504,):
            require(len(a) == 2 and function == 4 and block is not None, "invalid original output store")
            stores.append((index, block))
        if op in (249, 250, 253):
            require(block is not None and len(a) == {249: 1, 250: 3, 253: 0}[op], "invalid block terminator")
            successors[block] = list(a if op == 249 else a[1:] if op == 250 else ())
            block = None
        if op == 56:
            require(function is not None and block is None and not a, "invalid function end"); function = None
    require(function is None and function_count == 1 and len(entries) == 1 and len(stores) == 1,
            "single complete original entry/output required")
    ep = entries[0].operands
    require(len(ep) >= 4 and ep[:2] == (4, 4), "expected exact fragment entry")
    name_end = next((i for i in range(2, len(ep)) if 0 in struct.pack("<I", ep[i])), None)
    require(name_end is not None, "unterminated entry name")
    name_bytes = struct.pack("<%dI" % (name_end - 1), *ep[2:name_end + 1])
    require(name_bytes.rstrip(b"\0") == b"PS_DISTORT", "wrong native entry name")
    require(decorations.get(494) == 1 and decorations.get(504) == 0
            and 494 in ep[name_end + 1:] and 504 in ep[name_end + 1:], "original fragment interface differs")
    require({ident for ident in decorations if definitions[ident].opcode == 59
             and definitions[ident].operands[2] == 3} == {504}, "occupied or ambiguous output locations")
    dom = dominators(successors, entry)
    store_index, store_block = stores[0]
    used = {value for instruction in instructions for value in value_inputs(instruction)}
    for role, (ident, expected_type) in ANCHORS.items():
        item = definitions.get(ident)
        require(item is not None and item.opcode in TYPED_RESULT and item.operands[0] == expected_type,
                "original SSA type/definition differs: " + role)
        require(ident in used and block_of[ident] in dom[store_block]
                and positions[ident] < store_index, "original consumed SSA does not dominate output: " + role)
        require(all(value in definitions for value in value_inputs(item)), "undefined SSA dependency")
    require(definitions[709].opcode == 124 and definitions[495].opcode == 61
            and definitions[906].opcode == 61 and definitions[883].opcode == 136,
            "original load/bitcast/normalized-coordinate anchors differ")
    require(all(definitions[ident].opcode == 80 for ident in (777, 871))
            and all(definitions[ident].opcode == 79 for ident in (538, 797, 907)), "original vector anchors differ")
    return entries[0], store_index


def transform(original):
    header, instructions = parse_module(original)
    require(len(original) == ORIGINAL_BYTES and probe.digest(original) == ORIGINAL_SHA256,
            "unknown original native shader profile")
    entry, store_index = profile_structure(header, instructions)
    u3, u4, uptr, v1, v2, v3, v4, combined, tint_bits, tint_flags = range(header[3], header[3] + 10)
    make = lambda opcode, *operands: Instruction(opcode, tuple(operands))
    annotations = [make(71, variable, 30, location) for location, variable in enumerate((v1, v2, v3, v4), 1)]
    declarations = [make(23, u3, 6, 3), make(23, u4, 6, 4), make(32, uptr, 3, u4)]
    declarations += [make(59, 503, variable, 3) for variable in (v1, v2, v3)] + [make(59, uptr, v4, 3)]
    exports = [make(79, 9, combined, ANCHORS["u"][0], ANCHORS["patch"][0], 0, 1, 2, 3),
        make(124, u3, tint_bits, ANCHORS["tint"][0]),
        make(80, u4, tint_flags, tint_bits, ANCHORS["flags"][0]),
        make(62, v1, combined), make(62, v2, ANCHORS["R0"][0]), make(62, v3, ANCHORS["R1"][0]), make(62, v4, tint_flags)]
    first_type = next(i for i, item in enumerate(instructions) if item.opcode in TYPE_OPS)
    first_function = next(i for i, item in enumerate(instructions) if item.opcode == 54)
    modified = []
    for index, item in enumerate(instructions):
        if index == first_type: modified.extend(annotations)
        if index == first_function: modified.extend(declarations)
        if index == store_index: modified.extend(exports)
        modified.append(make(15, *item.operands, v1, v2, v3, v4) if item is entry else item)
    words = [*header[:3], header[3] + 10, header[4]]
    words.extend(word for instruction in modified for word in instruction.words())
    patched = struct.pack("<%dI" % len(words), *words)
    require(len(patched) <= MAX_BYTES and all(item.opcode != 61 for item in exports + declarations),
            "invalid instrumentation allocation/load envelope")
    record = {"schema": "native-fragment-ssa-probe-v1", "scope": "INERT_OFFLINE_MRT_INSTRUMENTATION_ONLY",
        "original_bytes": len(original), "original_sha256": ORIGINAL_SHA256,
        "instrumented_bytes": len(patched), "instrumented_sha256": probe.digest(patched), "entry": "PS_DISTORT",
        "outputs": OUTPUTS, "original_ssa_ids": {key: value[0] for key, value in ANCHORS.items()},
        "original_instruction_bodies_preserved": True, "original_output_zero_store_preserved": True,
        "added_ubo_loads": 0, "added_descriptor_bindings": 0, "original_ssa_dominance_checked": True,
        "q_is_original_ssa_but_not_exported": True, "runtime_or_pipeline_activation": False,
        "native_draw_coherence_proven": False, "native_frame_association_proven": False,
        "application_ray_mapping_proven": False, "physical_eye_proven": False,
        "native_sampler_equivalence_proven": False, "alpha_composition_authorized": False,
        "opaque_fallback_required": True, "passthrough_authorized": False}
    return patched, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "output", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        original = recovery.read_owned(args.original, MAX_BYTES)
        patched, record = transform(original)
        with tempfile.TemporaryDirectory(prefix="private-native-ssa-") as temporary:
            candidate = Path(temporary) / "instrumented.spv"; candidate.write_bytes(patched)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(candidate)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=False)
            require(result.returncode == 0, "SPIR-V validation failed; no output published")
        record["spirv_val_passed"] = True
        recovery.write_owned(args.output, patched)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True) + "\n").encode())
        print(json.dumps({"scope": record["scope"], "instrumented_sha256": record["instrumented_sha256"],
                          "spirv_val_passed": True, "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError, struct.error, subprocess.SubprocessError) as error:
        print("native fragment SSA probe: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
