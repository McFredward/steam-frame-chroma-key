#!/usr/bin/env python3
"""Inspect bounded native UBO bytes offline; never infer native eyes/UV/frame IDs."""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import sys

import pixel_sampler_recovery as sampler

recovery, probe = sampler.recovery, sampler.probe
SCHEMA, FILE = "frame-native-uniform-control-v1", "uniform-bytes.bin"
MAX_SLICE, MAX_BYTES = 4096, 20480
BINDINGS, MINIMUM = (0, 2, 3, 4, 6), (268, 944, 396, 260, 944)
PROFILE = ((1, "9cc04d186b9c8fe5", 9460, "VS_DISTORT"),
           (16, "f333a137b4e79ced", 6620, "PS_DISTORT"))
GUARDS = frozenset(("same_primary_after_native_render_scope", "selected_draw_is_last",
    "no_intervening_draw", "no_intervening_buffer_write", "buffer_generations_revalidated",
    "descriptor_revisions_revalidated", "no_known_overlapping_allocation", "queue_ownership_revalidated"))
FIELDS = frozenset(("schema", "file", "capture_id", "command_generation", "image_generation", "view_generation",
    "pipeline", "stages", "selected_draw_ordinal", "set", "reobserved_set_revision",
    "reobserved_command_binding_revision", "byte_length", "sha256", "staging_offset", "shared_storage_capacity", "guards", "slices"))
SLICE_FIELDS = frozenset(("binding", "descriptor_type", "descriptor_stages", "descriptor_revision", "buffer_handle",
    "buffer_generation", "buffer_size", "buffer_usage", "allocation_handle", "allocation_generation", "allocation_size",
    "memory_offset", "descriptor_offset", "descriptor_range", "dynamic_offset", "effective_offset", "effective_range",
    "file_offset", "byte_length", "sha256"))


def require(condition, message):
    if not condition:
        recovery.fail(message)


def fields(value, expected, name):
    require(isinstance(value, dict) and set(value) == set(expected), name + " schema fields differ")
    return value


def number(value, name, positive=False, maximum=2**64 - 1):
    result = recovery.uint(value, name)
    require(result <= maximum and (not positive or result > 0), name + " outside bounded integer range")
    return result


def handle(value):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{1,16}", value) and int(value, 16), "invalid native handle metadata")


def exact(value, expected):
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return len(value) == len(expected) and set(value) == set(expected) and all(exact(value[key], item) for key, item in expected.items())
    if isinstance(expected, list):
        return len(value) == len(expected) and all(exact(a, b) for a, b in zip(value, expected))
    return value == expected


