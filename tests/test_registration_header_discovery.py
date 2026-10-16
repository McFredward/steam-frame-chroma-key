"""Synthetic header fixtures only; these are not authentic dense C# sources."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import registration_header_discovery as reader

probe = reader.probe
SESSION = "0123456789abcdef0123456789abcdef"


def mapping(gray=False):
    return {"schema": reader.registration.MAP_SCHEMA, "kind": "caller_supplied_already_mapped_image",
        "source_canvas": [688, 548], "pixel_encoding": "sampled_g_gray8_codes" if gray else "rgb8_codes",
        "recorded_provenance": "Synthetic explicit identity map; no native mapping proof.",
        "scope": "FITTED_DIAGNOSTIC_ONLY", "points": []}


def paint_headers(image, headers):
    """Paint supplied synthetic packet bytes, without implementing source art."""
    canvas = bytearray(image.rgb)
    for copy_index, header in enumerate(headers):
        for bit in range(len(header) * 8):
            value = (header[bit // 8] >> (7 - bit % 8)) & 1
            x, y = 16 + bit % 64 * 8, 16 + copy_index * 76 + bit // 64 * 4
            for side in (0, 1):
                row = bytes((255 * (value if side == 0 else 1 - value),)) * 12
                for dy in range(4):
                    at = ((y + dy) * 688 + x + side * 4) * 3
                    canvas[at:at + 12] = row
    return probe.Image(688, 548, bytes(canvas))


class DiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.color = bytes((17, 83, 211)) * (320 * 192)
        cls.coverage = bytes((0, 64, 128, 191, 192, 255)) * (320 * 192 // 6)
        cls.image, _ = probe.render(320, 192, cls.color, cls.coverage, SESSION, 7, "left")
        cls.header = probe.sample_header(cls.image, 0)

    def discover(self, image=None, explicit_map=None, session=SESSION, eye="left"):
        return reader.discover_mapped(image or self.image, mapping() if explicit_map is None else explicit_map, session, eye)

    def changed(self, index, value):
        fields = list(probe.HEADER_STRUCT.unpack(self.header[:-4])); fields[index] = value
        body = probe.HEADER_STRUCT.pack(*fields)
        header = body + struct.pack(">I", zlib.crc32(body))
        return paint_headers(self.image, [header] * 3)

    def test_three_crc_checked_headers_discover_only_untrusted_tuple(self):
        result = self.discover()
        self.assertEqual(result["candidate_tuple"], {"session": SESSION, "pair_id": 7, "eye": "left", "phase": 6})
        self.assertEqual(result["discovery_status"], "UNTRUSTED_CANDIDATE")
        self.assertEqual(result["checked_header_copies"], 3)
        self.assertEqual(result["header_declared_color_sha256"], probe.digest(self.color))
        self.assertEqual(result["header_declared_coverage_sha256"], probe.digest(self.coverage))
        self.assertEqual(result["caller_supplied_mapping"], mapping())
        self.assertTrue(result["canonical_regeneration_required"])
        for key, value in reader.registration.authorities().items(): self.assertEqual(result[key], value)
        for key in ("source_reference_valid", "generator_authenticity_proven", "decoded_source_integrity_validated",
                    "mapping_estimated_or_authenticated", "source_pair_id_is_native_video_frame"):
            self.assertIs(result[key], False)

    def test_right_eye_and_exact_uint64_maximum_are_representable(self):
        fields = list(probe.HEADER_STRUCT.unpack(self.header[:-4])); fields[4] = 2**64 - 1; fields[5] = 1
        body = probe.HEADER_STRUCT.pack(*fields)
        header = body + struct.pack(">I", zlib.crc32(body))
        result = self.discover(paint_headers(self.image, [header] * 3), eye="right")
        self.assertEqual(result["candidate_tuple"]["pair_id"], 2**64 - 1)
        self.assertEqual(result["candidate_tuple"]["phase"], 126)
        # The binary Q field itself cannot encode an overflow. Also exercise the
        # explicit parsed-identity guard against hypothetical bad decoder output.
        fields[4] = 2**64
        with self.assertRaises(struct.error): probe.HEADER_STRUCT.pack(*fields)
        decoder = probe.HEADER_STRUCT
        for value in (0, -1, 2**64, True):
            fields[4] = value
            with self.subTest(value=value), mock.patch.object(probe, "HEADER_STRUCT") as patched:
                patched.unpack.return_value = tuple(fields)
                with self.assertRaisesRegex(probe.ProbeError, "positive app-owned uint64"):
                    self.discover(eye="right")
        self.assertIs(probe.HEADER_STRUCT, decoder)

    def test_independent_session_eye_and_canonical_expectations_required(self):
        for session, eye in (("1" * 32, "left"), (SESSION, "right"), (SESSION.upper(), "left"),
                             ("0" * 32, "left"), (True, "left"), (SESSION, "unknown")):
            with self.subTest(session=session, eye=eye), self.assertRaises(probe.ProbeError):
                self.discover(session=session, eye=eye)
        with self.assertRaises(probe.ProbeError): self.discover(self.changed(3, bytes(16)))

    def test_all_pinned_header_fields_not_just_crc_or_tuple(self):
        changes = [(0, b"NOTPROBE"), (1, 2), (2, 2), (4, 0), (5, 2), (6, 1), (7, 1)]
        changes += [(index, value + 1) for index, value in enumerate(reader.HEADER_GEOMETRY, 8)]
        for index, value in changes:
            with self.subTest(field=index), self.assertRaises(probe.ProbeError): self.discover(self.changed(index, value))

    def test_mixed_valid_packets_crc_failures_and_whole_cell_damage_refuse(self):
        other = probe.sample_header(self.changed(4, 8), 0)
        with self.assertRaisesRegex(probe.ProbeError, "mixed redundant"):
            self.discover(paint_headers(self.image, [self.header, other, self.header]))
        bad_crc = self.header[:-1] + bytes((self.header[-1] ^ 1,))
        with self.assertRaisesRegex(probe.ProbeError, "CRC"):
            self.discover(paint_headers(self.image, [bad_crc] * 3))
        canvas = bytearray(self.image.rgb)
        # A corner away from the cell centre must still be checked.
        at = ((16 + 3) * 688 + 16 + 3) * 3; canvas[at:at + 3] = b"\x80\x00\x00"
        with self.assertRaises(probe.ProbeError): self.discover(probe.Image(688, 548, bytes(canvas)))

    def test_declared_hashes_do_not_authenticate_source_planes(self):
        image = self.changed(20, b"\x42" * 32)
        report = self.discover(image)
        self.assertEqual(report["header_declared_color_sha256"], "42" * 32)
        self.assertIs(report["source_reference_valid"], False)
        canvas = bytearray(self.image.rgb); canvas[(300 * 688 + 50) * 3] ^= 127
        report = self.discover(probe.Image(688, 548, bytes(canvas)))
        self.assertIs(report["decoded_source_integrity_validated"], False)

    def test_explicit_bounded_mapping_and_exact_grayscale_required(self):
        for field, value in (("scope", "NATIVE_MAPPING_PROVEN"), ("recorded_provenance", ""),
                             ("source_canvas", [688, True]), ("pixel_encoding", "auto"), ("points", [None])):
            wrong = mapping(); wrong[field] = value
            with self.subTest(field=field), self.assertRaises(probe.ProbeError): self.discover(explicit_map=wrong)
        wrong = mapping(); wrong["new_field"] = True
        with self.assertRaises(probe.ProbeError): self.discover(explicit_map=wrong)
        with self.assertRaises(probe.ProbeError): reader.discover_mapped(self.image, None, SESSION, "left")
        with self.assertRaisesRegex(probe.ProbeError, "replicated grayscale"): self.discover(explicit_map=mapping(True))
        gray = bytes(value for value in self.image.rgb[1::3] for _ in range(3))
        self.assertEqual(self.discover(probe.Image(688, 548, gray), mapping(True))["candidate_tuple"]["pair_id"], 7)
        with self.assertRaises(probe.ProbeError): self.discover(probe.Image(688, 547, self.image.rgb[:688 * 547 * 3]))
        with self.assertRaises(probe.ProbeError): self.discover(probe.Image(688, 548, bytearray(self.image.rgb)))

    def cli_files(self, root):
        png, mapped, output = (root / name for name in ("candidate.png", "map.json", "result.json"))
        png.write_bytes(probe.encode_png(self.image)); mapped.write_text(json.dumps(mapping()))
        args = ["--candidate-png", str(png), "--mapped-record", str(mapped), "--record", str(output),
                "--expected-session", SESSION, "--expected-eye", "left"]
        return png, mapped, output, args

    def test_cli_bounded_safe_inputs_provenance_and_exclusive_output(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary); png, mapped, output, args = self.cli_files(root)
            self.assertEqual(reader.main(args), 0)
            report = json.loads(output.read_text()); original = output.read_bytes()
            self.assertEqual(report["candidate_png_sha256"], probe.digest(png.read_bytes()))
            self.assertEqual(report["mapped_record_sha256"], probe.digest(mapped.read_bytes()))
            self.assertEqual(reader.main(args), 2); self.assertEqual(output.read_bytes(), original)
            output.unlink()
            for victim in (png, mapped):
                target = root / (victim.name + ".target"); victim.rename(target); victim.symlink_to(target)
                self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())
                victim.unlink(); target.rename(victim)
            linked = root / "linked"; linked.symlink_to(root, target_is_directory=True)
            args[1] = str(linked / png.name)
            self.assertEqual(reader.main(args), 2); args[1] = str(png)
            args[5] = str(linked / output.name)
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists()); args[5] = str(output)
            mapped.write_text('{"schema":1,"schema":2}')
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())
            mapped.write_text(json.dumps(mapping()))
            png.write_bytes(probe.PNG_SIGNATURE + b"\0" * 8 + struct.pack(">II", 8192, 8192) + b"\0" * 9)
            with mock.patch.object(probe, "decode_png", side_effect=AssertionError("unbounded PNG decoded")):
                self.assertEqual(reader.main(args), 2)
            with png.open("wb") as stream: stream.truncate(reader.registration.MAX_PNG + 1)
            self.assertEqual(reader.main(args), 2); self.assertFalse(output.exists())

    def test_cli_requires_mapping_not_an_implicit_identity_fit(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            reader.main(["--candidate-png", "unused.png", "--record", "unused.json",
                         "--expected-session", SESSION, "--expected-eye", "left"])
        self.assertEqual(result.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
