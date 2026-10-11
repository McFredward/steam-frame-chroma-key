"""Recovery checks using temporary files and mocked service commands only."""
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("session", Path(__file__).resolve().parents[1] / "tools/frame_trace_session.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
PROBE_SPEC = importlib.util.spec_from_file_location("probe", SPEC.origin.replace("frame_trace_session.py", "probe_trace_loader.py"))
PROBE = importlib.util.module_from_spec(PROBE_SPEC)
PROBE_SPEC.loader.exec_module(PROBE)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="frame-trace-recovery-")
        self.root = Path(self.temporary.name)
        self.calls = []
        self.capture_writers = []
        self.session = MODULE.Session(self.root / "home", self.root / "runtime", self.run_command)
        self.session.folder.mkdir(parents=True)
        # Synthetic AArch64 ELF header; never loaded or executed.
        self.library = b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\xb7\x00" + b"\x00" * 44
        (self.session.folder / "libVkLayer_frame_mask_trace.so").write_bytes(self.library)
        self.sha = hashlib.sha256(self.library).hexdigest()

    def tearDown(self):
        for descriptor in self.capture_writers:
            os.close(descriptor)
        self.temporary.cleanup()

    def active_capture(self, extra=b"", **header_changes):
        header = {
            "event": "header", "version": 1, "layer": "VK_LAYER_FRAME_MASK_trace",
            "scope": "passive_pipeline_metadata", "fragment_filter": MODULE.FRAGMENT,
            "limit_bytes": MODULE.CAPTURE_LIMIT, "limit_events": MODULE.CAPTURE_EVENT_LIMIT,
            "binding_metadata_version": 1, "limit_binding_snapshots": 32,
            "binding_arm_required": True, "binding_ready_at_header": False,
            "deadline_seconds": 120,
        }
        header.update(header_changes)
        path = self.session.folder / "capture.jsonl"
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        self.capture_writers.append(descriptor)
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        os.write(descriptor, json.dumps(header).encode() + b"\n" + extra)
        return descriptor

    def binding_session(self):
        self.session.bindings = True
        self.session.manual_rollback = True
        self.session.start(self.sha)

    def assert_arm_refused(self):
        ledger = self.session.folder / "session.jsonl"
        before = ledger.read_bytes()
        with self.assertRaises(RuntimeError):
            self.session.arm_bindings()
        marker = self.session.folder / "bindings-arm.marker"
        self.assertFalse(marker.exists())
        self.assertFalse(marker.is_symlink())
        self.assertEqual(ledger.read_bytes(), before)

    def run_command(self, arguments, **options):
        self.calls.append(arguments)
        if "--quiet" in arguments and MODULE.WATCHDOG + ".service" in arguments:
            return subprocess.CompletedProcess(arguments, 3, "inactive", "")
        if arguments[0] == "systemd-run":
            MODULE.exclusive(self.session.folder / "watchdog-ready", b"ready\n")
        return subprocess.CompletedProcess(arguments, 0, "active", "")

    def test_watchdog_before_activation_and_idempotent_rollback(self):
        self.session.start(self.sha)
        self.assertTrue(self.session.manifest.exists())
        self.assertTrue(str(self.session.dropin).startswith(str(self.root / "runtime")))
        self.assertEqual(self.session.manifest.stat().st_mode & 0o777, 0o600)
        events = [json.loads(line)["event"] for line in (self.session.folder / "session.jsonl").read_text().splitlines()]
        self.assertIn("capture_runtime_active", events)
        self.assertEqual(self.calls[1][0], "systemd-run")
        self.session.rollback()
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        before = list(self.calls)
        self.session.rollback()
        self.assertEqual(before, self.calls)
        self.assertEqual(sum("restart" in command for command in self.calls), 2)

    def test_manual_mode_has_no_timer_and_retains_exact_idempotent_recovery(self):
        self.session.manual_rollback = True
        self.session.start(self.sha)
        self.assertFalse(any(command[0] == "systemd-run" for command in self.calls))
        self.assertFalse((self.session.folder / "watchdog-ready").exists())
        self.assertTrue(self.session.manifest.exists())
        self.assertTrue(self.session.dropin.exists())
        first = json.loads((self.session.folder / "session.jsonl").read_text().splitlines()[0])
        self.assertEqual(first["rollback_mode"], "manual")
        self.assertIsNone(first["limit_seconds"])
        self.assertNotIn(b"FRAME_MASK_TRACE_OUTPUT", self.session.manifest.read_bytes())
        self.assertEqual(sum("restart" in command for command in self.calls), 1)
        self.session.rollback()
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        before = list(self.calls)
        self.session.rollback()
        self.assertEqual(before, self.calls)
        self.assertEqual(sum("restart" in command for command in self.calls), 2)

    def test_manual_start_failure_still_removes_activation_before_native_restart(self):
        self.session.manual_rollback = True
        restarts = 0
        def failing(arguments, **options):
            nonlocal restarts
            result = self.run_command(arguments, **options)
            if "restart" in arguments:
                restarts += 1
                if restarts == 1:
                    raise subprocess.TimeoutExpired(arguments, 45)
                self.assertFalse(self.session.manifest.exists())
                self.assertFalse(self.session.dropin.exists())
            return result
        self.session.run = failing
        with self.assertRaises(subprocess.TimeoutExpired):
            self.session.start(self.sha)
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        self.assertFalse(any(command[0] == "systemd-run" for command in self.calls))

    def test_manual_start_refuses_a_leftover_timed_rollback(self):
        self.session.manual_rollback = True
        def stale(arguments, **options):
            if "--quiet" in arguments and MODULE.WATCHDOG + ".service" in arguments:
                return subprocess.CompletedProcess(arguments, 0, "active", "")
            return self.run_command(arguments, **options)
        self.session.run = stale
        with self.assertRaises(RuntimeError):
            self.session.start(self.sha)
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "armed.json").exists())
        self.assertFalse(any("restart" in command for command in self.calls))

    def test_preexisting_manifest_and_symlink_preserved(self):
        self.session.manifest.parent.mkdir(parents=True)
        self.session.manifest.write_text("unrelated")
        with self.assertRaises(RuntimeError):
            self.session.start(self.sha)
        self.assertEqual(self.session.manifest.read_text(), "unrelated")
        self.assertFalse(self.calls)
        self.session.manifest.unlink()
        self.session.manifest.symlink_to(self.root / "missing")
        with self.assertRaises(RuntimeError):
            self.session.start(self.sha)
        self.assertTrue(self.session.manifest.is_symlink())

    def test_library_mismatch_never_activates(self):
        with self.assertRaises(RuntimeError):
            self.session.start("0" * 64)
        self.assertFalse(self.calls)
        self.assertFalse(self.session.manifest.exists())

    def test_failed_restart_cleans_before_native_restore(self):
        restarts = 0
        def failing(arguments, **options):
            nonlocal restarts
            result = self.run_command(arguments, **options)
            if "restart" in arguments:
                restarts += 1
                if restarts == 1:
                    raise subprocess.TimeoutExpired(arguments, 45)
                self.assertFalse(self.session.manifest.exists())
                self.assertFalse(self.session.dropin.exists())
            return result
        self.session.run = failing
        with self.assertRaises(subprocess.TimeoutExpired):
            self.session.start(self.sha)
        self.assertFalse(self.session.manifest.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())

    def test_changed_file_preserved_but_other_path_removed(self):
        self.session.start(self.sha)
        self.session.dropin.write_text("unrelated change")
        with self.assertRaises(RuntimeError):
            self.session.rollback()
        self.assertFalse(self.session.manifest.exists())
        self.assertEqual(self.session.dropin.read_text(), "unrelated change")
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        self.assertEqual(sum("restart" in command for command in self.calls), 2)

    def test_failed_watchdog_does_not_activate(self):
        def fail(arguments, **options):
            if arguments[0] == "systemd-run":
                raise subprocess.CalledProcessError(1, arguments)
            return self.run_command(arguments, **options)
        self.session.run = fail
        with self.assertRaises(subprocess.CalledProcessError):
            self.session.start(self.sha)
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())

    def test_stale_watchdog_ready_refused(self):
        (self.session.folder / "watchdog-ready").write_text("stale")
        with self.assertRaises(RuntimeError):
            self.session.start(self.sha)
        self.assertFalse(self.calls)

    def test_rollback_finishes_before_delayed_activation(self):
        def completed(arguments, **options):
            result = self.run_command(arguments, **options)
            if arguments[0] == "systemd-run":
                (self.session.folder / "rollback-complete.json").write_text("{}")
            return result
        self.session.run = completed
        with self.assertRaises(RuntimeError):
            self.session.start(self.sha)
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse(any("restart" in command for command in self.calls))

    def test_reload_failure_still_attempts_native_restart(self):
        self.session.start(self.sha)
        def failure(arguments, **options):
            result = self.run_command(arguments, **options)
            if "daemon-reload" in arguments:
                raise subprocess.CalledProcessError(1, arguments)
            if "restart" in arguments:
                self.assertFalse(self.session.manifest.exists())
                self.assertFalse(self.session.dropin.exists())
            return result
        self.session.run = failure
        with self.assertRaises(RuntimeError):
            self.session.rollback()
        self.assertFalse(self.session.manifest.exists())
        self.assertEqual(sum("restart" in command for command in self.calls), 2)
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_full_ledger_cannot_block_recovery(self):
        self.session.start(self.sha)
        def full(*arguments, **options):
            raise OSError(28, "No space left on device")
        self.session.event = full
        self.session.rollback()
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertEqual(sum("restart" in command for command in self.calls), 2)

    def test_default_never_changes_native_loader_logging(self):
        self.assertNotIn(b"VK_LOADER_DEBUG", self.session.contents()[self.session.dropin])

    def test_binding_scope_is_opt_in_and_uses_exact_manual_recovery(self):
        self.assertNotIn(b"FRAME_MASK_TRACE_BINDINGS", self.session.contents()[self.session.dropin])
        self.session.bindings = True
        self.session.manual_rollback = True
        self.session.start(self.sha)
        self.assertIn(b"Environment=FRAME_MASK_TRACE_BINDINGS=1\n", self.session.dropin.read_bytes())
        ledger = [json.loads(line) for line in (self.session.folder / "session.jsonl").read_text().splitlines()]
        scope = next(row for row in ledger if row["event"] == "diagnostic_scope")
        self.assertEqual(scope["binding_snapshot_limit"], 32)
        self.assertFalse(scope["mapped_memory_read"])
        self.assertFalse(scope["image_readback"])
        self.active_capture()
        self.session.arm_bindings()
        self.assertEqual((self.session.folder / "bindings-arm.marker").read_bytes(), b"FRAME_MASK_TRACE_BINDINGS/1\n")
        requested = json.loads((self.session.folder / "session.jsonl").read_text().splitlines()[-1])
        self.assertEqual(requested["event"], "binding_capture_arm_requested")
        self.assertFalse(requested["capture_accepted"])
        with self.assertRaises(FileExistsError):
            self.session.arm_bindings()
        self.session.rollback()
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse(self.session.manifest.exists())
        with self.assertRaises(RuntimeError):
            self.session.arm_bindings()

    def test_binding_arm_rejects_unconfigured_or_changed_activation(self):
        self.session.manual_rollback = True
        self.session.start(self.sha)
        with self.assertRaises(RuntimeError):
            self.session.arm_bindings()
        self.assertFalse((self.session.folder / "bindings-arm.marker").exists())
        self.session.dropin.write_text("unrelated environment")
        with self.assertRaises(RuntimeError):
            self.session.arm_bindings()
        self.assertFalse((self.session.folder / "bindings-arm.marker").exists())

    def test_binding_arm_refuses_capture_missing_or_closed_without_footer(self):
        self.binding_session()
        self.assert_arm_refused()
        descriptor = self.active_capture()
        fcntl.flock(descriptor, fcntl.LOCK_UN)
        self.assert_arm_refused()

    def test_binding_arm_refuses_completed_capture_and_partial_footer(self):
        self.binding_session()
        for extra in (b'{"event":"footer","capture_bounded":true}\n',
                      b'{"event":"footer","capture_bounded":true}',
                      b'{ "event" : "footer", "emitted":'):
            with self.subTest(extra=extra):
                self.active_capture(extra)
                self.assert_arm_refused()
                (self.session.folder / "capture.jsonl").unlink()

    def test_binding_arm_accepts_concurrent_partial_event(self):
        self.binding_session()
        self.active_capture(b'{"event":"shader","serial":1}\n{"event":"pipeline","serial":')
        self.session.arm_bindings()
        self.assertTrue((self.session.folder / "bindings-arm.marker").exists())
        requested = json.loads((self.session.folder / "session.jsonl").read_text().splitlines()[-1])
        self.assertEqual(requested["event"], "binding_capture_arm_requested")
        self.assertFalse(requested["capture_accepted"])

    def test_binding_arm_refuses_invalid_header_or_complete_json(self):
        self.binding_session()
        for changes, extra in (
                ({"event": "pipeline"}, b""), ({"layer": "unrelated"}, b""),
                ({"binding_arm_required": False}, b""), ({"version": True}, b""),
                ({"fragment_filter": "0000000000000000"}, b""),
                ({}, b'{"event":"header"}\n'), ({}, b"not-json\n"),
                ({}, b"{not-json}\n"), ({}, b"[1,2,3]\n"),
                ({}, b'{"serial":1}\n'), ({}, b"not-json")):
            with self.subTest(changes=changes, extra=extra):
                self.active_capture(extra, **changes)
                self.assert_arm_refused()
                (self.session.folder / "capture.jsonl").unlink()

    def test_binding_arm_refuses_oversized_capture(self):
        self.binding_session()
        descriptor = self.active_capture()
        os.ftruncate(descriptor, MODULE.CAPTURE_LIMIT + 1)
        self.assert_arm_refused()

    def test_binding_arm_refuses_growth_beyond_bound_during_read(self):
        self.binding_session()
        writer = self.active_capture()
        original_fstat = MODULE.os.fstat
        checks = 0
        def expanded(descriptor):
            nonlocal checks
            checks += 1
            if checks == 2:
                os.ftruncate(writer, MODULE.CAPTURE_LIMIT + 1)
            return original_fstat(descriptor)
        with mock.patch.object(MODULE.os, "fstat", side_effect=expanded):
            self.assert_arm_refused()

    def test_binding_arm_refuses_excess_records_within_byte_bound(self):
        self.binding_session()
        self.active_capture(b'{"event":"shader"}\n' * (MODULE.CAPTURE_EVENT_LIMIT + 3))
        self.assert_arm_refused()

    def test_binding_arm_refuses_nonprivate_and_symlink_capture(self):
        self.binding_session()
        self.active_capture()
        path = self.session.folder / "capture.jsonl"
        path.chmod(0o644)
        self.assert_arm_refused()
        path.chmod(0o600)
        owned = self.session.folder / "private-capture.jsonl"
        path.rename(owned)
        path.symlink_to(owned)
        self.assert_arm_refused()
        self.assertTrue(path.is_symlink())
        self.assertTrue(owned.exists())

    def test_binding_arm_refuses_shared_capture_inode(self):
        self.binding_session()
        self.active_capture()
        path = self.session.folder / "capture.jsonl"
        other = self.session.folder / "borrowed-capture.jsonl"
        os.link(path, other)
        self.assert_arm_refused()
        self.assertTrue(other.exists())

    def test_binding_arm_refuses_fifo_without_blocking(self):
        self.binding_session()
        os.mkfifo(self.session.folder / "capture.jsonl", 0o600)
        self.assert_arm_refused()

    def test_binding_arm_refuses_path_replaced_during_read(self):
        self.binding_session()
        self.active_capture()
        path = self.session.folder / "capture.jsonl"
        original_fstat = MODULE.os.fstat
        checks = 0
        def replaced(descriptor):
            nonlocal checks
            checks += 1
            if checks == 2:
                path.unlink()
                path.write_text('replacement')
                path.chmod(0o600)
            return original_fstat(descriptor)
        with mock.patch.object(MODULE.os, "fstat", side_effect=replaced):
            self.assert_arm_refused()
        self.assertEqual(path.read_text(), "replacement")

    def test_binding_arm_refuses_writer_closing_during_read(self):
        self.binding_session()
        writer = self.active_capture()
        original_fstat = MODULE.os.fstat
        checks = 0
        def closed(descriptor):
            nonlocal checks
            checks += 1
            if checks == 2:
                fcntl.flock(writer, fcntl.LOCK_UN)
            return original_fstat(descriptor)
        with mock.patch.object(MODULE.os, "fstat", side_effect=closed):
            self.assert_arm_refused()

    def test_binding_arm_refuses_capture_ownership_change(self):
        self.binding_session()
        self.active_capture()
        original_fstat = MODULE.os.fstat
        def foreign(descriptor):
            actual = original_fstat(descriptor)
            fields = list(actual)
            fields[4] = os.getuid() + 1
            return os.stat_result(fields)
        with mock.patch.object(MODULE.os, "fstat", side_effect=foreign):
            self.assert_arm_refused()

    def test_opt_in_loader_debug_uses_same_exact_rollback(self):
        self.session.loader_debug = True
        self.session.start(self.sha)
        self.assertIn(b"Environment=VK_LOADER_DEBUG=error,warn,layer\n", self.session.dropin.read_bytes())
        self.assertNotIn(b"VK_LOADER_LAYERS_ENABLE", self.session.dropin.read_bytes())
        self.session.rollback()
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse(self.session.manifest.exists())

    def test_deployed_manifest_version_is_a_string(self):
        manifest = json.loads(self.session.contents()[self.session.manifest])
        self.assertEqual(manifest["layer"]["implementation_version"], "1")
        self.assertEqual(manifest["layer"]["api_version"], "1.3.0")
        PROBE.validate_manifest(manifest)

    def test_numeric_manifest_is_rejected_before_loader_discovery(self):
        manifest = json.loads(self.session.contents()[self.session.manifest])
        for value in (1, True, 1.0, None):
            with self.subTest(value=value):
                manifest["layer"]["implementation_version"] = value
                with self.assertRaises(RuntimeError):
                    PROBE.validate_manifest(manifest)

    def test_probe_requires_complete_properties_not_a_partial_name(self):
        manifest = json.loads(self.session.contents()[self.session.manifest])
        complete = PROBE.Layer()
        complete.name = manifest["layer"]["name"].encode()
        complete.spec = (1 << 22) | (3 << 12)
        complete.implementation = 1
        complete.description = manifest["layer"]["description"].encode()
        PROBE.validate_properties([complete], manifest)
        partial = PROBE.Layer()
        partial.name = complete.name
        partial.spec = complete.spec
        with self.assertRaises(RuntimeError):
            PROBE.validate_properties([partial], manifest)
        for field, value in (("spec", 0), ("implementation", 0), ("description", b"")):
            with self.subTest(field=field):
                wrong = PROBE.Layer()
                wrong.name, wrong.spec = complete.name, complete.spec
                wrong.implementation, wrong.description = complete.implementation, complete.description
                setattr(wrong, field, value)
                with self.assertRaises(RuntimeError):
                    PROBE.validate_properties([wrong], manifest)
        for properties in ([], [complete, complete]):
            with self.assertRaises(RuntimeError):
                PROBE.validate_properties(properties, manifest)


if __name__ == "__main__":
    unittest.main()
