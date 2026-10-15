#!/usr/bin/env python3
"""Inspect a paired P010/sampler diagnostic offline; never enables passthrough."""
from __future__ import annotations

import argparse
from array import array
import json
import os
from pathlib import Path
import sys

import pixel_payload_recovery as recovery

probe = recovery.probe
CONTROL_SCHEMA = "frame-decoded-p010-sampler-control-v1"
ENCODING = "RGBA8_UNORM sampled RGB_IDENTITY components; not reconstructed scene RGB"
POSITIONS = "normalized logical texel centers, explicit LOD zero"
SAMPLER_FILE = "sampler-components.rgba8"
CONTROL_FIELDS = frozenset(("schema", "file", "capture_id", "image_generation", "conversion_generation",
    "command_generation", "view_generation", "width", "height", "byte_length", "sha256", "encoding",
    "sample_positions", "same_command_generation", "same_image_view_generation", "graphics_state_untouched",
    "no_prior_compute_state", "shared_storage_capacity", "buffer_offset", "total_paired_bytes"))


def fail(message):
    recovery.fail(message)


def paired_extent(metadata):
    if not isinstance(metadata, dict):
        fail("snapshot metadata must be an object")
    width, height = metadata.get("width"), metadata.get("height")
    if (type(width) is not int or type(height) is not int or not 2 <= width <= 8192
            or not 2 <= height <= 8192 or width % 2 or height % 2
            or width * height > probe.MAX_PIXELS or width * height * 7 > recovery.MAX_RAW_BYTES):
        fail("paired even extent must fit the bounded 64 MiB storage allocation")
    return width, height, width * height


