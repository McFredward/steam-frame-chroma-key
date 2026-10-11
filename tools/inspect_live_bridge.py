#!/usr/bin/env python3
"""Read existing Frame process/log metadata without installing or initializing VR.

Standalone stdlib only; run with Python -B, including through stdin. No environment,
configuration, credentials, binary bodies, or original log lines enter the report.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import signal
import stat
import struct
import sys
import time

COMM_EXECUTABLES = {
    "vrlink": "vrlink", "vrlinkrunthread": "vrlink",
    "vrcompositor": "vrcompositor", "vrserver": "vrserver",
}
COMM_BYTES = frozenset(name.encode("ascii") for name in COMM_EXECUTABLES)
LOG_NAMES = ("vrlink.txt", "vrcompositor.txt", "streaming_log.txt")
DEFAULT_LOGS = Path("/home/steamos/.local/share/Steam/logs")
MAX_READ_BYTES = 1024 * 1024
MAX_TAIL_BYTES = 128 * 1024
MAX_LINE_BYTES = 2048
MAX_RECORDS = 128
MAX_FEC_RECORDS = 16
MAX_PROCESSES = 16
MAX_PROC_ENTRIES = 4096
MAX_PROGRAM_HEADERS = 128
MAX_NOTE_BYTES = 8192
MAX_RUNTIME_SECONDS = 5.0
LOG_RECENCY_SECONDS = 300

# Preserve spelling and meaning. None of these labels is an alias for another.
NUMERIC_LABELS = (
    "unFrameId", "lnFrameId", "frame_id", "m_iPresentNumber", "history_idx", "fdiff",
    "source_frame_id", "encoder_frame_id", "app_frame_id", "decode_frame_id",
    "present_frame_id", "capture_buffer_index", "buffer_index", "buffer_id",
)
FIELD = re.compile(
    rb"(?<![A-Za-z0-9_.])(" + b"|".join(s.encode("ascii") for s in NUMERIC_LABELS)
    + rb")[ \t]*=[ \t]*([+-]?[0-9]{1,20})(?=$|[ \t\r,;)])"
)
FEC_RETRY = re.compile(
    rb"SVLFEC: substantive retry \(([0-9]{1,10}) packets\) due to frame skip "
    rb"\(skipped ([0-9]{1,10}), last sub ([0-9]{1,10}), group ([0-9]{1,10}), "
    rb"expected size ([0-9]{1,10})\)"
)
CAPTURE_SETUP = re.compile(
    rb"SVLCodecV4L2::CaptureSetup: New decode width, height, format -> "
    rb"([0-9]{1,5})x([0-9]{1,5})(?=$|[ \t\r,;)])"
)
CAPTURE_FORMATS = frozenset((b"C01Q", b"NV12", b"P010"))
CAPTURE_COUNT = re.compile(
    rb"(?<![A-Za-z0-9_.])uCaptureBufferCount[ \t]*->[ \t]*"
    rb"([0-9]{1,4})(?=$|[ \t\r,;)])"
)


class RuntimeLimit(Exception):
    """A fixed, safe diagnostic; never contains input data."""


class ReadLimit(Exception):
    """Do not declare a complete process snapshot after exhausting read bounds."""


class Budget:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.started = clock()
        self.read_bytes = 0
        self.warnings = set()

    def check(self):
        if self.clock() - self.started >= MAX_RUNTIME_SECONDS:
            raise RuntimeLimit()

    def read(self, fd, offset, count):
        self.check()
        if count > MAX_READ_BYTES - self.read_bytes:
            raise ReadLimit()
        if offset < 0 or offset > (1 << 63) - 1:
            self.warnings.add("invalid_read_offset")
            return b""
        data = os.pread(fd, count, offset)
        self.read_bytes += len(data)
        return data


def open_regular(path, *, follow_symlink=False):
    """Use read-only nonblocking descriptors, then check the opened inode type."""
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC
    if not follow_symlink:
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags)
    try:
        metadata = os.fstat(fd)
        if not stat.S_ISREG(metadata.st_mode):
            raise OSError()
        return fd, metadata
    except BaseException:
        os.close(fd)
        raise


def elf_identity(fd, budget):
    """Extract only ELF class/machine and a bounded GNU build ID, never bodies."""
    header = budget.read(fd, 0, 64)
    if len(header) < 64 or header[:4] != b"\x7fELF" or header[4:6] != b"\x02\x01":
        return {"elf64_little_endian": False}
    result = {"elf64_little_endian": True,
              "machine": struct.unpack_from("<H", header, 18)[0], "build_id": None}
    table = struct.unpack_from("<Q", header, 32)[0]
    stride, count = struct.unpack_from("<HH", header, 54)
    if stride != 56 or count > MAX_PROGRAM_HEADERS:
        budget.warnings.add("elf_header_limit_or_format")
        return result
    for index in range(count):
        ph = budget.read(fd, table + index * stride, stride)
        if len(ph) != stride:
            break
        if struct.unpack_from("<I", ph)[0] != 4:  # PT_NOTE
            continue
        offset, size = struct.unpack_from("<Q", ph, 8)[0], struct.unpack_from("<Q", ph, 32)[0]
        if size > MAX_NOTE_BYTES:
            budget.warnings.add("elf_note_limit")
            continue
        notes = budget.read(fd, offset, size)
        cursor = 0
        while cursor + 12 <= len(notes):
            namesize, descsize, kind = struct.unpack_from("<III", notes, cursor)
            start = cursor + 12
            desc = start + ((namesize + 3) & ~3)
            end = desc + descsize
            next_note = desc + ((descsize + 3) & ~3)
            if end > len(notes) or next_note <= cursor:
                break
            if kind == 3 and namesize == 4 and notes[start:start + 4] == b"GNU\0":
                if 4 <= descsize <= 64:
                    result["build_id"] = notes[desc:end].hex()
                    return result
            cursor = next_note
    return result


def inspect_processes(proc_root, budget, report):
    processes = report["processes"]
    complete = True
    entries_seen = 0
    try:
        with os.scandir(proc_root) as entries:
            for entry in entries:
                budget.check()
                entries_seen += 1
                if entries_seen > MAX_PROC_ENTRIES:
                    budget.warnings.add("process_scan_limit")
                    complete = False
                    break
                if not entry.name.isascii() or not entry.name.isdecimal() or len(entry.name) > 10:
                    continue
                pid_root = Path(entry.path)
                try:
                    fd, _ = open_regular(pid_root / "comm")
                    try:
                        comm = budget.read(fd, 0, 32).strip()
                    finally:
                        os.close(fd)
                except FileNotFoundError:
                    continue  # Process ended during the snapshot.
                except OSError:
                    complete = False
                    budget.warnings.add("process_comm_unavailable")
                    continue
                if comm not in COMM_BYTES:
                    continue
                if len(processes) >= MAX_PROCESSES:
                    budget.warnings.add("process_count_limit")
                    complete = False
                    break
                process = {"comm": comm.decode("ascii"), "pid": int(entry.name)}
                processes.append(process)
                try:
                    target = os.readlink(pid_root / "exe")
                    basename = Path(target.removesuffix(" (deleted)")).name
                    if basename != COMM_EXECUTABLES[process["comm"]]:
                        budget.warnings.add("process_executable_name_unidentified")
                        continue
                    fd, metadata = open_regular(pid_root / "exe", follow_symlink=True)
                    try:
                        process["executable"] = {
                            "name": basename, "bytes": metadata.st_size,
                            "mtime_ns": metadata.st_mtime_ns, **elf_identity(fd, budget),
                        }
                    finally:
                        os.close(fd)
                except OSError:
                    budget.warnings.add("process_executable_unavailable")
    except OSError:
        complete = False
        budget.warnings.add("process_directory_unavailable")
    report["process_snapshot_complete"] = complete


def numeric_record(line):
    """Reject oversized, incomplete numbers and duplicate labels; omit all prose."""
    if len(line) > MAX_LINE_BYTES:
        return None
    fields = {}
    for match in FIELD.finditer(line):
        label = match[1].decode("ascii")
        value = int(match[2])
        if label in fields or not -(1 << 63) <= value < (1 << 64):
            return None
        fields[label] = value
    if not fields:
        retry = FEC_RETRY.search(line)
        if retry is not None:
            return {"event": "fec_substantive_retry", "namespace": "fec_reassembly",
                    "fields": dict(zip(("packets", "skipped", "last sub", "group", "expected size"),
                                       (int(v) for v in retry.groups())))}
        capture = CAPTURE_SETUP.search(line)
        if capture is not None:
            width, height = (int(v) for v in capture.groups())
            if not 0 < width <= 32768 or not 0 < height <= 32768:
                return None
            result = {"event": "decoder_capture_setup", "namespace": "decoder_configuration",
                      "fields": {"width": width, "height": height}}
            # Never expose arbitrary tokens after the extent (paths, credentials, etc.).
            format_match = re.match(rb"[ \t]+([A-Z0-9]{4})(?=$|[ \t\r,;)])", line[capture.end():])
            if format_match is not None and format_match[1] in CAPTURE_FORMATS:
                result["format"] = format_match[1].decode("ascii")
            return result
        count = CAPTURE_COUNT.search(line)
        if count is not None:
            return {"event": "decoder_capture_buffer_count", "namespace": "decoder_configuration",
                    "fields": {"uCaptureBufferCount": int(count[1])}}
        return None
    event = "numeric_frame_fields"
    if b"No matching metadata" in line:
        event = "missing_metadata"
    return {"event": event, "fields": fields}


def inspect_log(path, budget, report):
    # Callers construct paths exclusively from the three fixed basenames.
    item = {"name": path.name, "present": False, "records": []}
    report["logs"].append(item)
    try:
        fd, metadata = open_regular(path)
    except FileNotFoundError:
        return
    except OSError:
        budget.warnings.add("known_log_unavailable_or_not_regular")
        return
    try:
        item.update(present=True, bytes=metadata.st_size, mtime_ns=metadata.st_mtime_ns)
        age_ns = report["snapshot_unix_ns"] - metadata.st_mtime_ns
        item["age_ms"] = age_ns // 1000000
        item["file_recency"] = (
            "mtime_after_snapshot" if age_ns < 0 else
            "older_than_five_minutes" if age_ns > LOG_RECENCY_SECONDS * 1000000000 else
            "modified_within_five_minutes_stream_unverified")
        if age_ns < 0:
            budget.warnings.add("log_mtime_after_snapshot")
        offset = max(0, metadata.st_size - MAX_TAIL_BYTES)
        data = budget.read(fd, offset, min(metadata.st_size, MAX_TAIL_BYTES))
        item["tail_bytes_read"] = len(data)
        item["tail_limited"] = offset != 0
        if offset:
            # The first partial line could begin in the middle of a private field.
            split = data.find(b"\n")
            data = data[split + 1:] if split >= 0 else b""
        lines = data.split(b"\n")[:-1]  # Ignore a possibly still-writing last line.
        for line in lines:
            budget.check()
            report["counters"]["complete_tail_lines_scanned"] += 1
            if len(line) > MAX_LINE_BYTES:
                report["counters"]["oversized_lines_omitted"] += 1
                continue
            if ((b"OutputSetup" in line and b"ENOMEM" in line) or
                    (b"Failed to request output buffers." in line and
                     b"[12 Cannot allocate memory]" in line)):
                report["counters"]["decoder_allocation_failure_lines"] += 1
            record = numeric_record(line)
            if record is None:
                continue
            if record["event"] == "fec_substantive_retry":
                report["counters"]["fec_retry_lines"] += 1
                if report["counters"]["fec_retry_records"] >= MAX_FEC_RECORDS:
                    # Network retry noise must not hide later association fields.
                    budget.warnings.add("fec_record_limit")
                    continue
            if report["counters"]["numeric_records"] >= MAX_RECORDS:
                budget.warnings.add("record_limit")
                break
            item["records"].append(record)
            report["counters"]["numeric_records"] += 1
            if record["event"] == "missing_metadata":
                report["counters"]["missing_metadata_records"] += 1
            elif record["event"] == "fec_substantive_retry":
                report["counters"]["fec_retry_records"] += 1
        after = os.fstat(fd)
        item["changed_during_read"] = (
            metadata.st_size != after.st_size or metadata.st_mtime_ns != after.st_mtime_ns)
    finally:
        os.close(fd)


def inspect(*, proc_root=Path("/proc"), logs_root=DEFAULT_LOGS,
            clock=time.monotonic, unix_ns=time.time_ns):
    budget = Budget(clock)
    report = {
        "schema": "frame-live-bridge-inspection", "schema_version": 1,
        "snapshot_unix_ns": unix_ns(), "processes": [], "logs": [],
        "process_snapshot_complete": False,
        "association_proven": False, "mask_activation_safe": False,
        "stream_active": "unverified",
        "counters": {"complete_tail_lines_scanned": 0, "numeric_records": 0,
                     "missing_metadata_records": 0, "oversized_lines_omitted": 0,
                     "decoder_allocation_failure_lines": 0, "fec_retry_records": 0,
                     "fec_retry_lines": 0},
        "limits": {"read_bytes": MAX_READ_BYTES, "log_tail_bytes_each": MAX_TAIL_BYTES,
                   "records_total": MAX_RECORDS, "fec_records": MAX_FEC_RECORDS,
                   "processes": MAX_PROCESSES,
                   "process_directory_entries": MAX_PROC_ENTRIES,
                   "runtime_seconds": MAX_RUNTIME_SECONDS},
    }
    try:
        inspect_processes(Path(proc_root), budget, report)
        for name in LOG_NAMES:
            budget.check()
            inspect_log(Path(logs_root) / name, budget, report)
    except RuntimeLimit:
        budget.warnings.add("runtime_limit")
    except ReadLimit:
        budget.warnings.add("read_budget_reached")
    except OSError:
        budget.warnings.add("read_interrupted_or_unavailable")
    present = any(COMM_EXECUTABLES[p["comm"]] == "vrlink" for p in report["processes"])
    report["live_status"] = (
        "vrlink_process_present_stream_unverified" if present else
        "no_vrlink_process_observed" if report["process_snapshot_complete"] else
        "process_snapshot_incomplete")
    for item in report["logs"]:
        item["evidence_status"] = (
            "historical_or_non_streaming_no_vrlink_observed"
            if report["live_status"] == "no_vrlink_process_observed" else
            "stream_activity_unverified")
    report["warnings"] = sorted(budget.warnings)
    report["bytes_read"] = budget.read_bytes
    report["elapsed_ms"] = round(max(0, clock() - budget.started) * 1000)
    return report


def main(argv=None):
    # Avoid argument-parser localization, which consults environment variables.
    args = sys.argv[1:] if argv is None else argv
    logs_root = DEFAULT_LOGS
    if args == ["--help"]:
        sys.stdout.write("Usage: python3 -B - [--logs-root DIRECTORY]\n"
                         "Read-only bounded process/log metadata; JSON on stdout.\n")
        return 0
    if args:
        if len(args) != 2 or args[0] != "--logs-root":
            sys.stderr.write("Usage: python3 -B - [--logs-root DIRECTORY]\n")
            return 2
        logs_root = Path(args[1])

    def deadline(_signum, _frame):
        raise RuntimeLimit()

    previous = signal.signal(signal.SIGALRM, deadline)
    signal.setitimer(signal.ITIMER_REAL, MAX_RUNTIME_SECONDS)
    try:
        report = inspect(logs_root=logs_root)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
    json.dump(report, sys.stdout, sort_keys=True, separators=(",", ":"))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
