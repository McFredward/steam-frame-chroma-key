#!/usr/bin/env python3
"""Temporary, user-only SteamVR trace session with recorded exact rollback.

Run only after explicit headset authorization. No system files, sudo, boot
configuration or persistent service settings are changed.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

UNIT = "steamvr.service"
WATCHDOG = "frame-mask-trace-rollback"
FRAGMENT = "f333a137b4e79ced"


def exclusive(path, contents):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(contents)
        output.flush()
        os.fsync(output.fileno())


class Session:
    def __init__(self, home, runtime, run=subprocess.run, loader_debug=False,
                 manual_rollback=False):
        self.folder = home / ".local/share/frame-mask-trace"
        self.manifest_dir = home / ".local/share/vulkan/implicit_layer.d"
        self.manifest = self.manifest_dir / "frame_mask_trace.json"
        self.dropin_dir = runtime / "systemd/user/steamvr.service.d"
        self.dropin = self.dropin_dir / "90-frame-mask-trace.conf"
        self.run = run
        self.loader_debug = loader_debug
        self.manual_rollback = manual_rollback

    def command(self, *arguments, timeout=45, check=True):
        return self.run(arguments, check=check, timeout=timeout,
                        text=True, capture_output=True)

    def event(self, name, **fields):
        with (self.folder / "session.jsonl").open("a", encoding="utf-8") as output:
            output.write(json.dumps(dict(event=name, time=time.time(), **fields)) + "\n")
            output.flush()
            os.fsync(output.fileno())

    def contents(self):
        manifest = json.dumps({"file_format_version": "1.2.0", "layer": {
            "name": "VK_LAYER_FRAME_MASK_trace", "type": "GLOBAL",
            "library_path": str(self.folder / "libVkLayer_frame_mask_trace.so"),
            "api_version": "1.3.0", "implementation_version": "1",
            "description": "Opt-in bounded passive compositor pipeline metadata",
            "enable_environment": {"FRAME_MASK_TRACE": "1"},
            "disable_environment": {"FRAME_MASK_TRACE_DISABLE": "1"}}}, indent=2) + "\n"
        dropin = ("[Service]\nEnvironment=FRAME_MASK_TRACE=1\n"
                  f"Environment=FRAME_MASK_TRACE_FRAGMENT_HASH={FRAGMENT}\n"
                  f'Environment="FRAME_MASK_TRACE_OUTPUT={self.folder}/capture.jsonl"\n')
        if self.loader_debug:
            # Startup decisions only; no forced layer enable or call/API trace.
            dropin += "Environment=VK_LOADER_DEBUG=error,warn,layer\n"
        return {self.manifest: manifest.encode(), self.dropin: dropin.encode()}

    def recovery_event(self, name, **fields):
        # A full/damaged home filesystem must not prevent disabling the layer.
        try:
            self.event(name, **fields)
        except OSError:
            pass

    def start(self, expected_hash):
        for path in (self.manifest, self.dropin, self.folder / "armed.json",
                     self.folder / "capture.jsonl", self.folder / "rollback-complete.json",
                     self.folder / "watchdog-ready"):
            if path.exists() or path.is_symlink():
                raise RuntimeError(f"Refusing existing session path: {path}")
        binary = self.folder / "libVkLayer_frame_mask_trace.so"
        data = binary.read_bytes()
        if (hashlib.sha256(data).hexdigest() != expected_hash or len(data) > 256 * 1024
                or data[:6] != b"\x7fELF\x02\x01" or data[18:20] != b"\xb7\x00"):
            raise RuntimeError("ARM64 library identity/size check failed")
        self.command("systemctl", "--user", "is-active", UNIT)
        if self.manual_rollback and self.command(
                "systemctl", "--user", "is-active", "--quiet", WATCHDOG + ".service",
                check=False).returncode == 0:
            raise RuntimeError("A previous timed rollback is still active; manual activation refused")
        exclusive(self.folder / "session.jsonl", b"")
        self.event("approved_preflight", library_sha256=expected_hash,
                   manifest=str(self.manifest), runtime_dropin=str(self.dropin),
                   limit_seconds=None if self.manual_rollback else 180,
                   rollback_mode="manual" if self.manual_rollback else "watchdog",
                   limit_trace_bytes=1048576,
                   loader_debug=self.loader_debug)
        if not self.manual_rollback:
            # Separate user service, never PartOf=steamvr or in its cgroup.
            self.command("systemd-run", "--user", "--unit=" + WATCHDOG, "--collect",
                         "--property=Restart=no", "/usr/bin/python3",
                         str(self.folder / "frame_trace_session.py"), "watchdog")
            for _ in range(30):
                if (self.folder / "watchdog-ready").exists():
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Rollback watchdog did not acknowledge readiness; activation refused")
            self.command("systemctl", "--user", "is-active", WATCHDOG + ".service")
        try:
            descriptor = os.open(self.folder / "rollback.lock", os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o600)
            with os.fdopen(descriptor, "wb") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                if (self.folder / "rollback-complete.json").exists():
                    raise RuntimeError("Rollback already completed; refusing delayed activation")
                if not self.manual_rollback:
                    self.command("systemctl", "--user", "is-active", WATCHDOG + ".service")
                values = self.contents()
                # Preserve exact owned file bytes for recovery after crashes.
                exclusive(self.folder / "armed.json", json.dumps({str(path): content.hex()
                          for path, content in values.items()}).encode())
                for path, content in values.items():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    exclusive(path, content)
                    self.event("created_activation_file", path=str(path),
                               sha256=hashlib.sha256(content).hexdigest())
                self.command("systemctl", "--user", "daemon-reload")
                self.event("restart_requested", phase="capture")
                self.command("systemctl", "--user", "restart", UNIT)
                self.command("systemctl", "--user", "is-active", UNIT)
                # The native unit tracks vrcmd. This is not proof that the
                # compositor loaded our layer or rendered a submitted draw.
                self.event("capture_runtime_active", launcher_only=True,
                           capture_accepted=False)
        except BaseException:
            self.rollback()
            raise

    def rollback(self):
        # Manual recovery and the watchdog may arrive together. Serialize them
        # so only one service restart is needed after a completed cleanup.
        descriptor = os.open(self.folder / "rollback.lock", os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        with os.fdopen(descriptor, "wb") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            self._rollback()

    def _rollback(self):
        complete = self.folder / "rollback-complete.json"
        if complete.exists():
            return
        armed = self.folder / "armed.json"
        expected = json.loads(armed.read_text()) if armed.exists() else {}
        # Never unlink unrelated/preexisting content. Manifest removal precedes
        # reload/restart, so the next loader cannot enable our library.
        refused = []
        for path in (self.manifest, self.dropin):
            if not path.exists() and not path.is_symlink():
                continue
            if path.is_symlink() or str(path) not in expected or path.read_bytes() != bytes.fromhex(expected[str(path)]):
                self.recovery_event("rollback_refused_changed_file", path=str(path))
                refused.append(str(path))
                continue
            path.unlink()
            self.recovery_event("removed_activation_file", path=str(path))
        if expected:
            try:
                self.command("systemctl", "--user", "daemon-reload")
            except (subprocess.SubprocessError, OSError) as error:
                # The absent manifest already prevents loading. Still attempt
                # the native restart even if refreshing systemd failed.
                refused.append("daemon-reload failed")
                self.recovery_event("rollback_reload_failed", detail=str(error))
            self.recovery_event("restart_requested", phase="native_restore")
            self.command("systemctl", "--user", "restart", UNIT)
            self.command("systemctl", "--user", "is-active", UNIT)
        if refused:
            raise RuntimeError(f"Native restart attempted; changed files need manual review: {refused}")
        exclusive(complete, json.dumps({"restored": True, "time": time.time(),
                  "capture_unverified": True}).encode())
        self.recovery_event("rollback_complete")
        # Keep tiny capture/ledger/library until the operator copies evidence and
        # confirms the new compositor is native; cleanup then removes this folder.
        try:
            self.dropin_dir.rmdir()
        except OSError:
            pass

    def watchdog(self):
        exclusive(self.folder / "watchdog-ready", b"ready\n")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if (self.folder / "rollback-complete.json").exists():
                return
            time.sleep(1)
        self.rollback()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("start", "watchdog", "rollback"))
    parser.add_argument("--sha256")
    parser.add_argument("--loader-debug", action="store_true",
                        help="Record native loader startup decisions; never force-enable a layer")
    parser.add_argument("--manual-rollback", action="store_true",
                        help="Start without a timed SteamVR restart; keep explicit rollback and reboot recovery")
    args = parser.parse_args()
    if args.manual_rollback and args.mode != "start":
        parser.error("--manual-rollback applies only to start")
    if os.getuid() == 0 or platform.machine() != "aarch64":
        raise RuntimeError("Only an authorized non-root AArch64 headset session is allowed")
    home = Path.home()
    runtime = Path("/run/user") / str(os.getuid())
    folder = home / ".local/share/frame-mask-trace"
    if Path(__file__).resolve() != folder / "frame_trace_session.py":
        raise RuntimeError("Install helper only in the reviewed private session directory")
    if (folder.is_symlink() or folder.stat().st_uid != os.getuid()
            or folder.stat().st_mode & 0o777 != 0o700 or runtime.stat().st_uid != os.getuid()):
        raise RuntimeError("Private session/runtime ownership check failed")
    os.umask(0o077)
    session = Session(home, runtime, loader_debug=args.loader_debug,
                      manual_rollback=args.manual_rollback)
    if args.mode == "start":
        for parent in (session.manifest_dir, session.dropin_dir):
            for path in (parent, *parent.parents):
                if path == home or path == runtime:
                    break
                if path.is_symlink() or (path.exists() and
                        (not path.is_dir() or path.stat().st_uid != os.getuid())):
                    raise RuntimeError(f"Unexpected activation directory ownership/type: {path}")
    if args.mode == "start":
        if not args.sha256 or len(args.sha256) != 64:
            parser.error("start requires the reviewed --sha256")
        session.start(args.sha256)
    elif args.mode == "watchdog":
        session.watchdog()
    else:
        session.rollback()


if __name__ == "__main__":
    main()
