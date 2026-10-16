#!/usr/bin/env python3
"""Cold, reversible file staging for the one-shot public-Vulkan pixel diagnostic."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import selectors
import stat
import struct
import subprocess
import sys
import time

MAX_LIBRARY = 8 * 1024 * 1024
MAX_JSON = 16 * 1024
# Bounded cold diagnostic groups can exceed one small JSON document. Ordinary
# configuration/audit JSON keeps its independent strict16KiB limit.
MAX_PROGRESS = 512 * 1024
MAX_MAP = 1024 * 1024
MAX_MAP_TOTAL = 64 * MAX_MAP
MAX_PROCESSES = 4096
MAX_MAP_STDERR = 4096
MAP_READ_TIMEOUT = 3.0
CONFIG_MAGIC = b"GHVR_PIXEL_SNAPSHOT/1\n"
ARM_MAGIC = b"GHVR_PIXEL_SNAPSHOT_ARM/1\n"
CLAIM_MAGIC = b"GHVR_PIXEL_CLAIM/1\n"
DISARM_MAGIC = b"GHVR_PIXEL_SNAPSHOT_DISARMED/1\n"
LIBRARY = "libVkLayer_frame_pixel_snapshot.so"
MANIFEST = "VkLayer_GHVR_pixel_snapshot.json"
RECEIPT = "session-receipt.json"

class SafetyError(ValueError):
    pass

def digest(data):
    return hashlib.sha256(data).hexdigest()

def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()

def absolute(path):
    path = Path(path)
    if not path.is_absolute() or ".." in path.parts or len(str(path)) > 1024:
        raise SafetyError("paths must be bounded absolute paths without parent traversal")
    return path

def directory(path, *, create=False, private=False, owned=True):
    """Pin every parent without links; never chmod a pre-existing directory."""
    path = absolute(path)
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        for part in path.parts[1:]:
            try:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            except FileNotFoundError:
                if not create:
                    raise
                os.mkdir(part, mode=0o700, dir_fd=fd)
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            os.close(fd)
            fd = child
            info = os.fstat(fd)
            sticky_root = info.st_uid == 0 and info.st_mode & stat.S_ISVTX
            if info.st_mode & 0o022 and not sticky_root:
                raise SafetyError("writable foreign parent directory refused: " + str(path))
        info = os.fstat(fd)
        if (owned and info.st_uid != os.getuid()) or (private and stat.S_IMODE(info.st_mode) != 0o700):
            raise SafetyError("directory must be owned by this user" + (" with mode 0700" if private else ""))
        return fd
    except BaseException:
        os.close(fd)
        raise

def read_file(path, limit, *, mode=None, with_stat=False):
    parent = directory(path.parent)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=parent)
    finally:
        os.close(parent)
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid() or before.st_nlink != 1
                or before.st_size > limit or before.st_mode & 0o022
                or (mode is not None and stat.S_IMODE(before.st_mode) != mode)):
            raise SafetyError("input must be a bounded, owned, unlinked regular file: " + str(path))
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(limit + 1)
        after = os.fstat(fd)
        if (len(data) != before.st_size or len(data) > limit
                or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            raise SafetyError("file changed while reading: " + str(path))
        return (data, before) if with_stat else data
    finally:
        os.close(fd)

def write_new(path, data):
    parent = directory(path.parent)
    try:
        fd = os.open(path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=parent)
        try:
            os.fchmod(fd, 0o600)
            with os.fdopen(fd, "wb", closefd=False) as stream:
                stream.write(data)
                stream.flush()
            os.fsync(fd)
        finally:
            os.close(fd)
        os.fsync(parent)
    finally:
        os.close(parent)

def exists(path):
    try:
        parent = directory(path.parent)
    except FileNotFoundError:
        return False
    try:
        try:
            os.stat(path.name, dir_fd=parent, follow_symlinks=False)
            return True
        except FileNotFoundError:
            return False
    finally:
        os.close(parent)

def remove_exact(path, expected):
    if not exists(path):
        return False
    data, observed = read_file(path, MAX_LIBRARY, mode=0o600, with_stat=True)
    if digest(data) != expected:
        raise SafetyError("changed/foreign file preserved: " + str(path))
    parent = directory(path.parent)
    try:
        # Re-pin and compare the inode before unlink, never follow a raced link.
        current = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
        identity = lambda item: (item.st_dev, item.st_ino, item.st_size, item.st_mode, item.st_uid, item.st_nlink, item.st_ctime_ns)
        if identity(current) != identity(observed):
            raise SafetyError("file identity changed before removal")
        os.unlink(path.name, dir_fd=parent)
        os.fsync(parent)
    finally:
        os.close(parent)
    return True

def load_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise SafetyError("duplicate JSON key")
            result[key] = value
        return result
    def nonfinite(value):
        raise SafetyError("nonfinite JSON value refused: " + value)
    value = json.loads(read_file(path, MAX_JSON, mode=0o600), object_pairs_hook=pairs, parse_constant=nonfinite)
    if not isinstance(value, dict):
        raise SafetyError("receipt must be an object")
    return value

def verify_library(raw, wanted, *, host_fixture=False):
    if not isinstance(wanted, str) or len(wanted) != 64 or any(c not in "0123456789abcdef" for c in wanted):
        raise SafetyError("an explicit lowercase library SHA-256 is required")
    if digest(raw) != wanted or not 120 <= len(raw) <= MAX_LIBRARY:
        raise SafetyError("library hash or bounded size mismatch")
    if raw[:7] != b"\x7fELF\x02\x01\x01":
        raise SafetyError("requires a little-endian ELF64 shared library")
    kind, machine, version = struct.unpack_from("<HHI", raw, 16)
    phoff = struct.unpack_from("<Q", raw, 32)[0]
    ehsize, phsize, phnum = struct.unpack_from("<HHH", raw, 52)
    if (kind != 3 or version != 1 or machine not in ((183, 62) if host_fixture else (183,))
            or ehsize != 64 or phsize != 56 or not 1 <= phnum <= 256
            or phoff < 64 or phoff + phsize * phnum > len(raw)):
        raise SafetyError("requires bounded AArch64 ET_DYN program headers (x86-64 only with host-fixture)")
    loadable = False
    for offset in range(phoff, phoff + phsize * phnum, phsize):
        kind, _, start, _, _, size, memory, _ = struct.unpack_from("<IIQQQQQQ", raw, offset)
        if start > len(raw) or size > len(raw) - start or size > memory:
            raise SafetyError("ELF program segment exceeds owned file")
        loadable |= kind == 1 and size > 0
    if not loadable:
        raise SafetyError("ELF has no bounded load segment")
    return machine

def read_protected_maps(pid):
    """Read only one numeric proc map; never prompt, use a shell, or write as root."""
    if not isinstance(pid, str) or not pid.isascii() or not pid.isdecimal() or not 0 < int(pid) <= 2147483647:
        raise SafetyError("protected maps require a bounded numeric PID")
    argv = ["sudo", "-n", "/usr/bin/head", "-c", str(MAX_MAP + 1), "/proc/" + pid + "/maps"]
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, shell=False, close_fds=True)
    output = bytearray()
    deadline = time.monotonic() + MAP_READ_TIMEOUT
    try:
        with selectors.DefaultSelector() as selector:
            for stream, limit in ((process.stdout, MAX_MAP + 1), (process.stderr, MAX_MAP_STDERR)):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, [bytearray(), limit])
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SafetyError("protected maps read timed out")
                for key, _ in selector.select(remaining):
                    data, limit = key.data
                    chunk = os.read(key.fd, min(65536, limit + 1 - len(data)))
                    if not chunk:
                        if key.fileobj is process.stdout:
                            output = data
                        selector.unregister(key.fileobj)
                    else:
                        if len(chunk) > limit - len(data):
                            raise SafetyError("protected maps output exceeded its bound")
                        data.extend(chunk)
            remaining = deadline - time.monotonic()
            if remaining <= 0 or process.wait(timeout=remaining) != 0:
                raise SafetyError("protected maps read failed")
        return bytes(output)
    except (OSError, subprocess.SubprocessError) as error:
        raise SafetyError("protected maps read failed") from error
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
        process.stderr.close()

class Session:
    def __init__(self, home, run_dir, *, proc_root=Path("/proc"), host_fixture=False,
                 read_protected_maps=False, protected_map_reader=None):
        self.home, self.run = absolute(home), absolute(run_dir)
        self.proc, self.host_fixture = absolute(proc_root), bool(host_fixture)
        self.read_protected_maps = bool(read_protected_maps)
        if protected_map_reader is not None and not self.host_fixture:
            raise SafetyError("alternate protected-map readers require host-fixture mode")
        self.protected_map_reader = protected_map_reader
        if not host_fixture and (self.home != Path("/home/steamos") or self.proc != Path("/proc")):
            raise SafetyError("alternate home/proc paths require explicit host-fixture mode")
        self.base = self.home / ".local/share/GloomhavenVR/SteamLinkMaskProbe"
        if not self.run.is_relative_to(self.base / "runs") or self.run == self.base / "runs":
            raise SafetyError("run directory must be a unique child of the owned probe runs directory")
        self.config = self.base / "session.conf"
        self.manifest = self.home / ".local/share/vulkan/implicit_layer.d" / MANIFEST
        self.library, self.receipt = self.run / LIBRARY, self.run / RECEIPT
        self.arm_file, self.disarmed = self.run / "capture.arm", self.run / "disarmed.marker"

    def plan(self, capture, library_sha, machine):
        if not isinstance(capture, str) or len(capture) != 32 or any(c not in "0123456789abcdef" for c in capture) or capture == "0" * 32:
            raise SafetyError("capture identifier must be a nonzero lowercase 128-bit nonce")
        if type(machine) is not int or machine not in ((183, 62) if self.host_fixture else (183,)):
            raise SafetyError("invalid receipt library machine")
        if not isinstance(library_sha, str) or len(library_sha) != 64 or any(c not in "0123456789abcdef" for c in library_sha):
            raise SafetyError("invalid receipt library hash")
        config = CONFIG_MAGIC + capture.encode() + b"\n" + str(self.run).encode() + b"\n"
        manifest = encoded({"file_format_version": "1.2.0", "layer": {
            "name": "VK_LAYER_GHVR_pixel_snapshot", "type": "GLOBAL", "library_path": str(self.library),
            "api_version": "1.3.0", "implementation_version": "1",
            "description": "One-shot explicit-pattern pixel carriage diagnostic, not passthrough",
            "functions": {"vkGetInstanceProcAddr": "frame_pixel_vkGetInstanceProcAddr",
                          "vkGetDeviceProcAddr": "frame_pixel_vkGetDeviceProcAddr",
                          "vkNegotiateLoaderLayerInterfaceVersion": "frame_pixel_vkNegotiateLoaderLayerInterfaceVersion"},
            "disable_environment": {"GHVR_PIXEL_SNAPSHOT_DISABLE": "1"}}})
        receipt = {"schema": "frame-pixel-snapshot-session-v1", "capture_id": capture,
                   "home": str(self.home), "run_dir": str(self.run), "host_fixture": self.host_fixture,
                   "library_sha256": library_sha, "library_machine": machine,
                   "initial_files": "all_absent", "config_sha256": digest(config), "manifest_sha256": digest(manifest)}
        return receipt, config, manifest

    def receipt_plan(self):
        os.close(directory(self.base, private=True))
        os.close(directory(self.run, private=True))
        actual = load_json(self.receipt)
        planned = self.plan(actual.get("capture_id", ""), actual.get("library_sha256"), actual.get("library_machine"))
        if actual != planned[0] or actual["library_machine"] not in ((183, 62) if self.host_fixture else (183,)):
            raise SafetyError("receipt does not exactly describe this bounded owned session")
        return planned

    def active(self):
        """Conservative bounded same-user map scan; uncertainty blocks deletion."""
        result = {"owned_mapping_pids": [], "foreign_mapping_pids": [], "uncertain": False,
                  "protected_maps_read_enabled": self.read_protected_maps,
                  "protected_maps_read_pids": []}
        try:
            root = directory(self.proc, owned=False)
        except (OSError, SafetyError):
            result["uncertain"] = True
            return result
        identity = None
        if exists(self.library):
            parent = directory(self.library.parent)
            try:
                info = os.stat(self.library.name, dir_fd=parent, follow_symlinks=False)
                identity = (os.major(info.st_dev), os.minor(info.st_dev), info.st_ino)
            finally:
                os.close(parent)
        try:
            names = []
            with os.scandir(root) as entries:
                for entry in entries:
                    if entry.name.isascii() and entry.name.isdecimal() and 0 < int(entry.name) <= 2147483647:
                        if len(names) >= MAX_PROCESSES:
                            result["uncertain"] = True
                            return result
                        names.append(entry.name)
            total = 0
            for name in names:
                try:
                    process = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=root)
                    try:
                        process_info = os.fstat(process)
                        if process_info.st_uid != os.getuid():
                            continue
                        try:
                            fd = os.open("maps", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=process)
                            try:
                                with os.fdopen(fd, "rb", closefd=False) as stream:
                                    raw = stream.read(MAX_MAP + 1)
                            finally:
                                os.close(fd)
                        except PermissionError:
                            if (not self.read_protected_maps
                                    or (self.proc != Path("/proc") and self.protected_map_reader is None)):
                                raise
                            # The privileged path is fixed /proc/PID/maps, not any
                            # caller-supplied file. Preserve the pinned same-UID
                            # identity across the read; PID reuse stays unknown.
                            def same_process():
                                current = os.stat(name, dir_fd=root, follow_symlinks=False)
                                return (current.st_uid == os.getuid()
                                        and (current.st_dev, current.st_ino) == (process_info.st_dev, process_info.st_ino))
                            if not same_process():
                                raise SafetyError("process changed before protected maps read")
                            raw = (self.protected_map_reader or read_protected_maps)(name)
                            if not same_process():
                                raise SafetyError("process changed during protected maps read")
                            result["protected_maps_read_pids"].append(int(name))
                    finally:
                        os.close(process)
                    total += len(raw)
                    if len(raw) > MAX_MAP or total > MAX_MAP_TOTAL:
                        result["uncertain"] = True
                        break
                    for line in raw.decode("utf-8", "surrogateescape").splitlines():
                        fields = line.split(maxsplit=5)
                        if len(fields) < 6:
                            continue
                        mapped = fields[5].removesuffix(" (deleted)")
                        match_inode = False
                        try:
                            major, minor = (int(value, 16) for value in fields[3].split(":"))
                            match_inode = identity is not None and (major, minor, int(fields[4])) == identity
                        except ValueError:
                            result["uncertain"] = True
                        if mapped == str(self.library) or match_inode:
                            result["owned_mapping_pids"].append(int(name))
                        elif Path(mapped).name == LIBRARY:
                            result["foreign_mapping_pids"].append(int(name))
                except FileNotFoundError:
                    continue  # A process may exit during a cold scan.
                except (OSError, SafetyError):
                    result["uncertain"] = True
        finally:
            os.close(root)
        for key in ("owned_mapping_pids", "foreign_mapping_pids"):
            result[key] = sorted(set(result[key]))
        return result

    def prepare(self, library, wanted, *, capture_id=None):
        raw = read_file(absolute(library), MAX_LIBRARY)
        machine = verify_library(raw, wanted, host_fixture=self.host_fixture)
        os.close(directory(self.home))
        os.close(directory(self.base, create=True, private=True))
        os.close(directory(self.manifest.parent, create=True))
        scan = self.active()
        if scan["uncertain"] or scan["owned_mapping_pids"] or scan["foreign_mapping_pids"]:
            raise SafetyError("stage requires no mapped diagnostic library and a complete map scan")
        if exists(self.receipt):
            receipt, config, manifest = self.receipt_plan()
            if (receipt["library_sha256"] != wanted or machine != receipt["library_machine"]
                    or (capture_id is not None and receipt["capture_id"] != capture_id)
                    or exists(self.disarmed) or exists(self.run / "rollback-complete.json")):
                raise SafetyError("session cannot be replaced, rearmed or reused after disarm/rollback")
        else:
            if exists(self.config) or exists(self.manifest) or exists(self.library):
                raise SafetyError("pre-existing files preserved; a foreign or incomplete session conflicts")
            run_fd = directory(self.run, create=True, private=True)
            try:
                if os.listdir(run_fd):
                    raise SafetyError("nonempty pre-existing run directory preserved")
            finally:
                os.close(run_fd)
            receipt, config, manifest = self.plan(capture_id or secrets.token_hex(16), wanted, machine)
            write_new(self.receipt, encoded(receipt))
        for path, data in ((self.library, raw), (self.config, config), (self.manifest, manifest)):
            if exists(path):
                if read_file(path, MAX_LIBRARY, mode=0o600) != data:
                    raise SafetyError("changed/foreign staged file preserved: " + str(path))
            else:
                write_new(path, data)
        return self.status()

    def arm(self):
        receipt, config, manifest = self.receipt_plan()
        for path, expected in ((self.library, receipt["library_sha256"]), (self.config, digest(config)), (self.manifest, digest(manifest))):
            if digest(read_file(path, MAX_LIBRARY, mode=0o600)) != expected:
                raise SafetyError("cannot arm changed session")
        if exists(self.disarmed) or any(exists(self.run / name) for name in ("snapshot.p010", "snapshot.json", "rollback-complete.json")):
            raise SafetyError("spent or disarmed session cannot rearm")
        if exists(self.run / "launch.claim") and read_file(self.run / "launch.claim", 64, mode=0o600) != CLAIM_MAGIC:
            raise SafetyError("foreign launch claim preserved")
        scan = self.active()
        if scan["uncertain"] or scan["foreign_mapping_pids"]:
            raise SafetyError("cannot arm with unknown or foreign diagnostic mappings")
        if exists(self.arm_file):
            if read_file(self.arm_file, 64, mode=0o600) != ARM_MAGIC:
                raise SafetyError("changed arm file preserved")
        else:
            write_new(self.arm_file, ARM_MAGIC)
        return self.status()

    def disarm(self):
        self.receipt_plan()
        remove_exact(self.arm_file, digest(ARM_MAGIC))
        if exists(self.disarmed):
            if read_file(self.disarmed, 64, mode=0o600) != DISARM_MAGIC:
                raise SafetyError("foreign disarm marker preserved")
        else:
            write_new(self.disarmed, DISARM_MAGIC)
        return self.status()

    def status(self):
        receipt, config, manifest = self.receipt_plan()
        files = {}
        for path, expected in ((self.library, receipt["library_sha256"]), (self.config, digest(config)), (self.manifest, digest(manifest)),
                               (self.arm_file, digest(ARM_MAGIC))):
            try:
                files[str(path)] = "absent" if not exists(path) else (
                    "owned_exact" if digest(read_file(path, MAX_LIBRARY, mode=0o600)) == expected else "changed_preserved")
            except (OSError, SafetyError):
                files[str(path)] = "unreadable_or_unsafe_preserved"
        progress = None
        if exists(self.run / "status.jsonl"):
            try:
                progress = read_file(self.run / "status.jsonl", MAX_PROGRESS, mode=0o600).decode("utf-8")
            except (OSError, SafetyError, UnicodeError):
                progress = "unreadable_or_excessive_preserved"
        return {"schema": "frame-pixel-snapshot-session-status-v1", "capture_id": receipt["capture_id"],
                "files": files, "active_mappings": self.active(), "progress": progress,
                "launch_claim_present": exists(self.run / "launch.claim"),
                "disarmed": exists(self.disarmed), "rollback_record_present": exists(self.run / "rollback-complete.json"),
                "automatic_restart": False, "startup_screen_pixels_verified": False,
                "native_ui_health_verified": False, "native_frame_association_proven": False, "passthrough_authorized": False}

    def rollback(self):
        receipt, config, manifest = self.receipt_plan()
        errors = []
        # Disable future activation first, even when an old process still maps
        # the library. Neither removing an arm nor removing a manifest unloads it.
        for path, expected in ((self.manifest, digest(manifest)), (self.config, digest(config)), (self.arm_file, digest(ARM_MAGIC))):
            try:
                remove_exact(path, expected)
            except (OSError, SafetyError) as error:
                errors.append(str(error))
        try:
            if not exists(self.disarmed):
                write_new(self.disarmed, DISARM_MAGIC)
            elif read_file(self.disarmed, 64, mode=0o600) != DISARM_MAGIC:
                errors.append("changed/foreign disarm marker preserved")
        except (OSError, SafetyError) as error:
            errors.append(str(error))
        scan = self.active()
        loaded = scan["uncertain"] or scan["owned_mapping_pids"] or scan["foreign_mapping_pids"]
        if loaded:
            errors.append("library retained: active or unknown mappings; manually restart SteamVR before retrying rollback")
        else:
            try:
                remove_exact(self.library, receipt["library_sha256"])
            except (OSError, SafetyError) as error:
                errors.append(str(error))
        result = {"schema": "frame-pixel-snapshot-session-rollback-v1", "capture_id": receipt["capture_id"],
                  "complete": not errors, "errors": errors, "active_mappings": scan,
                  "raw_and_status_outputs_preserved": True, "automatic_restart": False,
                  "native_ui_health_verified": False, "startup_screen_pixels_verified": False,
                  "native_frame_association_proven": False, "passthrough_authorized": False}
        marker = self.run / "rollback-complete.json"
        if not errors:
            if not exists(marker):
                write_new(marker, encoded(result))
            elif read_file(marker, MAX_JSON, mode=0o600) != encoded(result):
                result["complete"] = False
                result["errors"].append("changed/foreign rollback record preserved")
        return result

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "stage", "arm", "status", "disarm", "rollback"))
    parser.add_argument("--home", type=Path, default=Path("/home/steamos"))
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--proc-root", type=Path, default=Path("/proc"))
    parser.add_argument("--host-fixture", action="store_true", help="local controlled fixtures only; never a device deployment bypass")
    parser.add_argument("--read-protected-maps", action="store_true",
                        help="explicitly allow bounded, read-only sudo -n head for denied same-user /proc maps; never prompts")
    parser.add_argument("--library", type=Path)
    parser.add_argument("--library-sha256")
    parser.add_argument("--capture-id")
    args = parser.parse_args(argv)
    try:
        session = Session(args.home, args.run_dir, proc_root=args.proc_root, host_fixture=args.host_fixture,
                          read_protected_maps=args.read_protected_maps)
        if args.command in ("prepare", "stage"):
            if args.library is None or args.library_sha256 is None:
                raise SafetyError("prepare requires --library and its explicit --library-sha256")
            result = session.prepare(args.library, args.library_sha256, capture_id=args.capture_id)
        else:
            if args.library is not None or args.library_sha256 is not None or args.capture_id is not None:
                raise SafetyError("library and capture selection apply only to prepare/stage")
            result = getattr(session, args.command)()
        print(json.dumps(result, sort_keys=True, indent=2))
        return 0 if result.get("complete", True) else 2
    except (OSError, SafetyError, ValueError, TypeError, KeyError, struct.error) as error:
        print("pixel snapshot session: " + str(error), file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
