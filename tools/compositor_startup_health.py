#!/usr/bin/env python3
"""Bounded, read-only SteamVR startup evidence; never a screen-pixel verdict.

Run outside rendering threads. A live compositor/service is insufficient: the
current UI browser, system UI and a libcef-backed renderer must also be alive.
An optional reviewed descriptor checks every managed executable for diagnostic
inheritance. Linux proc environ is initial-exec evidence, never a claim that late
getenv is clean; the observer's separately bound freeze receipt is still needed.
No recovery/restart command is issued by this module.
"""
from __future__ import annotations
import datetime
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import time

UNIT = "steamvr.service"
SCHEMA = "frame-compositor-startup-health-v1"
LIMIT = 512 * 1024
MAX_PROCESSES = 512
ISOLATION_SCHEMA = "frame-compositor-startup-isolation-v1"
ISOLATION_BYTE_LIMIT = 64 * 1024 * 1024
ISOLATION_SAMPLE_LIMIT = 64
ISOLATION_PROCESS_LIMIT = 4096
ISOLATION_GROUP_LIMIT = 128
ISOLATION_ENV_LIMIT = 64 * 1024
ISOLATION_MAP_LIMIT = 1024 * 1024
ISOLATION_ENV_KEYS = frozenset(("LD_PRELOAD", "LD_AUDIT", "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE",
    "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT", "FRAME_MASK_TRACE", "FRAME_MASK_TRACE_BINDINGS", "FRAME_MASK_TRACE_OUTPUT"))
DIAGNOSTIC_PREFIXES = (b"GHVR_FRAME_COMPOSITOR_RAW_", b"FRAME_MASK_TRACE")
INELIGIBLE_STATES = frozenset("TtXxZ")
PROPERTIES = ("InvocationID", "ActiveState", "SubState", "MainPID", "ControlGroup", "NRestarts", "ExecMainStartTimestampMonotonic")
COMPOSITOR = "/opt/steamvr/bin/linuxarm64/vrcompositor"
WEBHELPER = "/opt/steamvr/bin/vrwebhelper/linuxarm64/vrwebhelper"
LIBCEF = "/opt/steamvr/bin/vrwebhelper/linuxarm64/libcef.so"
FATAL = re.compile(r"cannot allocate memory in static TLS block|error while loading shared libraries|"
    r"symbol lookup error|\bVK_ERROR_DEVICE_LOST\b|\bvkCreateQueryPool\b.*(?:invalid|failed)|"
    r"(?:FATAL|fatal error).*?(?:renderer|Vulkan|graphics|CEF)|"
    r"(?:renderer|GPU process).*?(?:crashed|crash loop|failed to start)|"
    r"(?:segmentation fault|segfault|core dumped)", re.I)
DATE = re.compile(r"^\w{3} (\w{3} +\d{1,2} \d{4} \d{2}:\d{2}:\d{2}\.\d{6}) ")


def reviewed_absolute_path(value):
    return (isinstance(value, str) and 1 < len(value) <= 1024 and value.startswith("/") and
            value == os.path.normpath(value) and "//" not in value and
            all(33 <= ord(char) <= 126 and char not in ":=" for char in value))


def require_isolation_descriptor(descriptor):
    """Paths and target inputs are supplied by the reviewed artifact, not guessed."""
    if descriptor is None: return None
    keys = {"schema", "target_exe", "selector_path", "payload_path", "audit_path", "target_exec_environment"}
    if (not isinstance(descriptor, dict) or set(descriptor) != keys or descriptor["schema"] != ISOLATION_SCHEMA or
            any(not reviewed_absolute_path(descriptor[key]) for key in keys - {"schema", "target_exec_environment"}) or
            len({descriptor[key] for key in ("target_exe", "selector_path", "payload_path", "audit_path")}) != 4):
        raise RuntimeError("Invalid reviewed startup isolation descriptor")
    env = descriptor["target_exec_environment"]
    if (not isinstance(env, dict) or set(env) != ISOLATION_ENV_KEYS or
            any(not isinstance(value, str) or not value or len(value) > 4096 or
                any(not 32 <= ord(char) <= 126 for char in value) for value in env.values()) or
            env["FRAME_MASK_TRACE"] != "1" or env["FRAME_MASK_TRACE_BINDINGS"] != "1" or
            any(not reviewed_absolute_path(env[key]) for key in
                ("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE", "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT", "FRAME_MASK_TRACE_OUTPUT"))):
        raise RuntimeError("Invalid reviewed target initial environment")
    preload = re.split(r"[ :]+", env["LD_PRELOAD"])
    audit = re.split(r"[ :]+", env["LD_AUDIT"])
    if (preload.count(descriptor["payload_path"]) != 1 or preload.count(descriptor["selector_path"]) != 1 or
            descriptor["audit_path"] in preload or audit.count(descriptor["audit_path"]) != 1 or
            descriptor["payload_path"] in audit or descriptor["selector_path"] in audit):
        raise RuntimeError("Reviewed target loader input does not select the exact artifact")
    return {**descriptor, "target_exec_environment": dict(env)}


def require_isolation_usage(usage):
    if (not isinstance(usage, dict) or set(usage) != {"samples", "processes", "metadata_bytes"} or
            any(type(usage[key]) is not int or not 0 <= usage[key] <= limit for key, limit in
                (("samples", ISOLATION_SAMPLE_LIMIT), ("processes", ISOLATION_PROCESS_LIMIT),
                 ("metadata_bytes", ISOLATION_BYTE_LIMIT)))):
        raise RuntimeError("Invalid cumulative startup isolation budget")


def isolation_verdict(rows, errors, *, inventory_stable, unit_stable):
    """A fork without exec may retain mappings; never call it clean or unloadable."""
    for row in rows:
        if row["owned_mapping_deleted"]:
            return "contaminated", "Owned diagnostic mapping has been deleted or replaced"
        if row["target_initial_environment_matches"] is None:
            if (row["payload_mapped"] or row["audit_mapped"] or row["loader_payload"] or
                    row["loader_audit"] or row["diagnostic_keys"]):
                return "contaminated", "Non-target process inherited full diagnostic state"
        elif not row["target_initial_environment_matches"]:
            return "contaminated", "Target initial-exec environment differs from reviewed inputs"
    if errors or not inventory_stable or not unit_stable:
        return "not-isolated", "Managed process inventory or metadata is unreadable/changing"
    targets = [row for row in rows if row["target_initial_environment_matches"] is not None]
    if len(targets) != 1:
        return "not-isolated", "Exact target identity is missing or ambiguous, including possible fork descendants"
    target = targets[0]
    if target["state"] in INELIGIBLE_STATES or not all(target[key] for key in ("selector_mapped", "payload_mapped", "audit_mapped")):
        return "not-isolated", "Exact target diagnostic executable mappings are not ready"
    return "isolated", None


