#!/usr/bin/env python3
"""Bounded offline axis-aligned canvas hypotheses from current sampled-G pixels.

Header geometry locates candidates; disjoint canonical markers validate them.
No historical map is accepted. Pixel-grid fits never authorize native mapping,
an outer-band identity, a future decoded image, physical eyes or passthrough.
"""
import argparse
from itertools import combinations, product
import json
import math
from pathlib import Path
import struct
import sys
import zlib

import registration_header_discovery as discovery
import native_fragment_atlas_probe as atlas
import pixel_sampler_recovery as paired

registration, probe, recovery = discovery.registration, discovery.probe, discovery.recovery
require = registration.require
MAX_HITS, MAX_BANDS, MAX_TRIPLES = 256, 32, 32
MIN_SCALE, MAX_SCALE, EDGE_ERROR = .75, 4., 1.3
MAGIC_LEVELS = tuple(side for byte in probe.MAGIC for bit in range(7, -1, -1)
                     for side in ((byte >> bit) & 1, 1 - ((byte >> bit) & 1)))
MAGIC_EDGES = tuple(i * 4 for i in range(1, len(MAGIC_LEVELS)) if MAGIC_LEVELS[i] != MAGIC_LEVELS[i-1])
SPAN = MAGIC_EDGES[-1] - MAGIC_EDGES[0]
CHECKS = tuple(sorted({round(i * (len(MAGIC_EDGES)-1) / 11) for i in range(12)}))
SOURCE_FILES = ("tests/steamlink-registration-source/Program.cs", "src/GloomhavenVR/Core/SteamLink/ProbePolicy.cs",
    "src/GloomhavenVR/Core/SteamLink/ProbeManifest.cs", "src/GloomhavenVR/Core/SteamLink/RegistrationPairPolicy.cs",
    "src/GloomhavenVR/Core/SteamLink/RegistrationSource.cs")
LOCATED_FIELDS = frozenset(("parameters", "candidate_tuple", "header_sha256", "accepted_grid_points",
    "tested_grid_points", "parameter_grid_step", "header_only_edge_fit", "accepted_parameter_ranges", "uncertainty_scope",
    "schema", "scope", "receiver_extent", "mapped_input_rgb_sha256", "magic_row_hits", "magic_bands", "attempts",
    "supported_profile", "outer_band_proven", "decoded_content_epoch_proven", *registration.authorities()))


def supported_profile():
    return {"positive_scale_range": [MIN_SCALE, MAX_SCALE], "row_edge_error_limit": EDGE_ERROR,
        "maximum_row_hits": MAX_HITS, "maximum_bands": MAX_BANDS, "maximum_triples": MAX_TRIPLES,
        "maximum_grid_evaluations": MAX_TRIPLES*1250, "maximum_image_pixels":probe.MAX_PIXELS,
        "uniqueness_scope": "only tested bounded positive axis-aligned hypotheses"}


