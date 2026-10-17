#!/usr/bin/env python3
"""Inert fragment-only format guard for the frozen synthetic v2 canvas.

The current image must carry all twelve fixed raw-G codes. This is format
recognition, not body authentication, liveness or authorization for native MR.
The existing original expression is retained on invalid geometry/signature.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_r1_presenter as presenter

ssa, recovery, require = presenter.ssa, presenter.recovery, presenter.require
SPEC = Path(__file__).resolve().parents[1] / "docs/protocols/synthetic-presenter-v2.json"
PROFILE_PIN = "4b6f3b7efb1d59e8e950c58b37c9001122a1efa03ea1f77681a48dfdbf104aa3"
MARKER = "MRMASK/3 inert-fragment-synthetic-v2-format-guard-only"
EXTRA_OPS = frozenset((88, 167, 186, 188, 190, 330))


def profile_contract(profile):
    try:
        encoded = json.dumps(profile, sort_keys=True, separators=(",", ":"),
                             ensure_ascii=True, allow_nan=False).encode("ascii")
    except (TypeError, ValueError, UnicodeError):
        recovery.fail("exact finite frozen synthetic v2 profile required")
    require(hashlib.sha256(encoded).hexdigest() == PROFILE_PIN,
            "exact frozen synthetic v2 profile required; no caller geometry or codes")
    # Seal the accepted contract against subsequent caller mutation.
    result = json.loads(encoded)
    guard = result["guard"]
    digest = hashlib.sha256(guard["signature_preimage"].encode("ascii")).digest()
    require(digest.hex() == guard["signature_sha256"] and list(digest[:12]) == guard["codes"],
            "frozen format signature derivation differs")
    return result


def load_profile(path=SPEC):
    return profile_contract(recovery.load_json(Path(path))[0])


def panel_mapping(profile):
    p = profile_contract(profile)
    width, height = p["canvas"]
    def panel(rectangle):
        x, y, w, h = rectangle
        return [w / width, 0., x / width, 0., h / height, y / height]
    return {"schema": presenter.SCHEMA, "scope": presenter.SCOPE, "chart": presenter.CHART,
            "source_encoding": presenter.ENCODING, "rgb_convention": "straight",
            "color": panel(p["color"]), "coverage": panel(p["coverage"])}


def parse_module(binary):
    """Bounded framing for this generated profile, not a general validator."""
    require(type(binary) is bytes and 20 <= len(binary) <= ssa.MAX_BYTES and len(binary) % 4 == 0,
            "bounded generated module bytes required")
    words = struct.unpack("<%dI" % (len(binary) // 4), binary)
    require(words[0] == 0x07230203 and words[1] == 0x00010100 and words[4] == 0
            and 0 < words[3] <= 65536, "generated SPIR-V1.1 header required")
    instructions, at = [], 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        require(count > 0 and count <= len(words) - at and opcode in ssa.ALLOWED | EXTRA_OPS,
                "invalid generated instruction framing")
        instructions.append(ssa.Instruction(opcode, words[at + 1:at + count]))
        at += count
    return list(words[:5]), instructions


def result_id(item):
    if item.opcode in (88, 167, 186, 188, 190):
        return item.operands[1]
    return presenter.atlas.result_id(item)


def pack_module(header, instructions):
    words = [*header, *(word for item in instructions for word in item.words())]
    binary = struct.pack("<%dI" % len(words), *words)
    require(len(binary) <= ssa.MAX_BYTES, "bounded guarded module required")
    return binary


def transform(original, profile, enabled=True, descriptor_count=1):
    require(type(enabled) is bool, "enabled must be an explicit boolean")
    require(type(descriptor_count) is int and descriptor_count in (1, 3),
            "explicit owned descriptor count1 or3 required")
    p = profile_contract(profile)
    candidate, inherited = presenter.transform(original, panel_mapping(p), enabled, descriptor_count)
    record = {**inherited, "schema": "native-fragment-guarded-presenter-v1",
              "scope": "INERT_OFFLINE_FROZEN_SYNTHETIC_V2_FORMAT_ONLY", "profile": p,
              "format_guard": "twelve ordered raw-G inclusive code windows; format only",
              "authenticates_body": False, "proves_liveness": False,
              "synthetic_format_guard_implemented": enabled,
              "live_source_validity_guard_implemented": False,
              "original_fallback_store_value": 994, "guard_branch_before_panels": True,
              "source_mip_contract": "owned single mip; equal min/mag filter; level0 only",
              "guard_samples_explicit_lod0": 12 if enabled else 0,
              "first_cell_mismatch_guard_samples": 1 if enabled else 0,
              "first_cell_mismatch_skips_remaining_samples": True,
              "additional_panel_samples_explicit_lod0": 4 if enabled else 0,
              "original_samples_unchanged": True, "guard_ssa": {}}
    if not enabled:
        return candidate, record
    header, instructions = parse_module(candidate)
    original_header, native = ssa.parse_module(original)
    _, original_store = ssa.profile_structure(original_header, native)
    original_function = next(i for i, item in enumerate(native) if item.opcode == 54)
    function = next(i for i, item in enumerate(instructions) if item.opcode == 54)
    prefix = native[original_function:original_store]
    splice = function + len(prefix)
    require(instructions[function:splice] == prefix,
            "entire original function before SSA994 output must remain unchanged")
    panel = instructions[splice:]
    require(panel[-3:] == [ssa.Instruction(62, (504, inherited["new_ssa_ids"]["output"])),
                           ssa.Instruction(253, ()), ssa.Instruction(56, ())],
            "exact inherited candidate output tail required")
    panel = panel[:-2]
    bound = header[3]
    def fresh():
        nonlocal bound
        value = bound
        bound += 1
        return value
    make = lambda opcode, *args: ssa.Instruction(opcode, tuple(args))
    declarations, constants = [], {}
    def constant(value):
        bits = struct.unpack("<I", struct.pack("<f", value))[0]
        if bits not in constants:
            constants[bits] = fresh()
            declarations.append(make(43, 8, constants[bits], bits))
        return constants[bits]
    def emit(target, opcode, kind, *args):
        value = fresh()
        target.append(make(opcode, kind, value, *args))
        return value
    zero, one = constant(0.), constant(1.)
    # Branches can vary across a quad. New samples therefore use explicit LOD0;
    # the two original samples and all original SSA994 computation stay intact.
    changed = 0
    for index, item in enumerate(panel):
        if item.opcode == 87:
            require(len(item.operands) == 4 and item.operands[1] >= original_header[3],
                    "only four new panel samples may change LOD policy")
            panel[index] = make(88, *item.operands, 2, zero)  # ImageOperands Lod.
            changed += 1
    require(changed == 4, "exactly four additional panel samples required")
    geometry, checks = [], []
    minimum = [emit(geometry, 81, 8, 871, i) for i in (0, 1)]
    maximum = [emit(geometry, 81, 8, 871, i) for i in (2, 3)]
    span = [emit(geometry, 131, 8, hi, lo) for lo, hi in zip(minimum, maximum)]
    u = [emit(geometry, 81, 8, 538, i) for i in (0, 1)]
    for lo, hi, size, coordinate in zip(minimum, maximum, span, u):
        checks.extend((emit(geometry, 190, 96, lo, zero), emit(geometry, 188, 96, hi, one),
                       emit(geometry, 186, 96, size, zero), emit(geometry, 190, 96, coordinate, lo),
                       emit(geometry, 188, 96, coordinate, hi)))
    def conjunction(target, values):
        result = values[0]
        for value in values[1:]:
            result = emit(target, 167, 96, result, value)
        return result
    geometry_valid = conjunction(geometry, checks)
    (signature_header, remaining_header, panel_header, invalid_geometry, invalid_first_signature,
     invalid_signature, signature_merge, first_signature_merge, final_merge) = [fresh() for _ in range(9)]
    geometry += [make(247, final_merge, 0), make(250, geometry_valid, signature_header, invalid_geometry)]
    first_signature, signature, windows, cells = [], [], [], []
    # Both operands are values from the original first B9/B25 descriptor loads.
    # They dominate this splice; no descriptor load/access chain is added.
    sampled_image = emit(first_signature, 86, 297, 599, 600)
    width, height = p["canvas"]
    guard = p["guard"]
    remaining_sampled_image = None
    for index, code in enumerate(guard["codes"]):
        target = first_signature if index == 0 else signature
        if index == 1:
            # OpSampledImage results may only be consumed in their own block.
            # Recombine the same original image/sampler values, never reload.
            remaining_sampled_image = emit(signature, 86, 297, 599, 600)
        center = [guard["origin"][axis] + guard["stride"][axis] * index + guard["size"][axis] / 2
                  for axis in (0, 1)]
        normalized = [center[0] / width, center[1] / height]
        xy = [emit(target, 129, 8, lo, emit(target, 133, 8, size, constant(position)))
              for lo, size, position in zip(minimum, span, normalized)]
        coordinate = emit(target, 80, 39, *xy)
        sample = emit(target, 88, 9, sampled_image if index == 0 else remaining_sampled_image, coordinate, 2, zero)
        g = emit(target, 81, 8, sample, 1)
        low = constant((code - guard["code_tolerance"]) / 255.)
        high = constant((code + guard["code_tolerance"]) / 255.)
        lower, upper = emit(target, 190, 96, g, low), emit(target, 188, 96, g, high)
        windows.extend((lower, upper))
        cells.append({"index": index, "code": code, "center_canvas": center,
                      "center_normalized": normalized, "coordinate": coordinate,
                      "sample": sample, "g": g, "low": low, "high": high,
                      "lower": lower, "upper": upper})
    first_valid = conjunction(first_signature, windows[:2])
    first_signature += [make(247, first_signature_merge, 0),
                        make(250, first_valid, remaining_header, invalid_first_signature)]
    signature_valid = emit(signature, 167, 96, first_valid, conjunction(signature, windows[2:]))
    signature += [make(247, signature_merge, 0), make(250, signature_valid, panel_header, invalid_signature)]
    # Exact original SSA994 on both invalid routes, not a recomputed decoder or
    # a select that evaluates retargeted reads before deciding which to keep.
    guarded = geometry + [make(248, signature_header)] + first_signature
    guarded += [make(248, remaining_header)] + signature + [make(248, panel_header)]
    guarded += panel + [make(249, signature_merge), make(248, invalid_signature), make(62, 504, 994),
                        make(249, signature_merge), make(248, signature_merge), make(249, first_signature_merge),
                        make(248, invalid_first_signature), make(62, 504, 994), make(249, first_signature_merge),
                        make(248, first_signature_merge), make(249, final_merge),
                        make(248, invalid_geometry), make(62, 504, 994), make(249, final_merge),
                        make(248, final_merge), make(253), make(56)]
    result = instructions[:splice] + guarded
    result[function:function] = declarations
    markers = [i for i, item in enumerate(result) if item.opcode == 330]
    require(len(markers) == 1, "one inherited guard marker required")
    encoded = MARKER.encode("ascii") + b"\0"
    encoded += b"\0" * (-len(encoded) % 4)
    result[markers[0]] = make(330, *struct.unpack("<%dI" % (len(encoded) // 4), encoded))
    require(bound <= 65536, "bounded guarded result IDs required")
    header[3] = bound
    binary = pack_module(header, result)
    record.update(output_sha256=presenter.atlas.probe.digest(binary), output_bytes=len(binary), candidate_marker=MARKER,
                  guard_ssa={"geometry_valid": geometry_valid, "signature_valid": signature_valid,
                             "first_signature_valid": first_valid,
                             "signature_header": signature_header, "remaining_header": remaining_header,
                             "panel_header": panel_header, "invalid_first_signature": invalid_first_signature,
                             "invalid_geometry": invalid_geometry, "invalid_signature": invalid_signature,
                             "signature_merge": signature_merge, "first_signature_merge": first_signature_merge,
                             "final_merge": final_merge,
                             "sampled_image": sampled_image, "remaining_sampled_image": remaining_sampled_image,
                             "zero": zero, "cells": cells},
                  guard_original_image_value=599, guard_original_sampler_value=600,
                  original_function_prefix_unchanged=True)
    return binary, record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "output", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--profile", type=Path, default=SPEC)
    parser.add_argument("--disabled", action="store_true")
    parser.add_argument("--owned-descriptor-count", type=int, choices=(1, 3), default=1)
    args = parser.parse_args(argv)
    try:
        require(len({p.absolute() for p in (args.original, args.profile, args.output, args.record)}) == 4,
                "distinct original/profile/output/record paths required")
        require(not any(p.exists() or p.is_symlink() for p in (args.output, args.record)), "exclusive outputs required")
        profile, profile_sha = recovery.load_json(args.profile)
        binary, record = transform(recovery.read_owned(args.original, ssa.MAX_BYTES), profile,
                                   not args.disabled, args.owned_descriptor_count)
        with tempfile.TemporaryDirectory(prefix="private-fragment-guard-") as directory:
            path = Path(directory) / "guarded.spv"
            path.write_bytes(binary)
            result = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(path)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15, check=False)
            require(result.returncode == 0, "SPIR-V validation failed; no output published")
        record.update(spirv_val_passed=True, profile_file_sha256=profile_sha)
        recovery.write_owned(args.output, binary)
        recovery.write_owned(args.record, (json.dumps(record, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"scope": record["scope"], "output_sha256": record["output_sha256"],
                          "passthrough_authorized": False}))
        return 0
    except (presenter.atlas.probe.ProbeError, OSError, ValueError, TypeError, KeyError, IndexError,
            struct.error, subprocess.SubprocessError) as error:
        print("native guarded presenter: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
