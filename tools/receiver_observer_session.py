#!/usr/bin/env python3
"""Manual, exact-owned user-service receiver observer session.

An offline proposal is never authorization. This helper requires separate scope
and restart/stream readiness acknowledgments. No timers, sudo or native patches.
"""
from __future__ import annotations

import argparse
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import stat
import subprocess
import time

UNIT = "steam.service"
SCHEMA = "frame-receiver-observer-proposal-v2"
LIMIT = 1048576
RECEIVER = "31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153"
# Keep rollback self-contained, while refusing plans from the earlier pin.
SELECTION_CALLER_RETURNS = ["0x355f8", "0x3591c"]
METADATA_CALLER_RETURNS = ["0x538dc", "0x5390c"]
PREFIX = "FRAME_RECEIVER_"
FOREIGN_PREFIXES = (PREFIX, "FRAME_MASK_TRACE")
SESSION_MANAGER_PATHS = {"GAMESCOPE_LIMITER_FILE": "gamescope-limiter-",
                         "MANGOHUD_CONFIGFILE": "gamescope-mangoapp-"}
MOUNTINFO_LIMIT = 524288
MOUNTINFO_ROWS = 4096


def mountinfo_entries(data):
    """Parse bounded kernel mount metadata; never guess stacked mount order."""
    if len(data) > MOUNTINFO_LIMIT:
        raise RuntimeError("Receiver mountinfo byte bound exceeded")
    try:
        rows = data.decode("utf-8").splitlines()
    except UnicodeError as error:
        raise RuntimeError("Unsupported receiver mountinfo encoding") from error
    if not rows or len(rows) > MOUNTINFO_ROWS:
        raise RuntimeError("Receiver mountinfo row bound exceeded")
    entries, ids = [], set()
    def path_field(raw):
        escaped = {"040": " ", "011": "\t", "012": "\n", "134": "\\"}
        def unescape(match):
            if match[1] not in escaped:
                raise RuntimeError("Unsupported receiver mountinfo escape")
            return escaped[match[1]]
        value = re.sub(r"\\([0-9]{3})", unescape, raw)
        if ("\\" in value or not value.startswith("/") or value.startswith("//") or ".." in Path(value).parts
                or os.path.normpath(value) != value or str(Path(value)) != value
                or re.search(r"[\x00-\x1f\x7f]", value)):
            raise RuntimeError("Unsupported receiver mountinfo path")
        return value
    for row in rows:
        cols = row.split()
        if len(row) > 8192 or cols.count("-") != 1:
            raise RuntimeError("Unsupported receiver mountinfo row")
        separator = cols.index("-")
        if separator < 6 or len(cols) != separator + 4:
            raise RuntimeError("Unsupported receiver mountinfo fields")
        if any(not re.fullmatch(r"[0-9]{1,10}", value) for value in cols[:2]):
            raise RuntimeError("Unsupported receiver mountinfo ID")
        mount_id, parent = map(int, cols[:2])
        if mount_id <= 0 or parent <= 0 or mount_id in ids:
            raise RuntimeError("Ambiguous receiver mountinfo ID")
        ids.add(mount_id)
        if not re.fullmatch(r"[0-9]{1,10}:[0-9]{1,10}", cols[2]):
            raise RuntimeError("Unsupported receiver mountinfo device")
        device = tuple(map(int, cols[2].split(":")))
        if any(value > 0xffffffff for value in device):
            raise RuntimeError("Unsupported receiver mountinfo device")
        entries.append({"id": mount_id, "device": device, "root": path_field(cols[3]),
                        "point": path_field(cols[4]), "type": cols[separator + 1],
                        "options": cols[separator + 3].split(",")})
    return entries


def effective_mount(entries, name):
    if (not name.startswith("/") or name.startswith("//") or ".." in Path(name).parts or os.path.normpath(name) != name
            or str(Path(name)) != name
            or re.search(r"[\x00-\x1f\x7f]", name) or "\\" in name):
        raise RuntimeError("Unsupported mapped native path")
    candidates = [entry for entry in entries if entry["point"] == "/" or name == entry["point"]
                  or name.startswith(entry["point"] + "/")]
    if not candidates:
        raise RuntimeError("Mapped native file has no effective mount")
    depth = max(len(Path(entry["point"]).parts) for entry in candidates)
    selected = [entry for entry in candidates if len(Path(entry["point"]).parts) == depth]
    if len(selected) != 1:
        raise RuntimeError("Mapped native file has ambiguous effective mounts")
    return selected[0]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(path):
    path = Path(path)
    if not path.is_absolute() or ".." in path.parts or str(path) != os.path.normpath(str(path)):
        raise RuntimeError("Expected an absolute normalized path")
    current = Path("/")
    for part in path.parts[1:]:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode):
            raise RuntimeError("Symlink path refused")
        if current != path and (not stat.S_ISDIR(info.st_mode) or info.st_mode & 0o022):
            # /tmp is only allowed as the test fixture's ancestor, not by CLI.
            if not (stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and info.st_mode & stat.S_ISVTX):
                raise RuntimeError("Unsafe parent directory")
    return path


