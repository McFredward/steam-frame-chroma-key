"""Owned synthetic registration/receipt controls; no native generator authority."""
import copy
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_r1_registration as native
from test_registration_canvas_locator import fixture as source_fixture
from test_pixel_sampler_recovery import fixture as paired_fixture

probe, registration = native.probe, native.registration


def record_bytes(r1=(0., 0., 1., 1.)):
    words = [0]*32
    words[:4] = [1, native.ssbo.MAGIC, 1, native.ssbo.RECORD_WORDS]
    values = {"u": [.5, .5], "R1": r1, "R0": [.2, .2, .8, .8], "patch": [0., 0.],
              "tint": [1., 1., 1.], "first_sample": [.5, .5, .5, 1.],
              "second_sample": [.5, .5, .5, 1.], "original_rgba": [.5, .5, .5, 1.]}
    for name, value in values.items():
        first, count = native.ssbo.FIELDS[name]
        words[first:first+count] = struct.unpack("<%dI" % count, struct.pack("<%df" % count, *value))
    return struct.pack("<32I", *words)


def envelope(canvas):
    levels = [round(value*1023/255) for value in canvas[1::3]]
    raw, sampled, meta = paired_fixture(688, 548, levels)
    # This is a synthetic same-generation receipt, not an observed Vulkan run.
    meta["acquisition"].update(one_time_submit=False, retirement_required=True, generation_retired=True)
    record = record_bytes()
    meta["native_ssbo_control"] = {"available": True, "reason": "none", "transaction_reason": 0,
        "file": "ssbo-original-invocation.bin", "bytes": 128, "sha256": probe.digest(record),
        "original_color_expression_preserved": True, "native_visual_result_verified": False,
        "native_frame_association_proven": False, "physical_eye_proven": False, "passthrough_authorized": False,
        "command_generation": 4, "pool_generation": 2, "execution": 3, "paired_execution": 3, "source_executions": 3,
        "source_cohort": {"device": 11, "image": 40, "image_generation": 2, "view": 30,
                          "view_generation": 5, "memory": 20, "memory_generation": 6, "layout": 5, "extent": [688, 548]}}
    return raw, sampled, meta, record


def changed_record(case, data):
    raw, sampled, meta, _ = case
    meta = copy.deepcopy(meta)
    meta["native_ssbo_control"]["sha256"] = probe.digest(data)
    return raw, sampled, meta, data


