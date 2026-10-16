"""Cold sandbox controls; no native loading, services, SSH, or headset use."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("pixel_snapshot_session", Path(__file__).parents[1] / "tools/pixel_snapshot_session.py")
mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)
NONCE = "0123456789abcdef0123456789abcdef"


def elf(machine=183):
    raw = bytearray(120)
    raw[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", raw, 16, 3, machine, 1)
    struct.pack_into("<Q", raw, 32, 64)
    struct.pack_into("<HHH", raw, 52, 64, 56, 1)
    struct.pack_into("<IIQQQQQQ", raw, 64, 1, 5, 0, 0, 0, 120, 120, 4096)
    return bytes(raw)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pixel-session-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.home.mkdir(mode=0o700)
        self.proc = self.root / "proc"
        self.proc.mkdir(mode=0o700)
        self.run = self.home / ".local/share/GloomhavenVR/SteamLinkMaskProbe/runs/test"
        self.session = mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=True)
        self.source = self.root / "source.so"
        self.source.write_bytes(elf())
        self.source.chmod(0o600)
        self.sha = mod.digest(self.source.read_bytes())

    def prepare(self):
        return self.session.prepare(self.source, self.sha, capture_id=NONCE)

    def write(self, path, data):
        path.write_bytes(data)
        path.chmod(0o600)

    def process(self, text, name="101"):
        parent = self.proc / name
        parent.mkdir(mode=0o700, exist_ok=True)
        self.write(parent / "maps", text.encode())

    def test_prepare_exact_layer_config_and_private_receipt(self):
        result = self.prepare()
        self.assertFalse(result["automatic_restart"])
        self.assertFalse(result["native_ui_health_verified"])
        self.assertFalse(result["passthrough_authorized"])
        self.assertEqual(self.session.config.read_bytes(), mod.CONFIG_MAGIC + NONCE.encode() + b"\n" + str(self.run).encode() + b"\n")
        manifest = json.loads(self.session.manifest.read_text())
        self.assertEqual(manifest["layer"]["library_path"], str(self.session.library))
        self.assertEqual(manifest["layer"]["name"], "VK_LAYER_GHVR_pixel_snapshot")
        self.assertNotIn("enable_environment", manifest["layer"])
        self.assertEqual(self.session.library.read_bytes(), self.source.read_bytes())
        for path in (self.run, self.session.base):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)
        for path in (self.session.receipt, self.session.config, self.session.library, self.session.manifest):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertFalse(self.session.arm_file.exists())

    def test_idempotent_prepare_never_replaces_existing_owned_bytes_or_receipt(self):
        self.prepare()
        tracked = (self.session.receipt, self.session.config, self.session.library, self.session.manifest)
        before = [(path.stat().st_ino, path.read_bytes()) for path in tracked]
        self.prepare()
        self.assertEqual(before, [(path.stat().st_ino, path.read_bytes()) for path in tracked])
        with self.assertRaises(mod.SafetyError):
            self.session.prepare(self.source, self.sha, capture_id="f" * 32)

    def test_foreign_manifest_and_config_are_never_replaced(self):
        for path in (self.session.config, self.session.manifest):
            with self.subTest(path=path):
                os.close(mod.directory(path.parent, create=True, private=True))
                self.write(path, b"foreign original")
                with self.assertRaises(mod.SafetyError):
                    self.prepare()
                self.assertEqual(path.read_bytes(), b"foreign original")
                self.assertFalse(self.session.receipt.exists())
                path.unlink()

    def test_nonempty_private_run_is_not_adopted(self):
        os.close(mod.directory(self.run, create=True, private=True))
        self.write(self.run / "foreign", b"keep")
        with self.assertRaisesRegex(mod.SafetyError, "nonempty"):
            self.prepare()
        self.assertFalse(self.session.receipt.exists())
        self.assertEqual((self.run / "foreign").read_bytes(), b"keep")

    def test_stage_checks_input_hash_architecture_segments_and_size(self):
        self.assertEqual(mod.verify_library(elf(), mod.digest(elf())), 183)
        self.assertEqual(mod.verify_library(elf(62), mod.digest(elf(62)), host_fixture=True), 62)
        for raw in (elf(62), b"not ELF", elf()[:-1], elf() + b"x" * mod.MAX_LIBRARY):
            with self.subTest(length=len(raw)), self.assertRaises(mod.SafetyError):
                mod.verify_library(raw, mod.digest(raw))
        for wanted in ("0" * 64, "A" * 64, "wrong"):
            with self.assertRaises(mod.SafetyError):
                self.session.prepare(self.source, wanted, capture_id=NONCE)
        self.assertFalse(self.session.base.exists())

    def test_wrong_home_and_proc_are_not_accepted_without_fixture_flag(self):
        with self.assertRaises(mod.SafetyError):
            mod.Session(self.home, self.run, proc_root=self.proc)
        with self.assertRaises(mod.SafetyError):
            mod.Session(self.home, self.home, proc_root=self.proc, host_fixture=True)

    def test_symlinked_input_or_arbitrary_parent_refuses(self):
        linked = self.root / "linked"
        linked.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(OSError):
            self.session.prepare(linked / "source.so", self.sha)
        own = self.home / ".local"
        own.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(OSError):
            self.prepare()
        self.assertFalse((self.root / "share").exists())

    def test_symlinked_output_and_unsafe_permissions_refuse_without_repair(self):
        os.close(mod.directory(self.session.base, create=True, private=True))
        self.session.config.symlink_to(self.source)
        with self.assertRaises(mod.SafetyError):
            self.prepare()
        self.assertEqual(self.source.read_bytes(), elf())
        self.session.config.unlink()
        self.session.base.chmod(0o755)
        with self.assertRaises(mod.SafetyError):
            self.prepare()
        self.assertEqual(stat.S_IMODE(self.session.base.stat().st_mode), 0o755)

    def test_arm_and_disarm_are_manual_terminal_and_do_not_claim_startup_health(self):
        self.prepare()
        self.session.arm()
        self.assertEqual(self.session.arm_file.read_bytes(), mod.ARM_MAGIC)
        before = self.session.arm_file.stat().st_ino
        self.session.arm()
        self.assertEqual(self.session.arm_file.stat().st_ino, before)
        self.session.disarm()
        self.session.disarm()
        self.assertFalse(self.session.arm_file.exists())
        self.assertEqual(self.session.disarmed.read_bytes(), mod.DISARM_MAGIC)
        with self.assertRaises(mod.SafetyError):
            self.session.arm()
        with self.assertRaises(mod.SafetyError):
            self.prepare()

    def test_arm_accepts_original_launch_claim_but_rejects_spent_or_foreign_claim(self):
        self.prepare()
        self.write(self.run / "launch.claim", mod.CLAIM_MAGIC)
        self.session.arm()
        self.session.arm_file.unlink()
        self.write(self.run / "launch.claim", b"foreign")
        with self.assertRaises(mod.SafetyError):
            self.session.arm()
        self.write(self.run / "launch.claim", mod.CLAIM_MAGIC)
        self.write(self.run / "snapshot.p010", b"owned capture")
        with self.assertRaises(mod.SafetyError):
            self.session.arm()

    def test_rollback_preserves_capture_outputs_and_is_idempotent(self):
        self.prepare()
        self.session.arm()
        outputs = {"snapshot.p010": b"capture", "snapshot.json": b"{}", "status.jsonl": b'{"event":"complete"}\n', "launch.claim": mod.CLAIM_MAGIC}
        for name, value in outputs.items():
            self.write(self.run / name, value)
        result = self.session.rollback()
        self.assertTrue(result["complete"])
        self.assertFalse(result["native_ui_health_verified"])
        for path in (self.session.library, self.session.config, self.session.manifest, self.session.arm_file):
            self.assertFalse(path.exists())
        self.assertTrue(self.session.rollback()["complete"])
        for name, value in outputs.items():
            self.assertEqual((self.run / name).read_bytes(), value)
        with self.assertRaises(mod.SafetyError):
            self.prepare()

    def test_loaded_library_disables_future_activation_but_never_deletes_mapped_file(self):
        self.prepare()
        self.process("1000-2000 r-xp 00000000 00:00 0 " + str(self.session.library) + "\n")
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertEqual(result["active_mappings"]["owned_mapping_pids"], [101])
        self.assertTrue(self.session.library.exists())
        self.assertFalse(self.session.manifest.exists())
        self.assertFalse(self.session.config.exists())
        self.process("")
        self.assertTrue(self.session.rollback()["complete"])
        self.assertFalse(self.session.library.exists())

    def test_inode_alias_and_deleted_mapping_remain_active(self):
        self.prepare()
        info = self.session.library.stat()
        self.process(f"1000-2000 r-xp 00000000 {os.major(info.st_dev):x}:{os.minor(info.st_dev):x} {info.st_ino} /different/alias.so\n")
        self.assertEqual(self.session.active()["owned_mapping_pids"], [101])
        self.process("1000-2000 r-xp 00000000 00:00 0 " + str(self.session.library) + " (deleted)\n")
        self.assertEqual(self.session.active()["owned_mapping_pids"], [101])

    def test_foreign_active_helper_refuses_stage_and_library_cleanup(self):
        self.process("1000-2000 r-xp 00000000 00:00 0 /foreign/" + mod.LIBRARY + "\n")
        with self.assertRaises(mod.SafetyError):
            self.prepare()
        self.process("")
        self.prepare()
        self.process("1000-2000 r-xp 00000000 00:00 0 /foreign/" + mod.LIBRARY + "\n")
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertEqual(result["active_mappings"]["foreign_mapping_pids"], [101])
        self.assertTrue(self.session.library.exists())

    def test_unreadable_or_unbounded_map_scan_refuses_cleanup(self):
        self.prepare()
        parent = self.proc / "101"
        parent.mkdir(mode=0o700)
        self.write(parent / "maps", b"x" * (mod.MAX_MAP + 1))
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertTrue(result["active_mappings"]["uncertain"])
        self.assertTrue(self.session.library.exists())

    def test_mutated_files_block_arm_and_rollback_preserves_them(self):
        self.prepare()
        self.write(self.session.library, self.source.read_bytes() + b"mutation")
        self.write(self.session.config, b"foreign changed config")
        with self.assertRaises(mod.SafetyError):
            self.session.arm()
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertTrue(self.session.library.exists())
        self.assertEqual(self.session.config.read_bytes(), b"foreign changed config")
        self.assertFalse(self.session.manifest.exists())

    def test_changed_receipt_or_run_permissions_never_authorizes_removal(self):
        self.prepare()
        data = json.loads(self.session.receipt.read_text())
        data["run_dir"] = str(self.home)
        self.write(self.session.receipt, mod.encoded(data))
        with self.assertRaises(mod.SafetyError):
            self.session.rollback()
        self.assertTrue(self.session.manifest.exists())
        self.run.chmod(0o755)
        with self.assertRaises(mod.SafetyError):
            self.session.arm()

    def test_grouped_progress_has_a_separate_bound_from_small_json_documents(self):
        self.prepare()
        payload = b'{"event":"bounded_cold_context"}\n' * 900
        self.assertGreater(len(payload), mod.MAX_JSON)
        self.assertLess(len(payload), mod.MAX_PROGRESS)
        self.write(self.run / "status.jsonl", payload)
        self.assertEqual(self.session.status()["progress"], payload.decode())
        ordinary = self.root / "oversized-small-document.json"
        self.write(ordinary, b" " * mod.MAX_JSON + b"{}")
        with self.assertRaises(mod.SafetyError):
            mod.load_json(ordinary)

    def test_status_is_bounded_and_does_not_read_raw_capture(self):
        self.prepare()
        self.write(self.run / "status.jsonl", b"x" * (mod.MAX_PROGRESS + 1))
        raw = self.run / "snapshot.p010"
        raw.symlink_to(self.source)
        result = self.session.status()
        self.assertEqual(result["progress"], "unreadable_or_excessive_preserved")
        self.assertLess(len(mod.encoded(result)), 10000)
        self.assertEqual(raw.read_bytes(), self.source.read_bytes())

    def test_full_exact_target_receipts_fit_progress_without_enlarging_small_json(self):
        self.prepare()
        # Prior bounded creation/context receipts plus eight worst-case16KiB
        # target-state lines; the legacy256KiB reader would lose this evidence.
        payload = b" " * (245416 + 8 * 16 * 1024)
        self.assertGreater(len(payload), 256 * 1024)
        self.assertLess(len(payload), mod.MAX_PROGRESS)
        self.write(self.run / "status.jsonl", payload)
        self.assertEqual(self.session.status()["progress"], payload.decode())
        self.assertEqual(mod.MAX_JSON, 16 * 1024)

    def test_changed_arm_disarm_or_complete_records_are_preserved(self):
        self.prepare()
        self.write(self.session.arm_file, b"foreign arm")
        with self.assertRaises(mod.SafetyError):
            self.session.disarm()
        self.assertEqual(self.session.arm_file.read_bytes(), b"foreign arm")
        self.session.arm_file.unlink()
        self.write(self.session.disarmed, b"foreign disarm")
        self.assertFalse(self.session.rollback()["complete"])
        self.assertEqual(self.session.disarmed.read_bytes(), b"foreign disarm")

    def test_changed_complete_record_is_not_silently_claimed_as_success(self):
        self.prepare()
        self.assertTrue(self.session.rollback()["complete"])
        marker = self.run / "rollback-complete.json"
        self.write(marker, b"foreign result")
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertEqual(marker.read_bytes(), b"foreign result")

    def test_hardlinked_input_or_output_is_not_adopted_or_deleted(self):
        alias = self.root / "alias.so"
        os.link(self.source, alias)
        with self.assertRaises(mod.SafetyError):
            self.prepare()
        alias.unlink()
        self.prepare()
        os.link(self.session.library, alias)
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertEqual(alias.read_bytes(), elf())
        self.assertTrue(self.session.library.exists())

    def test_symlinked_process_map_is_unknown_and_never_followed(self):
        self.prepare()
        process = self.proc / "101"
        process.mkdir(mode=0o700)
        (process / "maps").symlink_to(self.source)
        self.assertTrue(self.session.active()["uncertain"])
        result = self.session.rollback()
        self.assertFalse(result["complete"])
        self.assertTrue(self.session.library.exists())

    def test_cli_filesystem_only_fixture_prepare_status_and_rollback(self):
        args = ["--home", str(self.home), "--run-dir", str(self.run), "--proc-root", str(self.proc), "--host-fixture"]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(mod.main(["stage", *args, "--library", str(self.source), "--library-sha256", self.sha]), 0)
            self.assertEqual(mod.main(["status", *args]), 0)
            self.assertEqual(mod.main(["rollback", *args]), 0)
        source = (Path(__file__).parents[1] / "tools/pixel_snapshot_session.py").read_text()
        self.assertNotIn("import socket", source)
        self.assertNotIn("os.system(", source)

    def denied_maps(self):
        original = os.open
        def guarded(path, *args, **kwargs):
            if path == "maps":
                raise PermissionError("fixture maps permission")
            return original(path, *args, **kwargs)
        return mock.patch.object(mod.os, "open", side_effect=guarded)

    def protected_session(self, reader):
        return mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=True,
                           read_protected_maps=True, protected_map_reader=reader)

    def test_default_denied_maps_never_attempt_privilege(self):
        self.process("")
        with self.denied_maps(), mock.patch.object(mod.subprocess, "Popen", side_effect=AssertionError("unexpected sudo")):
            self.assertTrue(self.session.active()["uncertain"])

    def test_explicit_protected_read_records_mode_and_preserves_mapping_guard(self):
        self.prepare()
        self.process("")
        for value, expected in ((b"", []),
                                (("1000-2000 r-xp 00000000 00:00 0 " + str(self.session.library) + "\n").encode(), [101])):
            reader = mock.Mock(return_value=value)
            session = self.protected_session(reader)
            with self.denied_maps():
                result = session.active()
            self.assertFalse(result["uncertain"])
            self.assertTrue(result["protected_maps_read_enabled"])
            self.assertEqual(result["protected_maps_read_pids"], [101])
            self.assertEqual(result["owned_mapping_pids"], expected)
            reader.assert_called_once_with("101")

    def test_protected_oversize_or_privilege_failure_remains_uncertain(self):
        self.process("")
        for reader in (mock.Mock(return_value=b"x" * (mod.MAX_MAP + 1)),
                       mock.Mock(side_effect=mod.SafetyError("sudo unavailable")),
                       mock.Mock(side_effect=PermissionError("refused"))):
            with self.denied_maps():
                self.assertTrue(self.protected_session(reader).active()["uncertain"])

    def test_readable_or_wrong_owner_never_uses_protected_reader(self):
        self.process("")
        reader = mock.Mock(side_effect=AssertionError("unexpected privilege"))
        self.assertFalse(self.protected_session(reader).active()["uncertain"])
        original = os.fstat
        process_inode = (self.proc / "101").stat().st_ino
        def other_owner(fd):
            value = original(fd)
            if value.st_ino == process_inode:
                items = list(value)
                items[4] = os.getuid() + 1
                return os.stat_result(items)
            return value
        with self.denied_maps(), mock.patch.object(mod.os, "fstat", side_effect=other_owner):
            self.assertFalse(self.protected_session(reader).active()["uncertain"])
        reader.assert_not_called()

    def test_fake_proc_never_uses_real_privileged_runner(self):
        self.process("")
        session = mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=True,
                              read_protected_maps=True)
        with self.denied_maps(), mock.patch.object(mod.subprocess, "Popen", side_effect=AssertionError("unexpected sudo")):
            self.assertTrue(session.active()["uncertain"])

    def test_nonnumeric_and_out_of_range_process_names_are_not_privileged_paths(self):
        for name in ("-1", "1;true", "2147483648", "１０１", "0"):
            self.process("", name=name)
        reader = mock.Mock(side_effect=AssertionError("unexpected privilege"))
        with self.denied_maps():
            self.assertFalse(self.protected_session(reader).active()["uncertain"])
        reader.assert_not_called()

    def test_privileged_reader_uses_exact_readonly_argv_and_bounded_pipes(self):
        original = subprocess.Popen
        seen = []
        def fixture(argv, **kwargs):
            seen.append((argv, kwargs))
            return original([sys.executable, "-c", "import os; os.write(1,b'owned maps\\n')"], **kwargs)
        with mock.patch.object(mod.subprocess, "Popen", side_effect=fixture):
            self.assertEqual(mod.read_protected_maps("101"), b"owned maps\n")
        self.assertEqual(seen[0][0], ["sudo", "-n", "/usr/bin/head", "-c", str(mod.MAX_MAP + 1), "/proc/101/maps"])
        self.assertEqual(seen[0][1], {"stdin": subprocess.DEVNULL, "stdout": subprocess.PIPE,
                                     "stderr": subprocess.PIPE, "shell": False, "close_fds": True})
        for pid in ("101/../1", "-1", "0", "2147483648", "１０１", "1;true"):
            with self.assertRaises(mod.SafetyError):
                mod.read_protected_maps(pid)

    def test_privileged_reader_reaps_failure_timeout_and_excess_output(self):
        original = subprocess.Popen
        for code in ("raise SystemExit(1)",
                     "import os; os.write(2,b'x'*4097)",
                     "import os; os.write(1,b'x'*1048578)",
                     "import time; time.sleep(1)"):
            children = []
            def fixture(argv, **kwargs):
                child = original([sys.executable, "-c", code], **kwargs)
                children.append(child)
                return child
            with self.subTest(code=code), mock.patch.object(mod, "MAP_READ_TIMEOUT", 0.1), \
                    mock.patch.object(mod.subprocess, "Popen", side_effect=fixture):
                with self.assertRaises(mod.SafetyError):
                    mod.read_protected_maps("101")
            self.assertIsNotNone(children[0].returncode)

if __name__ == "__main__":
    unittest.main()
