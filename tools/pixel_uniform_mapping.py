#!/usr/bin/env python3
"""Bounded offline native lookup algebra; not a live UV/eye/frame/alpha provider.

The reference starts with an explicitly supplied, already reprojected normalized
coordinate q. It does not reproduce the preceding native vertex transforms or
rasterization, infer application rays, or resample a captured RGBA8 image using
an unobserved native sampler state. All equations are original algebra derived
from the two version-pinned shader interfaces; no native instructions are kept.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

import pixel_uniform_recovery as uniform

MAX_POINTS, RECORDS, RECORD_STRIDE = 64, 3, 928
STAGE_SHA256 = (
    "94abc0c24124e8b4a301e7f1df5a4c8c0e8b0eb50e4c6ea78f3521370a6897c5",
    "f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2",
)
DEFAULT_POINTS = ((0., 0.), (.25, .25), (.5, .5), (.75, .75), (1., 1.))


def require(condition, message):
    uniform.require(condition, message)


def f32(value):
    """Round each reference arithmetic result to finite IEEE binary32."""
    require(type(value) in (int, float) and math.isfinite(value), "finite numeric reference value required")
    try:
        result = struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error):
        uniform.recovery.fail("binary32 arithmetic overflow")
    require(math.isfinite(result), "finite binary32 arithmetic required")
    return result


def vector(value, size, name):
    require(type(value) in (tuple, list) and len(value) == size, name + " vector shape differs")
    return [f32(item) for item in value]


def rectangle(value, name):
    result = vector(value, 4, name)
    require(result[2] > result[0] and result[3] > result[1], name + " must be finite and nondegenerate")
    return result


def add(a, b):
    return f32(a + b)


def sub(a, b):
    return f32(a - b)


def mul(a, b):
    return f32(a * b)


def div(a, b):
    require(b != 0., "zero reference divisor")
    return f32(a / b)


def flags_word(value):
    require(type(value) is int and 0 <= value <= 0xffffffff, "raw flags must be uint32, not FLOAT")
    return value >> 16


def decode_components(components, flags):
    """Decode explicit normalized RGB_IDENTITY R/G/B components, not scene RGB.

    The native samples both locations even where one weight is zero. Callers
    supplying component triples own their sampler provenance. No address mode,
    filtering, mip selection or conversion equivalence is inferred here.
    """
    r, g, b = vector(components, 3, "sample components")
    require(all(0. <= item <= 1. for item in (r, g, b)), "sample components outside normalized range")
    if flags_word(flags) & 4:
        co, cg = sub(mul(2., r), 1.), sub(mul(2., b), 1.)
        return [sub(add(g, co), cg), add(g, cg), sub(sub(g, co), cg)]
    u, v = sub(b, f32(.501960814)), sub(r, f32(.501960814))
    return [add(g, mul(f32(1.57480001), v)),
            add(add(g, mul(f32(-.187299997), u)), mul(f32(-.468100011), v)),
            add(g, mul(f32(1.8556), u))]


def attenuation(coordinate, threshold, slope):
    distance = max(abs(sub(mul(2., item), 1.)) for item in coordinate)
    ramp = mul(sub(distance, f32(threshold)), f32(slope))
    return sub(1., min(1., max(0., ramp)))


def lookup_reference(r0, r1, patch_origin, flags, q, first_components=None, second_components=None, tint=None, vertex_r1=None):
    """Return both packed lookups and the native inner/outer weights for q.

    This CPU reference uses binary32 operations without promising device bit
    identity: native floating-point contraction/interpolation remain separate.
    Explicit component triples optionally exercise both decode and blend paths.
    """
    inner, outer = rectangle(r0, "R0"), rectangle(r1, "R1")
    vertex_outer = outer if vertex_r1 is None else rectangle(vertex_r1, "vertex R1")
    patch, source = vector(patch_origin, 2, "patch origin"), vector(q, 2, "q")
    shifted_flags = flags_word(flags)
    outer_span = [sub(outer[i + 2], outer[i]) for i in range(2)]
    inner_span = [sub(inner[i + 2], inner[i]) for i in range(2)]
    vertex_span = [sub(vertex_outer[i + 2], vertex_outer[i]) for i in range(2)]
    first = [add(vertex_outer[i], mul(vertex_span[i], source[i])) for i in range(2)]
    inner_coordinate = [div(sub(first[i], inner[i]), inner_span[i]) for i in range(2)]
    second = [add(patch[i], mul(inner_coordinate[i], outer_span[i])) for i in range(2)]
    normalized = [div(sub(first[i], outer[i]), outer_span[i]) for i in range(2)]
    weight = 0. if shifted_flags & 2 else attenuation(inner_coordinate, .800000012, 5.26315784)
    outer_weight = attenuation(normalized, .949999988, 24.9999866)
    result = {"q_post_reprojection_input": source, "first_packed_lookup": first,
        "second_packed_lookup": second, "normalized_fragment_coordinate": normalized,
        "inner_coordinate": inner_coordinate, "second_sample_weight": weight,
        "first_sample_weight": sub(1., weight), "outer_attenuation": outer_weight,
        "decoder": "ycocg" if shifted_flags & 4 else "ycbcr",
        "inner_reconstruction_disabled": bool(shifted_flags & 2)}
    require((first_components is None) == (second_components is None), "both explicit sample triples are required together")
    require(tint is None or first_components is not None, "tint requires explicit sample triples")
    if first_components is not None:
        first_rgb, second_rgb = decode_components(first_components, flags), decode_components(second_components, flags)
        tint = [1., 1., 1.] if tint is None else vector(tint, 3, "native tint")
        mixed = [add(mul(second_rgb[i], weight), mul(first_rgb[i], sub(1., weight))) for i in range(3)]
        result.update(first_decoded_rgb=first_rgb, second_decoded_rgb=second_rgb,
            blended_rgb=mixed, attenuated_tinted_rgb=[mul(mul(mixed[i], outer_weight), tint[i]) for i in range(3)],
            component_values_are_explicit_inputs=True, native_sampler_equivalence_proven=False)
    return result


def verify_modules(vertex, fragment):
    """Optional exact local file check; never treats a fingerprint as authority."""
    require(type(vertex) is bytes and type(fragment) is bytes, "bounded original module bytes required")
    for data, expected, profile in zip((vertex, fragment), STAGE_SHA256, uniform.PROFILE):
        require(len(data) == profile[2] and uniform.probe.digest(data) == expected, "original shader SHA256/size differs")
    return True


def matrix(content, offset):
    require(type(offset) is int and 0 <= offset <= len(content) and 64 <= len(content) - offset,
            "selected metadata matrix outside full effective range")
    words = struct.unpack_from("<16I", content, offset)
    floating = lambda index: struct.unpack("<f", struct.pack("<I", words[index]))[0]
    r0 = rectangle([floating(i) for i in (0, 4, 8, 12)], "R0")
    r1 = rectangle([floating(i) for i in (1, 5, 9, 13)], "R1")
    patch = vector([floating(i) for i in (11, 15)], 2, "patch origin")
    return {"matrix_byte_offset": offset, "raw_u32_hex": [f"{item:08x}" for item in words],
            "R0": r0, "R1": r1, "patch_origin": patch, "flags_u32": words[3]}


def inspect_mapping(data, metadata, expected_capture=None, points=DEFAULT_POINTS):
    """Analyze bounded recorded bytes; output never grants live mapping rights."""
    parsed = uniform.inspect_uniforms(data, metadata, expected_capture)
    require(type(points) in (tuple, list) and 0 < len(points) <= MAX_POINTS, "one to64 explicit q points required")
    points = [vector(point, 2, "q") for point in points]
    control = metadata["native_uniform_control"]
    slices = {item["binding"]: item for item in control["slices"]}
    contents = {binding: data[item["file_offset"]:item["file_offset"] + item["byte_length"]]
                for binding, item in slices.items()}
    raw = parsed["raw_words"]
    current = raw["current_metadata_slot"]
    # The shader's fixed three-record array is indexed BEFORE the consistency
    # test. An invalid current index cannot be rescued by an in-range fallback.
    require(current < RECORDS, "current metadata slot outside pinned three-record array")
    check_offset = 24 + RECORD_STRIDE * current
    require(check_offset + 4 <= len(contents[2]), "current consistency word outside full effective range")
    actual_check = struct.unpack_from("<I", contents[2], check_offset)[0]
    fallback_used = actual_check != raw["consistency"]
    selected = raw["fallback"] if fallback_used else current
    require(selected < RECORDS, "selected fallback metadata slot outside pinned three-record array")
    vertex_offset = 432 + RECORD_STRIDE * selected + 448 * raw["vertex_selector"]
    fragment_offset = 432 + 448 * raw["fragment_selector"]
    vertex, fragment = matrix(contents[2], vertex_offset), matrix(contents[6], fragment_offset)
    left, right = slices[2], slices[6]
    same_source = all(left[key] == right[key] for key in ("buffer_handle", "buffer_generation", "allocation_handle", "allocation_generation"))
    same_effective_base = same_source and left["effective_offset"] == right["effective_offset"]
    same_selected_location = same_source and left["effective_offset"] + vertex_offset == right["effective_offset"] + fragment_offset
    r1_indices = (1, 5, 9, 13)
    r1_equal = all(vertex["raw_u32_hex"][i] == fragment["raw_u32_hex"][i] for i in r1_indices)
    matrix_equal = vertex["raw_u32_hex"] == fragment["raw_u32_hex"]
    return {"schema": "frame-native-uniform-mapping-reference-v1", "capture_id": parsed["capture_id"],
        "uniform_sha256": parsed["uniform_sha256"], "uniform_bytes_valid": True,
        "semantic_profile": {"vertex_sha256": STAGE_SHA256[0], "fragment_sha256": STAGE_SHA256[1],
            "profile_is_diagnostic_not_authentication": True, "original_module_files_verified": False},
        "selection": {"current_slot": current, "record_check_offset": check_offset,
            "current_record_consistency": actual_check, "expected_consistency": raw["consistency"],
            "fallback_used": fallback_used, "fallback_slot": raw["fallback"], "selected_slot": selected,
            "selected_slot_derived_from_captured_words": True, "selector_words_are_source_frame_ids": False},
        "selectors": [raw["vertex_selector"], raw["fragment_selector"]], "vertex_selected": vertex,
        "fragment_record_zero": fragment, "binding_relation": {"same_buffer_allocation_identity": same_source,
            "same_effective_base": same_effective_base, "same_selected_matrix_location": same_selected_location,
            "selected_r1_bits_equal": r1_equal, "selected_matrix_bits_equal": matrix_equal,
            "descriptor_effective_offsets": [left["effective_offset"], right["effective_offset"]],
            "descriptor_dynamic_offsets": [left["dynamic_offset"], right["dynamic_offset"]],
            "absolute_buffer_matrix_offsets": [left["effective_offset"] + vertex_offset, right["effective_offset"] + fragment_offset]},
        "conditional_r1_cancellation": raw["vertex_selector"] == raw["fragment_selector"] and r1_equal,
        "coordinate_scope": "q is explicitly supplied after native reprojection; no output-eye, app-ray, rasterization or PC composition mapping is derived",
        "reference_arithmetic": "binary32 at each operation; device contraction/interpolation and native texture sampling not reproduced",
        "lookup_references": [lookup_reference(fragment["R0"], fragment["R1"], fragment["patch_origin"], fragment["flags_u32"], point, vertex_r1=vertex["R1"]) for point in points],
        "paired_pixel_bytes_validated": False, "native_sampler_equivalence_proven": False,
        "native_draw_coherence_proven": False, "native_frame_association_proven": False,
        "source_frame_association_proven": False, "native_eye_placement_proven": False,
        "native_spatial_mapping": None, "physical_eye": None, "application_ray_mapping_proven": False,
        "alpha_composition_authorized": False, "opaque_fallback_required": True, "passthrough_authorized": False}


def parse_point(text):
    require(type(text) is str and len(text) <= 128, "q must contain two bounded comma-separated numbers")
    parts = text.split(",")
    require(len(parts) == 2, "q must contain two bounded comma-separated numbers")
    return vector([float(part) for part in parts], 2, "q")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("uniforms", "metadata", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-capture-id")
    parser.add_argument("--q", action="append", default=[])
    parser.add_argument("--vertex-module", type=Path)
    parser.add_argument("--fragment-module", type=Path)
    args = parser.parse_args(argv)
    try:
        require((args.vertex_module is None) == (args.fragment_module is None), "both original shader modules are required together")
        require(len(args.q) <= MAX_POINTS, "too many q points")
        metadata, metadata_digest = uniform.recovery.load_json(args.metadata)
        data = uniform.recovery.read_owned(args.uniforms, uniform.MAX_BYTES)
        points = [parse_point(point) for point in args.q] if args.q else DEFAULT_POINTS
        report = inspect_mapping(data, metadata, args.expected_capture_id, points)
        if args.vertex_module is not None:
            verify_modules(uniform.recovery.read_owned(args.vertex_module, 16384), uniform.recovery.read_owned(args.fragment_module, 16384))
            report["semantic_profile"]["original_module_files_verified"] = True
        report["metadata_sha256"] = metadata_digest
        uniform.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n").encode())
        print(json.dumps({"selected_slot": report["selection"]["selected_slot"], "fallback_used": report["selection"]["fallback_used"],
            "conditional_r1_cancellation": report["conditional_r1_cancellation"], "passthrough_authorized": False}))
        return 0
    except (uniform.probe.ProbeError, OSError, ValueError, TypeError, KeyError, struct.error, OverflowError) as error:
        print("pixel uniform mapping: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
