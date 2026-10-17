#!/usr/bin/env python3
"""Offline same-R1 scene-contribution atlas and strict current-image format guard.

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
SPEC = Path(__file__).resolve().parents[1] / "docs/protocols/scene-contribution-atlas-v1.json"
PROFILE_PIN = "2c1ca215715ec7d2d7591eb2a2e8071a15e11fb5f96530927d10ceab7405e30c"
MARKER = "MRMASK/5 current-r1-scene-contribution-atlas-v1-format-only"
EXTRA_OPS = frozenset((83, 88, 167, 186, 188, 190, 330))


def profile_contract(profile):
    try:
        encoded = json.dumps(profile, sort_keys=True, separators=(",", ":"),
                             ensure_ascii=True, allow_nan=False).encode("ascii")
    except (TypeError, ValueError, UnicodeError):
        recovery.fail("exact finite frozen scene atlas profile required")
    require(hashlib.sha256(encoded).hexdigest() == PROFILE_PIN,
            "exact frozen scene atlas profile required; no caller geometry or codes")
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
    """Affine construction template only; coverage is replaced by the tile map."""
    p = profile_contract(profile)
    width, height = p["canvas"]
    def panel(rectangle):
        x, y, w, h = rectangle
        return [w / width, 0., x / width, 0., h / height, y / height]
    x, y = p["coverage"]["data_origins"][0]
    return {"schema": presenter.SCHEMA, "scope": presenter.SCOPE, "chart": presenter.CHART,
            "source_encoding": presenter.ENCODING, "rgb_convention": "straight",
            "color": panel(p["scene"]),
            "coverage": panel([x, y, p["coverage"]["size"][0], p["coverage"]["tile_rows"]])}


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
    if item.opcode in (83, 88, 167, 186, 188, 190):
        return item.operands[1]
    return presenter.atlas.result_id(item)


def pack_module(header, instructions):
    words = [*header, *(word for item in instructions for word in item.words())]
    binary = struct.pack("<%dI" % len(words), *words)
    require(len(binary) <= ssa.MAX_BYTES, "bounded guarded module required")
    return binary


def _scene_candidate(original, profile, enabled, descriptor_count):
    """Reuse pinned native cones; replace only the owned coordinate/composition."""
    candidate, record = presenter.transform(original, panel_mapping(profile), enabled, descriptor_count)
    record.update(source_rgb_convention="scene_contribution", zero_coverage_rgb_is_zero=False,
                  color_policy="native scene RGB reconstruction times original aperture/tint once; never times coverage",
                  output_convention="scene RGB contribution plus independent coverage alpha",
                  coverage_source_contract="current same-eye scene-contribution-atlas-v1 numeric G",
                  source_transfer_scope=profile["source_transfer"],
                  additive_or_refraction_contract="additive contribution retained; refraction not established",
                  geometry_policy="clamp panel global q to scene texel centers; original aperture uses unchanged original q")
    record.pop("coverage",None) # Surrogate affine is not the executed mask map.
    record.pop("mapping",None)
    if not enabled:
        return candidate,record
    header,instructions=parse_module(candidate)
    ids=record["new_ssa_ids"]
    function=next(i for i,x in enumerate(instructions) if x.opcode==54)
    bound=header[3]
    def fresh():
        nonlocal bound
        value=bound;bound+=1;return value
    make=lambda op,*args:ssa.Instruction(op,tuple(args))
    declarations=[]
    constants={x.operands[2]:x.operands[1] for x in instructions[:function]
               if x.opcode==43 and x.operands[0]==8}
    def constant(value):
        bits=struct.unpack("<I",struct.pack("<f",value))[0]
        if bits not in constants:
            constants[bits]=fresh();declarations.append(make(43,8,constants[bits],bits))
        return constants[bits]
    def emit(target,opcode,kind,*values):
        value=fresh();target.append(make(opcode,kind,value,*values));return value
    low,high=constant(.5/2048),constant(1-.5/2048)
    lo2,hi2=fresh(),fresh();declarations += [make(44,39,lo2,low,low),make(44,39,hi2,high,high)]
    clamp=[];global_q=emit(clamp,12,39,1,43,883,lo2,hi2)
    # The two generated atlas q extracts are shared by color and coverage.
    extracts=[i for i,x in enumerate(instructions) if x.opcode==81 and x.operands[0]==8
              and x.operands[2]==883 and x.operands[1]>=1010]
    require(len(extracts)==2 and {instructions[i].operands[3] for i in extracts}=={0,1},
            "exact two inherited global-q extracts required")
    mask=[]
    qx,qy=[emit(mask,81,8,global_q,i) for i in (0,1)]
    px=emit(mask,131,8,emit(mask,133,8,qx,constant(1024)),constant(.5))
    py=emit(mask,131,8,emit(mask,133,8,qy,constant(1024)),constant(.5))
    # Equivalent to floor(max(py,0)/256) clamped to3, without a new integer
    # conversion or dynamic descriptor access. Halos continue the global image.
    above=[emit(mask,190,96,py,constant(v)) for v in (256,512,768)]
    right=emit(mask,169,96,above[1],above[2],above[0])
    origin_x=emit(mask,169,8,right,constant(1072),constant(16))
    origin_y=emit(mask,169,8,above[1],constant(2448),constant(48))
    tile_base=constant(0)
    for cond,value in zip(above,(256,512,768)):
        tile_base=emit(mask,169,8,cond,constant(value),tile_base)
    x=emit(mask,136,8,emit(mask,129,8,emit(mask,129,8,origin_x,px),constant(.5)),constant(2112))
    local_y=emit(mask,131,8,py,tile_base)
    y=emit(mask,136,8,emit(mask,129,8,emit(mask,129,8,origin_y,local_y),constant(.5)),constant(2752))
    dynamic_qa=make(80,39,ids["mapped_qa"],x,y)
    output=[];clamped=False;replaced_mask=0;replaced_rgb=0
    for index,item in enumerate(instructions):
        if index in extracts:
            if not clamped:output.extend(clamp);clamped=True
            item=make(81,item.operands[0],item.operands[1],global_q,item.operands[3])
        if result_id(item)==ids["mapped_qa"]:
            require(item.opcode==80,"inherited mapped mask q must be a vec2 construct")
            output.extend(mask);item=dynamic_qa;replaced_mask+=1
        if result_id(item)==ids["premultiplied_rgb"]:
            require(item==make(142,30,ids["premultiplied_rgb"],ids["combined_color_rgb"],ids["clamped_coverage"]),
                    "exact inherited straight-source RGB multiplication required")
            item=make(83,30,ids["premultiplied_rgb"],ids["combined_color_rgb"]);replaced_rgb+=1
        output.append(item)
    require(clamped and replaced_mask==replaced_rgb==1,"one dynamic mask map and one contribution composition required")
    function=next(i for i,x in enumerate(output) if x.opcode==54);output[function:function]=declarations
    header[3]=bound
    binary=pack_module(header,output)
    record.update(output_bytes=len(binary),output_sha256=hashlib.sha256(binary).hexdigest(),
                  coverage_coordinate_policy="four global-halo tiles selected from clamped current q; no CPU eye table",
                  new_ssa_ids={**ids,"scene_contribution_rgb":ids["premultiplied_rgb"],"scene_global_q":global_q,
                               "mask_origin_x":origin_x,"mask_origin_y":origin_y,"mask_tile_base":tile_base},
                  mask_coordinate_ssa={"qx":qx,"qy":qy,"pixel_x":px,"pixel_y":py,
                                       "above":above,"right":right,"mapped_x":x,"mapped_y":y})
    return binary,record


def transform(original, profile, enabled=True, descriptor_count=1):
    require(type(enabled) is bool, "enabled must be an explicit boolean")
    require(type(descriptor_count) is int and descriptor_count in (1, 3),
            "explicit owned descriptor count1 or3 required")
    p = profile_contract(profile)
    candidate, inherited = _scene_candidate(original, p, enabled, descriptor_count)
    record = {**inherited, "schema": "native-fragment-scene-presenter-v1",
              "scope": "OFFLINE_CURRENT_R1_SCENE_CONTRIBUTION_ATLAS_ONLY", "profile": p,
              "format_guard": "twelve ordered raw-G inclusive code windows; format only",
              "authenticates_body": False, "proves_liveness": False,
              "scene_format_guard_implemented": enabled,
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
