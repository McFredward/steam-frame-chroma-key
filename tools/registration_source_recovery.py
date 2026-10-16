#!/usr/bin/env python3
"""Strict registration-source artifacts and explicit mapped-code diagnostics only.

No generator is reimplemented here. The caller supplies an independently owned
expected tuple and its canonical C# source artifacts. Candidate images must
already be mapped to the fixed source canvas; this tool does not estimate,
resize, warp, calibrate or authenticate a native video/eye/application mapping.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import zlib

import pixel_payload_recovery as recovery

probe = recovery.probe
SCHEMA, PROFILE = "frame-app-registration-source-v1", "dense-asymmetric-registration-v1"
MAP_SCHEMA = "frame-registration-mapped-canvas-v1"
WIDTH, HEIGHT, SOURCE_WIDTH, SOURCE_HEIGHT = 688, 548, 320, 192
MAX_POINTS, MAX_PNG = 32, 4 * 1024 * 1024
LEVELS = frozenset((0, 64, 128, 191, 192, 255))
FIELDS = frozenset(("schema", "profile", "profile_revision", "session", "pair_id", "eye", "phase",
    "engine_frame_cohort", "content_id_meaning", "color_numeric_encoding", "coverage_numeric_encoding", "manifest",
    "native_frame_identity", "native_spatial_mapping", "physical_eye_proven", "source_frame_association_proven",
    "gpu_completion_proven", "opaque_fallback_required", "passthrough_authorized"))
MANIFEST_FIELDS = frozenset(("schema", "session", "content_id", "eye", "rgb_convention", "source_extent",
    "layout_revision", "placements", "header_copies", "color_sha256", "coverage_sha256", "canvas_rgb_sha256",
    "opaque_fallback_required", "passthrough_authorized"))
MAP_FIELDS = frozenset(("schema", "kind", "source_canvas", "pixel_encoding", "recorded_provenance", "scope", "points"))


def require(condition, message):
    if not condition:
        recovery.fail(message)


def fields(value, expected, name):
    require(type(value) is dict and set(value) == expected, name + " fields differ from the pinned profile")


def identity(session, pair, eye):
    require(type(session) is str and re.fullmatch(r"[0-9a-f]{32}", session) and any(c != "0" for c in session),
            "expected session must be canonical nonzero lowercase128-bit hex")
    require(type(pair) is int and 0 < pair < 2**64, "expected pair must be a positive app-owned uint64")
    require(type(eye) is str and eye in ("left", "right"), "expected source eye must be explicit")


def exact_int(value, expected):
    return type(value) is int and value == expected


def exact_vector(value, expected):
    return type(value) is list and len(value) == len(expected) and all(exact_int(a, b) for a, b in zip(value, expected))


def authorities():
    return {"native_frame_identity": None, "native_spatial_mapping": None,
        "native_frame_association_proven": False, "source_frame_association_proven": False,
        "physical_eye_proven": False, "application_ray_mapping_proven": False,
        "native_sampler_equivalence_proven": False, "gpu_completion_proven": False,
        "opaque_fallback_required": True, "passthrough_authorized": False}


def recovered_headers(image, expected):
    # The unchanged reader checks every4x4 complementary cell, not only one bit
    # center or a majority of copies. Never combine fragments from different IDs.
    headers = [probe.sample_header(image, index) for index in range(3)]
    require(all(item == headers[0] for item in headers), "mixed redundant header cohort")
    require(zlib.crc32(headers[0][:-4]) == struct.unpack(">I", headers[0][-4:])[0], "header CRC mismatch")
    require(headers[0] == expected, "header differs from the exact expected owned session/pair/eye/source")
    return probe.digest(headers[0])


def clean_header_cells(canvas, expected):
    # Trusted source artifacts are lossless originals, unlike decoded candidates.
    # Require every authored0/255 RGB code; do not use the lossy decode tolerance
    # to bless modified reference artwork with a self-updated canvas digest.
    for copy in range(3):
        for bit in range(132 * 8):
            on = (expected[bit // 8] >> (7 - bit % 8)) & 1
            x, y = 16 + bit % 64 * 8, 16 + copy * 76 + bit // 64 * 4
            for side in (0, 1):
                wanted = bytes((255 * (on if side == 0 else 1 - on),)) * 12
                for row in range(4):
                    at = ((y + row) * WIDTH + x + side * 4) * 3
                    require(canvas[at:at + 12] == wanted, "canonical source header codes differ")


def validate_source(record, color, coverage, canvas, session, pair, eye):
    """Validate exact provided artifacts; generator authenticity is not inferred."""
    identity(session, pair, eye)
    fields(record, FIELDS, "registration source")
    require(record["schema"] == SCHEMA and record["profile"] == PROFILE and exact_int(record["profile_revision"], 1),
            "unsupported registration source revision")
    require(record["session"] == session and type(record["pair_id"]) is int and record["pair_id"] == pair
            and record["eye"] == eye and exact_int(record["phase"], (pair - 1) % 128), "wrong/stale source tuple or phase")
    cohort = record["engine_frame_cohort"]
    require(type(cohort) is int and 0 <= cohort < 2**63, "invalid engine scheduling cohort")
    require(record["content_id_meaning"] == "app_owned_pair_id_not_native_video_frame"
            and record["color_numeric_encoding"] == "uninterpreted_rgb8_codes"
            and record["coverage_numeric_encoding"] == "linear_coverage_r8", "source numeric/identity meaning differs")
    for key in ("native_frame_identity", "native_spatial_mapping"):
        require(record[key] is None, "source cannot claim " + key)
    for key in ("physical_eye_proven", "source_frame_association_proven", "gpu_completion_proven", "passthrough_authorized"):
        require(record[key] is False, "source cannot claim " + key)
    require(record["opaque_fallback_required"] is True, "opaque fallback must remain required")
    manifest = record["manifest"]
    fields(manifest, MANIFEST_FIELDS, "source manifest")
    require(manifest["schema"] == "frame-app-payload-reference-v1" and manifest["session"] == session
            and type(manifest["content_id"]) is int and manifest["content_id"] == pair and manifest["eye"] == eye
            and manifest["rgb_convention"] == "straight" and exact_int(manifest["layout_revision"], 1)
            and exact_int(manifest["header_copies"], 3) and exact_vector(manifest["source_extent"], [320, 192])
            and manifest["opaque_fallback_required"] is True and manifest["passthrough_authorized"] is False,
            "manifest identity/layout/convention differs")
    placement = manifest["placements"]
    fields(placement, frozenset(("canvas", "color", "coverage")), "placements")
    known = probe.layout(SOURCE_WIDTH, SOURCE_HEIGHT)
    require(all(exact_vector(placement[key], value) for key, value in known.items()), "full fixed atlas layout differs")
    for data, count, key in ((color, 184320, "color_sha256"), (coverage, 61440, "coverage_sha256"),
                             (canvas, 1131072, "canvas_rgb_sha256")):
        require(type(data) is bytes and len(data) == count and probe.digest(data) == recovery.sha(manifest[key], key),
                "source byte length/digest differs: " + key)
    require(set(coverage) == LEVELS, "source coverage profile codes differ")
    image = probe.Image(WIDTH, HEIGHT, canvas)
    require(probe.extract(image, placement["color"]) == color, "full source RGB plane disagrees with canvas")
    grayscale = probe.extract(image, placement["coverage"])
    require(grayscale[::3] == grayscale[1::3] == grayscale[2::3] == coverage, "full source R8 plane disagrees with canvas")
    expected = probe.header_for(320, 192, color, coverage, session, pair, eye, "straight")
    clean_header_cells(canvas, expected)
    header_digest = recovered_headers(image, expected)
    return {"schema": "frame-registration-source-validation-v1", "source_reference_valid": True,
        "validation_scope": "exact explicit canonical artifacts and tuple/header agreement; generator authenticity is not proven",
        "expected_session": session, "expected_pair_id": pair, "expected_eye": eye, "expected_phase": record["phase"],
        "source_color_sha256": probe.digest(color), "source_coverage_sha256": probe.digest(coverage),
        "source_canvas_sha256": probe.digest(canvas), "header_sha256": header_digest,
        "header_copies_verified": 3, "header_format_version": 1, "generator_authenticity_proven": False,
        "source_pair_id_is_native_video_frame": False, **authorities()}


def point(value, name):
    require(type(value) is list and len(value) == 2 and all(type(item) is int for item in value)
            and 0 <= value[0] < WIDTH and 0 <= value[1] < HEIGHT, name + " must be a bounded canvas pixel")
    return value


def mapped_contract(mapping):
    fields(mapping, MAP_FIELDS, "mapped diagnostic")
    require(mapping["schema"] == MAP_SCHEMA and mapping["kind"] == "caller_supplied_already_mapped_image"
            and exact_vector(mapping["source_canvas"], [WIDTH, HEIGHT])
            and mapping["pixel_encoding"] in ("rgb8_codes", "sampled_g_gray8_codes")
            and type(mapping["pixel_encoding"]) is str and mapping["scope"] == "FITTED_DIAGNOSTIC_ONLY",
            "explicit mapped-canvas encoding/scope required")
    provenance = mapping["recorded_provenance"]
    require(type(provenance) is str and 1 <= len(provenance) <= 2048, "bounded caller mapping provenance required")
    points = mapping["points"]
    require(type(points) is list and len(points) <= MAX_POINTS, "at most32 caller control points required")
    for item in points:
        fields(item, frozenset(("source", "mapped")), "control point")
        point(item["source"], "source point"); point(item["mapped"], "mapped point")


def code_stats(reference, candidate, rect, gray):
    x, y, width, height = rect
    total = maximum = changed = 0
    channels = (1,) if gray else (0, 1, 2)
    for row in range(y, y + height):
        for column in range(x, x + width):
            index = (row * WIDTH + column) * 3
            for channel in channels:
                error = abs(candidate[index + channel] - reference[index + channel])
                total += error; maximum = max(maximum, error); changed += error != 0
    count = width * height * len(channels)
    return {"rect": list(rect), "samples": count, "mean_absolute_code_error": total / count,
            "maximum_absolute_code_error": maximum, "nonzero_code_errors": changed}


def measure_mapped(record, color, coverage, canvas, session, pair, eye, candidate, mapping):
    source = validate_source(record, color, coverage, canvas, session, pair, eye)
    mapped_contract(mapping)
    probe.check_image(candidate)
    require((candidate.width, candidate.height) == (WIDTH, HEIGHT), "candidate must already map the full fixed canvas")
    gray = mapping["pixel_encoding"] == "sampled_g_gray8_codes"
    if gray:
        require(candidate.rgb[::3] == candidate.rgb[1::3] == candidate.rgb[2::3], "sampled-G candidate must be exact replicated grayscale")
    expected = probe.header_for(320, 192, color, coverage, session, pair, eye, "straight")
    header_digest = recovered_headers(candidate, expected)
    stats = {name: code_stats(canvas, candidate.rgb, rect, gray) for name, rect in
             (("full_canvas", [0, 0, WIDTH, HEIGHT]), ("color", [16, 244, 320, 192]), ("coverage", [352, 244, 320, 192]))}
    tiles = [code_stats(canvas, candidate.rgb, [x * 86, y * 137, 86, 137], gray) for y in range(4) for x in range(8)]
    controls = []
    for item in mapping["points"]:
        a, b = item["source"], item["mapped"]
        at, bt = (a[1] * WIDTH + a[0]) * 3, (b[1] * WIDTH + b[0]) * 3
        channels = (1,) if gray else (0, 1, 2)
        controls.append({"source": a, "mapped": b, "caller_coordinate_displacement": [b[i] - a[i] for i in range(2)],
                         "code_residual": [candidate.rgb[bt + c] - canvas[at + c] for c in channels]})
    return {"schema": "frame-registration-mapped-code-measurement-v1", "source_validation": source,
        "header_cohort_recovered": True, "header_copies_verified": 3, "recovered_header_sha256": header_digest,
        "measurement_scope": "FITTED_DIAGNOSTIC_ONLY", "caller_supplied_mapping": mapping,
        "numeric_comparison": "candidate G8 versus expected source G8 codes; YCbCr/transfer equivalence is unproven" if gray
                              else "mapped RGB8 code errors only; no inferred native sampler/color equivalence",
        "region_code_errors": stats, "dense_canvas_grid_code_errors": tiles, "control_point_residuals": controls,
        "exact_mapped_code_agreement": stats["full_canvas"]["nonzero_code_errors"] == 0,
        "decoded_source_integrity_validated": False, "mapped_candidate_sha256": probe.digest(candidate.rgb),
        "mapping_estimated_or_authenticated": False, "source_pair_id_is_native_video_frame": False, **authorities()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source-record", "color", "coverage", "canvas", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-session", required=True)
    parser.add_argument("--expected-pair", type=int, required=True)
    parser.add_argument("--expected-eye", choices=("left", "right"), required=True)
    parser.add_argument("--candidate-png", type=Path)
    parser.add_argument("--mapped-record", type=Path)
    args = parser.parse_args(argv)
    try:
        require((args.candidate_png is None) == (args.mapped_record is None), "candidate image and explicit mapped contract are required together")
        record, record_sha = recovery.load_json(args.source_record)
        color, coverage, canvas = (recovery.read_owned(path, limit) for path, limit in
                                  ((args.color, 184320), (args.coverage, 61440), (args.canvas, 1131072)))
        identity(args.expected_session, args.expected_pair, args.expected_eye)
        if args.candidate_png is None:
            report = validate_source(record, color, coverage, canvas, args.expected_session, args.expected_pair, args.expected_eye)
        else:
            mapping, mapping_sha = recovery.load_json(args.mapped_record)
            encoded = recovery.read_owned(args.candidate_png, MAX_PNG)
            require(encoded.startswith(probe.PNG_SIGNATURE) and len(encoded) >= 33
                    and struct.unpack_from(">II", encoded, 16) == (WIDTH, HEIGHT), "candidate PNG must declare the exact bounded canvas extent")
            candidate = probe.decode_png(encoded)
            report = measure_mapped(record, color, coverage, canvas, args.expected_session, args.expected_pair, args.expected_eye, candidate, mapping)
            report.update(candidate_png_sha256=probe.digest(encoded), mapped_record_sha256=mapping_sha)
        report["source_record_sha256"] = record_sha
        recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"source_reference_valid": True, "header_cohort_recovered": report.get("header_cohort_recovered", False),
                          "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, AttributeError, struct.error, OverflowError) as error:
        print("registration source recovery: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