def original_metadata(metadata, expected_capture):
    """Metadata-only envelope; this does not reread/validate either pixel file."""
    require(isinstance(metadata, dict) and metadata.get("schema") == recovery.SCHEMA
            and type(metadata.get("format")) is int and metadata["format"] == recovery.FORMAT
            and metadata.get("immutable") is True, "original immutable P010 schema required")
    width, height, pixels = sampler.paired_extent(metadata)
    require(number(metadata.get("byte_length"), "snapshot byte_length") == pixels * 3, "snapshot extent mismatch")
    recovery.sha(metadata.get("sha256"), "snapshot sha256")
    for name in ("source_frame_associated", "uv_owned", "passthrough_authorized"):
        require(metadata.get(name) is False, "diagnostic must not claim " + name)
    generation, acquisition = metadata.get("generation"), metadata.get("acquisition")
    require(isinstance(generation, dict) and isinstance(acquisition, dict), "original generation/acquisition missing")
    capture = generation.get("capture_id")
    require(isinstance(capture, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", capture), "invalid original capture ID")
    require(expected_capture is None or capture == expected_capture, "unexpected capture ID")
    for name in ("image_generation", "conversion_generation", "acquisition_id"):
        number(generation.get(name), name, True)
    for name in ("command_generation", "view_generation"):
        number(acquisition.get(name), name, True)
    number(acquisition.get("queue_family"), "queue_family", maximum=2**32 - 1)
    require(acquisition.get("path") == "same_primary_after_distortion_renderpass_gpu_plane_copy"
            and acquisition.get("owned_fence_complete") is True
            and type(acquisition.get("generation_retired")) is bool
            and ((acquisition.get("one_time_submit") is True and acquisition.get("retirement_required") is False)
                 or (acquisition.get("one_time_submit") is False and acquisition.get("retirement_required") is True
                     and acquisition.get("generation_retired") is True)), "fence/exact reusable retirement required")
    planes = metadata.get("planes")
    require(isinstance(planes, list) and len(planes) == 2, "original tight planes missing")
    for plane, name, offset, length in zip(planes, ("Y", "UV"), (0, pixels * 2), (pixels * 2, pixels)):
        require(isinstance(plane, dict) and plane.get("name") == name
                and number(plane.get("offset"), "plane offset") == offset
                and number(plane.get("length"), "plane length") == length, "original tight plane layout differs")
        recovery.sha(plane.get("sha256"), "plane sha256")
    pair = fields(metadata.get("sampler_control"), sampler.CONTROL_FIELDS, "sampler-control")
    require(pair["schema"] == sampler.CONTROL_SCHEMA and pair["file"] == sampler.SAMPLER_FILE
            and pair["encoding"] == sampler.ENCODING and pair["sample_positions"] == sampler.POSITIONS,
            "original sampler profile differs")
    require(pair["capture_id"] == capture, "sampler capture mismatch")
    for name in ("same_command_generation", "same_image_view_generation", "graphics_state_untouched", "no_prior_compute_state"):
        require(pair[name] is True, "sampler acquisition guard missing")
    for name in ("image_generation", "conversion_generation"):
        require(number(pair[name], name, True) == generation[name], "sampler generation mismatch")
    for name in ("command_generation", "view_generation"):
        require(number(pair[name], name, True) == acquisition[name], "sampler acquisition mismatch")
    for name, expected in (("width", width), ("height", height), ("byte_length", pixels * 4),
                           ("buffer_offset", pixels * 3), ("total_paired_bytes", pixels * 7)):
        require(number(pair[name], name) == expected, "sampler extent/offset differs")
    recovery.sha(pair["sha256"], "sampler sha256")
    capacity = number(pair["shared_storage_capacity"], "capacity", maximum=recovery.MAX_RAW_BYTES)
    require(capacity >= pixels * 7, "sampler exceeds storage capacity")
    expected = {"complete": True, "has_view_conversion": True, "format": recovery.FORMAT,
        "force_explicit_reconstruction": 0, "model": 0, "range": 0, "components": [0, 0, 0, 0],
        "chroma_offsets": [1, 1], "chroma_filter": 1}
    conversion = {"model": "rgb_identity", "range": "itu_full", "components": {"r": "r", "g": "g", "b": "b", "a": "a"},
        "x_chroma_offset": "midpoint", "y_chroma_offset": "midpoint", "chroma_filter": "linear"}
    # Shape-bounded strict equality distinguishes false/0 and 1.0/1 without
    # serializing an arbitrarily large hostile pure-API profile value.
    for name, wanted in (("observed_vulkan_conversion", expected), ("conversion", conversion)):
        require(exact(metadata.get(name), wanted), "exact conversion profile required")
    return generation, acquisition, pair, capacity


def inspect_uniforms(data, metadata, expected_capture=None):
    generation, acquisition, pair, capacity = original_metadata(metadata, expected_capture)
    control = fields(metadata.get("native_uniform_control"), FIELDS, "native-uniform-control")
    require(control["schema"] == SCHEMA and control["file"] == FILE, "unsupported uniform companion schema/file")
    require(control["capture_id"] == generation["capture_id"], "uniform capture mismatch")
    for name, original in (("image_generation", generation), ("command_generation", acquisition), ("view_generation", acquisition)):
        require(number(control[name], name, True) == original[name], "uniform generation mismatch")
    length = number(control["byte_length"], "uniform length", True, MAX_BYTES)
    require(len(data) == length and probe.digest(data) == recovery.sha(control["sha256"], "uniform sha256"), "uniform length/digest mismatch")
    offset = number(control["staging_offset"], "staging offset")
    require(offset % 4 == 0 and offset >= pair["total_paired_bytes"] and offset <= capacity
            and length <= capacity - offset and number(control["shared_storage_capacity"], "uniform capacity") == capacity,
            "uniform staging tail/capacity differs")
    number(control["selected_draw_ordinal"], "draw ordinal", True)
    pipeline = fields(control["pipeline"], ("handle", "generation", "layout_handle", "layout_generation"), "pipeline")
    state = fields(control["set"], ("handle", "generation", "layout_handle", "layout_generation", "revision", "command_binding_revision"), "set")
    for item in (pipeline, state):
        for name in ("handle", "layout_handle"):
            handle(item[name])
        for name in set(item) - {"handle", "layout_handle"}:
            number(item[name], name, True)
    require(number(control["reobserved_set_revision"], "reobserved set revision", True) == state["revision"]
            and number(control["reobserved_command_binding_revision"], "reobserved command revision", True) == state["command_binding_revision"],
            "reobserved revisions differ")
    stages = control["stages"]
    require(isinstance(stages, list) and len(stages) == 2, "exact native stage pair required")
    for stage, expected in zip(stages, PROFILE):
        fields(stage, ("stage", "module", "generation", "fnv1a64", "bytes", "entry"), "stage")
        handle(stage["module"])
        number(stage["generation"], "stage generation", True)
        require((number(stage["stage"], "stage", maximum=2**32 - 1), stage["fnv1a64"],
                 number(stage["bytes"], "stage bytes", maximum=16384), stage["entry"]) == expected, "pinned native stage profile differs")
    require(stages[0]["module"] != stages[1]["module"], "stage module handles overlap")
    guards = fields(control["guards"], GUARDS, "uniform guard")
    require(all(value is True for value in guards.values()), "uniform producer guard incomplete")
    slices = control["slices"]
    require(isinstance(slices, list) and len(slices) == 5, "five ordered full uniform ranges required")
    cursor, contents = 0, {}
    for item, binding, minimum, visible in zip(slices, BINDINGS, MINIMUM, (1, 1, 1, 16, 16)):
        fields(item, SLICE_FIELDS, "slice")
        for name in ("buffer_handle", "allocation_handle"):
            handle(item[name])
        for name in ("buffer_generation", "allocation_generation", "descriptor_revision", "buffer_size", "allocation_size"):
            number(item[name], name, True)
        for name in set(item) - {"buffer_handle", "allocation_handle", "sha256"}:
            number(item[name], name, maximum=2**32 - 1 if name in ("binding", "descriptor_type", "descriptor_stages", "buffer_usage", "dynamic_offset") else 2**64 - 1)
        require(item["binding"] == binding and item["descriptor_type"] in (6, 8)
                and item["descriptor_stages"] & visible
                and (item["descriptor_stages"] == 0x7fffffff or not item["descriptor_stages"] & ~0x87fff)
                and item["descriptor_revision"] <= state["revision"], "uniform descriptor profile differs")
        require(item["buffer_usage"] & 17 == 17, "source needs actual UNIFORM_BUFFER and TRANSFER_SRC usage")
        memory, size = item["memory_offset"], item["buffer_size"]
        base, dynamic = item["descriptor_offset"], item["dynamic_offset"]
        require(memory <= item["allocation_size"] and size <= item["allocation_size"] - memory
                and base < size and base + dynamic < 2**64 and item["effective_offset"] == base + dynamic, "source allocation/offset bounds differ")
        require(item["descriptor_type"] == 8 or dynamic == 0, "non-dynamic descriptor has a dynamic offset")
        declared = item["descriptor_range"]
        require(declared != 2**64 - 1 or dynamic == 0, "WHOLE_SIZE cannot have a dynamic offset")
        count = size - base if declared == 2**64 - 1 else declared
        require(minimum <= count <= MAX_SLICE and count == item["effective_range"] == item["byte_length"]
                and item["effective_offset"] <= size and count <= size - item["effective_offset"]
                and not any(value % 4 for value in (base, dynamic, count, item["file_offset"]))
                and item["file_offset"] == cursor and count <= length - cursor, "full uniform range/contiguous file bounds differ")
        content = data[cursor:cursor + count]
        require(probe.digest(content) == recovery.sha(item["sha256"], "slice sha256"), "uniform slice digest mismatch")
        contents[binding] = content
        cursor += count
    require(cursor == length, "uniform file has unclaimed trailing bytes")
    word = lambda binding, offset: struct.unpack_from("<I", contents[binding], offset)[0]
    eye_v, eye_f = word(0, 256), word(4, 256)
    require(eye_v in (0, 1) and eye_f in (0, 1), "unsupported raw native selector; no eye mapping inferred")
    matrix_offset = 432 + 448 * eye_f
    matrix = struct.unpack_from("<16I", contents[6], matrix_offset)
    floating = lambda index: struct.unpack("<f", struct.pack("<I", matrix[index]))[0]
    r0, r1, patch = ([floating(index) for index in indices] for indices in ((0, 4, 8, 12), (1, 5, 9, 13), (11, 15)))
    require(all(math.isfinite(value) for value in r0 + r1 + patch), "nonfinite reconstruction rectangle/patch")
    return {"schema": "frame-native-uniform-recovery-v1", "uniform_bytes_valid": True,
        "validation_scope": "uniform bytes/digests and recorded metadata agreement; producer guards are assertions, not independent native proof",
        "capture_id": generation["capture_id"], "command_generation": acquisition["command_generation"],
        "image_generation": generation["image_generation"], "view_generation": acquisition["view_generation"],
        "uniform_sha256": probe.digest(data), "byte_length": length, "producer_guard_assertions": dict(guards),
        "paired_pixel_bytes_validated": False, "snapshot_sha256": metadata["sha256"], "sampler_sha256": pair["sha256"],
        "diagnostic_stage_profile": stages, "stage_fingerprints_are_authentication": False,
        "raw_words": {"vertex_selector": eye_v, "fragment_selector": eye_f, "projection": word(0, 264),
            "current_metadata_slot": word(2, 0), "fallback": word(3, 384), "consistency": word(3, 388), "depth": word(3, 392)},
        "selectors_agree": eye_v == eye_f, "selected_metadata_slot": None,
        "selected_metadata_slot_resolved": False, "physical_eye": None,
        "fragment_record_zero": {"matrix_byte_offset": matrix_offset, "raw_u32_hex": [f"{value:08x}" for value in matrix],
            "R0": r0, "R1": r1, "patch_origin": patch, "flags_u32": matrix[3],
            "rectangles_nondegenerate": all(rect[2] > rect[0] and rect[3] > rect[1] for rect in (r0, r1))},
        "native_draw_coherence_proven": False, "native_frame_association_proven": False,
        "source_frame_association_proven": False, "native_eye_placement_proven": False,
        "native_spatial_mapping": None, "opaque_fallback_required": True, "passthrough_authorized": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("uniforms", "metadata", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-capture-id")
    args = parser.parse_args(argv)
    try:
        metadata, digest = recovery.load_json(args.metadata)
        report = inspect_uniforms(recovery.read_owned(args.uniforms, MAX_BYTES), metadata, args.expected_capture_id)
        report["metadata_sha256"] = digest
        recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"uniform_bytes_valid": True, "raw_selectors": [report["raw_words"]["vertex_selector"],
            report["raw_words"]["fragment_selector"]], "selected_metadata_slot_resolved": False, "passthrough_authorized": False}))
        return 0
    except (probe.ProbeError, OSError, ValueError, TypeError, KeyError, struct.error, OverflowError) as error:
        print("pixel uniform recovery: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
