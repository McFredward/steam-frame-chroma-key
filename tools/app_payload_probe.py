#!/usr/bin/env python3
"""Offline app-owned color/coverage carriage fixture; never authorizes passthrough."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import zlib
from dataclasses import dataclass

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAGIC = b"FAPPROB1"
VERSION = 1
LAYOUT_REVISION = 1
MAX_SOURCE = 2048
MAX_PIXELS = 12_000_000
MAX_PNG_BYTES = 64 * 1024 * 1024
MAX_PNG_CHUNKS = 4096
CELL = 4
BITS_PER_ROW = 64
HEADER_COPIES = 3
MARGIN = 16
HEADER_STRUCT = struct.Struct(">8sHH16sQBBH12H32s32s")
HEADER_BYTES = HEADER_STRUCT.size + 4
HEADER_ROWS = (HEADER_BYTES * 8 + BITS_PER_ROW - 1) // BITS_PER_ROW
HEADER_HEIGHT = HEADER_ROWS * CELL
PLANE_Y = MARGIN + HEADER_COPIES * (HEADER_HEIGHT + 8)


class ProbeError(ValueError):
    pass


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgb: bytes


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bounded_read(path: Path, limit: int) -> bytes:
    """Reject links/devices and read at most the declared local-file limit."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        import stat
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise ProbeError("input is not a bounded regular file")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ProbeError("input exceeds limit")
        return data
    finally:
        os.close(fd)


