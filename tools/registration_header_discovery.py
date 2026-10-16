#!/usr/bin/env python3
"""Discover an UNTRUSTED app tuple from an explicitly already-mapped canvas.

Only an independently supplied session and source-eye expectation are accepted.
No mapping, frame association or source authenticity is inferred from headers.
Regenerate the tuple with the actual C# source emitter, then use the unchanged
registration_source_recovery.measure_mapped validator before drawing conclusions.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib

import registration_source_recovery as registration

recovery, probe = registration.recovery, registration.probe
SCHEMA = "frame-registration-header-discovery-v1"
HEADER_GEOMETRY = (320, 192, 688, 548, 16, 244, 320, 192, 352, 244, 320, 192)
require = registration.require


def discover_mapped(candidate, mapping, expected_session, expected_eye):
    """Check three whole headers, without authenticating their declared hashes."""
    registration.identity(expected_session, 1, expected_eye)
    registration.mapped_contract(mapping)
    require(type(candidate.width) is int and type(candidate.height) is int
            and type(candidate.rgb) is bytes, "candidate must contain integer extents and immutable RGB8 bytes")
    probe.check_image(candidate)
    require((candidate.width, candidate.height) == (registration.WIDTH, registration.HEIGHT),
            "candidate must already map the full fixed canvas")
    if mapping["pixel_encoding"] == "sampled_g_gray8_codes":
        require(candidate.rgb[::3] == candidate.rgb[1::3] == candidate.rgb[2::3],
                "sampled-G candidate must be exact replicated grayscale")
    headers = [probe.sample_header(candidate, index) for index in range(probe.HEADER_COPIES)]
    for header in headers:
        require(zlib.crc32(header[:-4]) == struct.unpack(">I", header[-4:])[0], "header CRC mismatch")
    require(all(header == headers[0] for header in headers), "mixed redundant header cohort")
    fields = probe.HEADER_STRUCT.unpack(headers[0][:-4])
    require(fields[:3] == (probe.MAGIC, probe.VERSION, probe.LAYOUT_REVISION), "unsupported header format")
    session, pair, eye = fields[3].hex(), fields[4], fields[5]
    registration.identity(session, pair, expected_eye)
    require(session == expected_session and eye == (0 if expected_eye == "left" else 1),
            "header disagrees with independent expected session/eye")
    require(fields[6:8] == (0, 0), "header convention/reserved flags differ from the straight profile")
    require(fields[8:20] == HEADER_GEOMETRY, "header extent/placements differ from the fixed profile")
    return {"schema": SCHEMA, "discovery_status": "UNTRUSTED_CANDIDATE",
        "candidate_tuple": {"session": session, "pair_id": pair, "eye": expected_eye, "phase": (pair - 1) % 128},
        "header_declared_color_sha256": fields[20].hex(), "header_declared_coverage_sha256": fields[21].hex(),
        "checked_header_copies": 3, "header_sha256": probe.digest(headers[0]),
        "header_crc_scope": "accidental-error check only; not authentication",
        "header_profile": {"magic": probe.MAGIC.decode("ascii"), "version": probe.VERSION,
            "layout_revision": probe.LAYOUT_REVISION, "rgb_convention": "straight", "reserved": 0,
            "source_extent": [320, 192], "placements": probe.layout(320, 192)},
        "caller_supplied_mapping": mapping, "mapped_candidate_rgb_sha256": probe.digest(candidate.rgb),
        "canonical_regeneration_required": True,
        "required_followup": "Regenerate this explicit tuple with the actual C# --emit source; compare both declared plane hashes, then call registration_source_recovery.measure_mapped.",
        "source_reference_valid": False, "generator_authenticity_proven": False,
        "decoded_source_integrity_validated": False, "mapping_estimated_or_authenticated": False,
        "source_pair_id_is_native_video_frame": False, **registration.authorities()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidate-png", "mapped-record", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-session", required=True)
    parser.add_argument("--expected-eye", choices=("left", "right"), required=True)
    args = parser.parse_args(argv)
    try:
        registration.identity(args.expected_session, 1, args.expected_eye)
        mapping, mapping_sha = recovery.load_json(args.mapped_record)
        registration.mapped_contract(mapping)
        encoded = recovery.read_owned(args.candidate_png, registration.MAX_PNG)
        require(encoded.startswith(probe.PNG_SIGNATURE) and len(encoded) >= 33
                and struct.unpack_from(">II", encoded, 16) == (registration.WIDTH, registration.HEIGHT),
                "candidate PNG must declare the exact bounded canvas extent")
        report = discover_mapped(probe.decode_png(encoded), mapping, args.expected_session, args.expected_eye)
        report.update(candidate_png_sha256=probe.digest(encoded), mapped_record_sha256=mapping_sha)
        recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"discovery_status": report["discovery_status"], "candidate_tuple": report["candidate_tuple"],
                          "source_reference_valid": False, "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, AttributeError, struct.error, OverflowError) as error:
        print("registration header discovery: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
