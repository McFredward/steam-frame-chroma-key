#!/usr/bin/env python3
"""Convert one owned P010 snapshot offline; never enables native passthrough."""
from __future__ import annotations

import argparse
from array import array
from fractions import Fraction
import json
import math
import os
from pathlib import Path
import stat
import struct
import sys
import zlib

import app_payload_probe as probe

MAX_RAW_BYTES = 64 * 1024 * 1024
MAX_JSON_BYTES = 16 * 1024
FORMAT = 1000156013
SCHEMA = "frame-decoded-p010-snapshot-v1"
MAP_SCHEMA = "frame-app-payload-header-map-v1"


def fail(message: str) -> None:
    raise probe.ProbeError(message)


def uint(value, name: str) -> int:
    if type(value) is not int or not 0 <= value < 2**64:
        fail(name + " must be an explicit unsigned 64-bit integer")
    return value


def sha(value, name: str) -> str:
    if (not isinstance(value, str) or len(value) != 64
            or any(char not in "0123456789abcdef" for char in value)):
        fail(name + " must be a lowercase SHA-256 digest")
    return value


def parent_fd(path: Path) -> tuple[int, str]:
    """Walk all directory components without following links, including parents."""
    path = Path(os.path.abspath(path))
    fd = os.open(path.anchor, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for component in path.parts[1:-1]:
            next_fd = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd, path.name
    except BaseException:
        os.close(fd)
        raise


def read_owned(path: Path, limit: int) -> bytes:
    directory, name = parent_fd(path)
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    finally:
        os.close(directory)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            fail("input is not a bounded owned regular file")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(limit + 1)
        after = os.fstat(fd)
        if (len(data) != before.st_size or len(data) > limit
                or (before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            fail("input changed during bounded snapshot read")
        return data
    finally:
        os.close(fd)


def write_owned(path: Path, data: bytes) -> None:
    directory, name = parent_fd(path)
    try:
        fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
    finally:
        os.close(directory)
    try:
        with os.fdopen(fd, "wb", closefd=False) as stream:
            stream.write(data)
    finally:
        os.close(fd)


def load_json(path: Path) -> tuple[dict, str]:
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                fail("duplicate JSON key: " + key)
            result[key] = value
        return result

    def constant(value):
        fail("nonfinite JSON number: " + value)

    raw = read_owned(path, MAX_JSON_BYTES)
    parsed = json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)
    if not isinstance(parsed, dict):
        fail("metadata must be a JSON object")
    return parsed, probe.digest(raw)


def inspect_snapshot(raw: bytes, metadata: dict) -> dict:
    """Require a complete tight-plane byte envelope before interpreting any pixels."""
    if (metadata.get("schema") != SCHEMA or metadata.get("format") != FORMAT
            or type(metadata.get("format")) is not int or metadata.get("immutable") is not True):
        fail("requires an explicitly immutable P010 snapshot and exact format")
    width, height = metadata.get("width"), metadata.get("height")
    if (type(width) is not int or type(height) is not int or not 2 <= width <= 8192
            or not 2 <= height <= 8192 or width % 2 or height % 2
            or width * height > probe.MAX_PIXELS):
        fail("P010 extent must be bounded, positive and even")
    total = width * height * 3
    if (len(raw) != total or total > MAX_RAW_BYTES
            or uint(metadata.get("byte_length"), "byte_length") != total
            or probe.digest(raw) != sha(metadata.get("sha256"), "snapshot sha256")):
        fail("snapshot byte extent or digest mismatch")
    generation = metadata.get("generation")
    if not isinstance(generation, dict):
        fail("explicit snapshot generation and acquisition metadata required")
    capture = generation.get("capture_id")
    if (not isinstance(capture, str) or not 1 <= len(capture) <= 128
            or any(char not in "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-" for char in capture)):
        fail("invalid capture identifier")
    for name in ("image_generation", "acquisition_id", "conversion_generation"):
        uint(generation.get(name), name)
    planes = metadata.get("planes")
    expected = (("Y", 0, width * height * 2), ("UV", width * height * 2, width * height))
    if not isinstance(planes, list) or len(planes) != 2:
        fail("exactly two explicit tight P010 planes required")
    for plane, (name, offset, length) in zip(planes, expected):
        if (not isinstance(plane, dict) or plane.get("name") != name
                or uint(plane.get("offset"), "plane offset") != offset
                or uint(plane.get("length"), "plane length") != length
                or probe.digest(raw[offset:offset + length]) != sha(plane.get("sha256"), "plane sha256")):
            fail("P010 plane offset, extent, order or digest mismatch")
    return {"schema": "frame-decoded-p010-recovery-v1", "raw_snapshot_valid": True,
            "rgb_converted": False, "header_recovered": False, "plane_fidelity_measured": False,
            "format": FORMAT, "extent": [width, height], "byte_length": total,
            "snapshot_sha256": probe.digest(raw), "generation": dict(generation),
            "planes": planes, "recorded_conversion": metadata.get("conversion"),
            "native_frame_association_proven": False, "source_frame_association_proven": False,
            "private_resource_association_proven": False, "eye_placement_proven": False,
            "native_frame_identity": None, "native_spatial_mapping": None,
            "opaque_fallback_required": True, "passthrough_authorized": False}


def conversion_parameters(metadata: dict) -> tuple:
    conversion = metadata.get("conversion")
    if not isinstance(conversion, dict):
        fail("recorded YCbCr conversion metadata missing; raw snapshot remains uninterpreted")
    model = conversion.get("model")
    models = {"ycbcr_709": (0.2126, 0.0722), "ycbcr_601": (0.299, 0.114),
              "ycbcr_2020": (0.2627, 0.0593)}
    if (not isinstance(model, str) or model not in models
            or conversion.get("range") not in ("itu_full", "itu_narrow")):
        fail("unknown or unsupported recorded YCbCr model/range; refusing a guessed conversion")
    components = conversion.get("components")
    if components != {"r": "r", "g": "g", "b": "b", "a": "a"}:
        fail("explicit identity YCbCr component mapping required; swizzles are unsupported")
    for name in ("x_chroma_offset", "y_chroma_offset"):
        if conversion.get(name) not in ("cosited_even", "midpoint"):
            fail("explicit recorded chroma siting required")
    if conversion.get("chroma_filter") not in ("nearest", "linear"):
        fail("explicit supported recorded chroma filter required")
    return models[model]


def convert_p010(raw: bytes, metadata: dict, luma_only: bool = False) -> tuple[probe.Image | None, dict]:
    report = inspect_snapshot(raw, metadata)
    try:
        kr, kb = conversion_parameters(metadata)
        report["color_conversion_unknown"] = False
    except probe.ProbeError as error:
        report["reason"] = str(error)
        report["color_conversion_unknown"] = True
        if not luma_only:
            return None, report
    width, height = metadata["width"], metadata["height"]
    words = array("H")
    words.frombytes(raw)
    if sys.byteorder != "little":
        words.byteswap()
    if any(value & 63 for value in words):
        fail("P010 samples must occupy the high ten bits with zero low-six padding")
    if luma_only:
        # These are normalized stored Y codes, not scene luminance, RGB, a mask,
        # a native conversion, or inferred eye placement. In particular the
        # native RGB_IDENTITY + private shader path supplies no known RGB matrix.
        pixels = bytearray(width * height * 3)
        for index in range(width * height):
            value = ((words[index] >> 6) * 255 + 511) // 1023
            pixels[index * 3:index * 3 + 3] = bytes((value,)) * 3
        image = probe.Image(width, height, bytes(pixels))
        report.update(luma_diagnostic_generated=True, diagnostic_rgb_sha256=probe.digest(image.rgb),
                      diagnostic_encoding="stored P010 high10-bit Y code normalized to 0..255 and replicated; not color-converted RGB",
                      color_conversion_applied=False, native_sampler_equivalence_proven=False,
                      reason="stored-Y grayscale diagnostic only; native color conversion and frame/eye/UV association unproved")
        return image, report
    conversion = metadata["conversion"]
    cw, ch, uv_offset = width // 2, height // 2, width * height
    offsets = [0.5 if conversion[key] == "midpoint" else 0.0
               for key in ("x_chroma_offset", "y_chroma_offset")]

    def axis(size, bound, offset):
        result = []
        for value in range(size):
            position = (value - offset) / 2
            if conversion["chroma_filter"] == "nearest":
                # Explicit offline convention. Native nearest ties may differ;
                # this diagnostic does not certify native sampler equality.
                index = max(0, min(bound - 1, math.ceil(position - 0.5)))
                result.append((index, index, 0.0))
            else:
                left = math.floor(position)
                result.append((max(0, min(bound - 1, left)),
                               max(0, min(bound - 1, left + 1)), position - left))
        return result

    xs, ys = axis(width, cw, offsets[0]), axis(height, ch, offsets[1])
    narrow = conversion["range"] == "itu_narrow"
    y_bias, y_scale, c_scale = (64, 876, 896) if narrow else (0, 1023, 1023)
    r_coeff, b_coeff = 2 * (1 - kr), 2 * (1 - kb)
    g_cb, g_cr = -kb * b_coeff / (1 - kr - kb), -kr * r_coeff / (1 - kr - kb)
    output = bytearray(width * height * 3)

    def code(value):
        return max(0, min(255, math.floor(value * 255 + 0.5)))

    cursor = 0
    for y, (y0, y1, fy) in enumerate(ys):
        row0, row1 = uv_offset + y0 * cw * 2, uv_offset + y1 * cw * 2
        for x, (x0, x1, fx) in enumerate(xs):
            samples = []
            for component in (0, 1):
                a, b = words[row0 + x0 * 2 + component] >> 6, words[row0 + x1 * 2 + component] >> 6
                c, d = words[row1 + x0 * 2 + component] >> 6, words[row1 + x1 * 2 + component] >> 6
                samples.append(((a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy - 512) / c_scale)
            cb, cr = samples
            luma = ((words[y * width + x] >> 6) - y_bias) / y_scale
            output[cursor:cursor + 3] = bytes((code(luma + r_coeff * cr),
                                               code(luma + g_cb * cb + g_cr * cr),
                                               code(luma + b_coeff * cb)))
            cursor += 3
    image = probe.Image(width, height, bytes(output))
    report.update(rgb_converted=True, color_conversion_applied=True,
                  luma_diagnostic_generated=False, rgb_sha256=probe.digest(image.rgb),
                  conversion=dict(conversion),
                  conversion_interpretation="offline BT-matrix RGB8 code conversion; no transfer-function or display transform",
                  chroma_sampling="luma-center coordinates; clamp-to-edge; nearest exact ties choose lower sample",
                  native_sampler_equivalence_proven=False,
                  reason="owned snapshot converted offline; eye layout, native association and UV mapping remain unproved")
    return image, report


def rational(value, name):
    if (not isinstance(value, list) or len(value) != 2
            or type(value[0]) is not int or type(value[1]) is not int
            or abs(value[0]) > 2**31 or not 1 <= value[1] <= 2**31):
        fail(name + " must be a bounded explicit [numerator, denominator] rational")
    return Fraction(*value)


def recover_header(candidate: probe.Image, reference: probe.Image, manifest: dict,
                   mapping: dict, session: str, content_id: int, eye: str) -> dict:
    """Sample only caller-mapped header coordinates, never resize/warp scene planes."""
    result = {"header_recovered": False, "plane_fidelity_measured": False,
              "native_frame_association_proven": False, "native_spatial_mapping": None,
              "source_frame_association_proven": False, "private_resource_association_proven": False,
              "eye_placement_proven": False, "caller_supplied_header_mapping_only": True,
              "opaque_fallback_required": True, "passthrough_authorized": False}
    try:
        if (manifest.get("schema") != "frame-app-payload-reference-v1"
                or manifest.get("session") != session.lower() or manifest.get("content_id") != content_id
                or manifest.get("eye") != eye or type(manifest.get("content_id")) is not int):
            fail("trusted source manifest identity differs from explicit expected identity")
        extent = manifest.get("source_extent", [])
        convention = manifest.get("rgb_convention")
        trusted = probe.validate_reference(reference, session, content_id, eye, tuple(extent),
                                           (manifest.get("color_sha256", ""), manifest.get("coverage_sha256", "")),
                                           convention)
        if not trusted["reference_valid"] or trusted["manifest"] != manifest:
            fail("trusted source reference or exact manifest is invalid")
        probe.check_image(candidate)
        if (set(mapping) != {"schema", "kind", "source_canvas", "receiver_extent", "scale", "offset", "recorded_provenance"}
                or mapping.get("schema") != MAP_SCHEMA or mapping.get("kind") != "axis_aligned_pixel_centers"
                or mapping.get("source_canvas") != [reference.width, reference.height]
                or mapping.get("receiver_extent") != [candidate.width, candidate.height]):
            fail("explicit axis-aligned mapping and exact extents required; no inference is supported")
        provenance = mapping.get("recorded_provenance")
        if not isinstance(provenance, str) or not 1 <= len(provenance) <= 2048:
            fail("mapping must state its explicit recorded provenance")
        scale, offset = mapping.get("scale"), mapping.get("offset")
        if not isinstance(scale, list) or not isinstance(offset, list) or len(scale) != 2 or len(offset) != 2:
            fail("two explicit scale and offset coefficients required")
        sx, sy = (rational(value, "scale") for value in scale)
        ox, oy = (rational(value, "offset") for value in offset)
        if not Fraction(1, 2) <= sx <= 64 or not Fraction(1, 2) <= sy <= 64:
            fail("mapping scale is unsupported or undersamples header cells")

        def pixel(x, y):
            rx = sx * Fraction(2 * x + 1, 2) + ox
            ry = sy * Fraction(2 * y + 1, 2) + oy
            if not (0 <= rx < candidate.width and 0 <= ry < candidate.height):
                fail("mapped header is clipped outside snapshot")
            position = (int(ry) * candidate.width + int(rx)) * 3
            return candidate.rgb[position:position + 3]

        headers = []
        for copy in range(probe.HEADER_COPIES):
            header = bytearray(probe.HEADER_BYTES)
            for bit in range(probe.HEADER_BYTES * 8):
                x = probe.MARGIN + bit % probe.BITS_PER_ROW * probe.CELL * 2
                y = probe.MARGIN + copy * (probe.HEADER_HEIGHT + 8) + bit // probe.BITS_PER_ROW * probe.CELL
                levels = []
                for side in (0, 1):
                    values = []
                    for dy in range(probe.CELL):
                        for dx in range(probe.CELL):
                            rgb = pixel(x + side * probe.CELL + dx, y + dy)
                            if max(rgb) - min(rgb) > 16:
                                fail("mapped header lacks grayscale fidelity")
                            values.extend(rgb)
                    levels.append(sum(values) / len(values))
                if levels[0] <= 64 and levels[1] >= 191:
                    value = 0
                elif levels[0] >= 191 and levels[1] <= 64:
                    value = 1
                else:
                    fail("mapped header contrast is ambiguous")
                header[bit // 8] |= value << (7 - bit % 8)
            headers.append(bytes(header))
        if any(header != headers[0] for header in headers):
            fail("mapped redundant header disagreement")
        if zlib.crc32(headers[0][:-4]) != struct.unpack(">I", headers[0][-4:])[0]:
            fail("mapped payload header CRC mismatch")
        if headers[0] != probe.sample_header(reference, 0):
            fail("mapped header differs from exact trusted source identity and digests")
        result.update(header_recovered=True, recovered_header_sha256=probe.digest(headers[0]),
                      header_copies_verified=probe.HEADER_COPIES, expected_session=session.lower(),
                      expected_content_id=content_id, expected_eye=eye, supplied_header_mapping=mapping,
                      reason="three source headers recovered at caller-supplied coordinates; no native frame/eye/UV association proved")
    except (probe.ProbeError, ValueError, TypeError, KeyError, AttributeError, struct.error) as error:
        result["reason"] = str(error)
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("snapshot", "metadata", "png", "record"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("source-manifest", "reference-png", "mapping"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--expected-session")
    parser.add_argument("--expected-content-id", type=int)
    parser.add_argument("--expected-eye", choices=("left", "right"))
    parser.add_argument("--luma-diagnostic", action="store_true",
                        help="emit normalized stored-Y grayscale only, never infer the native RGB conversion")
    args = parser.parse_args(argv)
    try:
        group = (args.source_manifest, args.reference_png, args.mapping,
                 args.expected_session, args.expected_content_id, args.expected_eye)
        if any(value is not None for value in group) and not all(value is not None for value in group):
            fail("header recovery requires original manifest/PNG, mapping and explicit expected session/content/eye together")
        metadata, metadata_sha = load_json(args.metadata)
        image, report = convert_p010(read_owned(args.snapshot, MAX_RAW_BYTES), metadata, args.luma_diagnostic)
        report["metadata_sha256"] = metadata_sha
        if image is not None:
            if args.mapping is not None:
                manifest, manifest_sha = load_json(args.source_manifest)
                mapping, mapping_sha = load_json(args.mapping)
                reference_bytes = read_owned(args.reference_png, probe.MAX_PNG_BYTES)
                report["header_recovery"] = recover_header(image, probe.decode_png(reference_bytes), manifest,
                                                           mapping, args.expected_session,
                                                           args.expected_content_id, args.expected_eye)
                report["header_recovered"] = report["header_recovery"]["header_recovered"]
                report.update(source_manifest_sha256=manifest_sha, header_mapping_sha256=mapping_sha,
                              source_reference_png_sha256=probe.digest(reference_bytes))
            encoded = probe.encode_png(image)
            report["png_sha256"] = probe.digest(encoded)
            write_owned(args.png, encoded)
        write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode())
        print(json.dumps({"rgb_converted": report["rgb_converted"],
                          "luma_diagnostic_generated": report.get("luma_diagnostic_generated", False),
                          "header_recovered": report["header_recovered"],
                          "native_frame_association_proven": False, "passthrough_authorized": False}))
        return 0 if image is not None and (args.mapping is None or report["header_recovered"]) else 2
    except (probe.ProbeError, OSError, ValueError, TypeError, OverflowError) as error:
        print("pixel payload recovery: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