def write_new(path: Path, data: bytes) -> None:
    """Do not replace existing files, follow links, or create parent directories."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(fd, "wb", closefd=False) as stream:
            stream.write(data)
    finally:
        os.close(fd)


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def encode_png(image: Image) -> bytes:
    check_image(image)
    stride = image.width * 3
    raw = b"".join(b"\0" + image.rgb[y * stride:(y + 1) * stride] for y in range(image.height))
    return (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def check_image(image: Image) -> None:
    if (not 1 <= image.width <= 8192 or not 1 <= image.height <= 8192
            or image.width * image.height > MAX_PIXELS or len(image.rgb) != image.width * image.height * 3):
        raise ProbeError("invalid or excessive RGB extent")


def decode_png(data: bytes) -> Image:
    """Bounded strict RGB8 PNG reader; all PNG filters, no animation/alpha/profile conversion."""
    if len(data) > MAX_PNG_BYTES or not data.startswith(PNG_SIGNATURE):
        raise ProbeError("invalid PNG signature or size")
    offset, compressed, seen, ended = 8, bytearray(), [], False
    width = height = 0
    while offset < len(data):
        if len(seen) >= MAX_PNG_CHUNKS:
            raise ProbeError("too many PNG chunks")
        if offset + 12 > len(data):
            raise ProbeError("truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + size
        if end > len(data):
            raise ProbeError("truncated PNG body")
        body = data[offset + 8:offset + 8 + size]
        if not seen and kind != b"IHDR":
            raise ProbeError("IHDR must be first")
        if zlib.crc32(kind + body) != struct.unpack_from(">I", data, end - 4)[0]:
            raise ProbeError("PNG CRC mismatch")
        if kind == b"IHDR":
            if seen or size != 13:
                raise ProbeError("invalid IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            if (not 1 <= width <= 8192 or not 1 <= height <= 8192 or width * height > MAX_PIXELS
                    or (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0)):
                raise ProbeError("only bounded noninterlaced RGB8 PNG is supported")
        elif kind == b"IDAT":
            if not seen or b"IEND" in seen or (b"IDAT" in seen and seen[-1] != b"IDAT"):
                raise ProbeError("invalid IDAT ordering")
            compressed.extend(body)
        elif kind == b"IEND":
            if size or b"IDAT" not in seen:
                raise ProbeError("invalid IEND")
            ended = True
        elif kind != b"tEXt" or size > 4096:
            # Uninterpreted profiles or transparency can change numeric meaning.
            raise ProbeError("unsupported PNG chunk")
        seen.append(kind)
        offset = end
        if ended:
            break
    if not ended or offset != len(data):
        raise ProbeError("missing IEND or trailing PNG data")
    stride, expected = width * 3, (width * 3 + 1) * height
    inflater = zlib.decompressobj()
    try:
        raw = inflater.decompress(bytes(compressed), expected + 1)
    except zlib.error as error:
        raise ProbeError("invalid PNG compression") from error
    if len(raw) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ProbeError("invalid or excessive PNG decompression")
    result, previous = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if mode > 4:
            raise ProbeError("unsupported PNG filter")
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            up = previous[x]
            upper_left = previous[x - 3] if x >= 3 else 0
            if mode == 1:
                value = left
            elif mode == 2:
                value = up
            elif mode == 3:
                value = (left + up) // 2
            elif mode == 4:
                p = left + up - upper_left
                distances = (abs(p - left), abs(p - up), abs(p - upper_left))
                value = (left, up, upper_left)[distances.index(min(distances))]
            else:
                value = 0
            row[x] = (row[x] + value) & 255
        result.extend(row)
        previous = row
    return Image(width, height, bytes(result))


def layout(width: int, height: int) -> dict:
    if (type(width) is not int or type(height) is not int
            or not 32 <= width <= MAX_SOURCE or not 32 <= height <= MAX_SOURCE):
        raise ProbeError("source extent must be 32..2048 on each axis")
    canvas_width = max(width * 2 + MARGIN * 3, BITS_PER_ROW * CELL * 2 + MARGIN * 2)
    canvas_height = PLANE_Y + height + 112
    return {"canvas": [canvas_width, canvas_height],
            "color": [MARGIN, PLANE_Y, width, height],
            "coverage": [MARGIN * 2 + width, PLANE_Y, width, height]}


def identity(session: str, content_id: int, eye: str, convention: str) -> tuple:
    try:
        nonce = bytes.fromhex(session)
    except (ValueError, TypeError) as error:
        raise ProbeError("session must be exactly 32 hexadecimal digits") from error
    if len(nonce) != 16 or len(session) != 32 or not any(nonce):
        raise ProbeError("session must be a nonzero 128-bit nonce")
    if type(content_id) is not int or not 0 <= content_id < 2**64:
        raise ProbeError("content ID must be an unsigned 64-bit app-owned generation")
    if eye not in ("left", "right") or convention not in ("straight", "premultiplied"):
        raise ProbeError("invalid eye or RGB convention")
    return nonce, content_id, int(eye == "right"), int(convention == "premultiplied")


def header_for(width: int, height: int, color: bytes, coverage: bytes,
               session: str, content_id: int, eye: str, convention: str) -> bytes:
    placement = layout(width, height)
    if len(color) != width * height * 3 or len(coverage) != width * height:
        raise ProbeError("RGB/R8 source length does not match extent")
    nonce, generation, eye_id, mode = identity(session, content_id, eye, convention)
    body = HEADER_STRUCT.pack(MAGIC, VERSION, LAYOUT_REVISION, nonce, generation, eye_id, mode, 0,
                              width, height, *placement["canvas"], *placement["color"], *placement["coverage"],
                              hashlib.sha256(color).digest(), hashlib.sha256(coverage).digest())
    return body + struct.pack(">I", zlib.crc32(body))


def paint(data: bytearray, width: int, x: int, y: int, w: int, h: int, rgb: tuple) -> None:
    row = bytes(rgb) * w
    for yy in range(y, y + h):
        begin = (yy * width + x) * 3
        data[begin:begin + w * 3] = row


def render(width: int, height: int, color: bytes, coverage: bytes,
           session: str, content_id: int, eye: str, convention: str = "straight") -> tuple[Image, dict]:
    header = header_for(width, height, color, coverage, session, content_id, eye, convention)
    placement = layout(width, height)
    cw, ch = placement["canvas"]
    canvas = bytearray(bytes((24, 24, 24)) * (cw * ch))
    # Complementary adjacent cells encode every bit; three complete spatial copies.
    for copy in range(HEADER_COPIES):
        top = MARGIN + copy * (HEADER_HEIGHT + 8)
        for bit in range(HEADER_BYTES * 8):
            on = (header[bit // 8] >> (7 - bit % 8)) & 1
            x = MARGIN + bit % BITS_PER_ROW * CELL * 2
            y = top + bit // BITS_PER_ROW * CELL
            paint(canvas, cw, x, y, CELL, CELL, (255 * on,) * 3)
            paint(canvas, cw, x + CELL, y, CELL, CELL, (255 * (1 - on),) * 3)
    for plane, source in (("color", color), ("coverage", b"".join(bytes((v, v, v)) for v in coverage))):
        x, y, w, h = placement[plane]
        for yy in range(h):
            begin = ((y + yy) * cw + x) * 3
            canvas[begin:begin + w * 3] = source[yy * w * 3:(yy + 1) * w * 3]
        # Coordinate rulers and non-symmetric corner marks outside owned plane pixels.
        for xx in range(0, w, 16):
            paint(canvas, cw, x + xx, y - 4, min(4, w - xx), 3,
                  (255, 96, 32) if plane == "color" else (32, 160, 255))
        for yy in range(0, h, 16):
            paint(canvas, cw, x - 4, y + yy, 3, min(4, h - yy), (240, 240, 240))
        for index, (dx, dy) in enumerate(((-8, -8), (w + 2, -8), (-8, h + 2), (w + 2, h + 2))):
            # Distinct corner keys remain outside the supplied source planes.
            paint(canvas, cw, x + dx, y + dy, 6, 6,
                  (255, 32 + index * 48, 64 if eye == "left" else 224))
    bottom = PLANE_Y + height + 32
    for index, value in enumerate((0, 64, 128, 191, 255)):
        x = MARGIN + index * 96
        paint(canvas, cw, x, bottom, 80, 32, (value,) * 3)
    # Eye-specific spatial grid and app content pattern are independent of the mask.
    for gy in range(3):
        for gx in range(16):
            value = (gx * 17 + gy * 59 + content_id + (83 if eye == "right" else 0)) % 256
            paint(canvas, cw, MARGIN + gx * 24, bottom + 40 + gy * 8, 20, 6,
                  (value, 255 - value, gx * 13))
    image = Image(cw, ch, bytes(canvas))
    manifest = {"schema": "frame-app-payload-reference-v1", "session": session.lower(),
                "content_id": content_id, "eye": eye, "rgb_convention": convention,
                "source_extent": [width, height], "layout_revision": LAYOUT_REVISION,
                "color_numeric_encoding": "uninterpreted_rgb8_codes",
                "coverage_numeric_encoding": "linear_coverage_r8",
                "placements": placement, "header_copies": HEADER_COPIES,
                "color_sha256": digest(color), "coverage_sha256": digest(coverage),
                "canvas_rgb_sha256": digest(image.rgb),
                "coverage_reference_r8": [0, 64, 128, 191, 255],
                "opaque_fallback_required": True, "passthrough_authorized": False,
                "native_frame_identity": None, "native_spatial_mapping": None}
    return image, manifest


def sample_header(image: Image, copy: int) -> bytes:
    result = bytearray(HEADER_BYTES)
    for bit in range(HEADER_BYTES * 8):
        x = MARGIN + bit % BITS_PER_ROW * CELL * 2
        y = MARGIN + copy * (HEADER_HEIGHT + 8) + bit // BITS_PER_ROW * CELL
        levels = []
        for side in (0, 1):
            values = []
            for dy in range(CELL):
                for dx in range(CELL):
                    pos = ((y + dy) * image.width + x + side * CELL + dx) * 3
                    pixel = image.rgb[pos:pos + 3]
                    if len(pixel) != 3 or max(pixel) - min(pixel) > 16:
                        raise ProbeError("header cell lacks grayscale fidelity")
                    values.extend(pixel)
            levels.append(sum(values) / len(values))
        if levels[0] <= 64 and levels[1] >= 191:
            value = 0
        elif levels[0] >= 191 and levels[1] <= 64:
            value = 1
        else:
            raise ProbeError("header cell contrast is ambiguous")
        result[bit // 8] |= value << (7 - bit % 8)
    return bytes(result)


def extract(image: Image, rect: list) -> bytes:
    x, y, width, height = rect
    return b"".join(image.rgb[((y + row) * image.width + x) * 3:
                             ((y + row) * image.width + x + width) * 3] for row in range(height))


def validate_reference(image: Image, session: str, content_id: int, eye: str,
                       source_extent: tuple[int, int], expected_digests: tuple[str, str],
                       convention: str = "straight") -> dict:
    """Strict clean-reference check. Even success never grants native presentation."""
    result = {"schema": "frame-app-payload-validation-v1", "reference_valid": False,
              "opaque_fallback_required": True, "passthrough_authorized": False,
              "native_frame_identity": None, "native_spatial_mapping": None}
    try:
        check_image(image)
        expected = layout(*source_extent)
        identity(session, content_id, eye, convention)
        if (image.width, image.height) != tuple(expected["canvas"]):
            raise ProbeError("canvas extent mismatch or clipping")
        headers = [sample_header(image, copy) for copy in range(HEADER_COPIES)]
        if any(header != headers[0] for header in headers):
            raise ProbeError("redundant header disagreement")
        header = headers[0]
        if zlib.crc32(header[:-4]) != struct.unpack(">I", header[-4:])[0]:
            raise ProbeError("payload header CRC mismatch")
        if (len(expected_digests) != 2 or any(len(value) != 64 or
                any(char not in "0123456789abcdef" for char in value) for value in expected_digests)):
            raise ProbeError("expected immutable source digests are required")
        color = extract(image, expected["color"])
        coverage_rgb = extract(image, expected["coverage"])
        if any(coverage_rgb[i] != coverage_rgb[i + 1] or coverage_rgb[i] != coverage_rgb[i + 2]
               for i in range(0, len(coverage_rgb), 3)):
            raise ProbeError("coverage plane is not exact grayscale")
        coverage = coverage_rgb[::3]
        if (digest(color), digest(coverage)) != tuple(expected_digests):
            raise ProbeError("immutable app source generation changed")
        wanted = header_for(*source_extent, color, coverage, session, content_id, eye, convention)
        if header != wanted:
            raise ProbeError("source identity, plane digest, convention or layout mismatch")
        rebuilt, manifest = render(*source_extent, color, coverage, session, content_id, eye, convention)
        if image.rgb != rebuilt.rgb:
            raise ProbeError("spatial fiducial, reference swatch or canvas changed")
        result.update(reference_valid=True, reason="exact app-owned reference only",
                      manifest=manifest)
    except (ProbeError, ValueError, TypeError, struct.error) as error:
        result["reason"] = str(error)
    return result



def measure_candidate(candidate: Image, reference: Image, session: str, content_id: int,
                      eye: str, source_extent: tuple[int, int], expected_digests: tuple[str, str],
                      convention: str = "straight") -> dict:
    """Offline pixel errors at unchanged source coordinates, never a UV calibration."""
    report = {"schema": "frame-app-payload-measurement-v1", "measurement_valid": False, "header_recovered": False,
              "opaque_fallback_required": True, "passthrough_authorized": False,
              "native_spatial_mapping": None, "coordinate_comparison_only": True}
    valid = validate_reference(reference, session, content_id, eye, source_extent,
                               expected_digests, convention)
    if not valid["reference_valid"]:
        report["reason"] = "trusted reference invalid: " + valid["reason"]
        return report
    try:
        check_image(candidate)
        if (candidate.width, candidate.height) != (reference.width, reference.height):
            raise ProbeError("extent changed; source-coordinate metrics are undefined")
        wanted = sample_header(reference, 0)
        try:
            report["header_recovered"] = all(sample_header(candidate, copy) == wanted
                                             for copy in range(HEADER_COPIES))
        except ProbeError:
            pass
        placement = layout(*source_extent)
        for plane in ("color", "coverage"):
            actual, original = extract(candidate, placement[plane]), extract(reference, placement[plane])
            errors = [abs(a - b) for a, b in zip(actual, original)]
            report[plane + "_rgb_code_error"] = {"mean_absolute": sum(errors) / len(errors),
                                                   "maximum": max(errors)}
        report["measurement_valid"] = True
        report["reason"] = ("source-coordinate measurements only; carriage, composition, "
                            "reprojection and foveation mapping remain unproved")
    except ProbeError as error:
        report["reason"] = str(error)
    return report


def fixture_sources(width: int, height: int, eye: str) -> tuple[bytes, bytes]:
    """Original deterministic source; same RGB can have different true coverage."""
    layout(width, height)
    color, coverage = bytearray(), bytearray()
    levels = (0, 64, 128, 191, 255, 192)  # Last: two 50% layers => 75% opacity, quantized R8.
    for y in range(height):
        for x in range(width):
            band = min(5, x * 6 // width)
            grid = (x // 16 + y // 16) % 2
            # Red/green appear at every opacity; no key color defines coverage.
            color.extend((32 + (x * 173 // width), 224 if grid else 32,
                          (y * 197 // height + (61 if eye == "right" else 0)) % 256))
            coverage.append(levels[band])
    return bytes(color), bytes(coverage)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("generate", "validate", "fixture", "measure"))
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--session", required=True, help="explicit nonzero 128-bit app session, 32 hex digits")
    parser.add_argument("--content-id", type=int, required=True, help="app-owned generation; never a native submit counter")
    parser.add_argument("--eye", choices=("left", "right"), required=True)
    parser.add_argument("--rgb-convention", choices=("straight", "premultiplied"), default="straight")
    parser.add_argument("--color-rgb", type=Path)
    parser.add_argument("--coverage-r8", type=Path)
    parser.add_argument("--png", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--source-manifest", type=Path,
                        help="trusted local generation record, required for validate/measure")
    parser.add_argument("--reference-png", type=Path, help="exact source PNG required for measure")
    args = parser.parse_args(argv)
    try:
        layout(args.width, args.height)
        identity(args.session, args.content_id, args.eye, args.rgb_convention)
        if args.command in ("validate", "measure"):
            try:
                if args.source_manifest is None:
                    raise ProbeError("validate/measure requires a trusted source manifest")
                manifest = json.loads(bounded_read(args.source_manifest, 8192))
                required = {"schema": "frame-app-payload-reference-v1", "session": args.session.lower(),
                            "content_id": args.content_id, "eye": args.eye,
                            "source_extent": [args.width, args.height], "rgb_convention": args.rgb_convention}
                if any(manifest.get(key) != value for key, value in required.items()):
                    raise ProbeError("trusted source manifest identity mismatch")
                image = decode_png(bounded_read(args.png, MAX_PNG_BYTES))
                expected_digests = (manifest.get("color_sha256", ""), manifest.get("coverage_sha256", ""))
                if args.command == "measure":
                    if args.reference_png is None:
                        raise ProbeError("measure requires the exact source PNG")
                    reference = decode_png(bounded_read(args.reference_png, MAX_PNG_BYTES))
                    record = measure_candidate(image, reference, args.session, args.content_id, args.eye,
                                               (args.width, args.height), expected_digests, args.rgb_convention)
                else:
                    record = validate_reference(image, args.session, args.content_id, args.eye,
                                                (args.width, args.height), expected_digests, args.rgb_convention)
            except (ProbeError, OSError, ValueError, TypeError, AttributeError) as error:
                record = {"schema": "frame-app-payload-validation-v1", "reference_valid": False,
                          "opaque_fallback_required": True, "passthrough_authorized": False, "reason": str(error)}
        else:
            if args.command == "fixture":
                if args.color_rgb or args.coverage_r8 or args.rgb_convention != "straight":
                    raise ProbeError("fixture mode uses original straight-RGB source only")
                color, coverage = fixture_sources(args.width, args.height, args.eye)
            else:
                if args.color_rgb is None or args.coverage_r8 is None:
                    raise ProbeError("generate requires both owned RGB and R8 inputs")
                color = bounded_read(args.color_rgb, args.width * args.height * 3)
                coverage = bounded_read(args.coverage_r8, args.width * args.height)
            image, record = render(args.width, args.height, color, coverage, args.session,
                                   args.content_id, args.eye, args.rgb_convention)
            write_new(args.png, encode_png(image))
        write_new(args.record, (json.dumps(record, indent=2, sort_keys=True) + "\n").encode())
        print(json.dumps({"record": str(args.record), "passthrough_authorized": False,
                          "reference_valid": record.get("reference_valid")}, sort_keys=True))
        if args.command == "validate":
            return 0 if record["reference_valid"] else 2
        if args.command == "measure":
            return 0 if record.get("measurement_valid", False) else 2
        return 0
    except (ProbeError, OSError) as error:
        print(f"app payload probe: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