def magic_bands(green, width, height):
    """Current threshold edges fit the fixed magic row; no payload/ref is used."""
    count, average = len(MAGIC_EDGES), sum(MAGIC_EDGES) / len(MAGIC_EDGES)
    variance = sum((value-average)**2 for value in MAGIC_EDGES)
    hits = []
    for y in range(height):
        row = green[y*width:(y+1)*width]; edges = []; last = row[0] > 127
        for x, value in enumerate(row[1:], 1):
            on = value > 127
            if on != last: edges.append(x); last = on
        for start in range(len(edges)-count+1):
            if (row[edges[start]] > 127) != bool(MAGIC_LEVELS[MAGIC_EDGES[0]//4]): continue
            scale = (edges[start+count-1]-edges[start]) / SPAN
            if not MIN_SCALE <= scale <= min(MAX_SCALE, (width+2)/688): continue
            if any(abs(edges[start+i]-edges[start]-scale*(MAGIC_EDGES[i]-MAGIC_EDGES[0])) > EDGE_ERROR for i in CHECKS): continue
            selected = edges[start:start+count]; mean = sum(selected)/count
            scale = sum((a-average)*(b-mean) for a, b in zip(MAGIC_EDGES, selected))/variance
            offset = mean-scale*average
            error = max(abs(b-(scale*a+offset)) for a, b in zip(MAGIC_EDGES, selected))
            if error > EDGE_ERROR: continue
            hits.append((y, scale, offset-16*scale, error))
            require(len(hits) <= MAX_HITS, "magic-row work budget exhausted")
    bands = []
    for item in hits:
        if bands and item[0] <= bands[-1][-1][0]+2 and abs(item[2]-bands[-1][-1][2]) < 2:
            bands[-1].append(item)
        else: bands.append([item])
    bands = [band for band in bands if len(band) >= 2]
    require(len(bands) <= MAX_BANDS, "magic-band work budget exhausted")
    return bands, len(hits)


def mapped_contract():
    return {"schema": registration.MAP_SCHEMA, "kind": "caller_supplied_already_mapped_image",
        "source_canvas": [688, 548], "pixel_encoding": "sampled_g_gray8_codes",
        "recorded_provenance": "Current-pixel magic-edge fit and fixed quantized refinement; no historical map input.",
        "scope": "FITTED_DIAGNOSTIC_ONLY", "points": []}


def coordinates(parameters, width, height):
    sx, sy, tx, ty = parameters
    if not MIN_SCALE <= sx <= MAX_SCALE or not MIN_SCALE <= sy <= MAX_SCALE: return None
    xs = [math.floor(sx*(x+.5)+tx) for x in range(688)]
    ys = [math.floor(sy*(y+.5)+ty) for y in range(548)]
    if min(xs) < 0 or max(xs) >= width or min(ys) < 0 or max(ys) >= height: return None
    return xs, ys


def remap(green, width, height, parameters, headers_only=False):
    axes = coordinates(parameters, width, height)
    require(axes is not None, "whole source-canvas centres must fit current image")
    xs, ys = axes; result = bytearray(688*548*3)
    for y in range(548):
        if headers_only and not any(16+copy*76 <= y < 84+copy*76 for copy in range(3)): continue
        left, right = (16, 528) if headers_only else (0, 688)
        row = bytes(green[ys[y]*width+xs[x]] for x in range(left, right)); at = (y*688+left)*3
        for channel in range(3): result[at+channel:at+len(row)*3:3] = row
    return probe.Image(688, 548, bytes(result))


def center_packet(green, width, parameters):
    """Cheap bootstrap only; final acceptance uses unchanged full4x4 reader."""
    sx, sy, tx, ty = parameters; packet = bytearray(probe.HEADER_BYTES)
    for bit in range(probe.HEADER_BYTES*8):
        x, y = 16+bit%64*8, 16+bit//64*4
        at = math.floor(sy*(y+2)+ty)*width
        a, b = (green[at+math.floor(sx*(x+side*4+2)+tx)] for side in (0, 1))
        if a <= 64 and b >= 191: value = 0
        elif a >= 191 and b <= 64: value = 1
        else: return None
        packet[bit//8] |= value << (7-bit%8)
    if zlib.crc32(packet[:-4]) != struct.unpack(">I", packet[-4:])[0]: return None
    return bytes(packet)


def header_edge_fit(green, width, height, seed, packet):
    """Fit current threshold crossings from CRC-recovered header cells only."""
    sx,sy,tx,ty=seed; samples=[[],[]]
    def crossing(axis, source, perpendicular, upward):
        expected=(sx*source+tx) if axis==0 else (sy*source+ty)
        fixed=math.floor(sy*perpendicular+ty) if axis==0 else math.floor(sx*perpendicular+tx)
        extent=width if axis==0 else height; choices=[]
        for edge in range(max(1,math.floor(expected)-3),min(extent,math.floor(expected)+4)):
            a,b=(green[fixed*width+edge-1],green[fixed*width+edge]) if axis==0 else (green[(edge-1)*width+fixed],green[edge*width+fixed])
            if abs(b-a)<32 or not ((a<127.5<=b) if upward else (b<127.5<=a)): continue
            value=edge-.5+(127.5-a)/(b-a); choices.append(value)
        if choices: samples[axis].append((source,min(choices,key=lambda value:abs(value-expected))))
    def level(row,column):
        bit=row*64+column//2
        if bit>=probe.HEADER_BYTES*8:return None
        value=(packet[bit//8]>>(7-bit%8))&1
        return value if column%2==0 else 1-value
    for copy in range(3):
        for bit in range(probe.HEADER_BYTES*8):
            crossing(0,16+(bit%64)*8+4,16+copy*76+(bit//64)*4+2,not bool((packet[bit//8]>>(7-bit%8))&1))
        for row in range(probe.HEADER_ROWS-1):
            for column in range(128):
                a,b=level(row,column),level(row+1,column)
                if a is not None and b is not None and a!=b:crossing(1,16+copy*76+(row+1)*4,16+column*4+2,a==0)
    result=[];statistics=[]
    for axis,points in enumerate(samples):
        require(len(points)>=128,"insufficient header-only threshold edges")
        mean_x=sum(a for a,b in points)/len(points);mean_y=sum(b for a,b in points)/len(points)
        variance=sum((a-mean_x)**2 for a,b in points)
        require(variance>0,"degenerate header edge distribution")
        scale=sum((a-mean_x)*(b-mean_y) for a,b in points)/variance;offset=mean_y-scale*mean_x
        require(max(abs(scale*a+offset-((sx*a+tx) if axis==0 else (sy*a+ty))) for a,b in points)<=2,
                "header-only refinement left bounded coarse neighborhood")
        result.append((scale,offset));statistics.append({"axis":axis,"edges":len(points),"maximum_edge_residual":max(abs(b-(scale*a+offset)) for a,b in points)})
    return [result[0][0],result[1][0],result[0][1],result[1][1]],statistics


def refine(green, width, height, base, session, eye):
    accepted, first_failure = [], None
    def scan(origin,units,pivot):
        nonlocal first_failure
        for ix,iy,dx,dy in product(range(-2,3),repeat=4):
            dsx,dsy=ix*units[0],iy*units[1]
            params=[origin[0]+dsx,origin[1]+dsy,origin[2]+dx*units[2]-dsx*pivot[0],origin[3]+dy*units[3]-dsy*pivot[1]]
            if coordinates(params,width,height) is None:continue
            packet=center_packet(green,width,params)
            if packet is None:continue
            fields=probe.HEADER_STRUCT.unpack(packet[:-4])
            if fields[3].hex()!=session or fields[5]!=(0 if eye=="left" else 1):continue
            try:
                recovered=discovery.discover_mapped(remap(green,width,height,params,True),mapped_contract(),session,eye)
                accepted.append((params,recovered))
            except probe.ProbeError as error:
                if first_failure is None:first_failure=str(error)
    units=[.25/SPAN,.5/152,.25,.25];scan(base,units,[272,94])
    if not accepted: return None, first_failure
    coarse=accepted[:];seed=coarse[len(coarse)//2][0]
    packet=probe.sample_header(remap(green,width,height,seed,True),0)
    edge_base,edge_statistics=header_edge_fit(green,width,height,seed,packet)
    accepted=[];units=[.125/SPAN,.125/220,.125,.125];scan(edge_base,units,[272,126])
    require(bool(accepted),"header-only fine grid has no strict whole-cell candidate")
    tuples = {json.dumps(item[1]["candidate_tuple"], sort_keys=True) for item in accepted}
    require(len(tuples) == 1, "ambiguous header tuple within refinement family")
    average = [sum(item[0][i] for item in accepted)/len(accepted) for i in range(4)]
    selected = min(accepted, key=lambda item: sum(((item[0][i]-average[i])/units[i])**2 for i in range(4)))
    return {"parameters": selected[0], "candidate_tuple": selected[1]["candidate_tuple"],
        "header_sha256": selected[1]["header_sha256"], "accepted_grid_points": len(accepted),
        "tested_grid_points": 1250, "parameter_grid_step": units,"header_only_edge_fit":edge_statistics,
        "accepted_parameter_ranges": [[min(item[0][i] for item in accepted), max(item[0][i] for item in accepted)] for i in range(4)],
        "uncertainty_scope": "sampled feasible grid range, not confidence bounds or unique continuous/native B"}, first_failure


def locate(image, session, eye):
    registration.identity(session, 1, eye); probe.check_image(image)
    require(type(image.rgb) is bytes and image.rgb[::3] == image.rgb[1::3] == image.rgb[2::3], "exact sampled-G grayscale required")
    require(image.width >= 516 and image.height >= 411, "image too small for supported complete canvas profile")
    green = image.rgb[1::3]; bands, hit_count = magic_bands(green, image.width, image.height)
    candidates, attempts = [], []
    for group in combinations(bands, 3):
        centres = [(band[0][0]+band[-1][0]+1)/2 for band in group]
        sy = (centres[2]-centres[0])/152
        if not MIN_SCALE <= sy <= MAX_SCALE or abs(centres[1]-(centres[0]+centres[2])/2) > 1: continue
        rows = [item for band in group for item in band]
        if max(item[1] for item in rows)-min(item[1] for item in rows) > .01: continue
        if max(item[2] for item in rows)-min(item[2] for item in rows) > 2: continue
        base = [sum(item[1] for item in rows)/len(rows), sy, sum(item[2] for item in rows)/len(rows),
                sum(y-(18+i*76)*sy for i, y in enumerate(centres))/3]
        require(len(attempts) < MAX_TRIPLES, "header-triple work budget exhausted")
        candidate, failure = refine(green, image.width, image.height, base, session, eye)
        attempts.append({"coarse_parameters": base, "first_full_cell_failure": failure, "accepted": candidate is not None})
        if candidate: candidates.append(candidate)
    require(len(candidates) == 1, "no unique complete source canvas in bounded profile: %d candidates" % len(candidates))
    result = candidates[0]
    result.update(schema="registration-canvas-locator-v1", scope="UNTRUSTED_BOUNDED_AXIS_ALIGNED_CANDIDATE",
        receiver_extent=[image.width, image.height], mapped_input_rgb_sha256=probe.digest(image.rgb),
        magic_row_hits=hit_count, magic_bands=len(bands), attempts=attempts,
        supported_profile=supported_profile(),
        outer_band_proven=False, decoded_content_epoch_proven=False, **registration.authorities())
    return result


def held_out_markers(canonical, actual):
    results = []
    for name, x0, y0, width, height, cell, bits in (("background",0,0,688,548,24,10), ("color",16,244,320,192,16,8)):
        count = incorrect = ambiguous = 0; minimum = 255.
        for y in range(y0, y0+height, cell):
            for x in range(x0, x0+width, cell):
                if x+bits*2 > x0+width or y+2 > y0+height: continue
                expected = [canonical[((y+dy)*688+x+dx)*3:((y+dy)*688+x+dx)*3+3] for dy in range(2) for dx in range(bits*2)]
                if any(len(set(pixel)) != 1 or pixel[0] not in (40,216) for pixel in expected): continue
                if expected[:bits*2] != expected[bits*2:]: continue
                if any(expected[i][0]+expected[i+1][0] != 256 for i in range(0,bits*2,2)): continue
                count += 1
                for bit in range(bits):
                    values = [sum(actual[((y+dy)*688+x+bit*2+side)*3+1] for dy in range(2))/2 for side in (0,1)]
                    contrast = values[0]-values[1]; minimum = min(minimum, abs(contrast))
                    incorrect += (contrast > 0) != (expected[bit*2][0] > expected[bit*2+1][0])
                    ambiguous += abs(contrast) < 32
        require(count >= 64 and incorrect == 0 and ambiguous == 0, "held-out marker validation refused: " + name)
        results.append({"region": name, "cells": count, "incorrect_bits": incorrect, "ambiguous_bits": ambiguous, "minimum_contrast": minimum})
    return results


def verify(located, image, source_record, color, coverage, canvas):
    require(type(located) is dict and set(located)==LOCATED_FIELDS and located.get("schema")=="registration-canvas-locator-v1"
            and located.get("scope")=="UNTRUSTED_BOUNDED_AXIS_ALIGNED_CANDIDATE", "unsupported located record schema/scope")
    require(located["supported_profile"]==supported_profile(),"unsupported located search profile")
    for key,value in {**registration.authorities(),"outer_band_proven":False,"decoded_content_epoch_proven":False}.items():
        require(key in located and located[key] is value,"located record must not assert "+key)
    require(located.get("receiver_extent") == [image.width,image.height]
            and located.get("mapped_input_rgb_sha256") == probe.digest(image.rgb), "located candidate belongs to different image bytes/extent")
    identity = located["candidate_tuple"]
    source = registration.validate_source(source_record, color, coverage, canvas,
        identity["session"], identity["pair_id"], identity["eye"])
    actual = remap(image.rgb[1::3], image.width, image.height, located["parameters"])
    current = discovery.discover_mapped(actual, mapped_contract(), identity["session"], identity["eye"])
    require(current["candidate_tuple"] == identity and current["header_sha256"] == located["header_sha256"], "stale located source header/tuple")
    require(current["header_sha256"] == source["header_sha256"]
            and current["header_declared_color_sha256"] == source["source_color_sha256"]
            and current["header_declared_coverage_sha256"] == source["source_coverage_sha256"], "observed header differs from canonical source planes")
    markers = held_out_markers(canvas, actual.rgb)
    levels = {value: {"count":0,"maximum_code_error":0} for value in registration.LEVELS}
    for y in range(3,189):
        for x in range(3,317):
            wanted = coverage[y*320+x]
            if any(coverage[(y+dy)*320+x-3:(y+dy)*320+x+4] != bytes((wanted,))*7 for dy in range(-3,4)): continue
            item=levels[wanted];item["count"]+=1
            item["maximum_code_error"] = max(item["maximum_code_error"],abs(actual.rgb[((y+244)*688+x+352)*3+1]-wanted))
    require(all(item["count"] >= 16 and item["maximum_code_error"] <= 2 for item in levels.values()), "homogeneous coverage interior validation refused")
    sx, sy, tx, ty = located["parameters"]
    b = [sx*688/image.width, 0., tx/image.width, 0., sy*548/image.height, ty/image.height]
    panels = {"color": [320/688,0.,16/688,0.,192/548,244/548], "coverage": [320/688,0.,352/688,0.,192/548,244/548]}
    pc, pa = atlas.mapping_contract({"schema": atlas.MAPPING_SCHEMA, "scope": atlas.MAPPING_SCOPE, "W": b, **panels})
    return {**located, "canonical_source_validation": source, "held_out_markers": markers,
        "B_normalized_texel_centres": b, "Pc_packed": pc, "Pa_packed": pa,
        "coordinates": "unquantized affine B hypothesis; actual pixel index is floor(scale*(i+.5)+offset), not an exact receiver-centre correspondence; Pc/Pa act on ORIGINAL packed u, not q",
        "coverage_interiors": {"source_uniform_window":7,"maximum_allowed_code_error":2,"required_samples_per_level":16,"levels":levels,"scope":"homogeneous source-selected interiors only; not edge quality, transfer equivalence or linear game alpha"},
        "mapped_candidate_sha256": probe.digest(actual.rgb), "source_reference_valid": True,
        "mapping_estimated_or_authenticated": False, "source_pair_id_is_native_video_frame": False,
        "generator_authenticity_proven": False, "decoded_source_integrity_validated": False,
        "outer_band_proven":False,"decoded_content_epoch_proven":False,**registration.authorities()}


def provenance_check(receipt, source_root, source_sha, files, identity):
    require(receipt.get("schema") == "changing-source-emitter-provenance-confirmation-v1"
            and receipt.get("source_sha256_matches_successful_compile_checkpoint") is True
            and receipt.get("native_frame_association_proven") is False and receipt.get("passthrough_authorized") is False,
            "explicit original C# emitter provenance receipt required")
    hashes = receipt.get("source_sha256")
    require(type(hashes) is dict and set(hashes) == set(SOURCE_FILES), "exact emitter source pins required")
    for name in SOURCE_FILES:
        require(probe.digest(recovery.read_owned(source_root/name, 256*1024)) == recovery.sha(hashes[name], name), "emitter source pin differs: " + name)
    matches = []
    for invocation in receipt.get("actual_invocations", []):
        argv = invocation.get("command", [])
        if len(argv) != 10 or argv[2] != "--emit" or argv[4:] != ["--session", identity["session"], "--pair", str(identity["pair_id"]), "--eye", identity["eye"]]: continue
        require(invocation.get("exit_code") == 0, "original emitter invocation failed")
        dll = Path(argv[1]); require(dll.is_absolute() and dll.name == "RegistrationSourceControls.dll", "pinned emitter DLL required")
        require(probe.digest(recovery.read_owned(dll, 8*1024*1024)) == recovery.sha(receipt.get("fixture_dll_sha256"), "emitter DLL"), "emitter DLL pin differs")
        recorded = invocation.get("files", {})
        for name, content in {**files, "registration.json": source_sha}.items():
            item = recorded.get(name, {})
            digest = content if name == "registration.json" else probe.digest(content)
            require(item.get("sha256") == digest and item.get("matches_original_canonical_artifact") is True, "emitter artifact pin differs: " + name)
        matches.append(invocation)
    require(len(matches) == 1, "unique matching original emitter receipt required")
    return {"scope": "source/DLL/artifact pins and explicit successful invocation receipt; not cryptographic generator authentication", "checked_source_files": len(SOURCE_FILES), "emitter_dll_sha256": receipt["fixture_dll_sha256"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("snapshot", "sampler", "metadata", "source-record", "color", "coverage", "canvas", "source-provenance", "source-root", "record"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--expected-session", required=True); parser.add_argument("--expected-eye", choices=("left","right"), required=True)
    args = parser.parse_args(argv)
    try:
        registration.identity(args.expected_session, 1, args.expected_eye)
        metadata, metadata_sha = recovery.load_json(args.metadata)
        raw = recovery.read_owned(args.snapshot, recovery.MAX_RAW_BYTES); sampled = recovery.read_owned(args.sampler, recovery.MAX_RAW_BYTES)
        _, image, pair = paired.inspect_pair(raw, sampled, metadata)
        located = locate(image, args.expected_session, args.expected_eye)
        source, source_sha = recovery.load_json(args.source_record)
        color, coverage, canvas = (recovery.read_owned(path, size) for path, size in ((args.color,184320),(args.coverage,61440),(args.canvas,1131072)))
        report = verify(located, image, source, color, coverage, canvas)
        receipt, receipt_sha = recovery.load_json(args.source_provenance)
        header = probe.header_for(320,192,color,coverage,args.expected_session,located["candidate_tuple"]["pair_id"],args.expected_eye,"straight")
        report["emitter_provenance"] = provenance_check(receipt,args.source_root,source_sha,
            {"color.rgb8":color,"coverage.r8":coverage,"canvas.rgb8":canvas,"header.bin":header},located["candidate_tuple"])
        report.update(source_record_sha256=source_sha, emitter_receipt_sha256=receipt_sha,
            capture_provenance={"capture_id":pair["generation"]["capture_id"],"metadata_sha256":metadata_sha,"snapshot_sha256":pair["snapshot_sha256"],"sampler_sha256":pair["sampler_sha256"],
                "generation":pair["generation"],"command_generation":pair["command_generation"],"view_generation":pair["view_generation"]})
        recovery.write_owned(args.record,(json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+"\n").encode())
        print(json.dumps({"candidate_tuple":located["candidate_tuple"],"scope":report["scope"],"passthrough_authorized":False})); return 0
    except (probe.ProbeError,OSError,ValueError,TypeError,KeyError,IndexError,AttributeError,struct.error,OverflowError) as error:
        print("registration canvas locator: "+str(error),file=sys.stderr); return 2


if __name__ == "__main__":
    raise SystemExit(main())