def inspect_pair(raw: bytes, sampled: bytes, metadata: dict):
    """Return original-component and G-code previews, plus bounded code statistics."""
    width, height, pixels = paired_extent(metadata)
    original = recovery.inspect_snapshot(raw, metadata)
    control = metadata.get("sampler_control")
    if not isinstance(control, dict) or control.get("schema") != CONTROL_SCHEMA:
        fail("paired sampler-control schema missing or unsupported")
    if set(control) != CONTROL_FIELDS:
        fail("sampler-control v1 fields differ; timestamps cannot substitute for exact generations")
    if (control.get("file") != SAMPLER_FILE or control.get("encoding") != ENCODING
            or control.get("sample_positions") != POSITIONS):
        fail("sampler component encoding, position contract or logical filename differs")
    for field in ("same_command_generation", "same_image_view_generation", "graphics_state_untouched", "no_prior_compute_state"):
        if control.get(field) is not True:
            fail("sampler acquisition guard missing: " + field)
    acquisition = metadata.get("acquisition")
    if (not isinstance(acquisition, dict)
            or acquisition.get("path") != "same_primary_after_distortion_renderpass_gpu_plane_copy"
            or acquisition.get("owned_fence_complete") is not True
            or type(acquisition.get("generation_retired")) is not bool
            or not ((acquisition.get("one_time_submit") is True and acquisition.get("retirement_required") is False)
                    or (acquisition.get("one_time_submit") is False and acquisition.get("retirement_required") is True
                        and acquisition.get("generation_retired") is True))):
        fail("requires an owned fence and either one-time recording or exact reusable-generation retirement")
    for field in ("source_frame_associated", "uv_owned", "passthrough_authorized"):
        if metadata.get(field) is not False:
            fail("diagnostic metadata must not assert source/frame/UV/passthrough authority")
    generation = metadata["generation"]
    if control.get("capture_id") != generation["capture_id"]:
        fail("sampler capture identity differs from the raw snapshot")
    for field in ("image_generation", "conversion_generation"):
        expected = recovery.uint(generation.get(field), field)
        if not expected or recovery.uint(control.get(field), field) != expected:
            fail("sampler " + field + " differs from the raw snapshot")
    for field in ("command_generation", "view_generation"):
        expected = recovery.uint(acquisition.get(field), field)
        if not expected or recovery.uint(control.get(field), field) != expected:
            fail("sampler " + field + " differs from the completed native recording")
    if not recovery.uint(generation.get("acquisition_id"), "acquisition_id"):
        fail("explicit positive acquisition generation required")
    for field, expected in (("width", width), ("height", height), ("buffer_offset", pixels * 3),
                            ("byte_length", pixels * 4), ("total_paired_bytes", pixels * 7)):
        if recovery.uint(control.get(field), field) != expected:
            fail("sampler " + field + " differs from the paired storage extent")
    capacity = recovery.uint(control.get("shared_storage_capacity"), "shared_storage_capacity")
    if not pixels * 7 <= capacity <= recovery.MAX_RAW_BYTES:
        fail("paired outputs exceed the recorded bounded storage capacity")
    if (len(sampled) != pixels * 4
            or probe.digest(sampled) != recovery.sha(control.get("sha256"), "sampler sha256")):
        fail("sampler byte extent or digest mismatch")
    conversion = {"model": "rgb_identity", "range": "itu_full",
                  "components": {"r": "r", "g": "g", "b": "b", "a": "a"},
                  "x_chroma_offset": "midpoint", "y_chroma_offset": "midpoint", "chroma_filter": "linear"}
    observed = {"complete": True, "has_view_conversion": True, "format": recovery.FORMAT,
                "force_explicit_reconstruction": 0, "model": 0, "range": 0,
                "components": [0, 0, 0, 0], "chroma_offsets": [1, 1], "chroma_filter": 1}
    actual = metadata.get("observed_vulkan_conversion")
    if (metadata.get("conversion") != conversion or actual != observed
            or actual.get("complete") is not True or actual.get("has_view_conversion") is not True
            or any(type(actual[key]) is not int for key in ("format", "force_explicit_reconstruction", "model", "range", "chroma_filter"))
            or any(type(value) is not int for key in ("components", "chroma_offsets") for value in actual[key])):
        fail("exact converted-view RGB_IDENTITY/FULL/MIDPOINT/LINEAR profile required")
    words = array("H")
    words.frombytes(raw)
    if sys.byteorder != "little":
        words.byteswap()
    if any(value & 63 for value in words):
        fail("P010 sample padding is not zero")
    rgb, gray = bytearray(pixels * 3), bytearray(pixels * 3)
    total_error = maximum = exact = within_one = nonopaque = 0
    for pixel in range(pixels):
        rgba = sampled[pixel * 4:pixel * 4 + 4]
        rgb[pixel * 3:pixel * 3 + 3] = rgba[:3]
        gray[pixel * 3:pixel * 3 + 3] = bytes((rgba[1],)) * 3
        # In this fixed Vulkan RGB_IDENTITY profile G carries the normalized
        # logical Y sample. This compares code values, not luminance or RGB.
        expected = ((words[pixel] >> 6) * 255 + 511) // 1023
        error = abs(rgba[1] - expected)
        total_error += error
        maximum = max(maximum, error)
        exact += error == 0
        within_one += error <= 1
        nonopaque += rgba[3] != 255
    report = {"schema": "frame-paired-sampler-recovery-v1", "pair_valid": True,
              "pair_validation_scope": "exact byte integrity and recorded generation agreement; no independent native/source association",
              "extent": [width, height], "generation": dict(generation),
              "command_generation": acquisition["command_generation"], "view_generation": acquisition["view_generation"],
              "generation_retired": acquisition["generation_retired"],
              "one_time_submit": acquisition["one_time_submit"], "retirement_required": acquisition["retirement_required"],
              "snapshot_sha256": original["snapshot_sha256"], "sampler_sha256": probe.digest(sampled),
              "raw_bytes": len(raw), "sampler_bytes": len(sampled), "total_paired_bytes": pixels * 7,
              "component_preview_encoding": ENCODING,
              "green_preview_encoding": "sampled G component replicated to gray; logical Y code, not scene luminance",
              "comparison": {"scope": "same recorded logical texel centers; no inferred source/eye/UV mapping",
                             "reference": "stored P010 high10 Y rounded to8bits with (code*255+511)//1023",
                             "pixels": pixels, "mean_absolute_error_8bit": total_error / pixels,
                             "maximum_absolute_error_8bit": maximum, "exact_pixels": exact,
                             "within_one_code_pixels": within_one, "over_one_code_pixels": pixels - within_one},
              "nonopaque_sampler_alpha_pixels": nonopaque, "rgb_converted": False,
              "header_recovered": False, "native_sampler_equivalence_proven": False,
              "native_frame_association_proven": False, "source_frame_association_proven": False,
              "private_resource_association_proven": False, "eye_placement_proven": False,
              "native_spatial_mapping": None, "opaque_fallback_required": True,
              "passthrough_authorized": False}
    return probe.Image(width, height, bytes(rgb)), probe.Image(width, height, bytes(gray)), report


