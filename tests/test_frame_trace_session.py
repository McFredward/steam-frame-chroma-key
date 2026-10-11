"""Recovery checks using temporary files and mocked service commands only."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

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
        self.session = MODULE.Session(self.root / "home", self.root / "runtime", self.run_command)
        self.session.folder.mkdir(parents=True)
        # Synthetic AArch64 ELF header; never loaded or executed.
        self.library = b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\xb7\x00" + b"\x00" * 44
        (self.session.folder / "libVkLayer_frame_mask_trace.so").write_bytes(self.library)
        self.sha = hashlib.sha256(self.library).hexdigest()

    def tearDown(self):
        self.temporary.cleanup()

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