def private_parents(path):
    """Create only missing parents privately; never chmod existing directories."""
    path = safe_path(path)
    missing = []
    current = path
    while not current.exists():
        missing.append(current)
        current = current.parent
    for directory in reversed(missing):
        directory.mkdir(mode=0o700)
        directory.chmod(0o700)
    safe_path(path / "checked-child")


def read_regular(path, limit=LIMIT, private=False):
    path = safe_path(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or before.st_size > limit:
            raise RuntimeError("Unsafe file type, links or size")
        if private and (before.st_uid != os.getuid() or stat.S_IMODE(before.st_mode) != 0o600):
            raise RuntimeError("Private file ownership or permissions changed")
        data = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
        current = path.lstat()
        fields = lambda x: (x.st_dev, x.st_ino, x.st_size, x.st_mtime_ns, x.st_mode, x.st_uid, x.st_nlink)
        if len(data) > limit or fields(before) != fields(after) or fields(after) != fields(current):
            raise RuntimeError("File changed during validation")
        return data


def exclusive(path, data):
    path = safe_path(path)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    with os.fdopen(fd, "wb") as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def compare_manager(original, restored, runtime):
    """Native restarts can replace only these two per-session file paths."""
    changed = sorted(key for key in original.keys() | restored.keys()
                     if key not in original or key not in restored or original[key] != restored[key])
    def session_path(key, value):
        return (isinstance(value, str) and value.startswith("/") and ".." not in Path(value).parts
                and os.path.normpath(value) == value and not re.search(r"[\x00-\x1f]", value)
                and Path(value).parent == runtime and Path(value).name.startswith(SESSION_MANAGER_PATHS[key])
                and len(Path(value).name) > len(SESSION_MANAGER_PATHS[key]))
    permitted = all(key in SESSION_MANAGER_PATHS and key in original and key in restored
                    and session_path(key, original[key]) and session_path(key, restored[key]) for key in changed)
    return {"exact_manager": original == restored,
            "manager_matches_except_session_paths": permitted, "manager_changed_keys": changed,
            "permitted_path_changes": changed if permitted else []}


class Session:
    def __init__(self, home, runtime, run=subprocess.run, environment=None, proc_root=Path("/proc")):
        self.home, self.runtime = safe_path(home), safe_path(runtime)
        self.folder = self.home / ".local/share/frame-receiver-observer"
        self.dropin = self.runtime / "systemd/user/steam.service.d/91-frame-receiver-observer.conf"
        self.old_dropin = self.runtime / "systemd/user/steamvr.service.d/91-frame-receiver-observer.conf"
        self.library = self.folder / "libframe_receiver_observer.so"
        self.capture = self.folder / "capture.jsonl"
        self.marker = self.folder / "arm.marker"
        self.run = run
        self.environment = dict(os.environ if environment is None else environment)
        self.proc = Path(proc_root)

    def command(self, *args):
        result = self.run(("systemctl", "--user", *args), check=False, timeout=45,
                          text=True, capture_output=True)
        if result.returncode or len(result.stdout) + len(result.stderr) > LIMIT:
            raise RuntimeError("User-service command failed or exceeded output bound")
        return result.stdout

    def private_folder(self):
        safe_path(self.folder)
        info = self.folder.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
            raise RuntimeError("Owned session directory is not private")

    def event(self, name, **fields):
        path = self.folder / "session.jsonl"
        self.private_folder()
        read_regular(path, private=True)
        fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_NOFOLLOW | os.O_CLOEXEC)
        with os.fdopen(fd, "wb") as stream:
            data = (json.dumps(dict(event=name, time=time.time(), **fields), sort_keys=True) + "\n").encode()
            if os.fstat(stream.fileno()).st_size + len(data) > LIMIT:
                raise RuntimeError("Session ledger bound reached")
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())

    def recovery_event(self, name, **fields):
        # A full ledger/filesystem must not keep the exact owned preload active.
        try:
            self.event(name, **fields)
        except (OSError, RuntimeError):
            pass

    def lock(self):
        self.private_folder()
        path = self.folder / "operation.lock"
        read_regular(path, private=True)
        fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC)
        stream = os.fdopen(fd, "rb+")
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            stream.close()
            raise
        return stream

    def state(self):
        data = self.command("show", UNIT, "--property=Environment,EnvironmentFiles,DropInPaths")
        result = dict(line.split("=", 1) for line in data.splitlines() if "=" in line)
        if set(result) != {"Environment", "EnvironmentFiles", "DropInPaths"}:
            raise RuntimeError("Incomplete service environment inspection")
        return result

    @staticmethod
    def env_values(text):
        values = {}
        for entry in shlex.split(text):
            if "=" not in entry:
                raise RuntimeError("Malformed service environment")
            key, value = entry.split("=", 1)
            if key in values:
                raise RuntimeError("Duplicate service environment entry")
            values[key] = value
        return values

    def reject_environment(self, state):
        manager = self.env_values(self.command("show-environment"))
        for values in (self.environment, manager, self.env_values(state["Environment"])):
            if any((k == "LD_PRELOAD" or k.startswith(FOREIGN_PREFIXES)) and v for k, v in values.items()):
                raise RuntimeError("Foreign preload or receiver observer environment")
        for path in shlex.split(state["DropInPaths"]):
            data = read_regular(Path(path), 65536)
            if b"LD_PRELOAD" in data or b"FRAME_RECEIVER_" in data or b"FRAME_MASK_TRACE" in data:
                raise RuntimeError("Foreign observer or preload drop-in")
        return manager

    def native_environment_files(self, state, reviewed):
        # systemctl's native shape is PATH (ignore_errors=no). Unknown syntax,
        # optional missing files, shell directives and multiline values fail closed.
        text = state["EnvironmentFiles"]
        entries = re.findall(r"([^\s]+) \(ignore_errors=(?:no|yes)\)", text)
        residue = re.sub(r"[^\s]+ \(ignore_errors=(?:no|yes)\)", "", text)
        if residue.strip() or len(entries) > 4 or len(entries) != len(set(entries)):
            raise RuntimeError("Unsupported native EnvironmentFiles syntax")
        if set(entries) != set(reviewed):
            raise RuntimeError("Explicit reviewed native environment file fingerprints required")
        snapshots = {}
        for name in entries:
            raw = read_regular(Path(name), 65536)
            if not re.fullmatch(r"[0-9a-f]{64}", reviewed[name]) or digest(raw) != reviewed[name]:
                raise RuntimeError("Native environment file fingerprint mismatch")
            values = {}
            for line in raw.decode("utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith(("#", ";")):
                    continue
                match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)=(.*)", line)
                if not match or "\\" in line:
                    raise RuntimeError("Unsupported native environment assignment")
                key, value = match.groups()
                value = value.strip()
                if value.startswith(("'", '"')):
                    if len(value) < 2 or value[-1] != value[0] or value[0] in value[1:-1]:
                        raise RuntimeError("Unsupported native environment quoting")
                    value = value[1:-1]
                elif "'" in value or '"' in value:
                    raise RuntimeError("Unsupported native environment quoting")
                if key in values:
                    raise RuntimeError("Duplicate native environment assignment")
                if (key == "LD_PRELOAD" or key.startswith(FOREIGN_PREFIXES)) and value:
                    raise RuntimeError("Foreign observer in native environment file")
                values[key] = value
            snapshots[name] = {"sha256": digest(raw), "bytes_hex": raw.hex(), "values": values}
        return snapshots

    def activation(self):
        values = {"LD_PRELOAD": str(self.library), "FRAME_RECEIVER_TRACE_OUTPUT": str(self.capture),
                  "FRAME_RECEIVER_TRACE_ARM": str(self.marker)}
        # systemd specifier expansion is separate from quoted Environment syntax.
        if any(re.search(r'[\s%\\"\x00-\x1f]', value) for value in values.values()):
            raise RuntimeError("Unsupported activation path characters")
        return ("[Service]\n" + "".join(f'Environment="{k}={v}"\n' for k, v in values.items())).encode()

    def plan(self, path, expected, inputs):
        raw = read_regular(path, private=True)
        if not re.fullmatch(r"[0-9a-f]{64}", expected) or digest(raw) != expected:
            raise RuntimeError("Reviewed plan fingerprint mismatch")
        plan = json.loads(raw)
        if (plan.get("schema") != SCHEMA or plan.get("offline_files_supported") is not True
                or plan.get("receiver_service") != UNIT
                or plan.get("installed") is not False or plan.get("runtime_authorized") is not False
                or plan.get("proposed_owned_directory") != str(self.folder)
                or plan.get("proposed_runtime_dropin_basename") != self.dropin.name
                or plan.get("selection_caller_returns") != SELECTION_CALLER_RETURNS
                or plan.get("metadata_caller_returns") != METADATA_CALLER_RETURNS
                or plan.get("limits") != {"selections": 32, "bytes": LIMIT, "seconds": 120}):
            raise RuntimeError("Unsupported proposed scope or paths")
        for flag in ("image_or_mapped_uniform_reads", "native_binary_patch", "kernel_or_system_image_change",
                     "shader_or_mask_activation", "automatic_restart"):
            if plan.get(flag) is not False:
                raise RuntimeError("Proposal exceeds the prepared passive scope")
        if set(inputs) != {"receiver", "provider", "loader", "observer"}:
            raise RuntimeError("Fresh native receiver, provider, loader and observer paths are required")
        files = {}
        for name, path in inputs.items():
            data = read_regular(path, 262144 if name == "observer" else 16 * LIMIT)
            info = plan.get(name)
            if (not isinstance(info, dict) or info.get("sha256") != digest(data)
                    or info.get("bytes") != len(data) or info.get("elf_machine") != 183
                    or info.get("elf_type") != 3 or len(data) < 64
                    or data[:7] != b"\x7fELF\x02\x01\x01" or data[16:20] != b"\x03\x00\xb7\x00"):
                raise RuntimeError("Fresh native artifact fingerprint or ABI mismatch")
            files[name] = {"path": str(safe_path(path)), "sha256": digest(data), "bytes": len(data)}
        if plan["receiver"]["sha256"] != RECEIVER or Path(inputs["receiver"]).name != "vrlink":
            raise RuntimeError("Unsupported native receiver")
        return raw, plan, files, read_regular(inputs["observer"], 262144)

    def start(self, path, expected, inputs, *, runtime_scope=False, restart_ready=False,
              reviewed_environment_files=None):
        if not runtime_scope or not restart_ready:
            raise RuntimeError("Private scalar/preload scope and restart readiness acknowledgments required")
        safe_path(self.folder)
        safe_path(self.dropin)
        safe_path(self.old_dropin)
        if self.old_dropin.exists() or self.old_dropin.is_symlink():
            raise RuntimeError("Old SteamVR activation remains; use its original session helper for rollback")
        if self.folder.exists() or self.dropin.exists():
            raise RuntimeError("Existing session or activation refused")
        raw, plan, files, binary = self.plan(path, expected, inputs)
        self.command("is-active", UNIT)
        original = self.state()
        original_manager = self.reject_environment(original)
        original_environment_files = self.native_environment_files(original, reviewed_environment_files or {})
        activation = self.activation()
        private_parents(self.folder.parent)
        self.folder.mkdir(mode=0o700)
        self.folder.chmod(0o700)
        exclusive(self.folder / "operation.lock", b"")
        exclusive(self.folder / "session.jsonl", b"")
        with self.lock():
            manifest = {"schema": "receiver-observer-owned-session-v2", "receiver_service": UNIT,
                        "scope_acknowledged": True,
                        "plan_sha256": expected, "original_service": original,
                        "original_manager_environment": original_manager,
                        "original_environment_files": original_environment_files, "native_files": files,
                        "activation": {"path": str(self.dropin), "bytes_hex": activation.hex(),
                                       "sha256": digest(activation)},
                        "owned_files": {"libframe_receiver_observer.so": digest(binary),
                                        "proposal.json": digest(raw),
                                        "receiver_observer_session.py": digest(Path(__file__).read_bytes())}}
            # Persist exact activation bytes BEFORE anything enables the preload.
            exclusive(self.folder / "owned.json", json_bytes(manifest))
            exclusive(self.folder / "proposal.json", raw)
            exclusive(self.library, binary)
            exclusive(self.folder / "receiver_observer_session.py", Path(__file__).read_bytes())
            self.event("scope_acknowledged", runtime_permission_from_plan=False,
                       restart_ready=True, plan_sha256=expected, original_service=original,
                       native_files=files, automatic_restart=False)
            try:
                private_parents(self.dropin.parent)
                exclusive(self.dropin, activation)
                self.event("activation_created", path=str(self.dropin), sha256=digest(activation))
                self.command("daemon-reload")
                self.event("restart_requested", phase="observer", readiness_acknowledged=True)
                self.command("restart", UNIT)
                self.command("is-active", UNIT)
                self.event("service_active", receiver_writer_proven=False, capture_accepted=False)
            except BaseException:
                self._rollback(restart_ready=True)
                raise

    def manifest(self):
        self.private_folder()
        manifest = json.loads(read_regular(self.folder / "owned.json", private=True))
        expected = self.activation()
        if (manifest.get("schema") != "receiver-observer-owned-session-v2"
                or manifest.get("receiver_service") != UNIT
                or manifest.get("scope_acknowledged") is not True
                or set(manifest.get("owned_files", {})) != {"libframe_receiver_observer.so",
                    "proposal.json", "receiver_observer_session.py"}
                or manifest.get("activation") != {"path": str(self.dropin),
                    "bytes_hex": expected.hex(), "sha256": digest(expected)}):
            raise RuntimeError("Owned activation snapshot is invalid")
        return manifest

    @staticmethod
    def proc_bytes(path, limit):
        # Only explicit proc metadata files are read, never mem/map_files.
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        with os.fdopen(fd, "rb") as stream:
            if not stat.S_ISREG(os.fstat(fd).st_mode):
                raise RuntimeError("Unexpected proc metadata type")
            data = stream.read(limit + 1)
            if len(data) > limit:
                raise RuntimeError("Proc metadata bound exceeded")
            return data

    def capture_owner(self, capture):
        rows = self.proc_bytes(self.proc / "locks", LIMIT).decode("ascii").splitlines()
        if len(rows) > 8192:
            raise RuntimeError("Proc lock row bound exceeded")
        owners = []
        for row in rows:
            cols = row.split()
            if len(cols) != 8 or cols[1:4] != ["FLOCK", "ADVISORY", "WRITE"]:
                continue
            device = cols[5].split(":")
            if len(device) != 3:
                raise RuntimeError("Unsupported proc lock identity")
            identity = (int(device[0], 16), int(device[1], 16), int(device[2]))
            if identity == (os.major(capture.st_dev), os.minor(capture.st_dev), capture.st_ino):
                if cols[6:] != ["0", "EOF"] or not cols[4].isdigit() or int(cols[4]) <= 0:
                    raise RuntimeError("Unsupported capture writer lock")
                owners.append(int(cols[4]))
        if len(owners) != 1:
            raise RuntimeError("Capture requires one identified exclusive writer process")
        return owners[0]

    def receiver_process(self, manifest, capture):
        pid = self.capture_owner(capture)
        process = self.proc / str(pid)
        info = process.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid():
            raise RuntimeError("Receiver process is not owned by the current user")
        def started():
            data = self.proc_bytes(process / "stat", 8192).decode("ascii")
            end = data.rfind(")")
            values = data[end + 2:].split()
            if end < 0 or data.split(" ", 1)[0] != str(pid) or len(values) < 20:
                raise RuntimeError("Unsupported receiver process stat")
            return int(values[19])  # Linux proc stat field 22, starttime.
        start = started()
        cgroup = self.proc_bytes(process / "cgroup", 8192).decode("ascii")
        groups = cgroup.splitlines()
        if not groups or len(groups) > 32 or any(len(row.split(":", 2)) != 3 or
                UNIT not in Path(row.split(":", 2)[2]).parts for row in groups):
            raise RuntimeError("Capture writer does not belong to steam.service")
        exe = os.readlink(process / "exe")
        if not exe.startswith("/") or Path(exe).name != "vrlink":
            raise RuntimeError("Capture writer is not the native receiver")

        mount_snapshot, mounts, reconciled, disk_files = None, None, {}, {}
        stable = lambda item: (item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns)
        def btrfs_identity(name, before, identity, expected):
            nonlocal mount_snapshot, mounts
            actual = (os.major(before.st_dev), os.minor(before.st_dev), before.st_ino)
            if (actual[2] != identity[2] or actual[0] != 0 or actual[1] == 0
                    or name != expected["path"]):
                raise RuntimeError("Mapped native file identity mismatch")
            if mount_snapshot is None:
                mount_snapshot = self.proc_bytes(process / "mountinfo", MOUNTINFO_LIMIT)
                mounts = mountinfo_entries(mount_snapshot)
            mount = effective_mount(mounts, name)
            # Linux v6.16 show_map_vma and show_mountinfo use i_sb->s_dev;
            # btrfs_getattr uses root->anon_dev instead. This reconciliation
            # supports only the observed top-level Btrfs root. It must prove
            # the file is on that same anonymous root, not another subvolume
            # with a colliding inode. No content hash substitutes for identity.
            # https://github.com/torvalds/linux/blob/v6.16/fs/btrfs/inode.c
            # https://github.com/torvalds/linux/blob/v6.16/fs/proc/task_mmu.c
            # https://github.com/torvalds/linux/blob/v6.16/fs/proc_namespace.c
            if (mount["type"] != "btrfs" or mount["device"] != identity[:2]
                    or identity[0] != 0 or identity[1] == 0
                    or mount["root"] != "/" or mount["point"] != "/"
                    or [value for value in mount["options"] if value.startswith("subvolid=")] != ["subvolid=5"]
                    or [value for value in mount["options"] if value.startswith("subvol=")] != ["subvol=/"]):
                raise RuntimeError("Mapped native file lacks the reviewed Btrfs root identity")
            root = (process / "root").stat()
            if (not stat.S_ISDIR(root.st_mode) or root.st_ino != 256 or root.st_dev != before.st_dev):
                raise RuntimeError("Mapped native file is not on the Btrfs anonymous root")
            reconciled[name] = {"mount_id": mount["id"], "map_device": list(identity[:2]),
                                "stat_device": list(actual[:2]), "inode": identity[2],
                                "root_identity": (root.st_dev, root.st_ino)}

        def native_file(path, expected, identity=None, *, limit=16 * LIMIT, mapped_name=None):
            # /proc/PID/exe and /proc/PID/root intentionally follow kernel links
            # to on-disk ELF files. They are not reads of mapped process memory.
            with os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC), "rb") as stream:
                before = os.fstat(stream.fileno())
                if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
                    raise RuntimeError("Mapped native file identity mismatch")
                actual = (os.major(before.st_dev), os.minor(before.st_dev), before.st_ino)
                if identity is not None and actual != identity:
                    if mapped_name is None:
                        raise RuntimeError("Mapped native file identity mismatch")
                    btrfs_identity(mapped_name, before, identity, expected)
                data = stream.read(limit + 1)
                after = os.fstat(stream.fileno())
                if (len(data) > limit or stable(before) != stable(after) or len(data) != expected["bytes"]
                        or digest(data) != expected["sha256"]):
                    raise RuntimeError("Mapped native file fingerprint mismatch")
                disk_files[path] = stable(before)
                return actual
        exe_identity = native_file(process / "exe", manifest["native_files"]["receiver"])

        def maps():
            rows = self.proc_bytes(process / "maps", 524288).decode("utf-8").splitlines()
            if len(rows) > 4096:
                raise RuntimeError("Receiver mapping row bound exceeded")
            files = set()
            for row in rows:
                cols = row.split(None, 5)
                if len(cols) == 6 and cols[5].startswith("/") and not cols[5].endswith(" (deleted)"):
                    device = cols[3].split(":")
                    if len(device) != 2:
                        raise RuntimeError("Unsupported receiver mapping device")
                    files.add((cols[5], int(device[0], 16), int(device[1], 16), int(cols[4])))
            return files
        mappings = maps()
        selected = {}
        for role in ("observer", "provider", "loader"):
            expected = manifest["native_files"][role]
            names = {Path(expected["path"]).name}
            if role == "observer":
                matches = [entry for entry in mappings if entry[0] == str(self.library)]
            else:
                names.add("libc.so.6" if role == "provider" else "ld-linux-aarch64.so.1")
                matches = [entry for entry in mappings if Path(entry[0]).name in names]
            if len(matches) != 1:
                raise RuntimeError("Missing or ambiguous actual receiver provider/observer mapping")
            name, major, minor, inode = matches[0]
            native_file(process / "root" / name.lstrip("/"), expected, (major, minor, inode),
                        limit=262144 if role == "observer" else 16 * LIMIT,
                        mapped_name=name if role in ("provider", "loader") else None)
            selected[role] = matches[0]
        environment = self.proc_bytes(process / "environ", 65536).split(b"\0")
        values = {}
        for entry in environment:
            if not entry:
                continue
            key, value = entry.decode("utf-8").split("=", 1)
            if key == "LD_PRELOAD" or key.startswith(FOREIGN_PREFIXES):
                if key in values:
                    raise RuntimeError("Duplicate receiver observer environment")
                values[key] = value
        expected_environment = {"LD_PRELOAD": str(self.library), "FRAME_RECEIVER_TRACE_OUTPUT": str(self.capture),
                                "FRAME_RECEIVER_TRACE_ARM": str(self.marker)}
        if values != expected_environment:
            raise RuntimeError("Actual receiver did not inherit the exact observer environment")
        if mount_snapshot is not None:
            if self.proc_bytes(process / "mountinfo", MOUNTINFO_LIMIT) != mount_snapshot:
                raise RuntimeError("Receiver mounts changed during verification")
            root = (process / "root").stat()
            if (not stat.S_ISDIR(root.st_mode) or any((root.st_dev, root.st_ino) != proof["root_identity"]
                                                    for proof in reconciled.values())):
                raise RuntimeError("Receiver Btrfs anonymous root changed during verification")
        if (started() != start or self.capture_owner(capture) != pid or
                self.proc_bytes(process / "cgroup", 8192).decode("ascii") != cgroup or
                not set(selected.values()).issubset(maps()) or
                any(stable(path.stat()) != fingerprint for path, fingerprint in disk_files.items()) or
                os.readlink(process / "exe") != exe or
                (os.major((last := (process / "exe").stat()).st_dev), os.minor(last.st_dev),
                 last.st_ino) != exe_identity):
            raise RuntimeError("Receiver process/lock/mappings changed during verification")
        return {"pid": pid, "starttime_ticks": start, "service": UNIT,
                "executable_sha256": manifest["native_files"]["receiver"]["sha256"],
                "mapped_file_sha256": {role: manifest["native_files"][role]["sha256"] for role in selected},
                "btrfs_root_identity_reconciliation": {name: {key: value for key, value in proof.items()
                    if key != "root_identity"} for name, proof in reconciled.items()}}

    def require_writer(self, manifest):
        safe_path(self.capture)
        fd = os.open(self.capture, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        with os.fdopen(fd, "rb", buffering=0) as stream:
            before = os.fstat(fd)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid()
                    or stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1
                    or not 0 < before.st_size <= LIMIT):
                raise RuntimeError("Unsafe capture file")
            def writer_locked():
                try:
                    fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
                except OSError as exc:
                    if exc.errno not in (errno.EAGAIN, errno.EWOULDBLOCK):
                        raise RuntimeError("Writer liveness cannot be checked") from exc
                else:
                    fcntl.flock(fd, fcntl.LOCK_UN)
                    raise RuntimeError("Capture writer is not alive; observer needs exclusive_flock_v1")
            writer_locked()
            data = stream.read(LIMIT + 1)
            after, current = os.fstat(fd), self.capture.lstat()
            if (len(data) > LIMIT or after.st_size > LIMIT or after.st_size < before.st_size
                    or (before.st_dev, before.st_ino) != (current.st_dev, current.st_ino)
                    or current.st_mode != before.st_mode or current.st_uid != before.st_uid
                    or current.st_nlink != 1):
                raise RuntimeError("Capture changed during liveness check")
            lines = data.split(b"\n")
            if len(lines) > 36 or len(lines) < 2:
                raise RuntimeError("Capture header/record bound invalid")
            header = json.loads(lines[0])
            expected = {"kind": "header", "supported": True,
                        "executable_sha256": manifest["native_files"]["receiver"]["sha256"],
                        "sample_limit": 32, "byte_limit": LIMIT, "deadline_seconds": 120,
                        "evidence": "protected_selection_attempt", "accepted_submit": False,
                        "source_frame_associated": False, "gpu_consumed_coherent": False,
                        "writer_liveness": "exclusive_flock_v1"}
            if (not isinstance(header, dict) or header != expected
                    or any(type(header[k]) is not type(v) for k, v in expected.items())):
                raise RuntimeError("Production capture header/liveness protocol mismatch")
            for line in lines[1:]:
                if b'"footer"' in line or b'"header"' in line:
                    raise RuntimeError("Writer is finished or capture restarted")
                if line:
                    row = json.loads(line)
                    if not isinstance(row, dict) or row.get("kind") != "selection_attempt":
                        raise RuntimeError("Unexpected capture record")
            process = self.receiver_process(manifest, before)
            writer_locked()
            return process

    def arm(self, *, stream_ready=False):
        if not stream_ready:
            raise RuntimeError("Visible PC VR stream readiness acknowledgment required")
        with self.lock():
            manifest = self.manifest()
            if (self.folder / "rollback-complete.json").exists():
                raise RuntimeError("Session already restored")
            if read_regular(self.dropin, private=True) != bytes.fromhex(manifest["activation"]["bytes_hex"]):
                raise RuntimeError("Owned activation changed")
            for name, sha in manifest["owned_files"].items():
                if Path(name).name != name or digest(read_regular(self.folder / name, private=True)) != sha:
                    raise RuntimeError("Owned observer or plan changed")
            self.command("is-active", UNIT)
            state = self.state()
            snapshots = self.native_environment_files(state, {name: data["sha256"]
                    for name, data in manifest["original_environment_files"].items()})
            if snapshots != manifest["original_environment_files"]:
                raise RuntimeError("Native environment files changed after activation")
            actual = self.env_values(state["Environment"])
            expected = {"LD_PRELOAD": str(self.library), "FRAME_RECEIVER_TRACE_OUTPUT": str(self.capture),
                        "FRAME_RECEIVER_TRACE_ARM": str(self.marker)}
            if any(actual.get(k) != v for k, v in expected.items()):
                raise RuntimeError("Service activation environment mismatch")
            if any(k.startswith(FOREIGN_PREFIXES) and k not in expected and v for k, v in actual.items()):
                raise RuntimeError("Foreign observer environment after activation")
            process = self.require_writer(manifest)
            exclusive(self.marker, b"")
            self.event("arm_requested", capture_accepted=False, stream_ready_acknowledged=True,
                       accepted_submit=False, source_frame_associated=False, receiver_process=process)

    def rollback(self, *, restart_ready=False):
        if not restart_ready:
            raise RuntimeError("Native restart readiness acknowledgment required")
        if not self.folder.exists():
            if self.dropin.exists() or self.dropin.is_symlink():
                raise RuntimeError("Foreign activation exists without an owned session")
            return
        with self.lock():
            self._rollback(restart_ready=restart_ready)

    def _rollback(self, *, restart_ready):
        manifest = self.manifest()
        complete = self.folder / "rollback-complete.json"
        if complete.exists():
            recorded = json.loads(read_regular(complete, private=True))
            if any(recorded.get(key) is not True for key in ("restored", "runtime_activation_absent",
                    "original_service_matches")):
                raise RuntimeError("Recorded restoration is invalid")
            if self.dropin.exists() or self.dropin.is_symlink():
                raise RuntimeError("Activation appeared after recorded restoration")
            return
        expected = bytes.fromhex(manifest["activation"]["bytes_hex"])
        if self.dropin.exists() or self.dropin.is_symlink():
            if read_regular(self.dropin, private=True) != expected:
                raise RuntimeError("Changed foreign drop-in preserved; manual recovery required")
            self.dropin.unlink()
        # Removal is the first recovery mutation; logging failure must not keep
        # the owned preload enabled on a full filesystem.
        self.recovery_event("activation_removed", path=str(self.dropin), sha256=digest(expected))
        self.command("daemon-reload")
        self.recovery_event("restart_requested", phase="native", readiness_acknowledged=restart_ready)
        self.command("restart", UNIT)
        self.command("is-active", UNIT)
        restored = self.state()
        restored_manager = self.reject_environment(restored)
        snapshots = self.native_environment_files(restored, {name: data["sha256"]
                    for name, data in manifest["original_environment_files"].items()})
        comparison = compare_manager(manifest["original_manager_environment"], restored_manager, self.runtime)
        self.recovery_event("restoration_comparison", original_service_matches=restored == manifest["original_service"],
                            native_environment_files_match=snapshots == manifest["original_environment_files"],
                            **comparison)
        if (restored != manifest["original_service"]
                or not comparison["manager_matches_except_session_paths"]
                or snapshots != manifest["original_environment_files"]):
            raise RuntimeError("Native service configuration differs from original snapshot")
        exclusive(complete, json_bytes({"restored": True, "runtime_activation_absent": True,
                                       "original_service_matches": True, "time": time.time(),
                                       "native_receiver_process_verified": False, **comparison}))
        self.event("rollback_complete", receiver_process_verified=False, **comparison)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("start", "arm", "rollback"))
    parser.add_argument("--reviewed-plan", type=Path)
    parser.add_argument("--reviewed-plan-sha256")
    parser.add_argument("--native-receiver", type=Path)
    parser.add_argument("--native-provider-libc", type=Path)
    parser.add_argument("--native-loader", type=Path)
    parser.add_argument("--observer", type=Path)
    parser.add_argument("--reviewed-environment-file", action="append", default=[], metavar="PATH=SHA256")
    parser.add_argument("--ack-private-scalar-preload", action="store_true")
    parser.add_argument("--ack-steam-client-restart-readiness", action="store_true")
    parser.add_argument("--ack-vr-stream-ready", action="store_true")
    args = parser.parse_args(argv)
    try:
        if platform.system() != "Linux" or platform.machine().lower() not in ("aarch64", "arm64"):
            raise RuntimeError("Receiver activation requires the native Linux/AArch64 runtime")
        home = Path.home()
        runtime = Path(os.environ.get("XDG_RUNTIME_DIR", ""))
        if runtime != Path(f"/run/user/{os.getuid()}"):
            raise RuntimeError("Expected the native user runtime directory")
        session = Session(home, runtime)
        if args.mode == "start":
            if any(value is None for value in (args.reviewed_plan, args.reviewed_plan_sha256,
                    args.native_receiver, args.native_provider_libc, args.native_loader, args.observer)):
                raise RuntimeError("Explicit reviewed plan and fresh native artifact paths required")
            reviewed = dict(entry.rsplit("=", 1) for entry in args.reviewed_environment_file)
            if len(reviewed) != len(args.reviewed_environment_file) or len(reviewed) > 4:
                raise RuntimeError("Duplicate or excessive reviewed environment file arguments")
            session.start(args.reviewed_plan, args.reviewed_plan_sha256,
                          {"receiver": args.native_receiver, "provider": args.native_provider_libc,
                           "loader": args.native_loader, "observer": args.observer},
                          runtime_scope=args.ack_private_scalar_preload,
                          restart_ready=args.ack_steam_client_restart_readiness,
                          reviewed_environment_files=reviewed)
        elif args.mode == "arm":
            session.arm(stream_ready=args.ack_vr_stream_ready)
        else:
            session.rollback(restart_ready=args.ack_steam_client_restart_readiness)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Receiver observer session refused: {type(exc).__name__}: {exc}\n")
    print("Manual session operation completed; no frame/GPU association or headset result is claimed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
