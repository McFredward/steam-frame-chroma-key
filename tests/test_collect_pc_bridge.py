"""Original small fixtures only; no Valve binary or private user data."""

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest import mock


TOOL = Path(__file__).resolve().parents[1] / "tools/collect_pc_bridge.py"
SPEC = importlib.util.spec_from_file_location("pc_bridge_collector", TOOL)
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class PcCollectorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pc-mask-source-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "SteamVR"
        self.root.mkdir()

    def write(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def file(self, report, relative):
        return next(f for f in report["files"] if f["path"] == relative)

    def test_unknown_build_is_metadata_only_and_cannot_enable_masks(self):
        relative = collector.KNOWN_FILES[0]
        payload = b"original fixture\0IVRVirtualDisplay_002\0secret-not-for-report\0"
        self.write(relative, payload)
        report = collector.collect(self.root)
        record = self.file(report, relative)
        self.assertEqual(record["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(record["interface_strings"], ["IVRVirtualDisplay_002"])
        self.assertEqual(record["binary_format"], "unrecognized")
        self.assertFalse(report["source_alpha_verified"])
        self.assertFalse(report["encoder_decoder_identity_verified"])
        serialized = json.dumps(report)
        self.assertNotIn("secret-not-for-report", serialized)
        self.assertNotIn(str(self.root), serialized)

    def test_pe_architecture_metadata_and_version_inventory_cap(self):
        prefix = bytearray(256)
        prefix[:2] = b"MZ"
        struct.pack_into("<I", prefix, 0x3c, 0x80)
        prefix[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<H", prefix, 0x84, 0x8664)
        for version in range(20):
            prefix.extend(("IVRVirtualDisplay_%03d\0" % version).encode("ascii"))
        self.write(collector.KNOWN_FILES[0], prefix)
        with mock.patch.object(collector, "MAX_INTERFACE_VERSIONS", 2):
            record = self.file(collector.collect(self.root), collector.KNOWN_FILES[0])
        self.assertEqual(record["machine"], "x86_64")
        self.assertEqual(len(record["interface_strings"]), 2)
        self.assertTrue(record["interface_inventory_truncated"])

    def test_manifest_redacts_arbitrary_fields_and_version_body(self):
        self.write(collector.KNOWN_FILES[2], json.dumps({
            "name": "vrlink", "redirectsDisplay": True,
            "resourceOnly": False, "alwaysActivate": "SECRET",
            "credential": "SECRET", "user_path": "SECRET",
        }).encode())
        self.write("bin/version.txt", b"PRIVATE VERSION BODY")
        report = collector.collect(self.root)
        self.assertEqual(self.file(report, collector.KNOWN_FILES[2])["manifest_fields"],
                         {"name": "vrlink", "redirectsDisplay": True,
                          "resourceOnly": False})
        self.assertNotIn("SECRET", json.dumps(report))
        self.assertNotIn("PRIVATE VERSION BODY", json.dumps(report))

    def test_optional_copies_private_exact_and_source_unchanged(self):
        payload = b"MZ" + bytes(range(100))
        source = self.write(collector.KNOWN_FILES[0], payload)
        self.write(collector.KNOWN_FILES[2], b'{"name":"vrlink"}')
        private = self.base / "private"
        before = source.stat().st_mtime_ns
        report = collector.collect(self.root, private)
        self.assertEqual((private / collector.KNOWN_FILES[0]).read_bytes(), payload)
        self.assertEqual(source.stat().st_mtime_ns, before)
        self.assertEqual((private / ".gitignore").read_text(), "*\n")
        self.assertTrue(report["private_copies_requested"])
        with self.assertRaises(FileExistsError):
            collector.collect(self.root, private)
        with self.assertRaises(ValueError):
            collector.collect(self.root, self.root / "must-not-write")
        self.assertFalse((self.root / "must-not-write").exists())

    def test_symlink_escape_rejected_without_reading(self):
        outside = self.base / "outside.dll"
        outside.write_bytes(b"DO NOT READ")
        path = self.root / collector.KNOWN_FILES[0]
        path.parent.mkdir(parents=True)
        try:
            path.symlink_to(outside)
        except OSError:
            self.skipTest("symlinks unavailable on this host")
        record = self.file(collector.collect(self.root), collector.KNOWN_FILES[0])
        self.assertEqual(record["status"], "rejected_external_or_symlink")
        self.assertNotIn("sha256", record)

    def test_file_and_total_budgets(self):
        self.write(collector.KNOWN_FILES[0], b"x" * 40)
        self.write(collector.KNOWN_FILES[1], b"y" * 40)
        with mock.patch.object(collector, "MAX_FILE_BYTES", 30):
            report = collector.collect(self.root)
            self.assertEqual(report["inspected_read_bytes"], 0)
            self.assertEqual(self.file(report, collector.KNOWN_FILES[0])["status"],
                             "skipped_size_or_total_budget")
        with mock.patch.object(collector, "MAX_TOTAL_BYTES", 60):
            report = collector.collect(self.root)
            self.assertEqual(report["inspected_read_bytes"], 40)
            self.assertEqual(self.file(report, collector.KNOWN_FILES[1])["status"],
                             "skipped_size_or_total_budget")

    def test_streaming_string_match_crosses_chunk_boundary(self):
        self.write(collector.KNOWN_FILES[0],
                   b"x" * 25 + b"IVRDriverDirectModeComponent_009\0")
        with mock.patch.object(collector, "CHUNK_BYTES", 32):
            report = collector.collect(self.root)
        self.assertEqual(self.file(report, collector.KNOWN_FILES[0])["interface_strings"],
                         ["IVRDriverDirectModeComponent_009"])
        with mock.patch.object(collector, "CHUNK_BYTES", 8):
            report = collector.collect(self.root)
        self.assertEqual(self.file(report, collector.KNOWN_FILES[0])["interface_strings"],
                         ["IVRDriverDirectModeComponent_009"])

    def test_malformed_or_deep_manifest_stays_bounded(self):
        for payload in (b"not json", b"[1,2,3]", b"[" * 3000 + b"]" * 3000):
            with self.subTest(payload_length=len(payload)):
                self.write(collector.KNOWN_FILES[2], payload)
                record = self.file(collector.collect(self.root), collector.KNOWN_FILES[2])
                self.assertEqual(record["manifest_parse"], "unrecognized")

    def test_shader_inventory_bounded_and_does_not_copy_shaders(self):
        self.write("drivers/vrlink/resources/shaders/a.cso", b"PRIVATE SHADER BODY")
        self.write("drivers/vrlink/resources/shaders/b.spv", b"OTHER PRIVATE BODY")
        self.write("drivers/vrlink/resources/shaders/config.json", b"SECRET")
        with mock.patch.object(collector, "MAX_SHADER_FILES", 1):
            report = collector.collect(self.root, self.base / "private")
        self.assertEqual(len(report["shader_inventory"]), 1)
        self.assertTrue(report["warnings"])
        self.assertFalse((self.base / "private/drivers/vrlink/resources/shaders").exists())
        self.assertNotIn("PRIVATE SHADER BODY", json.dumps(report))
        self.assertNotIn("config.json", json.dumps(report))

    def test_no_configuration_registry_environment_or_activation(self):
        self.write("config/steamvr.vrsettings", b"USER SECRET")
        with mock.patch.object(collector.os, "environ", {}), \
                mock.patch.object(collector.subprocess if hasattr(collector, "subprocess")
                                  else subprocess, "run", side_effect=AssertionError):
            report = collector.collect(self.root)
        self.assertNotIn("USER SECRET", json.dumps(report))
        self.assertTrue(all(r["status"] == "missing" for r in report["files"]))

    def test_cli_outputs_parseable_redacted_json(self):
        self.write(collector.KNOWN_FILES[0], b"IVRCompositorSystemInternal_XXX\0")
        completed = subprocess.run([sys.executable, str(TOOL),
                                    "--steamvr-root", str(self.root)],
                                   capture_output=True, text=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = json.loads(completed.stdout)
        self.assertEqual(report["schema"], "mr-pc-mask-source/1")
        self.assertEqual(completed.stderr, "")


if __name__ == "__main__":
    unittest.main()