def require_isolation_report(report, descriptor, unit):
    expected = {"schema", "descriptor", "invocation", "control_group", "status", "reason", "processes", "errors",
        "inventory_stable", "unit_stable", "target_env_view", "runtime_environ_verified", "metadata_usage"}
    if (not isinstance(report, dict) or set(report) != expected or report["schema"] != ISOLATION_SCHEMA or
            report["descriptor"] != descriptor or report["invocation"] != unit["InvocationID"] or
            report["control_group"] != unit["ControlGroup"] or report["target_env_view"] != "initial-exec-only" or
            report["runtime_environ_verified"] is not False or
            type(report["inventory_stable"]) is not bool or type(report["unit_stable"]) is not bool):
        raise RuntimeError("Invalid current startup isolation report")
    require_isolation_usage(report["metadata_usage"])
    if not report["metadata_usage"]["samples"]:
        raise RuntimeError("Startup isolation report contains no current sample budget")
    if (not isinstance(report["errors"], list) or len(report["errors"]) > 16 or
            any(not isinstance(error, dict) or set(error) != {"pid", "reason"} or
                type(error["pid"]) is not int or not 0 <= error["pid"] < 2**64 or
                not isinstance(error["reason"], str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,63}", error["reason"])
                for error in report["errors"])):
        raise RuntimeError("Invalid startup isolation unreadable-process report")
    rows = report["processes"]
    row_keys = {"pid", "start_ticks", "state", "ppid", "exe", "selector_mapped", "payload_mapped", "audit_mapped",
        "owned_mapping_deleted", "loader_payload", "loader_audit", "diagnostic_keys", "target_initial_environment_matches"}
    if not isinstance(rows, list) or len(rows) > MAX_PROCESSES:
        raise RuntimeError("Invalid startup isolation process count")
    for row in rows:
        if (not isinstance(row, dict) or set(row) != row_keys or
                any(type(row[key]) is not int or not 0 < row[key] < 2**64 for key in ("pid", "start_ticks")) or
                type(row["ppid"]) is not int or not 0 <= row["ppid"] < 2**64 or
                row["state"] not in tuple("RSDTtXxZIKPW") or not isinstance(row["exe"], str) or
                not row["exe"].startswith("/") or len(row["exe"]) > 4096 or
                any(type(row[key]) is not bool for key in
                    ("selector_mapped", "payload_mapped", "audit_mapped", "owned_mapping_deleted", "loader_payload", "loader_audit")) or
                not isinstance(row["diagnostic_keys"], list) or len(row["diagnostic_keys"]) > 16 or
                any(not isinstance(key, str) or len(key) > 128 or not key.encode("ascii").startswith(DIAGNOSTIC_PREFIXES)
                    for key in row["diagnostic_keys"]) or
                row["target_initial_environment_matches"] is not None and type(row["target_initial_environment_matches"]) is not bool or
                (row["exe"] == descriptor["target_exe"]) != (row["target_initial_environment_matches"] is not None)):
            raise RuntimeError("Invalid startup isolation process metadata")
    if len({row["pid"] for row in rows}) != len(rows):
        raise RuntimeError("Duplicate startup isolation process identity")
    if report["metadata_usage"]["processes"] < len(rows) or rows and not report["metadata_usage"]["metadata_bytes"]:
        raise RuntimeError("Startup isolation report metadata budget disagrees with process evidence")
    verdict = isolation_verdict(rows, report["errors"], inventory_stable=report["inventory_stable"], unit_stable=report["unit_stable"])
    if (report["status"], report["reason"]) != verdict:
        raise RuntimeError("Startup isolation verdict/evidence disagreement")


def identity(process):
    return (process["pid"], process["start_ticks"])


def require_process(process, *, main=False):
    keys = {"pid", "start_ticks", "state"} if main else {"pid", "start_ticks", "state", "exe", "kind", "libcef"}
    if (not isinstance(process, dict) or set(process) != keys or
            any(type(process.get(key)) is not int or not 0 < process[key] < 2**64 for key in ("pid", "start_ticks")) or
            not isinstance(process.get("state"), str) or process["state"] not in tuple("RSDTtXxZIKPW")):
        raise RuntimeError("Invalid startup process metadata")
    if not main and (process["exe"] not in (COMPOSITOR, WEBHELPER) or
                     not isinstance(process["kind"], str) or len(process["kind"]) > 64 or type(process["libcef"]) is not bool):
        raise RuntimeError("Invalid startup executable/mapping metadata")


def require_errors(errors):
    if (not isinstance(errors, list) or len(errors) > 16 or
            any(not isinstance(item, dict) or set(item) != {"pid", "reason"} or
                type(item["pid"]) is not int or not 0 < item["pid"] < 2**64 or
                not isinstance(item["reason"], str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,63}", item["reason"])
                for item in errors)):
        raise RuntimeError("Invalid startup unreadable process report")


def require_clock(value):
    if type(value) not in (float, int) or value < 0 or value > 2**64 or not math.isfinite(value):
        raise RuntimeError("Invalid startup clock evidence")


def require_unit(unit, *, allow_inactive=False):
    if (not isinstance(unit, dict) or set(unit) != set(PROPERTIES) or
            not (re.fullmatch(r"[0-9a-f]{32}", unit.get("InvocationID", "")) or
                allow_inactive and unit.get("ActiveState") in ("inactive", "failed") and unit.get("InvocationID") == "") or
            not (re.fullmatch(r"/[A-Za-z0-9_./@:-]+/steamvr\.service", unit.get("ControlGroup", "")) or
                allow_inactive and unit.get("ActiveState") in ("inactive", "failed") and unit.get("ControlGroup") == "") or
            any(not re.fullmatch(r"[0-9]{1,20}", unit.get(key, "")) for key in
                ("MainPID", "NRestarts", "ExecMainStartTimestampMonotonic"))):
        raise RuntimeError("Incomplete/unsupported SteamVR unit identity")


