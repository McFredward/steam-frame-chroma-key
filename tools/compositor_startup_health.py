#!/usr/bin/env python3
"""Bounded, read-only SteamVR startup evidence; never a screen-pixel verdict.

Run outside rendering threads. A live compositor/service is insufficient: the
current UI browser, system UI and a libcef-backed renderer must also be alive.
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
                 clock=time.monotonic, wall_clock=time.time, uid=None):
        self.home, self.proc, self.run = Path(home), Path(proc_root), run
        self.clock, self.wall_clock = clock, wall_clock
        self.sleep = time.sleep
        self.uid = os.getuid() if uid is None else uid
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
                "processes": processes, "main_process": main, "errors": errors, "monotonic": self.clock(), "wall_time": self.wall_clock()}

    def sample(self):
        unit = self.unit(); processes, errors = self.processes(unit)
        try: main = self.main_process(unit)
        except (OSError, RuntimeError) as error:
            main = None; errors.append({"pid": int(unit["MainPID"]), "reason": type(error).__name__})
        journal = self.journal()
        return {"unit": unit, "processes": processes, "main_process": main, "errors": errors, "journal": journal,
                "logs": {name: self.log_read(name) for name in self.logs}, "monotonic": self.clock()}

    def checkpoint(self):
        return {"cursor": self.cursor, "log_offsets": self.offsets, "pending_lines": self.pending_lines, "total": self.total}

    def resume(self, checkpoint):
        if not isinstance(checkpoint, dict) or set(checkpoint) != {"cursor", "log_offsets", "pending_lines", "total"}:
            raise RuntimeError("Invalid startup source checkpoint shape")
        if type(checkpoint["total"]) is not int or not 0 <= checkpoint["total"] <= LIMIT:
            raise RuntimeError("Invalid cumulative startup evidence budget")
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
        self.invocation, self.unit_identity = None, None
        self.markers, self.pinned = {}, {}
        self.ready_since, self.ready = None, False
        self.last_progress, self.progress = baseline["monotonic"], 0
        self.failed = "Baseline managed process metadata unreadable" if baseline.get("errors") else None

    def result(self, status, reason=None):
        return {"schema": SCHEMA, "status": status, "ready": status == "ready", "reason": reason,
                "invocation": self.invocation, "process_identities": self.pinned,
                "evidence": sorted(self.markers), "screen_pixels_verified": False,
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
                "failed": self.failed}

    @classmethod
    def resume(cls, checkpoint):
        expected = {"baseline", "invocation", "unit_identity", "markers", "pinned", "ready_since", "ready", "last_progress", "progress", "failed"}
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
