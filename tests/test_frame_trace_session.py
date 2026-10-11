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


if __name__ == "__main__":
    unittest.main()