class NativeEvidenceSource:
    """Kernel process metadata, bounded fresh native log ranges and journal only."""
    def __init__(self, home, *, proc_root=Path("/proc"), run=subprocess.run,
                 clock=time.monotonic, wall_clock=time.time, uid=None, isolation=None,
                 cgroup_root=Path("/sys/fs/cgroup")):
        self.home, self.proc, self.run = Path(home), Path(proc_root), run
        self.clock, self.wall_clock = clock, wall_clock
        self.sleep = time.sleep
        self.uid = os.getuid() if uid is None else uid
        self.isolation_descriptor = require_isolation_descriptor(isolation)
        self.cgroup_root = Path(cgroup_root)
        self.isolation_usage = {"samples": 0, "processes": 0, "metadata_bytes": 0}
        self.native_paths = {name: os.path.realpath(name) for name in (COMPOSITOR, WEBHELPER, LIBCEF)}
        self.logs = {name: self.home / ".local/share/Steam/logs" / (name + ".txt")
                     for name in ("vrcompositor", "vrserver")}
        self.cursor, self.offsets, self.total = None, {}, 0
        self.pending_lines = {name: "" for name in self.logs}

    def command(self, args):
        result = self.run(args, check=True, capture_output=True, text=True, timeout=5)
        if result.returncode or len(result.stdout.encode()) + len(result.stderr.encode()) > LIMIT: raise RuntimeError("Startup command output bound exceeded")
        return result.stdout

    def unit(self):
        raw = self.command(["systemctl", "--user", "show", UNIT, "--property=" + ",".join(PROPERTIES)])
        rows = raw.splitlines()
        if len(rows) != len(PROPERTIES) or any("=" not in row for row in rows):
            raise RuntimeError("Incomplete startup unit response")
        result = dict(row.split("=", 1) for row in rows)
        require_unit(result, allow_inactive=True)
        return result

    def journal(self, *, initial=False):
        args = ["journalctl", "--user-unit=" + UNIT, "--no-pager", "--output=json", "--show-cursor"]
        args += ["-n", "1"] if initial else ["--after-cursor=" + self.cursor, "-n", "512"]
        raw = self.command(args)
        records, cursor = [], None
        for row in raw.splitlines():
            if row.startswith("-- cursor: "):
                cursor = row[len("-- cursor: "):]
                continue
            if not row: continue
            item = json.loads(row)
            if not isinstance(item, dict): raise RuntimeError("Unsupported startup journal row")
            if not initial: records.append(item)
            cursor = item.get("__CURSOR", cursor)
        if not isinstance(cursor, str) or not re.fullmatch(r"[A-Za-z0-9_=;:-]{1,4096}", cursor):
            raise RuntimeError("Fresh journal cursor unavailable")
        if len(records) >= 512: raise RuntimeError("Startup journal record bound reached")
        self.cursor = cursor
        self.total += 0 if initial else len(raw.encode())
        if self.total > LIMIT: raise RuntimeError("Startup evidence byte bound exceeded")
        return records

    @staticmethod
    def proc_read(path, limit):
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            raw = stream.read(limit + 1)
        if len(raw) > limit: raise RuntimeError("Startup process metadata bound exceeded")
        return raw

    @classmethod
    def process_identity(cls, path):
        raw = cls.proc_read(path / "stat", 8192).decode("ascii")
        end = raw.rfind(")"); columns = raw[end + 2:].split()
        if end < 0 or raw.split(" ", 1)[0] != path.name or len(columns) < 20:
            raise RuntimeError("Unsupported startup process identity")
        return int(columns[19]), columns[0]

    def processes(self, unit):
        # An inactive unit with no group cannot own any of these candidates.
        # Do not accidentally treat every absolute cgroup as its descendant.
        if not unit["ControlGroup"]: return [], []
        entries = list(self.proc.iterdir())
        if len(entries) > 8192: raise RuntimeError("Startup process directory bound exceeded")
        result, errors = [], []
        for path in entries:
            if not path.name.isdigit(): continue
            managed = False
            try:
                node = path.lstat()
                # Protected kernel/system processes are unrelated and skipped
                # before reading anything. Unreadable managed user processes
                # remain an explicit evidence gap, never a positive result.
                if not stat.S_ISDIR(node.st_mode) or node.st_uid != self.uid: continue
                groups = self.proc_read(path / "cgroup", 8192).decode("ascii").splitlines()
                paths = [row.split(":", 2)[2] for row in groups if len(row.split(":", 2)) == 3]
                if not any(value == unit["ControlGroup"] or value.startswith(unit["ControlGroup"] + "/") for value in paths): continue
                managed = True
                comm = self.proc_read(path / "comm", 256).decode("ascii").strip()
                if comm not in ("vrcompositor", "vrwebhelper"): continue
                start, state = self.process_identity(path)
                mapped_exe = os.readlink(path / "exe")
                matches = [name for name in (COMPOSITOR, WEBHELPER) if mapped_exe in (name, self.native_paths[name])]
                if len(matches) != 1: continue
                exe = matches[0]
                cmd = self.proc_read(path / "cmdline", 32768).decode("utf-8").split("\0")
                kind = "compositor" if exe == COMPOSITOR else "browser"
                for arg in cmd:
                    if arg.startswith("--type="): kind = arg[len("--type="):]
                libcef = False
                if exe == WEBHELPER:
                    maps = self.proc_read(path / "maps", 1048576).decode("utf-8").splitlines()
                    if len(maps) > 8192: raise RuntimeError("Startup map row bound exceeded")
                    libcef = any(len(cols := row.split(None, 5)) == 6 and cols[5] in (LIBCEF, self.native_paths[LIBCEF]) and
                                 len(cols[1]) == 4 and cols[1][2] == "x" for row in maps)
                if self.process_identity(path)[0] != start:
                    raise RuntimeError("Startup process changed during sampling")
                result.append({"pid": int(path.name), "start_ticks": start, "state": state,
                    "exe": exe, "kind": kind, "libcef": libcef})
                if len(result) > MAX_PROCESSES: raise RuntimeError("Startup managed process bound exceeded")
            except FileNotFoundError:
                # A required identity already seen by the gate disappearing is
                # detected below; an unclassified transient child is not ready.
                continue
            except (OSError, RuntimeError, UnicodeError, ValueError) as error:
                # A scope-unknown unrelated PID is not a failed managed read.
                # Required missing roles fail the gate; a pinned PID disappearing
                # fails monitoring independently. The known service main is kept.
                if not managed and int(path.name) != int(unit["MainPID"]): continue
                if len(errors) >= 16: raise RuntimeError("Startup unreadable process bound exceeded")
                errors.append({"pid": int(path.name), "reason": type(error).__name__})
        return result, errors

    def isolation_read(self, path, limit):
        # This cumulative accounting includes metadata reads which later fail
        # identity checks. A retry/resume never restores the spent budget.
        remaining = ISOLATION_BYTE_LIMIT - self.isolation_usage["metadata_bytes"]
        if remaining <= 0: raise RuntimeError("Startup isolation byte budget exhausted")
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            data = stream.read(min(limit + 1, remaining))
        self.isolation_usage["metadata_bytes"] += len(data)
        if len(data) > limit: raise RuntimeError("Startup isolated metadata extent exceeded")
        if remaining <= limit and len(data) == remaining:
            raise RuntimeError("Startup isolation byte budget exhausted")
        return data

    def managed_inventory(self, unit):
        """Authoritative service cgroup tree, including otherwise unknown children."""
        group = unit["ControlGroup"]
        if not group or any(part in (".", "..") for part in group.split("/")):
            raise RuntimeError("Current managed control group unavailable")
        root = self.cgroup_root / group.lstrip("/")
        for path in (root, *root.parents):
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode): raise RuntimeError("Symlink control-group path refused")
        pending, groups, pids = [root], {}, set()
        while pending:
            path = pending.pop()
            info = path.lstat()
            if not stat.S_ISDIR(info.st_mode): raise RuntimeError("Invalid control-group directory")
            groups[str(path)] = (info.st_dev, info.st_ino)
            if len(groups) > ISOLATION_GROUP_LIMIT: raise RuntimeError("Startup control-group directory bound exceeded")
            raw = self.isolation_read(path / "cgroup.procs", 8192)
            for row in raw.splitlines():
                if not re.fullmatch(rb"[1-9][0-9]{0,9}", row) or int(row) >= 2**31:
                    raise RuntimeError("Unsupported control-group PID inventory")
                pids.add(int(row))
                if len(pids) > MAX_PROCESSES: raise RuntimeError("Startup managed process bound exceeded")
            with os.scandir(path) as entries:
                count = 0
                for entry in entries:
                    count += 1
                    if count > 512: raise RuntimeError("Startup control-group entry bound exceeded")
                    if entry.is_symlink(): raise RuntimeError("Symlink control-group entry refused")
                    if entry.is_dir(follow_symlinks=False): pending.append(Path(entry.path))
            if len(pending) + len(groups) > ISOLATION_GROUP_LIMIT:
                raise RuntimeError("Startup control-group directory bound exceeded")
        return groups, pids

    def isolation_identity(self, path):
        raw = self.isolation_read(path / "stat", 8192)
        end = raw.rfind(b")"); columns = raw[end + 2:].split()
        if (end < 0 or raw.split(b" ", 1)[0] != path.name.encode("ascii") or len(columns) < 20 or
                columns[0] not in tuple(state.encode("ascii") for state in "RSDTtXxZIKPW")):
            raise RuntimeError("Unsupported isolated process identity")
        start, ppid = int(columns[19]), int(columns[1])
        if not 0 < start < 2**64 or not 0 <= ppid < 2**64:
            raise RuntimeError("Invalid isolated process start/parent identity")
        return start, columns[0].decode("ascii"), ppid

    def isolation_link(self, path):
        value = os.readlink(path)
        size = len(value.encode("utf-8"))
        if size > 4096: raise RuntimeError("Startup executable link extent exceeded")
        if self.isolation_usage["metadata_bytes"] + size > ISOLATION_BYTE_LIMIT:
            self.isolation_usage["metadata_bytes"] = ISOLATION_BYTE_LIMIT
            raise RuntimeError("Startup isolation byte budget exhausted")
        self.isolation_usage["metadata_bytes"] += size
        return value

    def isolation_scope(self, path, unit):
        info = path.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != self.uid:
            raise RuntimeError("Managed process UID/type differs from service user")
        groups = self.isolation_read(path / "cgroup", 8192).decode("ascii").splitlines()
        group = unit["ControlGroup"]
        if not any(len(row.split(":", 2)) == 3 and
                (row.split(":", 2)[2] == group or row.split(":", 2)[2].startswith(group + "/")) for row in groups):
            raise RuntimeError("Managed inventory/process control group disagrees")
        return info.st_dev, info.st_ino, info.st_uid

    def isolation_environment(self, raw, *, target):
        descriptor = self.isolation_descriptor
        payload, audit = (descriptor[key].encode("ascii") for key in ("payload_path", "audit_path"))
        expected = descriptor["target_exec_environment"]
        seen, diagnostic, loader_payload, loader_audit = {}, [], False, False
        entries = raw.split(b"\0")
        if len(entries) > 4096 or raw and not raw.endswith(b"\0"):
            raise RuntimeError("Unsupported initial process environment extent")
        for entry in entries:
            # Cold cleanup may leave zeroed holes in the original exec range.
            if not entry: continue
            key, separator, value = entry.partition(b"=")
            if not separator or not key or len(key) > 256:
                raise RuntimeError("Unsupported initial process environment entry")
            if key.startswith(DIAGNOSTIC_PREFIXES):
                name = key.decode("ascii")
                if len(name) > 128 or len(diagnostic) >= 16:
                    raise RuntimeError("Startup diagnostic key bound exceeded")
                diagnostic.append(name)
            if key in (b"LD_PRELOAD", b"LD_AUDIT"):
                tokens = re.split(rb"[:\s]+", value)
                loader_payload |= payload in tokens
                loader_audit |= audit in tokens
            if key in {name.encode("ascii") for name in expected}:
                seen.setdefault(key.decode("ascii"), []).append(value)
        matches = None
        if target:
            matches = (set(seen) == set(expected) and
                all(seen[key] == [value.encode("ascii")] for key, value in expected.items()) and
                set(diagnostic) == set(expected) - {"LD_PRELOAD", "LD_AUDIT"} and
                len(diagnostic) == len(set(diagnostic)))
        return {"diagnostic_keys": diagnostic, "loader_payload": loader_payload,
                "loader_audit": loader_audit, "target_initial_environment_matches": matches}

    def isolation_mappings(self, raw):
        descriptor = self.isolation_descriptor
        result = {"selector_mapped": False, "payload_mapped": False, "audit_mapped": False,
                  "owned_mapping_deleted": False}
        rows = raw.decode("utf-8").splitlines()
        if len(rows) > 8192: raise RuntimeError("Startup isolated map row bound exceeded")
        for row in rows:
            fields = row.split(None, 5)
            if (len(fields) < 5 or not re.fullmatch(r"[0-9a-f]+-[0-9a-f]+", fields[0]) or
                    not re.fullmatch(r"[r-][w-][x-][ps]", fields[1])):
                raise RuntimeError("Unsupported managed executable mapping record")
            if len(fields) != 6 or fields[1][2] != "x": continue
            mapped = fields[5]
            for path_key, result_key in (("selector_path", "selector_mapped"),
                    ("payload_path", "payload_mapped"), ("audit_path", "audit_mapped")):
                if mapped == descriptor[path_key]: result[result_key] = True
                if mapped == descriptor[path_key] + " (deleted)":
                    result[result_key] = True; result["owned_mapping_deleted"] = True
        return result

    def isolated_process(self, pid, unit):
        path = self.proc / str(pid)
        scope = self.isolation_scope(path, unit)
        start, state, ppid = self.isolation_identity(path)
        exe = self.isolation_link(path / "exe")
        if not exe.startswith("/") or len(exe) > 4096 or exe.endswith(" (deleted)"):
            raise RuntimeError("Unknown managed executable identity")
        target = exe == self.isolation_descriptor["target_exe"]
        env = self.isolation_environment(self.isolation_read(path / "environ", ISOLATION_ENV_LIMIT), target=target)
        maps = self.isolation_mappings(self.isolation_read(path / "maps", ISOLATION_MAP_LIMIT))
        after = self.isolation_identity(path)
        if (after[0] != start or after[2] != ppid or self.isolation_link(path / "exe") != exe or
                self.isolation_scope(path, unit) != scope):
            raise RuntimeError("Managed process changed identity/scope during isolation sampling")
        return {"pid": pid, "start_ticks": start, "state": after[1], "ppid": ppid, "exe": exe, **env, **maps}

    def isolation(self, unit):
        if self.isolation_descriptor is None: return None
        self.isolation_usage["samples"] += 1
        if self.isolation_usage["samples"] > ISOLATION_SAMPLE_LIMIT:
            self.isolation_usage["samples"] = ISOLATION_SAMPLE_LIMIT
            raise RuntimeError("Startup isolation sample budget exhausted")
        rows, errors, inventory_stable, unit_stable = [], [], False, False
        def error(pid, failure):
            if len(errors) >= 16: raise RuntimeError("Startup isolation unreadable-process bound exceeded")
            errors.append({"pid": pid, "reason": type(failure).__name__})
        try:
            before = self.managed_inventory(unit)
            if int(unit["MainPID"]) <= 0 or int(unit["MainPID"]) not in before[1]:
                raise RuntimeError("Service main is absent from managed process inventory")
            for pid in sorted(before[1]):
                self.isolation_usage["processes"] += 1
                if self.isolation_usage["processes"] > ISOLATION_PROCESS_LIMIT:
                    self.isolation_usage["processes"] = ISOLATION_PROCESS_LIMIT
                    raise RuntimeError("Startup isolation process budget exhausted")
                try: rows.append(self.isolated_process(pid, unit))
                except (OSError, RuntimeError, UnicodeError, ValueError) as failure: error(pid, failure)
            inventory_stable = self.managed_inventory(unit) == before
            unit_stable = self.unit() == unit
        except (OSError, RuntimeError, UnicodeError, ValueError, subprocess.SubprocessError) as failure:
            # PID zero denotes a service/cgroup-level evidence gap, not an
            # invented process identity.
            error(int(unit["MainPID"]), failure)
        verdict = isolation_verdict(rows, errors, inventory_stable=inventory_stable, unit_stable=unit_stable)
        return {"schema": ISOLATION_SCHEMA, "descriptor": self.isolation_descriptor,
            "invocation": unit["InvocationID"], "control_group": unit["ControlGroup"],
            "status": verdict[0], "reason": verdict[1], "processes": rows, "errors": errors,
            "inventory_stable": inventory_stable, "unit_stable": unit_stable,
            "target_env_view": "initial-exec-only", "runtime_environ_verified": False,
            "metadata_usage": dict(self.isolation_usage)}

    def log_read(self, name, *, initial=False):
        path = self.logs[name]
        try:
            for parent in (path, *path.parents):
                if stat.S_ISLNK(parent.lstat().st_mode): raise RuntimeError("Symlink native startup log path refused")
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        except FileNotFoundError:
            if initial: self.offsets[name] = None; return ""
            return ""
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != self.uid or info.st_nlink != 1:
                raise RuntimeError("Unsafe native startup log type/owner")
            node = (info.st_dev, info.st_ino)
            previous = self.offsets.get(name)
            if initial:
                self.offsets[name] = {"dev": node[0], "ino": node[1], "offset": info.st_size}
                return ""
            offset = previous["offset"] if previous and node == (previous["dev"], previous["ino"]) and info.st_size >= previous["offset"] else 0
            if offset == 0: self.pending_lines[name] = ""
            available = info.st_size - offset
            if available > 128 * 1024: raise RuntimeError("Startup log delta bound exceeded")
            stream.seek(offset); data = stream.read(available)
            after = os.fstat(stream.fileno()); current = path.lstat()
            if (after.st_dev, after.st_ino) != node or (current.st_dev, current.st_ino) != node or after.st_size < offset + len(data):
                raise RuntimeError("Startup log rotated during sampling")
            self.offsets[name] = {"dev": node[0], "ino": node[1], "offset": offset + len(data)}
        self.total += len(data)
        if self.total > LIMIT: raise RuntimeError("Startup evidence byte bound exceeded")
        text = self.pending_lines[name] + data.decode("utf-8")
        rows = text.split("\n"); self.pending_lines[name] = rows.pop()
        if len(self.pending_lines[name]) > 8192: raise RuntimeError("Startup log line bound exceeded")
        return "\n".join(rows)

    def main_process(self, unit):
        if int(unit["MainPID"]) <= 0: return None
        path = self.proc / unit["MainPID"]
        node = path.lstat()
        if not stat.S_ISDIR(node.st_mode) or node.st_uid != self.uid: raise RuntimeError("Service main process ownership/type changed")
        start, state = self.process_identity(path)
        groups = self.proc_read(path / "cgroup", 8192).decode("ascii").splitlines()
        group = unit["ControlGroup"]
        if not any(len(row.split(":", 2)) == 3 and (row.split(":", 2)[2] == group or row.split(":", 2)[2].startswith(group + "/")) for row in groups):
            raise RuntimeError("Service main process left its control group")
        if self.process_identity(path)[0] != start: raise RuntimeError("Service main process changed during inspection")
        return {"pid": int(unit["MainPID"]), "start_ticks": start, "state": state}

    def baseline(self):
        self.total = 0
        self.isolation_usage = {"samples": 0, "processes": 0, "metadata_bytes": 0}
        self.pending_lines = {name: "" for name in self.logs}
        unit = self.unit(); self.journal(initial=True)
        for name in self.logs: self.log_read(name, initial=True)
        processes, errors = self.processes(unit)
        try: main = self.main_process(unit)
        except (OSError, RuntimeError) as error:
            main = None
            pid = int(unit["MainPID"])
            if pid > 0 and not any(item["pid"] == pid for item in errors):
                if len(errors) >= 16: raise RuntimeError("Startup unreadable process bound exceeded")
                errors.append({"pid": pid, "reason": type(error).__name__})
        return {"schema": SCHEMA, "unit": unit, "cursor": self.cursor, "log_offsets": dict(self.offsets),
                "processes": processes, "main_process": main, "errors": errors, "monotonic": self.clock(), "wall_time": self.wall_clock(),
                "isolation_descriptor": self.isolation_descriptor}

    def sample(self):
        unit = self.unit(); processes, errors = self.processes(unit)
        try: main = self.main_process(unit)
        except (OSError, RuntimeError) as error:
            main = None; errors.append({"pid": int(unit["MainPID"]), "reason": type(error).__name__})
        isolation = self.isolation(unit)
        journal = self.journal()
        return {"unit": unit, "processes": processes, "main_process": main, "errors": errors, "journal": journal,
                "logs": {name: self.log_read(name) for name in self.logs}, "monotonic": self.clock(), "isolation": isolation}

    def checkpoint(self):
        return {"cursor": self.cursor, "log_offsets": self.offsets, "pending_lines": self.pending_lines, "total": self.total,
                "isolation_descriptor": self.isolation_descriptor, "isolation_usage": self.isolation_usage}

    def resume(self, checkpoint):
        if not isinstance(checkpoint, dict) or set(checkpoint) != {"cursor", "log_offsets", "pending_lines", "total", "isolation_descriptor", "isolation_usage"}:
            raise RuntimeError("Invalid startup source checkpoint shape")
        if type(checkpoint["total"]) is not int or not 0 <= checkpoint["total"] <= LIMIT:
            raise RuntimeError("Invalid cumulative startup evidence budget")
        require_isolation_usage(checkpoint["isolation_usage"])
        descriptor = require_isolation_descriptor(checkpoint["isolation_descriptor"])
        if descriptor != self.isolation_descriptor:
            raise RuntimeError("Startup isolation descriptor changed across resume")
        self.isolation_usage = dict(checkpoint["isolation_usage"])
        self.total = checkpoint["total"]
        self.cursor = checkpoint["cursor"]
        self.offsets, self.pending_lines = checkpoint["log_offsets"], checkpoint["pending_lines"]
        if (not re.fullmatch(r"[A-Za-z0-9_=;:-]{1,4096}", self.cursor or "") or
                set(self.offsets) != set(self.logs) or set(self.pending_lines) != set(self.logs)):
            raise RuntimeError("Invalid startup source checkpoint")
        for name, item in self.offsets.items():
            if item is not None and (set(item) != {"dev", "ino", "offset"} or any(type(value) is not int or not 0 <= value < 2**63 for value in item.values())):
                raise RuntimeError("Invalid startup log checkpoint")
            if not isinstance(self.pending_lines[name], str) or len(self.pending_lines[name]) > 8192:
                raise RuntimeError("Invalid startup partial line checkpoint")


