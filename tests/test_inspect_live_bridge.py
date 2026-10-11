"""Synthetic read-only diagnostics tests; no Valve binaries or live Frame needed."""

from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

TOOL = Path(__file__).parents[1] / "tools" / "inspect_live_bridge.py"
SPEC = importlib.util.spec_from_file_location("live_observer", TOOL)
observer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(observer)

CANARY = "PRIVATE_USERNAME_PASSWORD_192.168.123.9"


def synthetic_elf(build_id=bytes.fromhex("1234567890abcdef")):
    note = struct.pack("<III", 4, len(build_id), 3) + b"GNU\0" + build_id
    note += b"\0" * ((-len(note)) % 4)
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", header, 18, 183)
    struct.pack_into("<Q", header, 32, 64)
    struct.pack_into("<HH", header, 54, 56, 1)
    ph = bytearray(56)
    struct.pack_into("<I", ph, 0, 4)
    struct.pack_into("<Q", ph, 8, 120)
    struct.pack_into("<Q", ph, 32, len(note))
    return bytes(header + ph) + note


class NumericTests(unittest.TestCase):
    def test_original_labels_remain_distinct_even_when_values_equal(self):
        record = observer.numeric_record(
            b"unFrameId=7 m_iPresentNumber=7 frame_id=7 lnFrameId=-2 history_idx=3 fdiff=-4")
        self.assertEqual(record["fields"], {
            "unFrameId": 7, "m_iPresentNumber": 7, "frame_id": 7, "lnFrameId": -2,
            "history_idx": 3, "fdiff": -4})

    def test_unknown_labels_prose_ips_and_embedded_names_do_not_escape(self):
        line = (f"{CANARY} account=987654 ip=192.168.123.9 secret_unFrameId=42 "
                ".frame_id=88 frame_id=5 timestamp=123 app_frame_id=6").encode()
        encoded = json.dumps(observer.numeric_record(line))
        self.assertNotIn(CANARY, encoded)
        self.assertNotIn("987654", encoded)
        self.assertNotIn("192.168", encoded)
        self.assertEqual(json.loads(encoded)["fields"], {"frame_id": 5, "app_frame_id": 6})

    def test_noninteger_or_oversized_identifiers_cannot_be_partially_matched(self):
        for text in (b"frame_id=192.168.1.1", b"frame_id=123abc", b"frame_id=0xff",
                     b"frame_id=1.4", b"frame_id=18446744073709551616",
                     b"frame_id=-9223372036854775809", b"frame_id=" + b"1" * 21):
            with self.subTest(text=text):
                self.assertIsNone(observer.numeric_record(text))

    def test_duplicate_label_is_ambiguous_and_dropped(self):
        self.assertIsNone(observer.numeric_record(b"frame_id=1 frame_id=2 unFrameId=3"))

    def test_line_limits_and_missing_metadata_event(self):
        self.assertIsNone(observer.numeric_record(b"x" * observer.MAX_LINE_BYTES + b" frame_id=1"))
        self.assertEqual(observer.numeric_record(b"No matching metadata unFrameId=9"),
                         {"event": "missing_metadata", "fields": {"unFrameId": 9}})
        self.assertEqual(observer.numeric_record(b"frame_id=3\r")["fields"], {"frame_id": 3})

    def test_fec_retry_values_preserve_separate_namespace(self):
        record = observer.numeric_record(
            b"SVLFEC: substantive retry (166 packets) due to frame skip "
            b"(skipped 463, last sub 462, group 2, expected size 178819)")
        self.assertEqual(record, {"event": "fec_substantive_retry", "namespace": "fec_reassembly",
                                 "fields": {"packets": 166, "skipped": 463, "last sub": 462,
                                            "group": 2, "expected size": 178819}})
        self.assertNotIn("frame_id", record["fields"])

    def test_capture_configuration_has_numeric_extent_without_eye_or_frame_inference(self):
        record = observer.numeric_record(
            b"SVLCodecV4L2::CaptureSetup: New decode width, height, format -> 1152x4608 C01Q")
        self.assertEqual(record, {"event": "decoder_capture_setup",
                                 "namespace": "decoder_configuration", "format": "C01Q",
                                 "fields": {"width": 1152, "height": 4608}})
        self.assertNotIn("eye", record)
        self.assertEqual(observer.numeric_record(b"uCaptureBufferCount -> 16"),
                         {"event": "decoder_capture_buffer_count", "namespace": "decoder_configuration",
                          "fields": {"uCaptureBufferCount": 16}})

    def test_unknown_capture_format_and_private_trailing_data_are_omitted(self):
        for suffix in (CANARY.encode(), b"SECR " + CANARY.encode(), b"C01QSECR"):
            line = b"SVLCodecV4L2::CaptureSetup: New decode width, height, format -> 100x200 " + suffix
            record = observer.numeric_record(line)
            self.assertEqual(record["fields"], {"width": 100, "height": 200})
            self.assertNotIn("format", record)
            self.assertNotIn(CANARY, json.dumps(record))
        self.assertIsNone(observer.numeric_record(
            b"SVLCodecV4L2::CaptureSetup: New decode width, height, format -> 0x0 C01Q"))


class ObservationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / "proc"
        self.logs = self.root / "logs"
        self.proc.mkdir()
        self.logs.mkdir()

    def process(self, pid=123, comm="vrlink", payload=None, executable_name=None):
        directory = self.proc / str(pid)
        directory.mkdir()
        (directory / "comm").write_text(comm + "\n")
        executable = self.root / CANARY / (executable_name or comm)
        executable.parent.mkdir(exist_ok=True)
        executable.write_bytes(synthetic_elf() if payload is None else payload)
        (directory / "exe").symlink_to(executable)
        # These must never be read, regardless of their apparent usefulness.
        for name in ("environ", "cmdline", "maps", "mem"):
            (directory / name).write_text(CANARY)
        return executable

    def inspect(self, **options):
        return observer.inspect(proc_root=self.proc, logs_root=self.logs,
                                unix_ns=lambda: 1700000000000000000, **options)

    def test_process_build_identity_without_paths_or_private_content(self):
        self.process()
        report = self.inspect()
        self.assertEqual(report["live_status"], "vrlink_process_present_stream_unverified")
        self.assertEqual(report["stream_active"], "unverified")
        self.assertFalse(report["association_proven"])
        self.assertFalse(report["mask_activation_safe"])
        executable = report["processes"][0]["executable"]
        self.assertEqual(executable["machine"], 183)
        self.assertEqual(executable["build_id"], "1234567890abcdef")
        self.assertNotIn(CANARY, json.dumps(report))
        self.assertNotIn(str(self.root), json.dumps(report))

    def test_actual_vrlink_thread_comm_alias_is_exact_and_executable_verified(self):
        self.process(comm="vrlinkrunthread", executable_name="vrlink")
        self.process(pid=124, comm="vrlinkrunthreadx", executable_name="vrlink")
        report = self.inspect()
        self.assertEqual(report["live_status"], "vrlink_process_present_stream_unverified")
        self.assertEqual(len(report["processes"]), 1)
        self.assertEqual(report["processes"][0]["comm"], "vrlinkrunthread")
        self.assertEqual(report["processes"][0]["executable"]["name"], "vrlink")

    def test_old_logs_without_process_do_not_invent_an_active_stream(self):
        path = self.logs / "vrlink.txt"
        path.write_text("unFrameId=100 m_iPresentNumber=100\n")
        os.utime(path, ns=(1000000000, 1000000000))
        report = self.inspect()
        self.assertEqual(report["live_status"], "no_vrlink_process_observed")
        self.assertEqual(report["logs"][0]["mtime_ns"], 1000000000)
        self.assertEqual(report["logs"][0]["file_recency"], "older_than_five_minutes")
        self.assertEqual(report["logs"][0]["evidence_status"],
                         "historical_or_non_streaming_no_vrlink_observed")
        self.assertEqual(report["counters"]["numeric_records"], 1)
        self.assertFalse(report["association_proven"])

    def test_fec_noise_does_not_hide_later_association_or_other_log_records(self):
        retry = ("SVLFEC: substantive retry (6 packets) due to frame skip "
                 "(skipped 7, last sub 6, group 1, expected size 1024)\n")
        (self.logs / "vrlink.txt").write_text(
            retry * (observer.MAX_RECORDS + 1) + "unFrameId=42\n")
        (self.logs / "vrcompositor.txt").write_text("present_frame_id=9\n")
        report = self.inspect()
        self.assertEqual(report["counters"]["fec_retry_lines"], observer.MAX_RECORDS + 1)
        self.assertEqual(report["counters"]["fec_retry_records"], observer.MAX_FEC_RECORDS)
        self.assertIn("fec_record_limit", report["warnings"])
        records = [r for log in report["logs"] for r in log["records"]]
        self.assertIn({"event": "numeric_frame_fields", "fields": {"unFrameId": 42}}, records)
        self.assertIn({"event": "numeric_frame_fields", "fields": {"present_frame_id": 9}}, records)

    def test_future_log_clock_is_metadata_not_a_new_frame_association(self):
        path = self.logs / "vrlink.txt"
        path.write_text("frame_id=1\n")
        os.utime(path, ns=(1800000000000000000, 1800000000000000000))
        report = self.inspect()
        self.assertIn("log_mtime_after_snapshot", report["warnings"])
        self.assertEqual(report["live_status"], "no_vrlink_process_observed")

    def test_only_known_files_and_process_metadata_are_read_and_nothing_written(self):
        self.process()
        (self.logs / "vrlink.txt").write_text(f"frame_id=4 password={CANARY}\n")
        (self.logs / "steamvr.vrsettings").write_text(CANARY)
        (self.logs / "unknown_log.txt").write_text("frame_id=99 " + CANARY)
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        calls = []
        real_open = os.open

        def opened(path, flags, *args, **kwargs):
            calls.append((str(path), flags))
            return real_open(path, flags, *args, **kwargs)

        with mock.patch.object(observer.os, "open", side_effect=opened):
            report = self.inspect()
        after = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        for path, flags in calls:
            self.assertEqual(flags & os.O_ACCMODE, os.O_RDONLY)
            self.assertFalse(flags & (os.O_CREAT | os.O_TRUNC | os.O_APPEND))
            self.assertNotIn(Path(path).name, ("environ", "cmdline", "maps", "mem",
                                               "steamvr.vrsettings", "unknown_log.txt"))
        self.assertNotIn(CANARY, json.dumps(report))
        self.assertEqual(report["counters"]["numeric_records"], 1)

    def test_unknown_process_has_no_executable_inspection(self):
        self.process(comm=CANARY)
        report = self.inspect()
        self.assertEqual(report["processes"], [])
        self.assertNotIn(CANARY, json.dumps(report))

    def test_logs_symlinks_and_special_files_are_not_followed_or_blocking(self):
        private = self.root / "private"
        private.write_text("frame_id=999 " + CANARY)
        (self.logs / "vrlink.txt").symlink_to(private)
        os.mkfifo(self.logs / "vrcompositor.txt")
        report = self.inspect()
        self.assertEqual(report["counters"]["numeric_records"], 0)
        self.assertIn("known_log_unavailable_or_not_regular", report["warnings"])

    def test_tail_omits_partial_first_and_unfinished_last_line(self):
        path = self.logs / "vrlink.txt"
        path.write_bytes(b"unFrameId=111\nframe_id=222\nframe_id=333")
        with mock.patch.object(observer, "MAX_TAIL_BYTES", 35):
            report = self.inspect()
        records = report["logs"][0]["records"]
        self.assertEqual(records, [{"event": "numeric_frame_fields", "fields": {"frame_id": 222}}])
        self.assertLessEqual(report["logs"][0]["tail_bytes_read"], 35)

    def test_record_limit_is_global_across_logs(self):
        for name in observer.LOG_NAMES:
            (self.logs / name).write_text("".join(f"frame_id={i}\n" for i in range(200)))
        report = self.inspect()
        self.assertEqual(report["counters"]["numeric_records"], observer.MAX_RECORDS)
        self.assertIn("record_limit", report["warnings"])
        self.assertLessEqual(report["bytes_read"], observer.MAX_READ_BYTES)

    def test_process_count_limit_cannot_claim_a_complete_snapshot(self):
        self.process(pid=1, comm="vrserver")
        self.process(pid=2, comm="vrlink")
        with mock.patch.object(observer, "MAX_PROCESSES", 1):
            report = self.inspect()
        self.assertEqual(len(report["processes"]), 1)
        self.assertFalse(report["process_snapshot_complete"])
        self.assertIn("process_count_limit", report["warnings"])

    def test_directory_scan_limit_cannot_claim_no_vrlink_present(self):
        self.process()
        with mock.patch.object(observer, "MAX_PROC_ENTRIES", 0):
            report = self.inspect()
        self.assertEqual(report["live_status"], "process_snapshot_incomplete")
        self.assertIn("process_scan_limit", report["warnings"])

    def test_observer_and_cli_do_not_consult_environment(self):
        class ForbiddenEnvironment:
            def __getattr__(self, _name):
                raise AssertionError("Environment must not be inspected")

            def __getitem__(self, _key):
                raise AssertionError("Environment must not be inspected")

        with mock.patch.object(observer.os, "environ", ForbiddenEnvironment()):
            with mock.patch.object(observer.os, "getenv", side_effect=AssertionError()):
                output = io.StringIO()
                with mock.patch.object(observer.sys, "stdout", output):
                    # sys.argv and the explicit directory are the only CLI inputs.
                    with mock.patch.object(observer.os, "scandir", return_value=os.scandir(self.proc)):
                        result = observer.main(["--logs-root", str(self.logs)])
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(output.getvalue())["stream_active"], "unverified")

    def test_missing_or_denied_proc_cannot_claim_no_live_stream(self):
        with mock.patch.object(observer.os, "scandir", side_effect=PermissionError(CANARY)):
            report = self.inspect()
        self.assertEqual(report["live_status"], "process_snapshot_incomplete")
        self.assertNotIn(CANARY, json.dumps(report))

    def test_read_budget_is_strict_and_process_status_stays_unverified(self):
        self.process()
        with mock.patch.object(observer, "MAX_READ_BYTES", 8):
            report = self.inspect()
        self.assertLessEqual(report["bytes_read"], 8)
        self.assertIn("read_budget_reached", report["warnings"])
        self.assertEqual(report["live_status"], "process_snapshot_incomplete")

    def test_runtime_limit_returns_partial_safe_json_without_further_reads(self):
        self.process()
        clock = mock.Mock(side_effect=[0, 6, 6])
        report = self.inspect(clock=clock)
        self.assertEqual(report["bytes_read"], 0)
        self.assertIn("runtime_limit", report["warnings"])
        self.assertEqual(report["live_status"], "process_snapshot_incomplete")

    def test_historic_decoder_errors_are_only_counts_never_live_error_claims(self):
        path = self.logs / "vrlink.txt"
        path.write_text(f"OutputSetup failed buffer allocation ENOMEM {CANARY}\n"
                        f"Failed to request output buffers. [12 Cannot allocate memory] {CANARY}\n")
        os.utime(path, ns=(1000000000, 1000000000))
        report = self.inspect()
        self.assertEqual(report["counters"]["decoder_allocation_failure_lines"], 2)
        self.assertEqual(report["counters"]["numeric_records"], 0)
        self.assertEqual(report["logs"][0]["evidence_status"],
                         "historical_or_non_streaming_no_vrlink_observed")
        self.assertNotIn(CANARY, json.dumps(report))

    def test_malformed_elf_cannot_expand_reads_or_leak_payload(self):
        payload = bytearray(synthetic_elf())
        struct.pack_into("<Q", payload, 32, (1 << 64) - 1)
        self.process(payload=bytes(payload) + CANARY.encode())
        report = self.inspect()
        self.assertIn("invalid_read_offset", report["warnings"])
        self.assertNotIn(CANARY, json.dumps(report))
        self.assertIsNone(report["processes"][0]["executable"]["build_id"])

    def test_cli_can_run_as_stdin_without_creating_files_or_bytecache(self):
        (self.logs / "vrlink.txt").write_text("frame_id=5\n")
        before = sorted(str(p.relative_to(self.root)) for p in self.root.rglob("*"))
        result = subprocess.run([sys.executable, "-B", "-", "--logs-root", str(self.logs)],
                                input=TOOL.read_bytes(), capture_output=True, timeout=8,
                                cwd=self.root, check=True)
        report = json.loads(result.stdout)
        self.assertEqual(result.stderr, b"")
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(report["counters"]["numeric_records"], 1)
        self.assertNotIn(str(self.root), result.stdout.decode())
        self.assertEqual(before, sorted(str(p.relative_to(self.root)) for p in self.root.rglob("*")))


if __name__ == "__main__":
    unittest.main()