class NativeR1Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = source_fixture()
        cls.case = envelope(cls.source[3])
        cls.candidate = native.discover(*cls.case)
        cls.temp = tempfile.TemporaryDirectory(prefix="native-r1-owned-")
        cls.source_root = Path(cls.temp.name)
        cls.source_sha = probe.digest(json.dumps(cls.source[0], sort_keys=True).encode())
        pins = {}
        for name in native.locator.SOURCE_FILES:
            path = cls.source_root/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Owned synthetic receipt fixture, not the authentic emitter: "+name)
            pins[name] = probe.digest(path.read_bytes())
        dll = cls.source_root/"RegistrationSourceControls.dll"
        dll.write_bytes(b"Owned .dll-shaped receipt fixture, not an executable/generator")
        identity = cls.candidate["candidate_tuple"]
        files = {name: {"sha256": probe.digest(data), "matches_original_canonical_artifact": True}
                 for name, data in zip(("color.rgb8", "coverage.r8", "canvas.rgb8"), cls.source[1:])}
        files["header.bin"] = {"sha256": cls.candidate["header_sha256"], "matches_original_canonical_artifact": True}
        files["registration.json"] = {"sha256": cls.source_sha, "matches_original_canonical_artifact": True}
        cls.provenance = {"schema": "changing-source-emitter-provenance-confirmation-v1",
            "source_sha256_matches_successful_compile_checkpoint": True, "source_sha256": pins,
            "native_frame_association_proven": False, "passthrough_authorized": False,
            "fixture_dll_sha256": probe.digest(dll.read_bytes()), "actual_invocations": [{
                "command": ["owned-test-fixture", str(dll), "--emit", "unused", "--session", identity["session"],
                            "--pair", str(identity["pair_id"]), "--eye", identity["eye"]], "exit_code": 0, "files": files}]}

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def verify(self, candidate=None, case=None, source=None, provenance=None):
        return native.verify(candidate or self.candidate, *(case or self.case), *(source or self.source)[:1], self.source_sha,
            *(source or self.source)[1:], provenance or self.provenance, self.source_root)

    def test_actual_r1_is_only_chart_and_all_authorities_stay_false(self):
        self.assertEqual(self.candidate["B"], [1., 0., 0., 0., 1., 0.])
        self.assertEqual(self.candidate["footprint"], native.FOOTPRINT)
        self.assertEqual(self.candidate["invocation"]["execution"], 3)
        report = self.verify()
        self.assertTrue(report["source_reference_valid"])
        self.assertEqual(report["candidate_tuple"], self.candidate["candidate_tuple"])
        self.assertEqual([v["incorrect_bits"] for v in report["held_out_markers"]], [0, 0])
        self.assertFalse(report["independent_pc_session_confirmed"])
        self.assertFalse(report["continuous_mapping_proven"])
        for key, value in registration.authorities().items():
            self.assertIs(report[key], value)

    def test_other_encoded_eye_is_read_from_header_not_physical_route(self):
        source = source_fixture(2**64-1, "right")
        report = native.discover(*envelope(source[3]))
        self.assertEqual(report["candidate_tuple"]["eye"], "right")
        self.assertEqual(report["candidate_tuple"]["pair_id"], 2**64-1)
        self.assertFalse(report["physical_eye_proven"])

    def test_no_search_or_other_quarter_fallback(self):
        r1 = [.01, 0., 1., 1.]
        with self.assertRaisesRegex(probe.ProbeError, "at least one receiver texel"):
            native.discover(*changed_record(self.case, record_bytes(r1)))
        # An otherwise valid image elsewhere cannot override the actual chart.
        for r1 in ((0., .5, 1., .5), (0., 0., 1., 1.1), (float("nan"), 0., 1., 1.)):
            with self.subTest(r1=r1), self.assertRaises(probe.ProbeError):
                native.discover(*changed_record(self.case, record_bytes(r1)))

    def test_every_record_header_word_and_nonfinite_field_refuse(self):
        words = list(struct.unpack("<32I", self.case[3]))
        for index, value in ((0, 0), (1, 0), (2, 2), (3, 31), (6, 0x7fc00000), (20, 0x7f800000)):
            changed = words[:]; changed[index] = value
            with self.subTest(index=index), self.assertRaises(probe.ProbeError):
                native.discover(*changed_record(self.case, struct.pack("<32I", *changed)))
        for data in (b"", self.case[3][:-1], self.case[3]+b"\0"):
            with self.assertRaises(probe.ProbeError): native.discover(*changed_record(self.case, data))

    def test_exact_record_cohort_replay_and_receipt_types(self):
        paths = [("execution", 2), ("paired_execution", 2), ("source_executions", 2), ("command_generation", 5),
                 ("pool_generation", 0), ("bytes", True), ("transaction_reason", False), ("sha256", "0"*64),
                 ("available", 1), ("native_visual_result_verified", True), ("physical_eye_proven", True)]
        for key, value in paths:
            raw, sampled, meta, record = self.case; meta = copy.deepcopy(meta); meta["native_ssbo_control"][key] = value
            with self.subTest(key=key), self.assertRaises(probe.ProbeError): native.discover(raw, sampled, meta, record)
        for key, value in (("device", 0), ("image_generation", 3), ("view_generation", 6), ("memory_generation", True), ("extent", [688, True])):
            raw, sampled, meta, record = self.case; meta = copy.deepcopy(meta); meta["native_ssbo_control"]["source_cohort"][key] = value
            with self.subTest(key=key), self.assertRaises(probe.ProbeError): native.discover(raw, sampled, meta, record)

    def test_unknown_source_layout_refuses(self):
        for value in (1, 1234, True):
            raw, sampled, meta, record = self.case
            meta = copy.deepcopy(meta)
            meta["native_ssbo_control"]["source_cohort"]["layout"] = value
            with self.subTest(value=value), self.assertRaises(probe.ProbeError):
                native.discover(raw, sampled, meta, record)

    def test_bytes_and_metadata_changed_after_discovery_refuse(self):
        raw, sampled, meta, record = self.case
        with self.assertRaises(probe.ProbeError): native.discover(raw[:-2], sampled, meta, record)
        with self.assertRaises(probe.ProbeError): native.discover(raw, sampled[:-1], meta, record)
        changed = copy.deepcopy(self.candidate); changed["B"][2] = .001
        with self.assertRaisesRegex(probe.ProbeError, "stale, altered, fitted"):
            self.verify(changed)
        for value in (True, 0):
            changed = copy.deepcopy(self.candidate); changed["physical_eye_proven"] = value
            with self.assertRaises(probe.ProbeError): self.verify(changed)
        changed = copy.deepcopy(self.candidate); changed["invocation"]["execution"] = 4
        with self.assertRaises(probe.ProbeError): self.verify(changed)

    def test_ambiguous_crc_and_mixed_header_copies_refuse(self):
        canvas = bytearray(self.source[3])
        for dy in (1, 2):
            for dx in (1, 2):
                for side in (0, 1):
                    at = ((16+dy)*688+16+side*4+dx)*3; canvas[at:at+3] = bytes((128,))*3
        with self.assertRaisesRegex(probe.ProbeError, "ambiguous"):
            native.discover(*envelope(bytes(canvas)))
        altered = source_fixture(8)[3]
        canvas = bytearray(self.source[3])
        for row in range(168, 236):
            at = (row*688+16)*3; canvas[at:at+512*3] = altered[at:at+512*3]
        with self.assertRaisesRegex(probe.ProbeError, "mixed"):
            native.discover(*envelope(bytes(canvas)))
        # Complementary bit flipped in all copies retains contrast but breaks CRC.
        canvas = bytearray(self.source[3])
        for top in (16, 92, 168):
            for side in (0, 1): probe.paint(canvas, 688, 16+side*4, top, 4, 4, (255*(1-side),)*3)
        with self.assertRaisesRegex(probe.ProbeError, "CRC"):
            native.discover(*envelope(bytes(canvas)))

    def test_interior_footprint_is_explicit_and_strict4x4_reader_unchanged(self):
        canvas = bytearray(self.source[3])
        # First magic bit is zero. Blur only its dark border; central2x2 stays0.
        for dy in range(4):
            for dx in range(4):
                if dx in (1, 2) and dy in (1, 2): continue
                at = ((16+dy)*688+16+dx)*3; canvas[at:at+3] = bytes((128,))*3
        with self.assertRaisesRegex(probe.ProbeError, "ambiguous"):
            probe.sample_header(probe.Image(688, 548, bytes(canvas)), 0)
        report = native.discover(*envelope(bytes(canvas)))
        self.assertEqual(report["header_sha256"], self.candidate["header_sha256"])
        self.assertEqual(report["footprint"], native.FOOTPRINT)

    def test_canonical_manifest_and_provenance_mutants_refuse(self):
        source = list(self.source); source[0] = copy.deepcopy(source[0]); source[0]["pair_id"] += 1
        with self.assertRaises(probe.ProbeError): self.verify(source=source)
        source = list(self.source); source[1] = b"\0"+source[1][1:]
        with self.assertRaises(probe.ProbeError): self.verify(source=source)
        for mutate in (lambda p: p.update(fixture_dll_sha256="0"*64),
                       lambda p: p["actual_invocations"][0].update(exit_code=1),
                       lambda p: p.update(source_sha256_matches_successful_compile_checkpoint=False)):
            changed = copy.deepcopy(self.provenance); mutate(changed)
            with self.assertRaises(probe.ProbeError): self.verify(provenance=changed)

    def test_cli_refuses_partial_verification_args_and_preserves_output(self):
        with tempfile.TemporaryDirectory(prefix="native-r1-cli-") as temporary:
            root = Path(temporary)
            raw, sampled, meta, record = self.case
            for name, data in (("raw", raw), ("sampled", sampled), ("ssbo", record), ("meta", json.dumps(meta).encode())):
                (root/name).write_bytes(data)
            args = ["--snapshot", str(root/"raw"), "--sampler", str(root/"sampled"), "--metadata", str(root/"meta"),
                    "--ssbo", str(root/"ssbo"), "--record", str(root/"out")]
            with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(native.main(args), 0)
            original = (root/"out").read_bytes()
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(native.main(args+["--source-record", str(root/"source")]), 2)
            self.assertEqual((root/"out").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
