"""Original small registration artifacts; no C# generator clone or native pixels."""
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import registration_source_recovery as reader

probe = reader.probe
SESSION = "0123456789abcdef0123456789abcdef"


def fixture(pair=1, eye="left"):
    # Deliberately different original toy art: it is NOT an implementation of
    # the production dense generator. The reader accepts trusted canonical bytes,
    # and never authenticates a generator from a profile string or digest alone.
    color = bytes(value for y in range(192) for x in range(320) for value in
                  ((x * 7 + y * 3) % 256, (x + y * 11) % 256, (x * 5 + y) % 256))
    coverage = bytes((0, 64, 128, 191, 255, 192)[min(5, x * 6 // 320)] for y in range(192) for x in range(320))
    old, manifest = probe.render(320, 192, color, coverage, SESSION, pair, eye)
    canvas = bytearray(value for y in range(548) for x in range(688) for value in
                       (32 + (x * 7 + y * 13) % 192, 32 + (x * 3 + y * 17) % 192, 32 + (x * 11 + y) % 192))
    # Preserve the exact old byte-format header cells and the two supplied planes.
    rects = [[16, 16 + copy * 76, 512, 68] for copy in range(3)]
    rects += [manifest["placements"]["color"], manifest["placements"]["coverage"]]
    for x, y, width, height in rects:
        for row in range(y, y + height):
            at = (row * 688 + x) * 3
            canvas[at:at + width * 3] = old.rgb[at:at + width * 3]
    canvas = bytes(canvas)
    manifest = {key: manifest[key] for key in reader.MANIFEST_FIELDS}
    manifest["canvas_rgb_sha256"] = probe.digest(canvas)
    record = {"schema": reader.SCHEMA, "profile": reader.PROFILE, "profile_revision": 1,
        "session": SESSION, "pair_id": pair, "eye": eye, "phase": (pair - 1) % 128, "engine_frame_cohort": 7,
        "content_id_meaning": "app_owned_pair_id_not_native_video_frame", "color_numeric_encoding": "uninterpreted_rgb8_codes",
        "coverage_numeric_encoding": "linear_coverage_r8", "manifest": manifest, "native_frame_identity": None,
        "native_spatial_mapping": None, "physical_eye_proven": False, "source_frame_association_proven": False,
        "gpu_completion_proven": False, "opaque_fallback_required": True, "passthrough_authorized": False}
    return record, color, coverage, canvas


def mapped(gray=False):
    return {"schema": reader.MAP_SCHEMA, "kind": "caller_supplied_already_mapped_image", "source_canvas": [688, 548],
            "pixel_encoding": "sampled_g_gray8_codes" if gray else "rgb8_codes", "recorded_provenance": "Synthetic explicit identity map only.",
            "scope": "FITTED_DIAGNOSTIC_ONLY", "points": [{"source": [17, 250], "mapped": [18, 250]}]}


class RegistrationReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = fixture()
        cls.other = fixture(2)

    def validate(self, source=None, session=SESSION, pair=1, eye="left"):
        return reader.validate_source(*(source or self.original), session, pair, eye)

    def measure(self, image=None, mapping=None, source=None):
        reference = source or self.original
        return reader.measure_mapped(*reference, SESSION, reference[0]["pair_id"], reference[0]["eye"],
                                     image or probe.Image(688, 548, reference[3]), mapping or mapped())

    def test_source_strict_complete_artifacts_and_old_gate_stays_strict(self):
        report = self.validate()
        self.assertTrue(report["source_reference_valid"])
        self.assertEqual(report["header_copies_verified"], 3)
        self.assertFalse(report["generator_authenticity_proven"])
        record, color, alpha, canvas = self.original
        old = probe.validate_reference(probe.Image(688, 548, canvas), SESSION, 1, "left", (320, 192),
                                       (probe.digest(color), probe.digest(alpha)))
        self.assertFalse(old["reference_valid"])
        for pair in (129, 2**64 - 1):
            source = fixture(pair)
            self.assertTrue(self.validate(source, pair=pair)["source_reference_valid"])

    def test_wrong_stale_unknown_and_noncanonical_tuple_refuses(self):
        for session, pair, eye in ((SESSION.upper(), 1, "left"), ("0" * 32, 1, "left"), (SESSION, 0, "left"),
                                  (SESSION, True, "left"), (SESSION, 2**64, "left"), (SESSION, 2, "left"),
                                  (SESSION, 1, "right"), (SESSION, 1, "unknown")):
            with self.subTest(session=session, pair=pair, eye=eye), self.assertRaises(probe.ProbeError):
                self.validate(session=session, pair=pair, eye=eye)
        record, *planes = self.original
        for key, value in (("schema", "frame-app-payload-reference-v1"), ("profile", "future"), ("phase", 1),
                           ("phase", False), ("profile_revision", True), ("engine_frame_cohort", -1),
                           ("engine_frame_cohort", 2**63), ("content_id_meaning", "native_video_frame")):
            changed = copy.deepcopy(record); changed[key] = value
            with self.subTest(key=key), self.assertRaises(probe.ProbeError):
                self.validate((changed, *planes))

    def test_every_authority_field_and_extended_schema_refuses(self):
        record, *planes = self.original
        for key in ("physical_eye_proven", "source_frame_association_proven", "gpu_completion_proven", "passthrough_authorized"):
            for value in (True, 0):
                changed = copy.deepcopy(record); changed[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(probe.ProbeError):
                    self.validate((changed, *planes))
        for path in ((), ("manifest",), ("manifest", "placements")):
            changed = copy.deepcopy(record); target = changed
            for key in path: target = target[key]
            target["future"] = 1
            with self.assertRaises(probe.ProbeError): self.validate((changed, *planes))
        for key in ("native_frame_identity", "native_spatial_mapping"):
            changed = copy.deepcopy(record); changed[key] = "claimed"
            with self.assertRaises(probe.ProbeError): self.validate((changed, *planes))

    def test_plane_header_and_full_layout_integrity_failures(self):
        record, color, alpha, canvas = self.original
        for index in (1, 2, 3):
            changed = list(self.original); payload = bytearray(changed[index]); payload[-1] ^= 1; changed[index] = bytes(payload)
            with self.assertRaisesRegex(probe.ProbeError, "digest"): self.validate(tuple(changed))
        for field, value in (("source_extent", [320, True]), ("header_copies", True), ("layout_revision", 2),
                             ("coverage_sha256", "A" * 64), ("rgb_convention", "premultiplied")):
            changed = copy.deepcopy(record); changed["manifest"][field] = value
            with self.assertRaises(probe.ProbeError): self.validate((changed, color, alpha, canvas))
        changed = copy.deepcopy(record); changed["manifest"]["placements"]["coverage"][0] += 1
        with self.assertRaisesRegex(probe.ProbeError, "layout"): self.validate((changed, color, alpha, canvas))
        # Rehashing a changed source plane does not rescue its old canvas/header.
        altered_color = bytes([color[0] ^ 1]) + color[1:]
        changed = copy.deepcopy(record); changed["manifest"]["color_sha256"] = probe.digest(altered_color)
        with self.assertRaisesRegex(probe.ProbeError, "plane disagrees"):
            self.validate((changed, altered_color, alpha, canvas))
        for source in (self.original, self.other):
            damaged = bytearray(source[3]); at = (16 * 688 + 16) * 3
            damaged[at:at + 3] = b"\x80" * 3
            changed = copy.deepcopy(source[0]); changed["manifest"]["canvas_rgb_sha256"] = probe.digest(damaged)
            with self.assertRaises(probe.ProbeError):
                reader.validate_source(changed, source[1], source[2], bytes(damaged), SESSION, source[0]["pair_id"], "left")

    def test_exact_mapped_code_agreement_has_no_native_authority(self):
        report = self.measure()
        self.assertTrue(report["header_cohort_recovered"])
        self.assertTrue(report["exact_mapped_code_agreement"])
        self.assertEqual(report["region_code_errors"]["full_canvas"]["samples"], 1131072)
        self.assertEqual(len(report["dense_canvas_grid_code_errors"]), 32)
        self.assertEqual(sum(item["samples"] for item in report["dense_canvas_grid_code_errors"]), 1131072)
        self.assertEqual(report["control_point_residuals"][0]["caller_coordinate_displacement"], [1, 0])
        self.assertEqual(report["measurement_scope"], "FITTED_DIAGNOSTIC_ONLY")
        for key in reader.authorities(): self.assertEqual(report[key], reader.authorities()[key])
        self.assertFalse(report["decoded_source_integrity_validated"])
        self.assertFalse(report["mapping_estimated_or_authenticated"])

    def test_same_headers_damaged_art_only_reports_errors_never_integrity(self):
        damaged = bytearray(self.original[3])
        for x, y in ((50, 280), (400, 280), (680, 540)):
            damaged[(y * 688 + x) * 3] ^= 31
        report = self.measure(probe.Image(688, 548, bytes(damaged)))
        self.assertTrue(report["header_cohort_recovered"])
        self.assertFalse(report["exact_mapped_code_agreement"])
        self.assertFalse(report["decoded_source_integrity_validated"])
        self.assertEqual(report["region_code_errors"]["full_canvas"]["nonzero_code_errors"], 3)
        self.assertEqual(report["region_code_errors"]["full_canvas"]["maximum_absolute_code_error"], 31)

    def test_wrong_and_mixed_header_cohorts_are_refused(self):
        with self.assertRaisesRegex(probe.ProbeError, "exact expected"):
            self.measure(probe.Image(688, 548, self.other[3]))
        mixed = bytearray(self.original[3])
        for row in range(92, 160):
            at = (row * 688 + 16) * 3
            mixed[at:at + 512 * 3] = self.other[3][at:at + 512 * 3]
        with self.assertRaisesRegex(probe.ProbeError, "mixed redundant"):
            self.measure(probe.Image(688, 548, bytes(mixed)))
        # Flip BOTH complementary cells of one full bit: contrast remains good,
        # so the CRC/cohort checks, rather than a weak visual threshold, refuse it.
        mixed = bytearray(self.original[3])
        for copy in range(3):
            for y in range(16 + copy * 76, 20 + copy * 76):
                for x in range(16, 24):
                    at = (y * 688 + x) * 3
                    mixed[at:at + 3] = bytes(255 - value for value in self.original[3][at:at + 3])
        with self.assertRaisesRegex(probe.ProbeError, "CRC"): self.measure(probe.Image(688, 548, bytes(mixed)))

    def test_sampled_green_scope_and_bounded_explicit_map(self):
        canvas = self.original[3]
        gray = bytes(value for i in range(1, len(canvas), 3) for value in (canvas[i],) * 3)
        report = self.measure(probe.Image(688, 548, gray), mapped(True))
        self.assertTrue(report["exact_mapped_code_agreement"])
        self.assertIn("YCbCr/transfer equivalence is unproven", report["numeric_comparison"])
        self.assertEqual(report["region_code_errors"]["full_canvas"]["samples"], 377024)
        for key, value in (("scope", "native_owned"), ("pixel_encoding", "linear_scene_rgb"), ("recorded_provenance", ""),
                           ("points", [{"source": [0, 0], "mapped": [0, 0]}] * 33),
                           ("points", [{"source": [True, 0], "mapped": [0, 0]}]),
                           ("points", [{"source": [0, 0], "mapped": [688, 0]}])):
            contract = mapped(); contract[key] = value
            with self.subTest(key=key), self.assertRaises(probe.ProbeError): self.measure(mapping=contract)
        with self.assertRaises(probe.ProbeError): self.measure(probe.Image(688, 548, canvas), mapped(True))
        with self.assertRaises(probe.ProbeError): self.measure(probe.Image(344, 548, canvas[:344 * 548 * 3]))

    def test_cli_reader_limits_identity_and_no_overwrite(self):
        record, color, alpha, canvas = self.original
        with tempfile.TemporaryDirectory(prefix="registration-reader-") as name:
            root = Path(name)
            for filename, value in (("source.json", json.dumps(record).encode()), ("color", color), ("alpha", alpha), ("canvas", canvas)):
                (root / filename).write_bytes(value)
            args = ["--source-record", str(root / "source.json"), "--color", str(root / "color"), "--coverage", str(root / "alpha"),
                    "--canvas", str(root / "canvas"), "--record", str(root / "result.json"), "--expected-session", SESSION,
                    "--expected-pair", "1", "--expected-eye", "left"]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(reader.main(args), 0)
                before = (root / "result.json").read_bytes()
                self.assertEqual(reader.main(args), 2)
                self.assertEqual(reader.main(args[:-1] + ["right"]), 2)
                self.assertEqual(reader.main(args + ["--candidate-png", "missing"]), 2)
                (root / "color").write_bytes(color + b"x")
                self.assertEqual(reader.main(args), 2)
            self.assertEqual((root / "result.json").read_bytes(), before)
            report = json.loads(before)
            self.assertEqual(report["source_record_sha256"], probe.digest((root / "source.json").read_bytes()))

    def test_cli_mapped_png_extent_limit_and_explicit_contract(self):
        record, color, alpha, canvas = self.original
        with tempfile.TemporaryDirectory(prefix="registration-mapped-cli-") as name:
            root = Path(name)
            for filename, value in (("source.json", json.dumps(record).encode()), ("color", color), ("alpha", alpha),
                                    ("canvas", canvas), ("map.json", json.dumps(mapped()).encode()),
                                    ("mapped.png", probe.encode_png(probe.Image(688, 548, canvas)))):
                (root / filename).write_bytes(value)
            args = ["--source-record", str(root / "source.json"), "--color", str(root / "color"), "--coverage", str(root / "alpha"),
                    "--canvas", str(root / "canvas"), "--expected-session", SESSION, "--expected-pair", "1", "--expected-eye", "left",
                    "--record", str(root / "result.json"), "--mapped-record", str(root / "map.json"), "--candidate-png", str(root / "mapped.png")]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(reader.main(args), 0)
                # Use a fresh output path so these controls cannot accidentally
                # succeed by failing only on the prior no-overwrite check.
                bad_args = list(args); bad_args[bad_args.index("--record") + 1] = str(root / "refused.json")
                (root / "mapped.png").write_bytes(probe.encode_png(probe.Image(2, 2, b"\0" * 12)))
                self.assertEqual(reader.main(bad_args), 2)
                with (root / "mapped.png").open("wb") as stream:
                    stream.truncate(reader.MAX_PNG + 1)
                self.assertEqual(reader.main(bad_args), 2)
                self.assertFalse((root / "refused.json").exists())
            report = json.loads((root / "result.json").read_text())
            self.assertTrue(report["header_cohort_recovered"])
            self.assertEqual(report["measurement_scope"], "FITTED_DIAGNOSTIC_ONLY")
            self.assertFalse(report["decoded_source_integrity_validated"])
            self.assertFalse(report["passthrough_authorized"])
            self.assertEqual(report["mapped_record_sha256"], probe.digest((root / "map.json").read_bytes()))
            contract = mapped(); contract["points"] *= 32
            reader.mapped_contract(contract)  # Exact maximum remains supported.


if __name__ == "__main__":
    unittest.main()