class StartupGate:
    """Pure state machine: injected evidence is easy to exercise without a Frame."""
    def __init__(self, baseline):
        if baseline.get("schema") != SCHEMA: raise RuntimeError("Invalid startup baseline schema")
        require_unit(baseline["unit"], allow_inactive=True)
        require_clock(baseline["monotonic"]); require_clock(baseline["wall_time"])
        if not isinstance(baseline["processes"], list) or len(baseline["processes"]) > MAX_PROCESSES:
            raise RuntimeError("Invalid startup baseline process count")
        for process in baseline["processes"]: require_process(process)
        require_errors(baseline.get("errors", []))
        if baseline.get("main_process") is not None: require_process(baseline["main_process"], main=True)
        self.baseline = baseline
        self.isolation_descriptor = require_isolation_descriptor(baseline.get("isolation_descriptor"))
        self.isolation_result = None
        self.invocation, self.unit_identity = None, None
        self.markers, self.pinned = {}, {}
        self.ready_since, self.ready = None, False
        self.last_progress, self.progress = baseline["monotonic"], 0
        self.failed = "Baseline managed process metadata unreadable" if baseline.get("errors") else None

    def result(self, status, reason=None):
        return {"schema": SCHEMA, "status": status, "ready": status == "ready", "reason": reason,
                "invocation": self.invocation, "process_identities": self.pinned,
                "evidence": sorted(self.markers), "screen_pixels_verified": False,
                "diagnostic_isolation_required": self.isolation_descriptor is not None,
                "isolation_verified": status == "ready" and self.isolation_result is not None and self.isolation_result["status"] == "isolated",
                "isolation": self.isolation_result,
                "automatic_restart": False}

    def consume(self, sample, *, stable_seconds=1.0):
        if self.failed: return self.result("fault", self.failed)
        try: return self._consume(sample, stable_seconds)
        except (KeyError, TypeError, ValueError, RuntimeError) as error:
            self.failed = str(error)
            return self.result("fault", self.failed)

    def _consume(self, sample, stable_seconds):
        unit, now = sample["unit"], sample["monotonic"]
        require_unit(unit, allow_inactive=True)
        require_clock(now)
        if not isinstance(sample["processes"], list) or len(sample["processes"]) > MAX_PROCESSES:
            raise RuntimeError("Invalid startup process count")
        for process in sample["processes"]: require_process(process)
        if len({p["pid"] for p in sample["processes"]}) != len(sample["processes"]):
            raise RuntimeError("Duplicate startup process identity")
        if sample.get("main_process") is not None: require_process(sample["main_process"], main=True)
        require_errors(sample["errors"])
        if not isinstance(sample["journal"], list) or len(sample["journal"]) >= 512:
            raise RuntimeError("Invalid startup journal bound")
        if not isinstance(sample["logs"], dict) or set(sample["logs"]) - {"vrserver", "vrcompositor"} or any(not isinstance(text, str) or len(text.encode()) > 128 * 1024 for text in sample["logs"].values()):
            raise RuntimeError("Invalid startup log evidence")
        if now < self.last_progress: raise RuntimeError("Startup monotonic clock regressed")
        if unit["InvocationID"] == self.baseline["unit"]["InvocationID"]:
            if self.invocation is not None: raise RuntimeError("SteamVR returned to its previous invocation")
            return self.result("not-ready", "Waiting for a fresh service invocation")
        current_unit = tuple(unit[key] for key in ("InvocationID", "MainPID", "ExecMainStartTimestampMonotonic", "NRestarts"))
        if self.unit_identity is not None and self.unit_identity != current_unit:
            raise RuntimeError("SteamVR invocation/process restarted during observation")
        self.invocation, self.unit_identity = unit["InvocationID"], current_unit
        if unit["ActiveState"] in ("failed", "inactive") or unit["SubState"] in ("failed", "dead"):
            raise RuntimeError("SteamVR service exited or failed during startup")
        if self.isolation_descriptor is not None:
            report = sample.get("isolation")
            if report is not None:
                require_isolation_report(report, self.isolation_descriptor, unit)
                if self.isolation_result is not None:
                    if self.isolation_result["control_group"] != report["control_group"]:
                        raise RuntimeError("Startup isolation control-group identity changed")
                    if any(report["metadata_usage"][key] < self.isolation_result["metadata_usage"][key]
                            for key in self.isolation_result["metadata_usage"]):
                        raise RuntimeError("Startup isolation cumulative evidence budget regressed")
                if report["status"] == "contaminated":
                    self.isolation_result = report
                    raise RuntimeError("Startup diagnostic isolation failure: " + report["reason"])
            self.isolation_result = report
        elif sample.get("isolation") is not None:
            raise RuntimeError("Startup isolation evidence has no reviewed descriptor")
        old_ids = {identity(p) for p in self.baseline["processes"]}
        old_pids = {p["pid"] for p in self.baseline["processes"]}
        if self.baseline.get("main_process"): old_pids.add(self.baseline["main_process"]["pid"])
        for entry in sample["journal"]:
            message = entry.get("MESSAGE")
            invocation = entry.get("_SYSTEMD_INVOCATION_ID", entry.get("_SYSTEMD_USER_INVOCATION_ID"))
            pid = entry.get("_PID", "0")
            stamp = entry.get("__MONOTONIC_TIMESTAMP")
            if (not isinstance(message, str) or len(message) > 32768 or not isinstance(pid, str) or not re.fullmatch(r"[0-9]{1,20}", pid) or not isinstance(stamp, str) or not re.fullmatch(r"[0-9]{1,20}", stamp)):
                raise RuntimeError("Unsupported current journal evidence")
            if invocation and invocation != self.invocation: continue
            if int(stamp) / 1000000 < self.baseline["monotonic"]: continue
            # A PID number may be reused after restart. An explicit current
            # invocation overrides the old-number filter, never its converse.
            if invocation != self.invocation:
                if int(pid) in old_pids: continue
                if old_pids and re.search(r"(?:Process |pid=)(?:" + "|".join(str(p) for p in old_pids) + r")(?:\D|$)", message): continue
            if FATAL.search(message): raise RuntimeError("Fresh startup failure: " + message[:384])
        current = {p["pid"]: p for p in sample["processes"] if identity(p) not in old_ids and p["state"] not in INELIGIBLE_STATES}
        if len(current) > MAX_PROCESSES: raise RuntimeError("Startup process evidence bound exceeded")
        main = sample.get("main_process")
        if main and main["state"] not in INELIGIBLE_STATES and main["pid"] == int(unit["MainPID"]):
            self.pinned.setdefault("service_main", {"pid": main["pid"], "start_ticks": main["start_ticks"]})
        for role, pinned in self.pinned.items():
            process = main if role == "service_main" else current.get(pinned["pid"])

            if process is None or process["state"] in INELIGIBLE_STATES or identity(process) != (pinned["pid"], pinned["start_ticks"]):
                raise RuntimeError("Required " + role + " process exited/replaced")
        # Replaced/truncated native logs are accepted only with post-baseline
        # timestamps and exact current PID signals; .previous is never read.
        rows = {name: [] for name in ("vrserver", "vrcompositor")}
        for name in rows:
            for row in sample["logs"].get(name, "").splitlines():
                match = DATE.match(row)
                if not match: continue
                stamp = datetime.datetime.strptime(match[1], "%b %d %Y %H:%M:%S.%f").timestamp()
                if stamp < self.baseline["wall_time"]: continue
                rows[name].append(row)
                if FATAL.search(row): raise RuntimeError("Fresh native startup failure: " + row[:384])
        compositors = [p for p in current.values() if p["exe"] == COMPOSITOR and p["kind"] == "compositor"]
        if len(compositors) > 1: raise RuntimeError("Multiple current compositor identities")
        if compositors:
            compositor = compositors[0]
            for row in rows["vrcompositor"]:
                if re.search(r"vrcompositor .* startup with PID=" + str(compositor["pid"]) + r",", row):
                    self.markers["compositor_startup"] = compositor["pid"]
                if self.markers.get("compositor_startup") == compositor["pid"]:
                    if "Done initializing CGraphicsDevice resources" in row: self.markers["graphics_initialized"] = True
                    if "Compositor render thread started" in row: self.markers["render_thread_started"] = True
            if self.markers.get("compositor_startup") == compositor["pid"]:
                self.pinned.setdefault("compositor", {"pid": compositor["pid"], "start_ticks": compositor["start_ticks"]})
        for row in rows["vrserver"]:
            connected = re.search(r"New Connect message from " + re.escape(WEBHELPER) + r" \(VRApplication_WebHelper\).*\) ([0-9]+) *$", row)
            if connected:
                p = current.get(int(connected[1]))
                if p and p["exe"] == WEBHELPER and p["kind"] == "browser" and p["libcef"]:
                    self.markers["ui_browser_connected"] = p["pid"]
            system = re.search(r"SetApplicationPid appkey=system\.systemui pid=([0-9]+),", row)
            if system:
                p = current.get(int(system[1]))
                if p and p["exe"] == WEBHELPER and p["libcef"]:
                    self.markers["system_ui_connected"] = p["pid"]
        for role, marker in (("ui_browser", "ui_browser_connected"), ("system_ui", "system_ui_connected")):
            pid = self.markers.get(marker); p = current.get(pid)
            if p and p["libcef"]:
                self.pinned.setdefault(role, {"pid": p["pid"], "start_ticks": p["start_ticks"]})
        renderers = [p for p in current.values() if p["exe"] == WEBHELPER and p["kind"] == "renderer" and p["libcef"]]
        if renderers:
            self.markers["libcef_renderer_live"] = True
            if "ui_renderer" not in self.pinned:
                p = renderers[0]; self.pinned["ui_renderer"] = {"pid": p["pid"], "start_ticks": p["start_ticks"]}
        progress = len(self.markers) + bool(self.invocation)
        if progress > self.progress: self.progress, self.last_progress = progress, now
        required = {"compositor_startup", "graphics_initialized", "render_thread_started", "ui_browser_connected", "system_ui_connected", "libcef_renderer_live"}
        complete = required <= self.markers.keys() and set(self.pinned) == {"service_main", "compositor", "ui_browser", "system_ui", "ui_renderer"}
        if complete:
            complete = all(current[pin["pid"]]["libcef"] for role, pin in self.pinned.items() if role not in ("compositor", "service_main"))
            complete = complete and current[self.pinned["ui_renderer"]["pid"]]["kind"] == "renderer"
        if unit["ActiveState"] != "active" or unit["SubState"] != "running" or int(unit["MainPID"]) <= 0 or sample["errors"]:
            complete = False
        if self.isolation_descriptor is not None and (self.isolation_result is None or self.isolation_result["status"] != "isolated"):
            complete = False
        elif self.isolation_descriptor is not None and complete:
            target = next(row for row in self.isolation_result["processes"] if row["target_initial_environment_matches"] is not None)
            pin = self.pinned["compositor"]
            if identity(target) != (pin["pid"], pin["start_ticks"]):
                raise RuntimeError("Startup UI compositor and isolated target identity disagree")
        if not complete:
            if self.ready: raise RuntimeError("Previously ready compositor/UI evidence was lost")
            self.ready_since = None
            return self.result("not-ready", "Required compositor/UI evidence missing or unreadable")
        if self.ready_since is None: self.ready_since = now
        self.ready = now - self.ready_since >= stable_seconds
        return self.result("ready" if self.ready else "not-ready", None if self.ready else "Waiting for stable compositor/UI processes")

    def checkpoint(self):
        return {"baseline": self.baseline, "invocation": self.invocation, "unit_identity": self.unit_identity,
                "markers": self.markers, "pinned": self.pinned, "ready_since": self.ready_since,
                "ready": self.ready, "last_progress": self.last_progress, "progress": self.progress,
                "failed": self.failed, "isolation": self.isolation_result}

    @classmethod
    def resume(cls, checkpoint):
        expected = {"baseline", "invocation", "unit_identity", "markers", "pinned", "ready_since", "ready", "last_progress", "progress", "failed", "isolation"}
        if not isinstance(checkpoint, dict) or set(checkpoint) != expected:
            raise RuntimeError("Invalid startup gate checkpoint shape")
        gate = cls(checkpoint["baseline"])
        require_clock(checkpoint["last_progress"])
        if checkpoint["ready_since"] is not None: require_clock(checkpoint["ready_since"])
        if (type(checkpoint["ready"]) is not bool or type(checkpoint["progress"]) is not int or not 0 <= checkpoint["progress"] <= 7 or
                checkpoint["failed"] is not None and (not isinstance(checkpoint["failed"], str) or len(checkpoint["failed"]) > 1024)):
            raise RuntimeError("Invalid startup gate checkpoint values")
        marker_keys = {"compositor_startup", "graphics_initialized", "render_thread_started", "ui_browser_connected", "system_ui_connected", "libcef_renderer_live"}
        role_keys = {"service_main", "compositor", "ui_browser", "system_ui", "ui_renderer"}
        if (not isinstance(checkpoint["markers"], dict) or set(checkpoint["markers"]) - marker_keys or
                not isinstance(checkpoint["pinned"], dict) or set(checkpoint["pinned"]) - role_keys):
            raise RuntimeError("Invalid startup checkpoint proof shape")
        pid_markers = {"compositor_startup", "ui_browser_connected", "system_ui_connected"}
        for key, value in checkpoint["markers"].items():
            if (key in pid_markers and (type(value) is not int or not 0 < value < 2**31) or key not in pid_markers and value is not True):
                raise RuntimeError("Invalid startup checkpoint marker")
        for pin in checkpoint["pinned"].values():
            if not isinstance(pin, dict) or set(pin) != {"pid", "start_ticks"} or any(type(value) is not int or value <= 0 for value in pin.values()):
                raise RuntimeError("Invalid startup checkpoint process pin")
        if checkpoint["ready"] and (set(checkpoint["markers"]) != marker_keys or set(checkpoint["pinned"]) != role_keys or checkpoint["ready_since"] is None or checkpoint["failed"] is not None or checkpoint["unit_identity"] is None):
            raise RuntimeError("Incomplete ready checkpoint")
        for role, marker in (("compositor", "compositor_startup"), ("ui_browser", "ui_browser_connected"), ("system_ui", "system_ui_connected")):
            if role in checkpoint["pinned"] and checkpoint["pinned"][role]["pid"] != checkpoint["markers"].get(marker):
                raise RuntimeError("Startup checkpoint marker/process disagreement")
        if checkpoint["unit_identity"] is not None and (not isinstance(checkpoint["unit_identity"], (list, tuple)) or len(checkpoint["unit_identity"]) != 4 or checkpoint["unit_identity"][0] != checkpoint["invocation"] or not re.fullmatch(r"[0-9a-f]{32}", checkpoint["invocation"] or "")):
            raise RuntimeError("Invalid startup checkpoint unit identity")
        if checkpoint["isolation"] is not None:
            if gate.isolation_descriptor is None or checkpoint["unit_identity"] is None:
                raise RuntimeError("Unscoped startup isolation checkpoint")
            report_unit = {**gate.baseline["unit"], "InvocationID": checkpoint["invocation"],
                           "ControlGroup": checkpoint["isolation"]["control_group"]}
            require_isolation_report(checkpoint["isolation"], gate.isolation_descriptor, report_unit)
            if gate.baseline["unit"]["ControlGroup"] and checkpoint["isolation"]["control_group"] != gate.baseline["unit"]["ControlGroup"]:
                raise RuntimeError("Startup isolation checkpoint control group changed")
        if checkpoint["ready"] and gate.isolation_descriptor is not None and (
                checkpoint["isolation"] is None or checkpoint["isolation"]["status"] != "isolated"):
            raise RuntimeError("Ready startup checkpoint lacks diagnostic isolation proof")
        if checkpoint["ready"] and gate.isolation_descriptor is not None:
            target = next(row for row in checkpoint["isolation"]["processes"] if row["target_initial_environment_matches"] is not None)
            pin = checkpoint["pinned"]["compositor"]
            if identity(target) != (pin["pid"], pin["start_ticks"]):
                raise RuntimeError("Ready checkpoint compositor/isolation identity disagreement")
        gate.isolation_result = checkpoint["isolation"]
        for key in ("invocation", "unit_identity", "markers", "pinned", "ready_since", "ready", "last_progress", "progress", "failed"):
            setattr(gate, key, checkpoint[key])
        if gate.unit_identity is not None: gate.unit_identity = tuple(gate.unit_identity)
        return gate


