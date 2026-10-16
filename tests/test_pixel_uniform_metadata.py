"""Bridge real C++ formatter output to the offline uniform reader; no device."""
import argparse
import contextlib
import copy
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_pixel_uniform_recovery as original

uniform = original.uniform
FIXTURE = None
CAPTURE = "0123456789abcdef0123456789abcdef"


class FormatterBridge(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="uniform-formatter-bridge-")
        cls.directory = Path(cls.temporary.name)
        process = subprocess.run([str(FIXTURE), "--emit", str(cls.directory)],
                                 capture_output=True, text=True, timeout=30)
        if process.returncode:
            raise AssertionError("C++ formatter fixture failed: " + process.stdout + process.stderr)
        cls.emitted = json.loads((cls.directory / "native-uniform-control.json").read_text())
        cls.data = (cls.directory / uniform.FILE).read_bytes()
        cls.oracle_data, cls.metadata = original.fixture(eye=1)
        # Preserve the original paired2x2 envelope. Only the control actually
        # emitted by production append() replaces the hand-written test control.
        cls.metadata["native_uniform_control"] = cls.emitted["native_uniform_control"]
        cls.metadata["generation"]["capture_id"] = CAPTURE
        cls.metadata["sampler_control"]["capture_id"] = CAPTURE
        cls.cpp_stdout = process.stdout

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_production_schema_full_ranges_and_original_payload(self):
        emitted = self.emitted
        control = emitted["native_uniform_control"]
        self.assertIs(emitted["synthetic_fixture"], True)
        self.assertGreater(emitted["cpp_checks"], 0)
        self.assertRegex(self.cpp_stdout, rf"\b{emitted['cpp_checks']} controls passed\b")
        self.assertNotEqual(emitted["core_command_generation"], control["command_generation"])
        self.assertEqual(control["command_generation"], 4)
        self.assertEqual(set(control), set(uniform.FIELDS))
        self.assertEqual(control["capture_id"], CAPTURE)
        self.assertEqual(control["byte_length"], 6544)
        self.assertEqual(control["staging_offset"], 28)
        self.assertEqual(control["shared_storage_capacity"], 65536)
        self.assertEqual([s["byte_length"] for s in control["slices"]], [272, 2800, 400, 272, 2800])
        self.assertEqual([s["binding"] for s in control["slices"]], list(uniform.BINDINGS))
        self.assertEqual(self.data, self.oracle_data)
        self.assertEqual(control["sha256"], uniform.probe.digest(self.data))
        for item in control["slices"]:
            content = self.data[item["file_offset"]:item["file_offset"] + item["byte_length"]]
            self.assertEqual(set(item), set(uniform.SLICE_FIELDS))
            self.assertEqual(item["sha256"], uniform.probe.digest(content))
            for key in ("buffer_handle", "allocation_handle"):
                self.assertRegex(item[key], r"^[0-9a-f]{1,16}$")

    def test_actual_reader_float_offsets_raw_words_and_false_authority(self):
        report = uniform.inspect_uniforms(self.data, self.metadata, CAPTURE)
        self.assertTrue(report["uniform_bytes_valid"])
        self.assertEqual(report["byte_length"], 6544)
        self.assertEqual(report["raw_words"], {"vertex_selector": 1, "fragment_selector": 1,
            "projection": 0x12345678, "current_metadata_slot": 0xffffffff,
            "fallback": 2, "consistency": 7, "depth": 0x3f800000})
        matrix = report["fragment_record_zero"]
        self.assertEqual(matrix["matrix_byte_offset"], 880)
        self.assertEqual(matrix["flags_u32"], 0x7fc00001)
        self.assertEqual(matrix["raw_u32_hex"][3], "7fc00001")
        for key, expected in (("R0", (.1, .2, .8, .9)), ("R1", (.12, .23, .78, .89)), ("patch_origin", (.31, .42))):
            for actual, target in zip(matrix[key], expected):
                self.assertAlmostEqual(actual, target, places=6)
        self.assertEqual([(s["stage"], s["fnv1a64"], s["bytes"], s["entry"]) for s in report["diagnostic_stage_profile"]],
                         list(uniform.PROFILE))
        for key in ("paired_pixel_bytes_validated", "stage_fingerprints_are_authentication", "native_draw_coherence_proven",
                    "native_frame_association_proven", "source_frame_association_proven", "native_eye_placement_proven",
                    "selected_metadata_slot_resolved", "passthrough_authorized"):
            self.assertIs(report[key], False)
        self.assertIs(report["opaque_fallback_required"], True)
        for key in ("selected_metadata_slot", "physical_eye", "native_spatial_mapping"):
            self.assertIsNone(report[key])

    def test_reader_rejects_tampering_with_production_hash_and_metadata(self):
        tampered = bytearray(self.data)
        tampered[256] ^= 1
        with self.assertRaisesRegex(uniform.probe.ProbeError, "digest"):
            uniform.inspect_uniforms(tampered, self.metadata, CAPTURE)
        for section, key, value in (("native_uniform_control", "command_generation", 4004),
                                    ("native_uniform_control", "image_generation", 3),
                                    ("acquisition", "generation_retired", False)):
            changed = copy.deepcopy(self.metadata)
            changed[section][key] = value
            with self.subTest(section=section, key=key), self.assertRaises(uniform.probe.ProbeError):
                uniform.inspect_uniforms(self.data, changed, CAPTURE)
        changed = copy.deepcopy(self.metadata)
        changed["native_uniform_control"]["slices"][2]["sha256"] = "0" * 64
        with self.assertRaisesRegex(uniform.probe.ProbeError, "slice digest"):
            uniform.inspect_uniforms(self.data, changed, CAPTURE)

    def test_real_reader_cli_round_trip_keeps_diagnostic_scope(self):
        metadata_path = self.directory / "paired-snapshot.json"
        report_path = self.directory / "reader-report.json"
        metadata_path.write_text(json.dumps(self.metadata))
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = uniform.main(["--uniforms", str(self.directory / uniform.FILE), "--metadata", str(metadata_path),
                                   "--record", str(report_path), "--expected-capture-id", CAPTURE])
        self.assertEqual(result, 0)
        report = json.loads(report_path.read_text())
        self.assertEqual(report["uniform_sha256"], uniform.probe.digest(self.data))
        self.assertEqual(report["metadata_sha256"], uniform.probe.digest(metadata_path.read_bytes()))
        self.assertFalse(report["paired_pixel_bytes_validated"])
        self.assertFalse(report["native_draw_coherence_proven"])
        self.assertFalse(report["passthrough_authorized"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    arguments, remaining = parser.parse_known_args()
    FIXTURE = arguments.fixture.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
