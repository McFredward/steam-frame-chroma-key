"""Original offline controls; no native application, transport or headset use."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

SPEC = importlib.util.spec_from_file_location("app_payload_probe", Path(__file__).parents[1] / "tools/app_payload_probe.py")
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)
SESSION = "0123456789abcdef0123456789abcdef"
OTHER = "fedcba9876543210fedcba9876543210"


class AppPayloadProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.width, cls.height = 48, 40
        cls.color, cls.coverage = probe.fixture_sources(cls.width, cls.height, "left")
        cls.image, cls.manifest = probe.render(cls.width, cls.height, cls.color, cls.coverage, SESSION, 7, "left")
        cls.digests = (probe.digest(cls.color), probe.digest(cls.coverage))

    def validate(self, image=None, **kwargs):
        values = dict(session=SESSION, content_id=7, eye="left", source_extent=(self.width, self.height),
                      expected_digests=self.digests)
        values.update(kwargs)
        return probe.validate_reference(image or self.image, **values)

    def changed(self, x, y, rgb):
        data = bytearray(self.image.rgb)
        start = (y * self.image.width + x) * 3
        data[start:start + 3] = bytes(rgb)
        return probe.Image(self.image.width, self.image.height, bytes(data))

    def assertOpaque(self, result):
        self.assertFalse(result["reference_valid"])
        self.assertTrue(result["opaque_fallback_required"])
        self.assertFalse(result["passthrough_authorized"])
        self.assertIsNone(result["native_frame_identity"])
        self.assertIsNone(result["native_spatial_mapping"])

    def test_exact_reference_png_round_trip_never_authorizes_native_output(self):
        decoded = probe.decode_png(probe.encode_png(self.image))
        self.assertEqual(decoded, self.image)
        result = self.validate(decoded)
        self.assertTrue(result["reference_valid"])
        self.assertTrue(result["opaque_fallback_required"])
        self.assertFalse(result["passthrough_authorized"])
        self.assertIsNone(result["native_frame_identity"])

    def test_source_planes_are_separate_and_preserved_exactly(self):
        placement = self.manifest["placements"]
        self.assertEqual(probe.extract(self.image, placement["color"]), self.color)
        rgb = probe.extract(self.image, placement["coverage"])
        self.assertEqual(rgb[::3], self.coverage)
        self.assertEqual(rgb[1::3], self.coverage)
        self.assertEqual(rgb[2::3], self.coverage)
        self.assertEqual(set(self.coverage), {0, 64, 128, 191, 192, 255})

    def test_asymmetric_eyes_have_independent_headers_and_spatial_patterns(self):
        color, coverage = probe.fixture_sources(self.width, self.height, "right")
        right, _ = probe.render(self.width, self.height, color, coverage, SESSION, 7, "right")
        self.assertNotEqual(probe.sample_header(right, 0), probe.sample_header(self.image, 0))
        self.assertNotEqual(color, self.color)
        self.assertEqual(coverage, self.coverage)
        self.assertOpaque(self.validate(right))
        self.assertTrue(probe.validate_reference(right, SESSION, 7, "right", (self.width, self.height),
                                                (probe.digest(color), probe.digest(coverage)))["reference_valid"])

    def test_wrong_identity_convention_or_extent_remains_opaque(self):
        for change in (dict(session=OTHER), dict(content_id=8), dict(eye="right"),
                       dict(convention="premultiplied"), dict(source_extent=(49, 40)),
                       dict(source_extent=(48, 41)), dict(expected_digests=("0" * 64, self.digests[1]))):
            with self.subTest(change=change):
                self.assertOpaque(self.validate(**change))

    def test_reused_app_generation_cannot_replace_original_source(self):
        changed_color = bytes((self.color[0] ^ 1,)) + self.color[1:]
        changed, _ = probe.render(self.width, self.height, changed_color, self.coverage, SESSION, 7, "left")
        self.assertOpaque(self.validate(changed))

    def test_one_header_bit_copy_disagreement_is_rejected(self):
        data = bytearray(self.image.rgb)
        for side in (0, 1):
            for dy in range(probe.CELL):
                for dx in range(probe.CELL):
                    start = ((probe.MARGIN + dy) * self.image.width + probe.MARGIN + side * probe.CELL + dx) * 3
                    data[start:start + 3] = bytes(255 - value for value in data[start:start + 3])
        result = self.validate(probe.Image(self.image.width, self.image.height, bytes(data)))
        self.assertOpaque(result)
        self.assertIn("disagreement", result["reason"])

    def test_all_three_corrupted_header_copies_fail_crc(self):
        data = bytearray(self.image.rgb)
        for copy in range(probe.HEADER_COPIES):
            for side in (0, 1):
                for dy in range(probe.CELL):
                    for dx in range(probe.CELL):
                        y = probe.MARGIN + copy * (probe.HEADER_HEIGHT + 8) + dy
                        start = (y * self.image.width + probe.MARGIN + side * probe.CELL + dx) * 3
                        data[start:start + 3] = bytes(255 - value for value in data[start:start + 3])
        result = self.validate(probe.Image(self.image.width, self.image.height, bytes(data)))
        self.assertOpaque(result)
        self.assertIn("CRC", result["reason"])

    def test_ambiguous_grayscale_header_and_colored_header_reject(self):
        self.assertOpaque(self.validate(self.changed(probe.MARGIN, probe.MARGIN, (127, 127, 127))))
        self.assertOpaque(self.validate(self.changed(probe.MARGIN, probe.MARGIN, (0, 255, 0))))

    def test_reference_rejects_damage_even_when_cell_mean_still_recovers(self):
        original = self.image.rgb[(probe.MARGIN * self.image.width + probe.MARGIN) * 3]
        level = 1 if original == 0 else 254
        changed = self.changed(probe.MARGIN, probe.MARGIN, (level,) * 3)
        self.assertEqual(probe.sample_header(changed, 0), probe.sample_header(self.image, 0))
        self.assertOpaque(self.validate(changed))

    def test_color_coverage_ruler_and_swatches_each_fail_when_damaged(self):
        for x, y in ((16, probe.PLANE_Y), (80, probe.PLANE_Y),
                     (12, probe.PLANE_Y + 16), (16, probe.PLANE_Y + self.height + 32)):
            with self.subTest(x=x, y=y):
                self.assertOpaque(self.validate(self.changed(x, y, (33, 44, 55))))

    def test_clip_rotation_mirror_truncation_reject(self):
        width = self.image.width
        cropped = b"".join(self.image.rgb[y * width * 3:(y * width + width - 1) * 3]
                           for y in range(self.image.height))
        self.assertOpaque(self.validate(probe.Image(width - 1, self.image.height, cropped)))
        mirrored = b"".join(b"".join(self.image.rgb[(y * width + x) * 3:(y * width + x + 1) * 3]
                                     for x in reversed(range(width))) for y in range(self.image.height))
        self.assertOpaque(self.validate(probe.Image(width, self.image.height, mirrored)))
        self.assertOpaque(self.validate(probe.Image(width, self.image.height, self.image.rgb[:-3])))

    def test_bad_source_inputs_and_nonce_reject(self):
        for session in ("0" * 32, "x" * 32, "01", SESSION + "00"):
            with self.subTest(session=session), self.assertRaises(probe.ProbeError):
                probe.render(self.width, self.height, self.color, self.coverage, session, 7, "left")
        for content in (-1, 2**64, True):
            with self.subTest(content=content), self.assertRaises(probe.ProbeError):
                probe.render(self.width, self.height, self.color, self.coverage, SESSION, content, "left")
        for width, height in ((0, 40), (2049, 40), (True, 40)):
            with self.subTest(width=width), self.assertRaises(probe.ProbeError):
                probe.layout(width, height)
        with self.assertRaises(probe.ProbeError):
            probe.render(self.width, self.height, self.color[:-1], self.coverage, SESSION, 7, "left")

    def test_reference_digests_cannot_be_missing(self):
        for digests in ((), ("", ""), (None, "a" * 64), ("A" * 64, "b" * 64)):
            with self.subTest(digests=digests):
                self.assertOpaque(self.validate(expected_digests=digests))

    def test_png_rejects_crc_trailing_truncation_unsupported_and_bomb(self):
        clean = probe.encode_png(self.image)
        corrupted = bytearray(clean)
        corrupted[20] ^= 1
        ihdr = probe.chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
        bomb = (probe.PNG_SIGNATURE + ihdr + probe.chunk(b"IDAT", zlib.compress(b"\0" * 1000))
                + probe.chunk(b"IEND", b""))
        rgba = probe.PNG_SIGNATURE + probe.chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
        before = probe.PNG_SIGNATURE + probe.chunk(b"tEXt", b"abc") + clean[8:]
        for bad in (bytes(corrupted), clean + b"x", clean[:-1], bomb, rgba, before):
            with self.subTest(digest=hashlib.sha256(bad).hexdigest()[:8]), self.assertRaises(probe.ProbeError):
                probe.decode_png(bad)

    def test_png_all_five_filters_decode(self):
        original = bytes(range(30))
        rows, previous = [], bytes(6)
        for mode in range(5):
            row = original[mode * 6:mode * 6 + 6]
            encoded = bytearray()
            for x, value in enumerate(row):
                left, up, upper = row[x - 3] if x >= 3 else 0, previous[x], previous[x - 3] if x >= 3 else 0
                p = left + up - upper
                predictor = (left, up, upper)[(abs(p - left), abs(p - up), abs(p - upper)).index(
                    min(abs(p - left), abs(p - up), abs(p - upper)))]
                delta = (0, left, up, (left + up) // 2, predictor)[mode]
                encoded.append((value - delta) & 255)
            rows.append(bytes((mode,)) + encoded)
            previous = row
        png = (probe.PNG_SIGNATURE + probe.chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 5, 8, 2, 0, 0, 0))
               + probe.chunk(b"IDAT", zlib.compress(b"".join(rows))) + probe.chunk(b"IEND", b""))
        self.assertEqual(probe.decode_png(png), probe.Image(2, 5, original))

    def test_safe_io_does_not_overwrite_follow_links_or_read_devices(self):
        with tempfile.TemporaryDirectory(prefix="app-payload-probe-") as temp:
            root = Path(temp)
            target, link = root / "target", root / "link"
            probe.write_new(target, b"original")
            link.symlink_to(target)
            with self.assertRaises(OSError):
                probe.write_new(target, b"replace")
            with self.assertRaises(OSError):
                probe.write_new(link, b"replace")
            with self.assertRaises(OSError):
                probe.bounded_read(link, 100)
            with self.assertRaises(probe.ProbeError):
                probe.bounded_read(target, 1)
            self.assertEqual(target.read_bytes(), b"original")

    def test_measurement_distinguishes_header_recovery_from_plane_fidelity(self):
        x, y, _, _ = self.manifest["placements"]["coverage"]
        changed = self.changed(x, y, (1, 1, 1))
        result = probe.measure_candidate(changed, self.image, SESSION, 7, "left", (self.width, self.height), self.digests)
        self.assertTrue(result["header_recovered"])
        self.assertTrue(result["measurement_valid"])
        self.assertEqual(result["coverage_rgb_code_error"]["maximum"], 1)
        self.assertGreater(result["coverage_rgb_code_error"]["mean_absolute"], 0)
        self.assertEqual(result["color_rgb_code_error"]["maximum"], 0)
        self.assertTrue(result["opaque_fallback_required"])
        self.assertFalse(result["passthrough_authorized"])
        self.assertIsNone(result["native_spatial_mapping"])

    def test_measurement_does_not_guess_registration_after_resizing(self):
        resized = probe.Image(1, 1, b"\0\0\0")
        result = probe.measure_candidate(resized, self.image, SESSION, 7, "left", (self.width, self.height), self.digests)
        self.assertFalse(result["header_recovered"])
        self.assertFalse(result["measurement_valid"])
        self.assertIn("undefined", result["reason"])
        self.assertNotIn("coverage_rgb_code_error", result)


    def test_png_limits_chunk_count_and_does_not_accept_raw_extra_frames(self):
        ihdr = probe.chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
        raw = b"\0" * 14
        body = probe.chunk(b"IDAT", zlib.compress(raw)) + probe.chunk(b"IEND", b"")
        too_many = probe.PNG_SIGNATURE + ihdr + probe.chunk(b"tEXt", b"") * probe.MAX_PNG_CHUNKS + body
        extra_zlib = (probe.PNG_SIGNATURE + ihdr
                      + probe.chunk(b"IDAT", zlib.compress(raw) + zlib.compress(raw))
                      + probe.chunk(b"IEND", b""))
        for bad in (too_many, extra_zlib):
            with self.assertRaises(probe.ProbeError):
                probe.decode_png(bad)

    def test_cli_measure_reports_numeric_control_without_activation(self):
        with tempfile.TemporaryDirectory(prefix="app-payload-measure-") as temp:
            root = Path(temp)
            png, source, result = root / "left.png", root / "source.json", root / "measurement.json"
            png.write_bytes(probe.encode_png(self.image))
            source.write_text(json.dumps(self.manifest))
            base = ["--width", "48", "--height", "40", "--session", SESSION, "--content-id", "7", "--eye", "left"]
            self.assertEqual(probe.main(["measure", *base, "--png", str(png), "--source-manifest", str(source),
                                         "--reference-png", str(png), "--record", str(result)]), 0)
            record = json.loads(result.read_text())
            self.assertTrue(record["measurement_valid"])
            self.assertFalse(record["passthrough_authorized"])
            self.assertEqual(record["coverage_rgb_code_error"]["mean_absolute"], 0)
            missing = root / "missing-reference.json"
            self.assertEqual(probe.main(["measure", *base, "--png", str(png), "--source-manifest", str(source),
                                         "--record", str(missing)]), 2)

    def test_cli_owned_inputs_generation_and_manifest_validation(self):
        with tempfile.TemporaryDirectory(prefix="app-payload-cli-") as temp:
            root = Path(temp)
            rgb, coverage = root / "color.rgb", root / "coverage.r8"
            rgb.write_bytes(self.color)
            coverage.write_bytes(self.coverage)
            base = ["--width", "48", "--height", "40", "--session", SESSION, "--content-id", "7", "--eye", "left"]
            png, record, check = root / "left.png", root / "source.json", root / "check.json"
            self.assertEqual(probe.main(["generate", *base, "--color-rgb", str(rgb), "--coverage-r8", str(coverage),
                                         "--png", str(png), "--record", str(record)]), 0)
            self.assertEqual(probe.main(["validate", *base, "--png", str(png), "--source-manifest", str(record),
                                         "--record", str(check)]), 0)
            self.assertTrue(json.loads(check.read_text())["reference_valid"])
            stale = root / "stale.json"
            self.assertEqual(probe.main(["validate", *base, "--png", str(png), "--record", str(stale)]), 2)
            self.assertFalse(json.loads(stale.read_text())["reference_valid"])


if __name__ == "__main__":
    unittest.main()
