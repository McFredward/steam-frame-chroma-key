#!/usr/bin/env python3
"""Bounded OFFLINE registration in one observed native invocation's R1 chart.

This separate fixed 2x2 cell-interior reader is never a fallback from the strict
4x4 API. It does not search/fit B, infer physical eyes, or authorize a presenter.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import zlib

import native_fragment_ssbo_probe as ssbo
import registration_canvas_locator as locator

paired, registration, discovery = locator.paired, locator.registration, locator.discovery
probe, recovery, require = locator.probe, locator.recovery, locator.require
SCHEMA = "native-r1-registration-candidate-v1"
SCOPE = "OFFLINE_NATIVE_R1_CELL_INTERIORS_ONLY"
FOOTPRINT = "fixed_source_cell_central_2x2_v1"


def _metadata_digest(metadata):
    try:
        return probe.digest(json.dumps(metadata, sort_keys=True, separators=(",", ":"), allow_nan=False).encode())
    except (TypeError, ValueError, OverflowError):
        require(False, "metadata must have bounded JSON values")


def _invocation(metadata, record):
    require(type(metadata) is dict and type(record) is bytes and len(record) == ssbo.RECORD_BYTES,
            "exact original-invocation 128-byte record required")
    control, sampler, acquisition, generation = (metadata.get(key) for key in
        ("native_ssbo_control", "sampler_control", "acquisition", "generation"))
    require(all(type(item) is dict for item in (control, sampler, acquisition, generation)),
            "complete invocation/sampler/acquisition/generation metadata required")
    require(control.get("available") is True and control.get("reason") == "none"
            and type(control.get("transaction_reason")) is int and control["transaction_reason"] == 0
            and control.get("file") == "ssbo-original-invocation.bin" and type(control.get("bytes")) is int
            and control["bytes"] == ssbo.RECORD_BYTES and control.get("sha256") == probe.digest(record)
            and control.get("original_color_expression_preserved") is True,
            "successful exact original-invocation receipt and digest required")
    for key in ("native_frame_association_proven", "physical_eye_proven", "passthrough_authorized", "native_visual_result_verified"):
        require(control.get(key) is False, "invocation cannot assert " + key)
    for key in ("command_generation", "pool_generation", "execution", "paired_execution", "source_executions"):
        require(recovery.uint(control.get(key), key) > 0, "positive exact invocation generation/execution required")
    require(control["execution"] == control["paired_execution"] == control["source_executions"],
            "record and sampler must describe the same final accepted execution")
    command = recovery.uint(sampler.get("command_generation"), "sampler command")
    require(command == control["command_generation"] == recovery.uint(acquisition.get("command_generation"), "acquisition command"),
            "original record and image command generations differ")
    cohort = control.get("source_cohort")
    require(type(cohort) is dict, "exact source cohort required")
    for key in ("device", "image", "view", "memory", "image_generation", "view_generation", "memory_generation", "layout"):
        require(recovery.uint(cohort.get(key), key) > 0, "positive source-cohort identity required")
    require(cohort["layout"] == 5, "source cohort requires the captured SHADER_READ_ONLY_OPTIMAL layout")
    width, height, _ = paired.paired_extent(metadata)
    require(type(cohort.get("extent")) is list and len(cohort["extent"]) == 2
            and all(type(x) is int for x in cohort["extent"]) and cohort["extent"] == [width, height]
            and cohort["image_generation"] == recovery.uint(generation.get("image_generation"), "raw image generation")
            == recovery.uint(sampler.get("image_generation"), "sampler image generation")
            and cohort["view_generation"] == recovery.uint(acquisition.get("view_generation"), "acquisition view generation")
            == recovery.uint(sampler.get("view_generation"), "sampler view generation"), "source cohort differs from sampled image")
    words = struct.unpack("<32I", record)
    require(words[:4] == (1, ssbo.MAGIC, 1, ssbo.RECORD_WORDS), "invalid claimed record magic/version/word count")
    floats = {}
    for name, (first, count) in ssbo.FIELDS.items():
        if name in ("claim", "magic", "version", "words", "flags"):
            continue
        values = list(struct.unpack("<%df" % count, struct.pack("<%dI" % count, *words[first:first+count])))
        require(all(math.isfinite(value) for value in values), "nonfinite original SSA field: " + name)
        floats[name] = values
    r1, u = floats["R1"], floats["u"]
    require(all(0 <= r1[i] < r1[i+2] <= 1 for i in range(2)), "R1 must be a positive bounded complete-canvas rectangle")
    require(all(r1[i] <= u[i] <= r1[i+2] for i in range(2)), "selected original lookup does not intersect this R1 canvas")
    require((r1[2]-r1[0])*width >= registration.WIDTH and (r1[3]-r1[1])*height >= registration.HEIGHT,
            "fixed source profile requires at least one receiver texel per source pixel")
    return {"R1": r1, "u": u, "command_generation": command, "execution": control["execution"],
            "pool_generation": control["pool_generation"], "source_cohort": dict(cohort),
            "record_sha256": probe.digest(record)}


def _chart(image, r1):
    sx = (r1[2]-r1[0])*image.width/registration.WIDTH
    sy = (r1[3]-r1[1])*image.height/registration.HEIGHT
    tx, ty = r1[0]*image.width, r1[1]*image.height
    xs = [math.floor(sx*(x+.5)+tx) for x in range(registration.WIDTH)]
    ys = [math.floor(sy*(y+.5)+ty) for y in range(registration.HEIGHT)]
    require(min(xs) >= 0 and max(xs) < image.width and min(ys) >= 0 and max(ys) < image.height,
            "complete source-canvas centers must fit the observed R1 chart")
    gray = image.rgb[1::3]
    canvas = bytearray(registration.WIDTH*registration.HEIGHT*3)
    for y, yy in enumerate(ys):
        row = bytes(gray[yy*image.width+xx] for xx in xs)
        for channel in range(3):
            canvas[(y*registration.WIDTH)*3+channel:((y+1)*registration.WIDTH)*3:3] = row
    return probe.Image(registration.WIDTH, registration.HEIGHT, bytes(canvas)), [sx, sy, tx, ty]


def sample_header_interior2x2(image, copy):
    """Explicit fixed source-cell interior; no threshold reduction or fitting."""
    require(type(copy) is int and 0 <= copy < 3 and (image.width, image.height) == (688, 548),
            "fixed full source canvas and header copy required")
    result = bytearray(probe.HEADER_BYTES)
    for bit in range(probe.HEADER_BYTES*8):
        x, y = 16+(bit % 64)*8, 16+copy*76+(bit//64)*4
        levels = [sum(image.rgb[((y+dy)*688+x+side*4+dx)*3+1]
                      for dy in (1, 2) for dx in (1, 2))/4 for side in (0, 1)]
        if levels[0] <= 64 and levels[1] >= 191:
            value = 0
        elif levels[0] >= 191 and levels[1] <= 64:
            value = 1
        else:
            require(False, "interior2x2 header cell contrast is ambiguous")
        result[bit//8] |= value << (7-bit % 8)
    return bytes(result)


def _headers(canvas):
    headers = [sample_header_interior2x2(canvas, copy) for copy in range(3)]
    require(all(zlib.crc32(packet[:-4]) == struct.unpack(">I", packet[-4:])[0] for packet in headers), "header CRC mismatch")
    require(headers.count(headers[0]) == 3, "mixed redundant header cohort")
    fields = probe.HEADER_STRUCT.unpack(headers[0][:-4])
    require(fields[:3] == (probe.MAGIC, probe.VERSION, probe.LAYOUT_REVISION), "unsupported header format")
    require(fields[5] in (0, 1) and fields[6:8] == (0, 0) and fields[8:20] == discovery.HEADER_GEOMETRY,
            "unsupported encoded eye/convention/reserved/layout/extent")
    session, pair, eye = fields[3].hex(), fields[4], "right" if fields[5] else "left"
    registration.identity(session, pair, eye)
    return {"candidate_tuple": {"session": session, "pair_id": pair, "eye": eye, "phase": (pair-1) % 128},
            "header_sha256": probe.digest(headers[0]), "header_declared_color_sha256": fields[20].hex(),
            "header_declared_coverage_sha256": fields[21].hex(), "header_copies": 3}


def _discover(raw, sampled, metadata, record):
    invocation = _invocation(metadata, record)
    require(type(raw) is bytes and type(sampled) is bytes, "immutable paired bytes required")
    _, image, pair = paired.inspect_pair(raw, sampled, metadata)
    canvas, parameters = _chart(image, invocation["R1"])
    headers = _headers(canvas)
    r1 = invocation["R1"]
    candidate = {"schema": SCHEMA, "scope": SCOPE, "footprint": FOOTPRINT,
        "thresholds": [64, 191], "source_profile": registration.PROFILE,
        "source_canvas": [688, 548], "source_extent": [320, 192], "receiver_extent": pair["extent"],
        "B": [r1[2]-r1[0], 0., r1[0], 0., r1[3]-r1[1], r1[1]],
        "parameters": parameters, "invocation": invocation, **headers,
        "capture_id": pair["generation"]["capture_id"], "snapshot_sha256": pair["snapshot_sha256"],
        "sampler_sha256": pair["sampler_sha256"], "metadata_canonical_sha256": _metadata_digest(metadata),
        "raw_vs_sampler_code_comparison": pair["comparison"],
        "raw_comparison_is_source_oracle": False, "source_reference_valid": False,
        "independent_pc_session_confirmed": False, "source_pair_id_is_native_video_frame": False,
        "mapping_estimated_or_authenticated": False, "generator_authenticity_proven": False,
        "continuous_mapping_proven": False, **registration.authorities()}
    return candidate, canvas


def discover(raw, sampled, metadata, record):
    return _discover(raw, sampled, metadata, record)[0]


def verify(candidate, raw, sampled, metadata, record, source_record, source_record_sha256,
           color, coverage, canvas, provenance, source_root):
    require(type(candidate) is dict, "native-R1 discovery record required")
    recovery.sha(source_record_sha256, "canonical source-record digest")
    # Validate the expected artifacts before expensive repeated image inspection.
    identity = candidate.get("candidate_tuple")
    require(type(identity) is dict, "explicit discovered tuple required")
    source = registration.validate_source(source_record, color, coverage, canvas,
        identity.get("session"), identity.get("pair_id"), identity.get("eye"))
    header = probe.header_for(320, 192, color, coverage, identity["session"], identity["pair_id"], identity["eye"], "straight")
    checked_provenance = locator.provenance_check(provenance, Path(source_root), source_record_sha256,
        {"color.rgb8": color, "coverage.r8": coverage, "canvas.rgb8": canvas, "header.bin": header}, identity)
    current, mapped = _discover(raw, sampled, metadata, record)
    require(_metadata_digest(candidate) == _metadata_digest(current),
            "stale, altered, fitted or authority-upgraded native-R1 candidate")
    require(current["header_sha256"] == source["header_sha256"]
            and current["header_declared_color_sha256"] == source["source_color_sha256"]
            and current["header_declared_coverage_sha256"] == source["source_coverage_sha256"], "header differs from exact canonical source planes")
    markers = locator.held_out_markers(canvas, mapped.rgb)
    levels = {value: {"count": 0, "maximum_code_error": 0} for value in registration.LEVELS}
    for y in range(3, 189):
        for x in range(3, 317):
            wanted = coverage[y*320+x]
            if any(coverage[(y+dy)*320+x-3:(y+dy)*320+x+4] != bytes((wanted,))*7 for dy in range(-3, 4)):
                continue
            item = levels[wanted]; item["count"] += 1
            item["maximum_code_error"] = max(item["maximum_code_error"], abs(mapped.rgb[((y+244)*688+x+352)*3+1]-wanted))
    require(all(item["count"] >= 16 and item["maximum_code_error"] <= 2 for item in levels.values()), "homogeneous coverage interior verification refused")
    return {**current, "source_reference_valid": True, "canonical_source_validation": source,
        "emitter_provenance": checked_provenance, "source_record_sha256": source_record_sha256,
        "held_out_markers": markers, "coverage_interiors": {"source_uniform_window": 7, "maximum_allowed_code_error": 2,
            "levels": levels, "scope": "source-selected homogeneous codes only; not edges or linear game alpha"},
        "scope_limits": "fixed current sampled R1 chart; no physical-eye, native-video-frame, continuous mapping, original-draw full-header or live authority"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("snapshot", "sampler", "metadata", "ssbo", "record"):
        parser.add_argument("--"+name, type=Path, required=True)
    for name in ("candidate", "source-record", "color", "coverage", "canvas", "source-provenance", "source-root"):
        parser.add_argument("--"+name, type=Path)
    args = parser.parse_args(argv)
    try:
        raw = recovery.read_owned(args.snapshot, recovery.MAX_RAW_BYTES)
        sampled = recovery.read_owned(args.sampler, recovery.MAX_RAW_BYTES)
        metadata, _ = recovery.load_json(args.metadata)
        record = recovery.read_owned(args.ssbo, ssbo.RECORD_BYTES)
        verification = [args.candidate, args.source_record, args.color, args.coverage, args.canvas, args.source_provenance, args.source_root]
        require(all(verification) or not any(verification), "verification requires the complete canonical/provenance argument set")
        if any(verification):
            candidate, _ = recovery.load_json(args.candidate)
            source, source_sha = recovery.load_json(args.source_record)
            provenance, _ = recovery.load_json(args.source_provenance)
            report = verify(candidate, raw, sampled, metadata, record, source, source_sha,
                recovery.read_owned(args.color, 184320), recovery.read_owned(args.coverage, 61440),
                recovery.read_owned(args.canvas, 1131072), provenance, args.source_root)
        else:
            report = discover(raw, sampled, metadata, record)
        recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+"\n").encode())
        print(json.dumps({"candidate_tuple": report["candidate_tuple"], "source_reference_valid": report["source_reference_valid"], "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, struct.error, OverflowError) as error:
        print("native-R1 registration: "+str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
