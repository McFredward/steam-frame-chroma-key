"""User-session recovery tests. No Frame, network or real systemctl calls."""
import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("receiver_session", Path(__file__).resolve().parents[1] / "tools/receiver_observer_session.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="receiver-session-fixture-")
        self.root = Path(self.temp.name)
        self.home, self.runtime = self.root / "home", self.root / "runtime"
        self.home.mkdir(mode=0o700)
        self.runtime.mkdir(mode=0o700)
        self.calls, self.writer_fds = [], []
        self.proc = self.root / "proc"
        self.proc.mkdir(mode=0o700)
        self.session = M.Session(self.home, self.runtime, self.command, environment={}, proc_root=self.proc)
        self.files = {}
        self.plan = {"schema": M.SCHEMA, "offline_files_supported": True, "installed": False,
                     "runtime_authorized": False, "receiver_service": M.UNIT,
                     "proposed_owned_directory": str(self.session.folder),
                     "proposed_runtime_dropin_basename": self.session.dropin.name,
                     "selection_caller_returns": list(M.SELECTION_CALLER_RETURNS),
                     "metadata_caller_returns": list(M.METADATA_CALLER_RETURNS),
                     "limits": {"selections": 32, "bytes": M.LIMIT, "seconds": 120}}
        for flag in ("image_or_mapped_uniform_reads", "native_binary_patch", "kernel_or_system_image_change",
                     "shader_or_mask_activation", "automatic_restart"):
            self.plan[flag] = False
        for n, name in enumerate(("receiver", "provider", "loader", "observer")):
            data = b"\x7fELF\x02\x01\x01" + b"\x00" * 9 + b"\x03\x00\xb7\x00" + bytes([n]) * 44
            path = self.root / ("vrlink" if name == "receiver" else name)
            M.exclusive(path, data)
            self.files[name] = path
            self.plan[name] = {"sha256": M.digest(data), "bytes": len(data), "elf_machine": 183, "elf_type": 3}
        self.receiver_patch = mock.patch.object(M, "RECEIVER", self.plan["receiver"]["sha256"])
        self.receiver_patch.start()
        self.plan_path = self.root / "proposal.json"
        self.save_plan()
        self.original = {"Environment": "STEAM_LAUNCH_WRAPPER_SCOPE=true NATIVE_SETTING=unchanged",
                         "EnvironmentFiles": "", "DropInPaths": ""}
        self.manager = "NATIVE_ENV=yes\n"
        self.fail_restart = 0

    def tearDown(self):
        self.receiver_patch.stop()
        for fd in self.writer_fds:
            os.close(fd)
        self.temp.cleanup()

    def save_plan(self):
        if self.plan_path.exists():
            self.plan_path.unlink()
        M.exclusive(self.plan_path, M.json_bytes(self.plan))
        self.plan_sha = M.digest(self.plan_path.read_bytes())

    def command(self, args, **options):
        self.calls.append(tuple(args))
        text = "active\n"
        if "show-environment" in args:
            text = self.manager
        elif "show" in args:
            state = dict(self.original)
            if self.session.dropin.exists():
                state["Environment"] += " " + " ".join([
                    f"LD_PRELOAD={self.session.library}",
                    f"FRAME_RECEIVER_TRACE_OUTPUT={self.session.capture}",
                    f"FRAME_RECEIVER_TRACE_ARM={self.session.marker}"])
                state["DropInPaths"] = str(self.session.dropin)
            text = "\n".join(f"{k}={v}" for k, v in state.items()) + "\n"
        if "restart" in args and self.fail_restart:
            self.fail_restart -= 1
            raise subprocess.TimeoutExpired(args, 45)
        return subprocess.CompletedProcess(args, 0, text, "")

    def start(self, **kwargs):
        opts = dict(runtime_scope=True, restart_ready=True)
        opts.update(kwargs)
        self.session.start(self.plan_path, self.plan_sha, self.files, **opts)

    def writer(self, *, locked=True, extra=b"", **changes):
        header = {"kind": "header", "supported": True,
                  "executable_sha256": self.plan["receiver"]["sha256"],
                  "sample_limit": 32, "byte_limit": M.LIMIT, "deadline_seconds": 120,
                  "evidence": "protected_selection_attempt", "accepted_submit": False,
                  "source_frame_associated": False, "gpu_consumed_coherent": False,
                  "writer_liveness": "exclusive_flock_v1"}
        header.update(changes)
        fd = os.open(self.session.capture, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        self.writer_fds.append(fd)
        if locked:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        os.write(fd, json.dumps(header).encode() + b"\n" + extra)
        self.receiver_fixture()
        return fd

    def receiver_fixture(self, *, service="steam.service", observer=True, pid=737455):
        """Mock actual run_vrlink.sh -> Steam ancestry; no executable is run."""
        self.pid = pid
        self.process = self.proc / str(pid)
        self.process.mkdir(mode=0o700, exist_ok=True)
        (self.process / "exe").symlink_to(self.files["receiver"])
        (self.process / "root").symlink_to("/", target_is_directory=True)
        (self.process / "stat").write_text(f"{pid} (vrlink) R " + " ".join(["0"] * 18 + ["123", "0"]))
        (self.process / "cgroup").write_text(f"0::/user.slice/user-{os.getuid()}.slice/user@{os.getuid()}.service/app.slice/{service}\n")
        (self.process / "environ").write_bytes(b"\0".join([
            f"LD_PRELOAD={self.session.library}".encode(),
            f"FRAME_RECEIVER_TRACE_OUTPUT={self.session.capture}".encode(),
            f"FRAME_RECEIVER_TRACE_ARM={self.session.marker}".encode(),
            b"LD_LIBRARY_PATH=/home/steamos/.local/share/Steam/steamrtarm64"]) + b"\0")
        mappings = []
        paths = [self.files["provider"], self.files["loader"]]
        if observer: paths.append(self.session.library)
        for n, path in enumerate(paths):
            info = path.stat()
            mappings.append(f"{1000+n*100:x}-{1050+n*100:x} r-xp 00000000 {os.major(info.st_dev):x}:{os.minor(info.st_dev):x} {info.st_ino} {path}\n")
        (self.process / "maps").write_text("".join(mappings))
        info = self.session.capture.stat()
        (self.proc / "locks").write_text(f"1: FLOCK ADVISORY WRITE {pid} {os.major(info.st_dev):x}:{os.minor(info.st_dev):x}:{info.st_ino} 0 EOF\n")

    @contextlib.contextmanager
    def btrfs_fixture(self):
        """Real file/hash reads with the recorded Frame kernel device identities.

        Only stat metadata is substituted: no Btrfs mount or privileged process
        is needed on the host. Maps retain the exact Frame paths and inodes.
        """
        self.start()
        self.writer()
        native_root = self.root / "native-root"
        (native_root / "usr/lib").mkdir(parents=True)
        paths = {"provider": "/usr/lib/libc.so.6", "loader": "/usr/lib/ld-linux-aarch64.so.1"}
        ids = {"provider": 50309, "loader": 48411}
        actual_files = {}
        manifest_path = self.session.folder / "owned.json"
        manifest = json.loads(manifest_path.read_text())
        rows = []
        for role, name in paths.items():
            actual = native_root / name.lstrip("/")
            actual.write_bytes(self.files[role].read_bytes())
            actual_files[role] = actual
            manifest["native_files"][role]["path"] = name
            rows.append(f"1000-1100 r-xp 00000000 00:1d {ids[role]} {name}\n")
        manifest_path.write_bytes(M.json_bytes(manifest))
        observer = native_root / str(self.session.library).lstrip("/")
        observer.parent.mkdir(parents=True)
        observer.symlink_to(self.session.library)
        info = self.session.library.stat()
        rows.append(f"2000-2100 r-xp 00000000 {os.major(info.st_dev):x}:{os.minor(info.st_dev):x} {info.st_ino} {self.session.library}\n")
        (self.process / "maps").write_text("".join(rows))
        (self.process / "root").unlink()
        (self.process / "root").symlink_to(native_root, target_is_directory=True)
        mountinfo = self.process / "mountinfo"
        mountinfo.write_text("32 2 0:29 / / rw,relatime shared:1 - btrfs /dev/sda5 rw,ssd,discard=async,space_cache=v2,subvolid=5,subvol=/\n")
        original_fstat, original_stat = os.fstat, os.stat
        def changed(info, device, inode):
            fields = {key: getattr(info, key) for key in dir(info) if key.startswith("st_")}
            fields.update(st_dev=device, st_ino=inode)
            return SimpleNamespace(**fields)
        def file_stat(fd):
            info = original_fstat(fd)
            name = os.readlink(f"/proc/self/fd/{fd}")
            for role, path in actual_files.items():
                if name == str(path):
                    return changed(info, os.makedev(0, 30), ids[role])
            return info
        def path_stat(path, *args, **kwargs):
            info = original_stat(path, *args, **kwargs)
            if str(path) == str(self.process / "root"):
                return changed(info, os.makedev(0, 30), 256)
            for role, actual in actual_files.items():
                if str(path) in (str(actual), str(self.process / "root" / paths[role].lstrip("/"))):
                    return changed(info, os.makedev(0, 30), ids[role])
            return info
        with mock.patch.object(M.os, "fstat", file_stat), mock.patch.object(M.os, "stat", path_stat):
            yield {"paths": actual_files, "mountinfo": mountinfo, "path_stat": path_stat,
                   "file_stat": file_stat, "changed": changed}

    def test_start_arm_rollback_and_idempotent_recovery(self):
        self.start()
        self.writer()
        self.session.arm(stream_ready=True)
        self.assertEqual(self.session.marker.read_bytes(), b"")
        for path in self.session.folder.iterdir():
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.session.folder.stat().st_mode), 0o700)
        manifest = json.loads((self.session.folder / "owned.json").read_text())
        self.assertEqual(manifest["original_service"], self.original)
        self.assertEqual(bytes.fromhex(manifest["activation"]["bytes_hex"]), self.session.dropin.read_bytes())
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        previous = list(self.calls)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.calls, previous)
        self.assertEqual(sum("restart" in x for x in self.calls), 2)
        self.assertFalse(any(x[0] != "systemctl" for x in self.calls))
        completion = json.loads((self.session.folder / "rollback-complete.json").read_text())
        self.assertIs(completion["exact_manager"], True)
        self.assertIs(completion["manager_matches_except_session_paths"], True)
        self.assertEqual(completion["manager_changed_keys"], [])

    def test_native_manager_session_paths_may_change_without_being_overwritten(self):
        self.manager += f"GAMESCOPE_LIMITER_FILE={self.runtime}/gamescope-limiter-XX7eT9as\nMANGOHUD_CONFIGFILE={self.runtime}/gamescope-mangoapp-XXCn3LeI\n"
        self.start()
        self.manager = f"NATIVE_ENV=yes\nGAMESCOPE_LIMITER_FILE={self.runtime}/gamescope-limiter-XXJgzni8\nMANGOHUD_CONFIGFILE={self.runtime}/gamescope-mangoapp-XXx8y6yP\n"
        self.session.rollback(restart_ready=True)
        completion = json.loads((self.session.folder / "rollback-complete.json").read_text())
        self.assertIs(completion["exact_manager"], False)
        self.assertIs(completion["manager_matches_except_session_paths"], True)
        changed = ["GAMESCOPE_LIMITER_FILE", "MANGOHUD_CONFIGFILE"]
        self.assertEqual(completion["manager_changed_keys"], changed)
        self.assertEqual(completion["permitted_path_changes"], changed)
        events = [json.loads(row) for row in (self.session.folder / "session.jsonl").read_text().splitlines()]
        event = next(row for row in events if row["event"] == "rollback_complete")
        self.assertIs(event["exact_manager"], False)
        self.assertEqual(event["manager_changed_keys"], changed)
        self.assertIn("gamescope-limiter-XXJgzni8", self.manager)
        self.assertFalse(any("set-environment" in call or "unset-environment" in call for call in self.calls))

    def test_other_manager_change_still_refuses_completion(self):
        self.start()
        self.manager = "NATIVE_ENV=changed\n"
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        events = [json.loads(row) for row in (self.session.folder / "session.jsonl").read_text().splitlines()]
        event = next(row for row in events if row["event"] == "restoration_comparison")
        self.assertIs(event["manager_matches_except_session_paths"], False)
        self.assertEqual(event["manager_changed_keys"], ["NATIVE_ENV"])

    def test_session_path_exception_requires_existing_absolute_path_values(self):
        original = {"GAMESCOPE_LIMITER_FILE": str(self.runtime / "gamescope-limiter-original")}
        for restored in ({}, {"GAMESCOPE_LIMITER_FILE": "relative"},
                         {"GAMESCOPE_LIMITER_FILE": "/tmp/../changed"},
                         {"GAMESCOPE_LIMITER_FILE": "/tmp/changed\n"},
                         {"GAMESCOPE_LIMITER_FILE": "/tmp/gamescope-limiter-other"},
                         {"GAMESCOPE_LIMITER_FILE": str(self.runtime / "wrong-prefix")},
                         {"GAMESCOPE_LIMITER_FILE": str(self.runtime / "sub/gamescope-limiter-other")},
                         {"GAMESCOPE_LIMITER_FILE": str(self.runtime / "gamescope-limiter-other"), "NEW_KEY": "unexpected"}):
            self.assertIs(M.compare_manager(original, restored, self.runtime)["manager_matches_except_session_paths"], False)
        self.assertIs(M.compare_manager({}, {"MANGOHUD_CONFIGFILE": "/tmp/new"}, self.runtime)["manager_matches_except_session_paths"], False)

    def test_preload_values_never_use_the_session_path_exception(self):
        self.start()
        self.manager = "NATIVE_ENV=yes\nFRAME_RECEIVER_TRACE_OUTPUT=/tmp/foreign\n"
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_scope_and_restart_readiness_are_required_separately(self):
        for options in ({"runtime_scope": False}, {"restart_ready": False}):
            with self.assertRaises(RuntimeError):
                self.start(**options)
            self.assertFalse(self.session.folder.exists())
        self.start()
        with self.assertRaises(RuntimeError):
            self.session.arm()
        with self.assertRaises(RuntimeError):
            self.session.rollback()

    def test_offline_support_and_fake_authorization_do_not_authorize_runtime(self):
        self.plan["runtime_authorized"] = True
        self.save_plan()
        with self.assertRaises(RuntimeError):
            self.start()
        self.assertFalse(self.session.folder.exists())

    def test_wrong_reviewed_plan_hash(self):
        self.plan_sha = "0" * 64
        with self.assertRaises(RuntimeError):
            self.start()
        self.assertFalse(self.session.folder.exists())

    def test_pre_update_receiver_pin_cannot_enable_the_current_observer(self):
        old = "0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b"
        # Keep the fake artifact otherwise internally consistent, as in a stale
        # reviewed native plan. The current pin must refuse before any restart.
        current = "31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153"
        self.plan["receiver"]["sha256"] = old
        self.save_plan()
        receiver_bytes, digest = self.files["receiver"].read_bytes(), M.digest
        with mock.patch.object(M, "RECEIVER", current), \
                mock.patch.object(M, "digest", side_effect=lambda raw: old if raw == receiver_bytes else digest(raw)):
            with self.assertRaisesRegex(RuntimeError, "Unsupported native receiver"):
                self.start()
        self.assertFalse(self.session.folder.exists())
        self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.calls, [])

    def test_each_native_fingerprint_is_checked_fresh(self):
        for name in self.files:
            data = self.files[name].read_bytes()
            self.files[name].write_bytes(data + b"changed")
            with self.assertRaises(RuntimeError, msg=name):
                self.start()
            self.files[name].write_bytes(data)
            self.assertFalse(self.session.folder.exists())

    def test_wrong_abi_and_missing_loader(self):
        self.plan["observer"]["elf_machine"] = 62
        self.save_plan()
        with self.assertRaises(RuntimeError):
            self.start()
        self.plan["observer"]["elf_machine"] = 183
        self.save_plan()
        self.files.pop("loader")
        with self.assertRaises(RuntimeError):
            self.start()

    def test_plan_unsafe_scope_or_path(self):
        for key, value in (("native_binary_patch", True), ("proposed_owned_directory", "/foreign"),
                           ("proposed_runtime_dropin_basename", "different.conf"),
                           ("receiver_service", "steamvr.service"),
                           ("selection_caller_returns", ["0x34a50", "0x34d74"]),
                           ("metadata_caller_returns", ["0x5200c", "0x5203c"]),
                           ("schema", "frame-receiver-observer-proposal-v1")):
            original = self.plan[key]
            self.plan[key] = value
            self.save_plan()
            with self.assertRaises(RuntimeError):
                self.start()
            self.plan[key] = original

    def test_foreign_process_manager_and_service_environments(self):
        self.session.environment["LD_PRELOAD"] = "/foreign.so"
        with self.assertRaises(RuntimeError): self.start()
        self.session.environment.clear()
        self.manager = "FRAME_RECEIVER_TRACE_ARM=/foreign\n"
        with self.assertRaises(RuntimeError): self.start()
        self.manager = ""
        self.original["Environment"] = "LD_PRELOAD=/foreign.so"
        with self.assertRaises(RuntimeError): self.start()
        self.assertFalse(self.session.folder.exists())

    def test_environment_file_is_not_implicitly_trusted(self):
        self.original["EnvironmentFiles"] = "/some/file"
        with self.assertRaises(RuntimeError): self.start()

    def test_native_mesavars_file_is_pinned_recorded_and_never_changed(self):
        path = self.root / "mesavars.sh"
        raw = b'# Native systemd environment, never executed.\nMESA_VK_WSI_PRESENT_MODE=immediate\nMESA_SETTING="bounded value"\n'
        M.exclusive(path, raw)
        path.chmod(0o644)
        self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        with self.assertRaises(RuntimeError): self.start()
        with self.assertRaises(RuntimeError): self.start(reviewed_environment_files={str(path): "0" * 64})
        self.start(reviewed_environment_files={str(path): M.digest(raw)})
        manifest = json.loads((self.session.folder / "owned.json").read_text())
        self.assertEqual(bytes.fromhex(manifest["original_environment_files"][str(path)]["bytes_hex"]), raw)
        self.writer()
        self.session.arm(stream_ready=True)
        self.session.rollback(restart_ready=True)
        self.assertEqual(path.read_bytes(), raw)
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o644)

    def test_native_environment_preload_and_shell_syntax_refused(self):
        path = self.root / "mesavars.sh"
        self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        for raw in (b'LD_PRELOAD="/foreign.so"\n', b'FRAME_MASK_TRACE=1\n',
                    b'export MESA_SETTING=1\n', b'MESA_SETTING="unfinished\n', b'KEY=one\\\ntwo\n'):
            if path.exists(): path.unlink()
            M.exclusive(path, raw)
            with self.assertRaises(RuntimeError):
                self.start(reviewed_environment_files={str(path): M.digest(raw)})
        self.assertFalse(self.session.folder.exists())

    def test_changed_native_environment_file_refuses_arm_but_disables_on_rollback(self):
        path = self.root / "mesavars.sh"
        raw = b"MESA_SETTING=1\n"
        M.exclusive(path, raw)
        self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        self.start(reviewed_environment_files={str(path): M.digest(raw)})
        self.writer()
        path.write_bytes(b"MESA_SETTING=2\n")
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        self.assertEqual(path.read_bytes(), b"MESA_SETTING=2\n")

    def test_foreign_dropin_and_preexisting_session_refused(self):
        M.private_parents(self.session.dropin.parent)
        M.exclusive(self.session.dropin, b"foreign\n")
        with self.assertRaises(RuntimeError): self.start()
        self.session.dropin.unlink()
        M.private_parents(self.session.folder)
        with self.assertRaises(RuntimeError): self.start()

    def test_symlink_plan_artifact_parent_and_session_refused(self):
        link = self.root / "linked-plan"
        link.symlink_to(self.plan_path)
        original = self.plan_path
        self.plan_path = link
        with self.assertRaises(RuntimeError): self.start()
        self.plan_path = original
        link.unlink()
        link.symlink_to(self.files["provider"])
        original = self.files["provider"]
        self.files["provider"] = link
        with self.assertRaises(RuntimeError): self.start()
        self.files["provider"] = original
        (self.home / ".local").symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(RuntimeError): self.start()

    def test_world_readable_plan_refused(self):
        self.plan_path.chmod(0o644)
        with self.assertRaises(RuntimeError): self.start()

    def test_start_restart_failure_removes_activation_then_recovers_once(self):
        self.fail_restart = 1
        with self.assertRaises(subprocess.TimeoutExpired): self.start()
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        self.assertEqual(sum("restart" in x for x in self.calls), 2)

    def test_activation_snapshot_exists_before_creation(self):
        original = M.exclusive
        def check(path, data):
            if path == self.session.dropin:
                snap = json.loads((self.session.folder / "owned.json").read_text())
                self.assertEqual(bytes.fromhex(snap["activation"]["bytes_hex"]), data)
            original(path, data)
        with mock.patch.object(M, "exclusive", check): self.start()

    def test_interrupted_activation_install_has_manual_recovery(self):
        original = M.exclusive
        def interrupt(path, data):
            if path == self.session.dropin: raise KeyboardInterrupt()
            original(path, data)
        with mock.patch.object(M, "exclusive", interrupt):
            with self.assertRaises(KeyboardInterrupt): self.start()
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())

    def test_changed_foreign_dropin_is_preserved_without_restart(self):
        self.start()
        self.session.dropin.write_bytes(b"foreign replacement\n")
        before = list(self.calls)
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertEqual(self.session.dropin.read_bytes(), b"foreign replacement\n")
        self.assertEqual(self.calls, before)
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_foreign_activation_without_session_preserved(self):
        M.private_parents(self.session.dropin.parent)
        M.exclusive(self.session.dropin, b"foreign\n")
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertEqual(self.session.dropin.read_bytes(), b"foreign\n")

    def test_old_steamvr_activation_is_preserved_and_refuses_new_start(self):
        M.private_parents(self.session.old_dropin.parent)
        M.exclusive(self.session.old_dropin, b"old owned session\n")
        with self.assertRaises(RuntimeError): self.start()
        self.assertFalse(self.session.folder.exists())
        self.assertEqual(self.session.old_dropin.read_bytes(), b"old owned session\n")
        self.assertFalse(self.calls)

    def test_service_active_without_actual_receiver_capture_cannot_arm(self):
        self.start()
        with self.assertRaises(FileNotFoundError): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())
        self.assertTrue(all("steamvr.service" not in call for call in self.calls))

    def test_matching_locked_header_from_wrong_service_cannot_arm(self):
        self.start()
        self.writer()
        (self.process / "cgroup").write_text("0::/user.slice/steamvr.service\n")
        with self.assertRaisesRegex(RuntimeError, "steam.service"): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())

    def test_wrong_executable_or_missing_observer_mapping_cannot_arm(self):
        self.start()
        self.writer()
        (self.process / "exe").unlink()
        (self.process / "exe").symlink_to(self.files["loader"])
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
        (self.process / "exe").unlink()
        (self.process / "exe").symlink_to(self.files["receiver"])
        path = self.process / "maps"
        path.write_text("\n".join(line for line in path.read_text().splitlines() if str(self.session.library) not in line))
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_actual_provider_and_loader_hashes_are_required_despite_library_path(self):
        self.start()
        self.writer()
        for role in ("provider", "loader"):
            path = self.files[role]
            original = path.read_bytes()
            path.write_bytes(original[:-1] + b"x")
            with self.assertRaisesRegex(RuntimeError, "fingerprint"):
                self.session.arm(stream_ready=True)
            path.write_bytes(original)
        self.assertFalse(self.session.marker.exists())

    def test_mapping_path_alone_does_not_prove_inode_or_loaded_observer(self):
        self.start()
        self.writer()
        path = self.process / "maps"
        original = path.read_text()
        inode = self.session.library.stat().st_ino
        path.write_text(original.replace(f" {inode} {self.session.library}", f" {inode+1} {self.session.library}"))
        with self.assertRaisesRegex(RuntimeError, "identity"): self.session.arm(stream_ready=True)

    def test_direct_mapping_identity_does_not_need_or_relax_mountinfo(self):
        self.start()
        self.writer()
        self.assertFalse((self.process / "mountinfo").exists())
        proof = self.session.receiver_process(self.session.manifest(), self.session.capture.stat())
        self.assertEqual(proof["btrfs_root_identity_reconciliation"], {})
        self.session.arm(stream_ready=True)

    def test_recorded_frame_btrfs_superblock_and_anonymous_root_can_arm(self):
        with self.btrfs_fixture():
            proof = self.session.receiver_process(self.session.manifest(), self.session.capture.stat())
            entries = proof["btrfs_root_identity_reconciliation"]
            self.assertEqual(set(entries), {"/usr/lib/libc.so.6", "/usr/lib/ld-linux-aarch64.so.1"})
            for name, inode in (("/usr/lib/libc.so.6", 50309), ("/usr/lib/ld-linux-aarch64.so.1", 48411)):
                self.assertEqual(entries[name], {"mount_id": 32, "map_device": [0, 29],
                                                "stat_device": [0, 30], "inode": inode})
            self.session.arm(stream_ready=True)
            self.assertTrue(self.session.marker.exists())

    def test_btrfs_reconciliation_still_requires_exact_mapped_inode(self):
        with self.btrfs_fixture():
            path = self.process / "maps"
            path.write_text(path.read_text().replace("50309", "50310"))
            with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
                self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_btrfs_reconciliation_refuses_non_btrfs_or_wrong_superblock(self):
        with self.btrfs_fixture() as fixture:
            original = fixture["mountinfo"].read_text()
            for replacement in (original.replace(" - btrfs ", " - ext4 "), original.replace("0:29", "0:31")):
                fixture["mountinfo"].write_text(replacement)
                with self.assertRaisesRegex(RuntimeError, "reviewed Btrfs root"):
                    self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_btrfs_reconciliation_refuses_non_anonymous_stat_device(self):
        with self.btrfs_fixture() as fixture:
            def non_anonymous(fd):
                info = fixture["file_stat"](fd)
                return fixture["changed"](info, os.makedev(8, 8), info.st_ino) if info.st_ino == 50309 else info
            with mock.patch.object(M.os, "fstat", non_anonymous):
                with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
                    self.session.arm(stream_ready=True)

    def test_btrfs_reconciliation_refuses_other_root_or_colliding_subvolume(self):
        with self.btrfs_fixture() as fixture:
            def other_root(path, *args, **kwargs):
                info = fixture["path_stat"](path, *args, **kwargs)
                return fixture["changed"](info, os.makedev(0, 31), 256) if str(path) == str(self.process / "root") else info
            with mock.patch.object(M.os, "stat", other_root):
                with self.assertRaisesRegex(RuntimeError, "anonymous root"):
                    self.session.arm(stream_ready=True)
            def not_subvolume_root(path, *args, **kwargs):
                info = fixture["path_stat"](path, *args, **kwargs)
                return fixture["changed"](info, info.st_dev, 257) if str(path) == str(self.process / "root") else info
            with mock.patch.object(M.os, "stat", not_subvolume_root):
                with self.assertRaisesRegex(RuntimeError, "anonymous root"):
                    self.session.arm(stream_ready=True)

    def test_btrfs_reconciliation_refuses_bind_or_other_subvolume_mount(self):
        with self.btrfs_fixture() as fixture:
            original = fixture["mountinfo"].read_text()
            for replacement in (original.replace(" / / ", " /usr / "),
                                original.replace("subvolid=5", "subvolid=256"),
                                original.replace("subvol=/", "subvol=/snapshot"),
                                original.replace("subvolid=5", "subvolid=5,subvolid=5")):
                fixture["mountinfo"].write_text(replacement)
                with self.assertRaisesRegex(RuntimeError, "reviewed Btrfs root"):
                    self.session.arm(stream_ready=True)

    def test_btrfs_reconciliation_uses_deepest_mount_not_first_matching_device(self):
        with self.btrfs_fixture() as fixture:
            fixture["mountinfo"].write_text(fixture["mountinfo"].read_text() +
                "33 32 0:29 / /usr rw - btrfs /dev/sda5 rw,subvolid=5,subvol=/\n")
            with self.assertRaisesRegex(RuntimeError, "reviewed Btrfs root"):
                self.session.arm(stream_ready=True)

    def test_effective_mount_prefix_is_component_bound_and_order_independent(self):
        entries = M.mountinfo_entries(b"33 32 0:99 / /usr/libc rw - tmpfs tmpfs rw\n"
                                     b"32 2 0:29 / / rw - btrfs /dev/sda5 rw,subvolid=5,subvol=/\n")
        self.assertEqual(M.effective_mount(entries, "/usr/lib/libc.so.6")["id"], 32)
        self.assertEqual(M.effective_mount(entries, "/usr/libc/libc.so.6")["id"], 33)

    def test_btrfs_reconciliation_refuses_ambiguous_stacked_mounts(self):
        with self.btrfs_fixture() as fixture:
            original = fixture["mountinfo"].read_text()
            fixture["mountinfo"].write_text(original + original.replace("32 2", "33 32"))
            with self.assertRaisesRegex(RuntimeError, "ambiguous effective mounts"):
                self.session.arm(stream_ready=True)

    def test_btrfs_reconciliation_refuses_same_hash_at_other_mapping_path(self):
        with self.btrfs_fixture() as fixture:
            alias = fixture["paths"]["provider"].parent / "other/libc.so.6"
            alias.parent.mkdir()
            os.link(fixture["paths"]["provider"], alias)
            path = self.process / "maps"
            path.write_text(path.read_text().replace("/usr/lib/libc.so.6", "/usr/lib/other/libc.so.6"))
            def alias_stat(fd):
                info = fixture["file_stat"](fd)
                if os.readlink(f"/proc/self/fd/{fd}") == str(alias):
                    return fixture["changed"](info, os.makedev(0, 30), 50309)
                return info
            with mock.patch.object(M.os, "fstat", alias_stat):
                with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
                    self.session.arm(stream_ready=True)

    def test_btrfs_mount_metadata_is_bounded_and_malformed_rows_refused(self):
        valid = b"32 2 0:29 / / rw - btrfs /dev/sda5 rw,subvolid=5,subvol=/\n"
        cases = [b"", b"x" * (M.MOUNTINFO_LIMIT + 1), valid + b"x" * 8193,
                 valid + valid, valid.replace(b"0:29", b"0:xyz"), valid.replace(b"32 2", b"x 2"),
                 valid.replace(b"32 2", b"32 0"), valid.replace(b" - ", b" "),
                 valid.replace(b" / / ", b" / /usr/../ "),
                 valid.replace(b" / / ", b" / // "),
                 valid.replace(b" / / ", b" / /\\123 "),
                 valid.replace(b" / / ", b" / /\\012 "),
                 valid.replace(b"0:29", b"0:4294967296"), b"\xff" + valid]
        for raw in cases:
            with self.subTest(raw=raw[:80]):
                with self.assertRaises(RuntimeError): M.mountinfo_entries(raw)
        rows = b"".join(valid.replace(b"32 2", f"{i+1} 2".encode()) for i in range(M.MOUNTINFO_ROWS + 1))
        with self.assertRaisesRegex(RuntimeError, "row bound"): M.mountinfo_entries(rows)
        for name in ("usr/lib/libc.so.6", "//usr/lib/libc.so.6", "/usr/../lib/libc.so.6", "/usr/lib\n/libc.so.6"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, "mapped native path"):
                    M.effective_mount(M.mountinfo_entries(valid), name)
        with self.btrfs_fixture() as fixture:
            fixture["mountinfo"].write_bytes(b"x" * (M.MOUNTINFO_LIMIT + 1))
            with self.assertRaisesRegex(RuntimeError, "bound"): self.session.arm(stream_ready=True)

    def test_btrfs_mount_or_root_change_during_verification_cannot_arm(self):
        with self.btrfs_fixture() as fixture:
            original_proc = self.session.proc_bytes
            calls = 0
            def changing_mount(path, limit):
                nonlocal calls
                raw = original_proc(path, limit)
                if path == fixture["mountinfo"]:
                    calls += 1
                    if calls == 2: return raw.replace(b"shared:1", b"shared:2")
                return raw
            with mock.patch.object(self.session, "proc_bytes", changing_mount):
                with self.assertRaisesRegex(RuntimeError, "mounts changed"):
                    self.session.arm(stream_ready=True)
            root_calls = 0
            def changing_root(path, *args, **kwargs):
                nonlocal root_calls
                info = fixture["path_stat"](path, *args, **kwargs)
                if str(path) == str(self.process / "root"):
                    root_calls += 1
                    if root_calls == 3: return fixture["changed"](info, os.makedev(0, 31), 256)
                return info
            with mock.patch.object(M.os, "stat", changing_root):
                with self.assertRaisesRegex(RuntimeError, "anonymous root changed"):
                    self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_btrfs_still_checks_content_and_open_file_and_path_stability(self):
        with self.btrfs_fixture() as fixture:
            original = fixture["paths"]["provider"].read_bytes()
            fixture["paths"]["provider"].write_bytes(original[:-1] + b"x")
            with self.assertRaisesRegex(RuntimeError, "fingerprint"):
                self.session.arm(stream_ready=True)
            fixture["paths"]["provider"].write_bytes(original)
            reads = 0
            def changed_fd(fd):
                nonlocal reads
                info = fixture["file_stat"](fd)
                if info.st_ino == 50309:
                    reads += 1
                    if reads == 2: return fixture["changed"](info, info.st_dev, 50310)
                return info
            with mock.patch.object(M.os, "fstat", changed_fd):
                with self.assertRaisesRegex(RuntimeError, "fingerprint"):
                    self.session.arm(stream_ready=True)
            original_proc = self.session.proc_bytes
            def changed_path(path, limit):
                raw = original_proc(path, limit)
                if path == self.process / "environ": fixture["paths"]["loader"].write_bytes(b"changed")
                return raw
            with mock.patch.object(self.session, "proc_bytes", changed_path):
                with self.assertRaisesRegex(RuntimeError, "changed during verification"):
                    self.session.arm(stream_ready=True)

    def test_btrfs_cannot_reconcile_observer_device_mismatch(self):
        self.start()
        self.writer()
        path = self.process / "maps"
        device = self.session.library.stat().st_dev
        original = path.read_text()
        row = next(row for row in original.splitlines() if str(self.session.library) in row)
        path.write_text(original.replace(row, row.replace(f"{os.major(device):x}:{os.minor(device):x}", "0:1d")))
        with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
            self.session.arm(stream_ready=True)

    def test_receiver_without_inherited_preload_cannot_arm(self):
        self.start()
        self.writer()
        (self.process / "environ").write_bytes(b"LD_LIBRARY_PATH=Steam/steamrtarm64\0")
        with self.assertRaisesRegex(RuntimeError, "inherit"): self.session.arm(stream_ready=True)

    def test_capture_owner_pid_must_be_unique_and_stable(self):
        self.start()
        self.writer()
        path = self.proc / "locks"
        raw = path.read_text()
        path.write_text(raw + raw.replace(str(self.pid), str(self.pid+1)))
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
        path.write_text(raw)
        with mock.patch.object(self.session, "capture_owner", side_effect=[self.pid, self.pid+1]):
            with self.assertRaisesRegex(RuntimeError, "changed"): self.session.arm(stream_ready=True)

    @unittest.skipUnless(M.platform.system() == "Linux", "Linux flock metadata required")
    def test_real_host_flock_owner_matches_kernel_proc_format(self):
        self.start()
        fd = self.writer()
        host = M.Session(self.home, self.runtime, self.command, environment={})
        self.assertEqual(host.capture_owner(self.session.capture.stat()), os.getpid())
        fcntl.flock(fd, fcntl.LOCK_UN)
        with self.assertRaises(RuntimeError): host.capture_owner(self.session.capture.stat())

    def test_receiver_starttime_change_cannot_arm(self):
        self.start()
        self.writer()
        original = self.session.proc_bytes
        calls = 0
        def changing(path, limit):
            nonlocal calls
            raw = original(path, limit)
            if path == self.process / "stat":
                calls += 1
                if calls == 2: return raw.replace(b"123", b"124")
            return raw
        with mock.patch.object(self.session, "proc_bytes", changing):
            with self.assertRaisesRegex(RuntimeError, "changed"): self.session.arm(stream_ready=True)

    def test_missing_or_oversized_proc_metadata_cannot_arm(self):
        self.start()
        self.writer()
        path = self.process / "maps"
        raw = path.read_bytes()
        path.write_bytes(b"x" * 524289)
        with self.assertRaisesRegex(RuntimeError, "bound"): self.session.arm(stream_ready=True)
        path.write_bytes(raw)
        (self.proc / "locks").unlink()
        with self.assertRaises(FileNotFoundError): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())

    def test_unlocked_capture_refused_even_when_header_matches(self):
        self.start()
        self.writer(locked=False)
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())

    def test_old_or_foreign_header_refused(self):
        self.start()
        self.writer(writer_liveness="unverified")
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_false_boolean_types_in_header_refused(self):
        self.start()
        self.writer(supported=1)
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_old_public_trace_environment_refused(self):
        self.manager = "FRAME_MASK_TRACE=1\n"
        with self.assertRaises(RuntimeError): self.start()

    def test_recovery_ignores_full_ledger_but_removes_only_owned_activation(self):
        self.start()
        with mock.patch.object(self.session, "event", side_effect=OSError("disk full")):
            with self.assertRaises(OSError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        self.assertEqual(sum("restart" in x for x in self.calls), 2)
        self.session.rollback(restart_ready=True)
        self.assertEqual(sum("restart" in x for x in self.calls), 2)

    def test_new_parent_permissions_ignore_permissive_umask(self):
        previous = os.umask(0)
        try:
            self.start()
        finally:
            os.umask(previous)
        self.assertEqual(stat.S_IMODE(self.session.folder.parent.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(self.session.dropin.parent.stat().st_mode), 0o700)

    def test_finished_writer_and_unsupported_header_refused(self):
        self.start()
        self.writer(extra=b'{"kind":"footer"}\n')
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_release_writer_lock_before_arm_refused(self):
        self.start()
        fd = self.writer()
        fcntl.flock(fd, fcntl.LOCK_UN)
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_tampered_observer_refuses_arm(self):
        self.start()
        self.writer()
        self.session.library.write_bytes(b"other")
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_capture_symlink_mode_or_hardlink_refuses_arm(self):
        self.start()
        self.writer()
        self.session.capture.chmod(0o644)
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
        self.session.capture.chmod(0o600)
        os.link(self.session.capture, self.root / "other-capture-link")
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_duplicate_arm_and_arm_after_rollback_refused(self):
        self.start()
        self.writer()
        self.session.arm(stream_ready=True)
        with self.assertRaises(FileExistsError): self.session.arm(stream_ready=True)
        self.session.rollback(restart_ready=True)
        with self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)

    def test_changed_restore_configuration_does_not_claim_completion(self):
        self.start()
        self.original["Environment"] = "UNRELATED_CHANGE=yes"
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_false_or_incomplete_restore_record_does_not_skip_recovery(self):
        self.start()
        self.session.rollback(restart_ready=True)
        (self.session.folder / "rollback-complete.json").write_text('{"restored": false}\n')
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)

    def test_ledger_bound_does_not_prevent_native_restoration(self):
        self.start()
        with mock.patch.object(self.session, "event", side_effect=RuntimeError("ledger bound")):
            with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists())
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())
        self.assertEqual(sum("restart" in x for x in self.calls), 2)

    def test_cli_refuses_foreign_architecture_before_commands(self):
        with mock.patch.object(M.platform, "system", return_value="Linux"), \
                mock.patch.object(M.platform, "machine", return_value="x86_64"), \
                mock.patch.object(M.subprocess, "run") as command:
            with self.assertRaises(SystemExit): M.main(["rollback", "--ack-steam-client-restart-readiness"])
            command.assert_not_called()

    def test_cli_requires_native_runtime_path(self):
        with mock.patch.dict(os.environ, {"XDG_RUNTIME_DIR": "/tmp/not-native"}), \
                mock.patch.object(M.platform, "system", return_value="Linux"), \
                mock.patch.object(M.platform, "machine", return_value="aarch64"):
            with self.assertRaises(SystemExit): M.main(["rollback", "--ack-steam-client-restart-readiness"])


if __name__ == "__main__":
    unittest.main()