def check_outputs(paths):
    """Preflight all destinations; O_EXCL still protects each actual write."""
    names = [os.path.abspath(path) for path in paths]
    if len(set(names)) != len(names):
        fail("output paths must be distinct")
    for path in paths:
        parent, name = recovery.parent_fd(path)
        try:
            try:
                os.stat(name, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                continue
            fail("output already exists; refusing overwrite")
        finally:
            os.close(parent)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("snapshot", "sampler", "metadata", "png", "green-png", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("source-manifest", "reference-png", "mapping"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--expected-session")
    parser.add_argument("--expected-content-id", type=int)
    parser.add_argument("--expected-eye", choices=("left", "right"))
    args = parser.parse_args(argv)
    try:
        group = (args.source_manifest, args.reference_png, args.mapping,
                 args.expected_session, args.expected_content_id, args.expected_eye)
        if any(value is not None for value in group) and not all(value is not None for value in group):
            fail("header recovery requires trusted manifest/PNG, mapping and explicit expected session/content/eye together")
        metadata, metadata_sha = recovery.load_json(args.metadata)
        _, _, pixels = paired_extent(metadata)
        control = metadata.get("sampler_control")
        if not isinstance(control, dict) or control.get("schema") != CONTROL_SCHEMA:
            fail("paired sampler-control schema missing or unsupported")
        image, green, report = inspect_pair(recovery.read_owned(args.snapshot, pixels * 3),
                                            recovery.read_owned(args.sampler, pixels * 4), metadata)
        report["metadata_sha256"] = metadata_sha
        if args.mapping is not None:
            manifest, manifest_sha = recovery.load_json(args.source_manifest)
            mapping, mapping_sha = recovery.load_json(args.mapping)
            reference_bytes = recovery.read_owned(args.reference_png, probe.MAX_PNG_BYTES)
            report["header_recovery"] = recovery.recover_header(green, probe.decode_png(reference_bytes), manifest,
                                                                mapping, args.expected_session,
                                                                args.expected_content_id, args.expected_eye)
            report["header_recovered"] = report["header_recovery"]["header_recovered"]
            report.update(source_manifest_sha256=manifest_sha, header_mapping_sha256=mapping_sha,
                          source_reference_png_sha256=probe.digest(reference_bytes))
        check_outputs((args.png, args.green_png, args.record))
        encoded, green_encoded = probe.encode_png(image), probe.encode_png(green)
        report.update(png_sha256=probe.digest(encoded), green_png_sha256=probe.digest(green_encoded))
        recovery.write_owned(args.png, encoded)
        recovery.write_owned(args.green_png, green_encoded)
        recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
        print(json.dumps({"pair_valid": True, "header_recovered": report["header_recovered"],
                          "comparison": report["comparison"], "passthrough_authorized": False}))
        return 0 if args.mapping is None or report["header_recovered"] else 2
    except (probe.ProbeError, OSError, ValueError, TypeError, OverflowError) as error:
        print("pixel sampler recovery: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