def wait_ready(source, baseline, *, timeout=20.0, poll_seconds=0.25,
               stable_seconds=1.0, no_progress_seconds=10.0, clock=time.monotonic, sleep=time.sleep):
    """Short bounded polling. Return a reasoned result; never restart/rollback."""
    if not (0 < timeout <= 30 and 0.05 <= poll_seconds <= 1 and 0 <= stable_seconds <= 3 and 0 < no_progress_seconds <= timeout):
        raise RuntimeError("Unsupported startup polling bounds")
    gate = StartupGate(baseline); deadline = clock() + timeout
    while True:
        try: result = gate.consume(source.sample(), stable_seconds=stable_seconds)
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            result = gate.result("not-ready", "Startup evidence unreadable: " + str(error)[:384])
        now = clock()
        if result["status"] in ("ready", "fault"): break
        if now >= deadline:
            result = gate.result("not-ready", "Startup readiness timed out"); break
        if now - gate.last_progress >= no_progress_seconds:
            result = gate.result("not-ready", "Startup readiness stopped advancing"); break
        sleep(min(poll_seconds, deadline - now))
    return {**result, "gate_checkpoint": gate.checkpoint(), "source_checkpoint": source.checkpoint()}


def monitor(source, gate, *, seconds=10.0, poll_seconds=0.5, clock=time.monotonic, sleep=time.sleep):
    """Continue a passed gate; fail on UI loss/current errors without restarting."""
    if not gate.ready or not (0 < seconds <= 30 and 0.05 <= poll_seconds <= 1):
        raise RuntimeError("Ready checkpoint and bounded monitor required")
    deadline = clock() + seconds; samples = 0
    while True:
        try: result = gate.consume(source.sample(), stable_seconds=0)
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            gate.failed = "Startup monitor evidence unreadable: " + str(error)[:384]
            result = gate.result("not-ready", gate.failed)
        samples += 1
        if result["status"] != "ready" or clock() >= deadline: break
        sleep(min(poll_seconds, deadline - clock()))
    return {**result, "monitor_samples": samples, "gate_checkpoint": gate.checkpoint(),
            "source_checkpoint": source.checkpoint()}
