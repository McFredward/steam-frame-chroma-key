"""Injected startup evidence and local proc/log fixtures; no Frame access."""
import copy
import datetime
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("startup_health_fixture", Path(__file__).resolve().parents[1] / "tools/compositor_startup_health.py")
M = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(M)


def unit(invocation="b" * 32):
    return {"InvocationID": invocation, "ActiveState": "active", "SubState": "running", "MainPID": "100",
        "ControlGroup": "/user.slice/user-1000.slice/user@1000.service/app.slice/steamvr.service",
        "NRestarts": "0", "ExecMainStartTimestampMonotonic": "1000000"}


def process(pid, kind):
    return {"pid": pid, "start_ticks": pid * 100, "state": "S", "exe": M.COMPOSITOR if kind == "compositor" else M.WEBHELPER,
            "kind": kind, "libcef": kind != "compositor"}


def baseline():
    return {"schema": M.SCHEMA, "unit": unit("a" * 32), "processes": [process(10, "compositor")],
            "monotonic": 1.0, "wall_time": 100.0, "cursor": "s=old", "log_offsets": {}}


def line(message, wall=101):
    date = datetime.datetime.fromtimestamp(wall).strftime("%a %b %d %Y %H:%M:%S.%f")
    return date + " [Info] - " + message


def sample(now=2.0):
    return {"unit": unit(), "monotonic": now, "errors": [], "journal": [],
        "main_process": {"pid": 100, "start_ticks": 10000, "state": "S"},
        "processes": [process(101, "compositor"), process(102, "browser"), process(103, "zygote"), process(104, "renderer")],
        "logs": {"vrcompositor": "\n".join([line("vrcompositor 2.18.2 startup with PID=101, config=/native"),
            line("Done initializing CGraphicsDevice resources"), line("Compositor render thread started")]),
            "vrserver": "\n".join([line("New Connect message from " + M.WEBHELPER + " (VRApplication_WebHelper) (Args: -lang=en_us) 102 "),
                line("SetApplicationPid appkey=system.systemui pid=103, external transition")])}}


def descriptor():
    result = {"schema": M.ISOLATION_SCHEMA, "target_exe": M.COMPOSITOR,
        "selector_path": "/reviewed/selector.so", "payload_path": "/reviewed/observer.so", "audit_path": "/reviewed/audit.so"}
    result["target_exec_environment"] = {"LD_PRELOAD": result["selector_path"] + " " + result["payload_path"],
        "LD_AUDIT": result["audit_path"], "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE": "/reviewed/envelope.bin",
        "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT": "/reviewed/raw.jsonl", "FRAME_MASK_TRACE": "1",
        "FRAME_MASK_TRACE_BINDINGS": "1", "FRAME_MASK_TRACE_OUTPUT": "/reviewed/trace.jsonl"}
    return result


def isolated_report():
    config = descriptor()
    rows = []
    for p in sample()["processes"]:
        target = p["exe"] == M.COMPOSITOR
        rows.append({key: p[key] for key in ("pid", "start_ticks", "state", "exe")})
        rows[-1].update(ppid=100, selector_mapped=True, payload_mapped=target, audit_mapped=target,
            owned_mapping_deleted=False, loader_payload=target, loader_audit=target,
            diagnostic_keys=sorted(set(config["target_exec_environment"]) - {"LD_PRELOAD", "LD_AUDIT"}) if target else [],
            target_initial_environment_matches=True if target else None)
    return {"schema": M.ISOLATION_SCHEMA, "descriptor": config, "invocation": unit()["InvocationID"],
        "control_group": unit()["ControlGroup"], "status": "isolated", "reason": None, "processes": rows,
        "errors": [], "inventory_stable": True, "unit_stable": True, "target_env_view": "initial-exec-only",
        "runtime_environ_verified": False, "metadata_usage": {"samples": 1, "processes": 4, "metadata_bytes": 4000}}


def rejudge(report):
    report["status"], report["reason"] = M.isolation_verdict(report["processes"], report["errors"],
        inventory_stable=report["inventory_stable"], unit_stable=report["unit_stable"])
    return report


class Source:
    def __init__(self, samples): self.samples = samples; self.now = 2.0; self.calls = 0
    def sample(self):
        self.calls += 1
        item = copy.deepcopy(self.samples[min(self.calls - 1, len(self.samples) - 1)])
        if isinstance(item, Exception): raise item
        item["monotonic"] = self.now
        return item
    def clock(self): return self.now
    def sleep(self, seconds): self.now += seconds
    def checkpoint(self): return {"fixture": True}


class JournalMessageTests(unittest.TestCase):
    def test_all_binary_bytes_are_preserved_without_utf8_assumptions(self):
        raw = list(range(256))
        self.assertEqual(M.journal_message(raw).encode("latin-1"), bytes(raw))

    def test_string_semantics_and_exact_message_bounds_are_preserved(self):
        for value in ("", "\x1b[93mReady\x1b[m", "\u00e4" * M.JOURNAL_MESSAGE_LIMIT):
            with self.subTest(value_length=len(value)):
                self.assertEqual(M.journal_message(value), value)
        self.assertEqual(M.journal_message([65] * M.JOURNAL_MESSAGE_LIMIT), "A" * M.JOURNAL_MESSAGE_LIMIT)
        self.assertEqual(M.journal_message([]), "")
        for value in ("x" * (M.JOURNAL_MESSAGE_LIMIT + 1), [65] * (M.JOURNAL_MESSAGE_LIMIT + 1)):
            with self.subTest(type=type(value).__name__), self.assertRaises(RuntimeError):
                M.journal_message(value)

    def test_unknown_encodings_and_non_byte_array_elements_are_refused(self):
        for value in (None, 1, True, {}, {"bytes": [65]}, (65,), b"A",
                      ["A"], [[65]], [65, True], [65, False], [65, -1], [65, 256],
                      [65, 1.0], [65, None], [65, {}]):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                M.journal_message(value)


class GateTests(unittest.TestCase):
    def test_missing_actual_main_process_cannot_pass_service_active(self):
        s = sample(); s["main_process"] = None
        self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_process_duplicate_bad_types_and_nan_are_rejected(self):
        for variant in ("duplicate", "bool_pid", "bad_kind", "nan", "huge_clock"):
            s = sample()
            if variant == "duplicate": s["processes"].append(copy.deepcopy(s["processes"][0]))
            if variant == "bool_pid": s["processes"][0]["pid"] = True
            if variant == "bad_kind": s["processes"][0]["kind"] = {}
            if variant == "nan": s["monotonic"] = float("nan")
            if variant == "huge_clock": s["monotonic"] = 2**1024
            with self.subTest(variant=variant): self.assertEqual(M.StartupGate(baseline()).consume(s)["status"], "fault")

    def test_corrupt_ready_checkpoints_never_bypass_health_sampling(self):
        gate = M.StartupGate(baseline()); gate.consume(sample(), stable_seconds=0)
        for key, value in (("markers", {}), ("pinned", {}), ("unit_identity", None),
                           ("ready", 1), ("last_progress", float("nan")), ("invocation", "foreign")):
            saved = copy.deepcopy(gate.checkpoint()); saved[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError): M.StartupGate.resume(saved)

    def test_failed_or_inactive_baseline_still_permits_fresh_recovery_start(self):
        b = baseline(); b["unit"].update(InvocationID="", ActiveState="failed", SubState="failed", ControlGroup="", MainPID="0")
        self.assertTrue(M.StartupGate(b).consume(sample(), stable_seconds=0)["ready"])

    def test_valid_startup_requires_positive_ui_and_stable_live_processes(self):
        gate = M.StartupGate(baseline()); first = gate.consume(sample())
        self.assertFalse(first["ready"]); self.assertEqual(first["status"], "not-ready")
        s = sample(3.1); s["logs"] = {}; result = gate.consume(s)
        self.assertTrue(result["ready"]); self.assertFalse(result["screen_pixels_verified"])
        self.assertEqual(set(result["process_identities"]), {"service_main", "compositor", "ui_browser", "system_ui", "ui_renderer"})

    def test_active_compositor_with_actual_static_tls_ui_failure_is_rejected(self):
        gate = M.StartupGate(baseline()); s = sample(); s["processes"] = s["processes"][:1]
        s["journal"] = [{"MESSAGE": "./vrwebhelper: error while loading shared libraries: " + M.LIBCEF + ": cannot allocate memory in static TLS block",
            "_PID": "368028", "_SYSTEMD_INVOCATION_ID": "b" * 32, "__MONOTONIC_TIMESTAMP": "2000000"}]
        result = gate.consume(s); self.assertEqual(result["status"], "fault")
        self.assertIn("static TLS", result["reason"])
        self.assertEqual(gate.consume(sample(4))["status"], "fault")

    def test_active_service_and_compositor_prepare_are_not_ui_readiness(self):
        s = sample(); s["processes"] = s["processes"][:1]; s["logs"]["vrserver"] = "rawPrepare succeeded"
        self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_known_gamescope_and_input_warning_are_not_fatal_alone(self):
        s = sample(); s["journal"] = [{"MESSAGE": "[Gamescope WSI] Failed to connect to gamescope socket",
            "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}]
        s["logs"]["vrserver"] += "\n" + line("[Error] - [Input] LoadActionManifest failed. Could not find action manifest for vrwebhelper_systemui")
        self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_actual_shaped_ansi_binary_journal_warning_is_not_a_startup_fault(self):
        # Same systemd byte-array shape and text as the captured Frame warning.
        message = (b'\x1b[0;93m2026-10-14 18:37:44.453260815 [W:onnxruntime:, device_discovery.cc:211 '
            b'DiscoverDevicesForPlatform] GPU device discovery failed: device_discovery.cc:91 '
            b'ReadFileContents Failed to open file: "/sys/class/drm/card0/device/vendor"\x1b[m')
        s = sample(); s["journal"] = [{"MESSAGE": list(message), "_PID": "102",
            "_SYSTEMD_INVOCATION_ID": "b" * 32, "__MONOTONIC_TIMESTAMP": "2000000"}]
        result = M.StartupGate(baseline()).consume(s, stable_seconds=0)
        self.assertTrue(result["ready"])
        self.assertFalse(result["screen_pixels_verified"])

    def test_binary_journal_fatal_evidence_is_scanned_and_stays_failed(self):
        for message in (b"./vrwebhelper: error while loading shared libraries: " + M.LIBCEF.encode() +
                        b": cannot allocate memory in static TLS block",
                        b"VK_ERROR_DEVICE_LOST", b"symbol lookup error", b"renderer process crashed"):
            s = sample(); s["journal"] = [{"MESSAGE": list(b"\xff\x00\x1b[93m" + message + b"\x1b[m"),
                "_PID": "102", "_SYSTEMD_USER_INVOCATION_ID": "b" * 32,
                "__MONOTONIC_TIMESTAMP": "2000000"}]
            gate = M.StartupGate(baseline())
            with self.subTest(message=message):
                result = gate.consume(s, stable_seconds=0)
                self.assertEqual(result["status"], "fault")
                self.assertIn("Fresh startup failure:", result["reason"])
                self.assertIn(message.decode(), result["reason"])
                self.assertEqual(gate.consume(sample(4))["status"], "fault")

    def test_ansi_control_codes_cannot_hide_fatal_text_in_either_encoding(self):
        for text in ("\x1b[93mVK_ERROR_DEVICE_LOST\x1b[m", "cannot allocate memory in \x1b[93mstatic TLS block\x1b[m"):
            for message in (text, list(text.encode())):
                s = sample(); s["journal"] = [{"MESSAGE": message, "_PID": "102",
                    "__MONOTONIC_TIMESTAMP": "2000000"}]
                with self.subTest(text=text, encoding=type(message).__name__):
                    result = M.StartupGate(baseline()).consume(s)
                    self.assertEqual(result["status"], "fault")
                    self.assertEqual(result["reason"], "Fresh startup failure: " + text)

    def test_malformed_or_oversize_binary_messages_are_not_ignored_or_truncated(self):
        for message in ([65, True], [65, -1], [65, 256], [65, 1.0], [65, None],
                        [[65]], ["cannot allocate memory in static TLS block"],
                        [65] * M.JOURNAL_MESSAGE_LIMIT + list(b"VK_ERROR_DEVICE_LOST"),
                        {"bytes": [65]}):
            s = sample(); s["journal"] = [{"MESSAGE": message, "_PID": "102",
                "_SYSTEMD_INVOCATION_ID": "b" * 32, "__MONOTONIC_TIMESTAMP": "2000000"}]
            gate = M.StartupGate(baseline())
            with self.subTest(type=type(message).__name__, length=len(message)):
                result = gate.consume(s, stable_seconds=0)
                self.assertEqual(result["status"], "fault")
                self.assertEqual(result["reason"], "Unsupported current journal evidence")
                self.assertEqual(gate.consume(sample(4))["status"], "fault")

    def test_binary_journal_keeps_invocation_timestamp_and_old_pid_filters(self):
        for updates in ({"_SYSTEMD_INVOCATION_ID": "a" * 32}, {"_PID": "10"},
                        {"__MONOTONIC_TIMESTAMP": "999999"}, {"MESSAGE": list(b"Process 10 (vrcompositor) core dumped")}):
            s = sample(); row = {"MESSAGE": list(b"segmentation fault core dumped"),
                "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}; row.update(updates)
            s["journal"] = [row]
            with self.subTest(updates=updates):
                self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])
        s = sample(); s["journal"] = [{"MESSAGE": list(b"cannot allocate memory in static TLS block"),
            "_PID": "10", "_SYSTEMD_INVOCATION_ID": "b" * 32, "__MONOTONIC_TIMESTAMP": "2000000"}]
        self.assertEqual(M.StartupGate(baseline()).consume(s, stable_seconds=0)["status"], "fault")

    def test_binary_journal_does_not_relax_required_metadata_or_row_bounds(self):
        for updates in ({"_PID": [102]}, {"_PID": "102\n"}, {"__MONOTONIC_TIMESTAMP": True},
                        {"__MONOTONIC_TIMESTAMP": "2.0"}):
            s = sample(); row = {"MESSAGE": [65], "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}
            row.update(updates); s["journal"] = [row]
            with self.subTest(updates=updates):
                self.assertEqual(M.StartupGate(baseline()).consume(s)["status"], "fault")
        s = sample(); s["journal"] = [[65]]
        self.assertEqual(M.StartupGate(baseline()).consume(s)["status"], "fault")
        s["journal"] = [{"MESSAGE": [65], "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}] * 512
        result = M.StartupGate(baseline()).consume(s)
        self.assertEqual(result["status"], "fault")
        self.assertEqual(result["reason"], "Invalid startup journal bound")

    def test_old_invocation_pid_or_pre_baseline_journal_errors_are_not_current(self):
        for updates in ({"_SYSTEMD_INVOCATION_ID": "a" * 32}, {"_PID": "10"}, {"__MONOTONIC_TIMESTAMP": "999999"}):
            s = sample(); row = {"MESSAGE": "segmentation fault core dumped", "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}; row.update(updates)
            s["journal"] = [row]
            with self.subTest(updates=updates): self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_old_core_exit_report_from_manager_is_not_new_ui_crash(self):
        s = sample(); s["journal"] = [{"MESSAGE": "Process 10 (vrcompositor) core dumped", "_PID": "1592", "__MONOTONIC_TIMESTAMP": "2000000"}]
        self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_current_invocation_fault_is_not_hidden_by_reused_old_pid(self):
        s = sample(); s["journal"] = [{"MESSAGE": "cannot allocate memory in static TLS block",
            "_PID": "10", "_SYSTEMD_USER_INVOCATION_ID": "b" * 32,
            "__MONOTONIC_TIMESTAMP": "2000000"}]
        result = M.StartupGate(baseline()).consume(s, stable_seconds=0)
        self.assertEqual(result["status"], "fault")
        self.assertIn("static TLS", result["reason"])

    def test_stale_rotated_native_readiness_and_error_lines_are_not_admitted(self):
        s = sample()
        s["logs"] = {key: "\n".join(line(row.split("[Info] - ", 1)[1], wall=99) for row in value.splitlines()) for key, value in s["logs"].items()}
        s["logs"]["vrserver"] += "\n" + line("error while loading shared libraries", wall=99)
        result = M.StartupGate(baseline()).consume(s, stable_seconds=0)
        self.assertFalse(result["ready"]); self.assertEqual(result["status"], "not-ready")

    def test_each_required_process_or_marker_or_mapping_is_needed(self):
        for index in range(4):
            s = sample(); s["processes"].pop(index)
            with self.subTest(missing_process=index): self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])
        for index in range(1, 4):
            s = sample(); s["processes"][index]["libcef"] = False
            with self.subTest(missing_libcef=index): self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])
        for marker in ("Done initializing CGraphicsDevice resources", "Compositor render thread started", "system.systemui", "New Connect"):
            s = sample(); s["logs"] = {k: "\n".join(row for row in v.splitlines() if marker not in row) for k, v in s["logs"].items()}
            with self.subTest(missing_marker=marker): self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_unreadable_required_process_never_passes(self):
        s = sample(); s["errors"] = [{"pid": 102, "reason": "PermissionError"}]
        self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_premature_exit_identity_replacement_or_service_restart_fails(self):
        for variant in ("exit", "replace", "invocation", "old_invocation", "restarts", "main", "failed", "zombie", "main_exit", "main_replace"):
            gate = M.StartupGate(baseline()); gate.consume(sample(), stable_seconds=0)
            s = sample(4)
            if variant == "exit": s["processes"].pop(1)
            if variant == "replace": s["processes"][1]["start_ticks"] += 1
            if variant == "invocation": s["unit"]["InvocationID"] = "c" * 32
            if variant == "old_invocation": s["unit"]["InvocationID"] = "a" * 32
            if variant == "restarts": s["unit"]["NRestarts"] = "1"
            if variant == "main": s["unit"]["MainPID"] = "200"
            if variant == "failed": s["unit"]["ActiveState"] = "failed"
            if variant == "zombie": s["processes"][1]["state"] = "Z"
            if variant == "main_exit": s["main_process"] = None
            if variant == "main_replace": s["main_process"]["start_ticks"] += 1
            with self.subTest(variant=variant): self.assertEqual(gate.consume(s)["status"], "fault")

    def test_missing_libcef_after_ready_is_not_ready(self):
        gate = M.StartupGate(baseline()); gate.consume(sample(), stable_seconds=0)
        s = sample(4); s["processes"][1]["libcef"] = False
        result = gate.consume(s)
        self.assertFalse(result["ready"]); self.assertEqual(result["status"], "fault")
        self.assertEqual(gate.consume(sample(5))["status"], "fault")

    def test_stopped_or_dead_required_process_is_never_ready(self):
        for state in "TtXxZ":
            for index in range(5):
                s = sample()
                target = s["main_process"] if index == 4 else s["processes"][index]
                target["state"] = state
                with self.subTest(state=state, initial_role=index):
                    self.assertFalse(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])
                gate = M.StartupGate(baseline()); gate.consume(sample(), stable_seconds=0)
                s["monotonic"] = 4
                with self.subTest(state=state, monitored_role=index):
                    self.assertEqual(gate.consume(s)["status"], "fault")

    def test_baseline_managed_read_gap_is_retained_and_cannot_prove_ready(self):
        b = baseline(); b["errors"] = [{"pid": 10, "reason": "PermissionError"}]
        gate = M.StartupGate(b)
        self.assertEqual(gate.checkpoint()["baseline"]["errors"], b["errors"])
        result = gate.consume(sample(), stable_seconds=0)
        self.assertEqual(result["status"], "fault"); self.assertIn("Baseline", result["reason"])

    def test_same_pid_old_start_identity_cannot_satisfy_new_readiness(self):
        b = baseline(); b["processes"] = copy.deepcopy(sample()["processes"])
        self.assertFalse(M.StartupGate(b).consume(sample(), stable_seconds=0)["ready"])

    def test_wait_is_bounded_and_requires_progress(self):
        s = sample(); s["logs"] = {}; source = Source([s])
        result = M.wait_ready(source, baseline(), timeout=2, no_progress_seconds=1, clock=source.clock, sleep=source.sleep)
        self.assertEqual(result["status"], "not-ready"); self.assertIn("stopped advancing", result["reason"])
        self.assertLessEqual(source.calls, 6)

    def test_missing_or_unreadable_evidence_times_out_without_restart(self):
        source = Source([PermissionError("managed metadata denied")])
        result = M.wait_ready(source, baseline(), timeout=1, no_progress_seconds=1, clock=source.clock, sleep=source.sleep)
        self.assertFalse(result["ready"]); self.assertLessEqual(source.calls, 5)

    def test_wait_and_resumable_monitor_detect_ui_loss(self):
        source = Source([sample()]); result = M.wait_ready(source, baseline(), timeout=3, no_progress_seconds=3,
            clock=source.clock, sleep=source.sleep)
        self.assertTrue(result["ready"])
        gate = M.StartupGate.resume(json.loads(json.dumps(result["gate_checkpoint"])))
        s = sample(); s["processes"].pop(1); source.samples = [s]
        result = M.monitor(source, gate, seconds=1, clock=source.clock, sleep=source.sleep)
        self.assertEqual(result["status"], "fault"); self.assertEqual(result["monitor_samples"], 1)
        self.assertFalse(result["automatic_restart"])

    def test_monitor_unreadable_evidence_is_sticky_without_restarting(self):
        gate = M.StartupGate(baseline()); gate.consume(sample(), stable_seconds=0)
        source = Source([PermissionError("UI process unreadable")])
        result = M.monitor(source, gate, seconds=1, clock=source.clock, sleep=source.sleep)
        self.assertFalse(result["ready"]); self.assertEqual(result["monitor_samples"], 1)
        self.assertEqual(gate.consume(sample(5))["status"], "fault")

    def test_poll_limits_and_monotonic_regression_fail_closed(self):
        for key, value in (("timeout", 31), ("poll_seconds", 0), ("stable_seconds", 4), ("no_progress_seconds", 21)):
            with self.subTest(key=key), self.assertRaises(RuntimeError): M.wait_ready(Source([sample()]), baseline(), **{key: value})
        gate = M.StartupGate(baseline()); self.assertEqual(gate.consume(sample(0.5))["status"], "fault")

    def test_critical_current_renderer_failure_detected(self):
        for message in ("vkCreateQueryPool invalid device", "VK_ERROR_DEVICE_LOST", "renderer process crashed", "FATAL graphics initialization failed", "symbol lookup error"):
            s = sample(); s["journal"] = [{"MESSAGE": message, "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}]
            with self.subTest(message=message): self.assertEqual(M.StartupGate(baseline()).consume(s)["status"], "fault")


class NativeSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="compositor-health-fixture-"); self.root = Path(self.temp.name)
        self.home = self.root / "home"; self.home.mkdir(); self.proc = self.root / "proc"; self.proc.mkdir()
        self.now = 1.0; self.commands = []; self.journal_rows = []
        self.source = M.NativeEvidenceSource(self.home, proc_root=self.proc, run=self.command,
            clock=lambda: self.now, wall_clock=lambda: 100.0)
        self.logdir = self.home / ".local/share/Steam/logs"; self.logdir.mkdir(parents=True)
        for name in self.source.logs: (self.logdir / (name + ".txt")).write_text("old line\n")
    def tearDown(self): self.temp.cleanup()
    def command(self, args, **kwargs):
        self.commands.append(args)
        if args[0] == "systemctl": raw = "\n".join(f"{k}={v}" for k, v in unit().items()) + "\n"
        else:
            rows = self.journal_rows if any(arg.startswith("--after-cursor") for arg in args) else []
            raw = "\n".join(json.dumps(row) for row in rows) + "\n-- cursor: s=current\n"
        return subprocess.CompletedProcess(args, 0, raw, "")
    def procfile(self, pid, kind="browser", *, group=None, libcef=True):
        p = self.proc / str(pid); p.mkdir()
        exe = M.COMPOSITOR if kind == "compositor" else M.WEBHELPER
        (p / "comm").write_text(Path(exe).name + "\n")
        (p / "cgroup").write_text("0::" + (group or unit()["ControlGroup"]) + "\n")
        cols = ["S"] + ["0"] * 18 + [str(pid * 100)]
        (p / "stat").write_text(str(pid) + " (vrwebhelper) " + " ".join(cols))
        (p / "exe").symlink_to(exe)
        (p / "cmdline").write_bytes((exe + ("\0--type=" + kind if kind not in ("browser", "compositor") else "") + "\0").encode())
        (p / "maps").write_text("1000-2000 r-xp 00000000 00:00 0 " + (M.LIBCEF if libcef else "/not-libcef.so") + "\n")
        return p
    def test_log_offsets_read_only_new_bytes_and_rotation_uses_current_file(self):
        self.source.baseline()
        with (self.logdir / "vrserver.txt").open("a") as stream: stream.write("fresh line\n")
        self.assertEqual(self.source.sample()["logs"]["vrserver"], "fresh line")
        (self.logdir / "vrserver.txt").rename(self.logdir / "vrserver.previous.txt")
        (self.logdir / "vrserver.txt").write_text("new invocation\n")
        self.assertEqual(self.source.sample()["logs"]["vrserver"], "new invocation")
        self.assertNotIn("previous", str(self.commands))
    def test_partial_lines_are_only_emitted_once_complete(self):
        self.source.baseline()
        path = self.logdir / "vrserver.txt"
        with path.open("a") as stream: stream.write("part")
        self.assertEqual(self.source.sample()["logs"]["vrserver"], "")
        with path.open("a") as stream: stream.write("ial\n")
        self.assertEqual(self.source.sample()["logs"]["vrserver"], "partial")
    def test_proc_reads_only_managed_required_executable_metadata(self):
        self.procfile(101, "compositor"); self.procfile(102); self.procfile(103, "renderer")
        self.procfile(200, group="/not/steamvr.service")
        p = self.proc / "1"; p.mkdir(); (p / "comm").write_text("systemd\n")
        processes, errors = self.source.processes(unit())
        self.assertEqual({p["pid"] for p in processes}, {101, 102, 103}); self.assertFalse(errors)
        self.assertTrue(next(p for p in processes if p["pid"] == 103)["libcef"])
    def test_exact_resolved_native_paths_are_normalized_without_filename_guessing(self):
        p = self.procfile(102, "renderer")
        p.joinpath("exe").unlink(); p.joinpath("exe").symlink_to("/resolved/steamvr/vrwebhelper")
        p.joinpath("maps").write_text("1000-2000 r-xp 00000000 00:00 0 /resolved/steamvr/libcef.so\n")
        self.source.native_paths[M.WEBHELPER] = "/resolved/steamvr/vrwebhelper"
        self.source.native_paths[M.LIBCEF] = "/resolved/steamvr/libcef.so"
        processes, errors = self.source.processes(unit())
        self.assertFalse(errors); self.assertEqual(processes[0]["exe"], M.WEBHELPER); self.assertTrue(processes[0]["libcef"])
        p.joinpath("exe").unlink(); p.joinpath("exe").symlink_to("/unrelated/vrwebhelper")
        self.assertFalse(self.source.processes(unit())[0])

    def test_cgroup_is_checked_before_unrelated_comm_and_read_errors(self):
        unrelated = self.procfile(200, group="/not/steamvr.service")
        managed = self.procfile(102)
        original = self.source.proc_read
        seen = []
        def selective(path, limit):
            seen.append(path)
            if path in (unrelated / "comm", managed / "comm"): raise PermissionError("comm denied")
            return original(path, limit)
        with mock.patch.object(self.source, "proc_read", side_effect=selective):
            processes, errors = self.source.processes(unit())
        self.assertFalse(processes)
        self.assertNotIn(unrelated / "comm", seen)
        self.assertEqual(errors, [{"pid": 102, "reason": "PermissionError"}])
        with mock.patch.object(self.source, "proc_read", side_effect=selective):
            b = self.source.baseline()
        self.assertTrue(any(item["pid"] == 102 for item in b["errors"]))

    def test_scope_unknown_unrelated_read_error_is_not_managed(self):
        p = self.procfile(200, group="/not/steamvr.service")
        original = self.source.proc_read
        def deny(path, limit):
            if path == p / "cgroup": raise PermissionError("group denied")
            return original(path, limit)
        with mock.patch.object(self.source, "proc_read", side_effect=deny):
            processes, errors = self.source.processes(unit())
        self.assertFalse(processes); self.assertFalse(errors)
        u = unit(); u["MainPID"] = "200"
        with mock.patch.object(self.source, "proc_read", side_effect=deny):
            _, errors = self.source.processes(u)
        self.assertEqual(errors, [{"pid": 200, "reason": "PermissionError"}])

    def test_inactive_empty_cgroup_does_not_scan_all_user_processes(self):
        self.procfile(102)
        u = unit(); u.update(ControlGroup="", ActiveState="inactive", MainPID="0")
        with mock.patch.object(self.source, "proc_read", side_effect=AssertionError("unexpected proc read")):
            self.assertEqual(self.source.processes(u), ([], []))

    def test_unreadable_managed_required_process_is_explicit(self):
        p = self.procfile(102)
        original = self.source.proc_read
        def deny(path, limit):
            if path == p / "maps": raise PermissionError("maps denied")
            return original(path, limit)
        with mock.patch.object(self.source, "proc_read", side_effect=deny): processes, errors = self.source.processes(unit())
        self.assertFalse(processes); self.assertEqual(errors, [{"pid": 102, "reason": "PermissionError"}])
    def test_symlink_log_and_oversize_delta_refused(self):
        self.source.baseline(); p = self.logdir / "vrserver.txt"; p.unlink(); p.symlink_to(self.logdir / "vrcompositor.txt")
        with self.assertRaises((OSError, RuntimeError)): self.source.sample()
        p.unlink(); p.write_text("x" * (128 * 1024 + 1))
        with self.assertRaises(RuntimeError): self.source.sample()
    def test_journal_requires_cursor_and_bounded_delta(self):
        with mock.patch.object(self.source, "command", return_value=""):
            with self.assertRaises(RuntimeError): self.source.baseline()
        self.source.baseline(); self.journal_rows = [{"MESSAGE": "x", "__CURSOR": "s=x"}] * 512
        with self.assertRaises(RuntimeError): self.source.sample()

    def test_json_byte_arrays_keep_current_invocation_and_budget_checks(self):
        self.source.baseline()
        self.journal_rows = [
            {"MESSAGE": list(b"\x1b[93mcannot allocate memory in static TLS block\x1b[m"),
             "_PID": "10", "_SYSTEMD_USER_INVOCATION_ID": "a" * 32, "__MONOTONIC_TIMESTAMP": "2000000"},
            {"MESSAGE": list(b"\x1b[0;93m[W:onnxruntime:] GPU device discovery failed\x1b[m"),
             "_PID": "102", "_SYSTEMD_USER_INVOCATION_ID": "b" * 32, "__MONOTONIC_TIMESTAMP": "2000000"}]
        s = sample(); s["journal"] = self.source.journal()
        self.assertEqual(s["journal"], self.journal_rows)
        self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])
        self.journal_rows[1]["MESSAGE"] = list(b"\x1b[93mVK_ERROR_DEVICE_LOST\x1b[m")
        s["journal"] = self.source.journal()
        self.assertEqual(M.StartupGate(baseline()).consume(s)["status"], "fault")
        self.source.total = M.LIMIT
        with self.assertRaises(RuntimeError):
            self.source.journal()
    def test_empty_journal_delta_retains_actual_cursor(self):
        self.source.baseline()
        previous = self.source.cursor
        self.assertEqual(self.source.journal(), [])
        self.assertEqual(self.source.cursor, previous)
        # No invented success for an absent cursor, even when stdout is empty.
        with mock.patch.object(self.source, "command", return_value=""):
            with self.assertRaises(RuntimeError): self.source.journal()

    def test_checkpoint_resume_preserves_cumulative_evidence_budget(self):
        self.source.baseline()
        self.source.total = M.LIMIT - 1
        checkpoint = copy.deepcopy(self.source.checkpoint())
        resumed = M.NativeEvidenceSource(self.home, proc_root=self.proc, run=self.command)
        resumed.resume(checkpoint)
        self.assertEqual(resumed.total, M.LIMIT - 1)
        with self.assertRaises(RuntimeError): resumed.journal()
        for invalid in (-1, M.LIMIT + 1, True):
            checkpoint["total"] = invalid
            with self.subTest(total=invalid), self.assertRaises(RuntimeError): resumed.resume(checkpoint)
        checkpoint.pop("total")
        with self.assertRaises(RuntimeError): resumed.resume(checkpoint)

    def test_resume_rejects_malformed_offsets(self):
        self.source.baseline(); checkpoint = self.source.checkpoint()
        checkpoint["log_offsets"]["vrserver"]["offset"] = -1
        with self.assertRaises(RuntimeError): self.source.resume(checkpoint)

class IsolationGateTests(unittest.TestCase):
    def setUp(self):
        self.b = baseline(); self.b["isolation_descriptor"] = descriptor()
        self.s = sample(); self.s["isolation"] = isolated_report()

    def test_reviewed_isolation_is_required_in_addition_to_actual_ui_health(self):
        self.assertFalse(M.StartupGate(self.b).consume(sample(), stable_seconds=0)["ready"])
        result = M.StartupGate(self.b).consume(self.s, stable_seconds=0)
        self.assertTrue(result["ready"]); self.assertTrue(result["isolation_verified"])
        self.assertFalse(result["screen_pixels_verified"])
        self.assertEqual(result["isolation"]["target_env_view"], "initial-exec-only")
        self.assertFalse(result["isolation"]["runtime_environ_verified"])
        self.s["processes"].pop(1)
        self.assertFalse(M.StartupGate(self.b).consume(self.s, stable_seconds=0)["ready"])

    def test_each_non_target_mapping_or_initial_flag_contaminates_and_fault_is_sticky(self):
        for key in ("payload_mapped", "audit_mapped", "loader_payload", "loader_audit", "diagnostic_keys"):
            s = copy.deepcopy(self.s)
            s["isolation"]["processes"][1][key] = ["FRAME_MASK_TRACE"] if key == "diagnostic_keys" else True
            rejudge(s["isolation"])
            gate = M.StartupGate(self.b)
            with self.subTest(key=key):
                result = gate.consume(s, stable_seconds=0)
                self.assertEqual(result["status"], "fault"); self.assertFalse(result["isolation_verified"])
                self.assertEqual(gate.consume(self.s)["status"], "fault")

    def test_unknown_unreadable_changing_and_possible_fork_inventory_never_proves_isolation(self):
        for variant in ("unreadable", "inventory", "unit", "duplicate_target", "target_mapping", "target_state"):
            s = copy.deepcopy(self.s); report = s["isolation"]
            if variant == "unreadable": report["errors"] = [{"pid": 200, "reason": "PermissionError"}]
            if variant == "inventory": report["inventory_stable"] = False
            if variant == "unit": report["unit_stable"] = False
            if variant == "duplicate_target":
                child = copy.deepcopy(report["processes"][0]); child.update(pid=200, start_ticks=20000, ppid=101)
                report["processes"].append(child); report["metadata_usage"]["processes"] += 1
            if variant == "target_mapping": report["processes"][0]["audit_mapped"] = False
            if variant == "target_state": report["processes"][0]["state"] = "Z"
            rejudge(report)
            with self.subTest(variant=variant):
                result = M.StartupGate(self.b).consume(s, stable_seconds=0)
                self.assertFalse(result["ready"]); self.assertEqual(result["status"], "not-ready")

    def test_target_initial_input_mismatch_and_deleted_mapping_refuse(self):
        for key in ("target_initial_environment_matches", "owned_mapping_deleted"):
            s = copy.deepcopy(self.s)
            s["isolation"]["processes"][0][key] = key == "owned_mapping_deleted"
            rejudge(s["isolation"])
            with self.subTest(key=key): self.assertEqual(M.StartupGate(self.b).consume(s)["status"], "fault")

    def test_ready_monitor_cannot_reuse_missing_isolation_or_reset_budget(self):
        for variant in ("missing", "unreadable", "budget", "cgroup"):
            gate = M.StartupGate(self.b); self.assertTrue(gate.consume(self.s, stable_seconds=0)["ready"])
            s = copy.deepcopy(self.s); s["monotonic"] = 4
            if variant == "missing": s.pop("isolation")
            if variant == "unreadable":
                s["isolation"]["errors"] = [{"pid": 200, "reason": "PermissionError"}]; rejudge(s["isolation"])
            if variant == "budget": s["isolation"]["metadata_usage"]["metadata_bytes"] -= 1
            if variant == "cgroup":
                s["unit"]["ControlGroup"] = "/changed/steamvr.service"
                s["isolation"]["control_group"] = s["unit"]["ControlGroup"]
            with self.subTest(variant=variant):
                result = gate.consume(s)
                self.assertEqual(result["status"], "fault"); self.assertFalse(result["isolation_verified"])

    def test_exact_isolated_target_must_be_the_actual_ui_compositor(self):
        report = self.s["isolation"]
        report["processes"][0]["pid"] += 10
        self.assertEqual(M.StartupGate(self.b).consume(self.s)["status"], "fault")

    def test_reports_do_not_self_attest_isolation_or_runtime_environment(self):
        for variant in ("verdict", "descriptor", "runtime", "invocation", "unknown", "duplicate", "zero_budget"):
            s = copy.deepcopy(self.s); report = s["isolation"]
            if variant == "verdict": report["processes"][1]["payload_mapped"] = True
            if variant == "descriptor": report["descriptor"]["payload_path"] = "/unreviewed/observer.so"
            if variant == "runtime": report["runtime_environ_verified"] = True
            if variant == "invocation": report["invocation"] = "c" * 32
            if variant == "unknown": report["extra"] = True
            if variant == "duplicate": report["processes"].append(copy.deepcopy(report["processes"][0]))
            if variant == "zero_budget": report["metadata_usage"]["samples"] = 0
            with self.subTest(variant=variant): self.assertEqual(M.StartupGate(self.b).consume(s)["status"], "fault")

    def test_checkpoint_retains_required_isolation_and_rejects_invented_proof(self):
        gate = M.StartupGate(self.b); gate.consume(self.s, stable_seconds=0)
        saved = json.loads(json.dumps(gate.checkpoint()))
        self.assertTrue(M.StartupGate.resume(saved).consume(self.s, stable_seconds=0)["ready"])
        for variant in ("missing", "false", "unscoped", "bad_budget", "target_identity"):
            broken = copy.deepcopy(saved)
            if variant == "missing": broken["isolation"] = None
            if variant == "false": broken["isolation"]["processes"][1]["payload_mapped"] = True
            if variant == "unscoped": broken["baseline"].pop("isolation_descriptor")
            if variant == "bad_budget": broken["isolation"]["metadata_usage"]["samples"] = M.ISOLATION_SAMPLE_LIMIT + 1
            if variant == "target_identity": broken["isolation"]["processes"][0]["start_ticks"] += 1
            with self.subTest(variant=variant), self.assertRaises(RuntimeError): M.StartupGate.resume(broken)


class IsolationSourceTests(unittest.TestCase):
    command = NativeSourceTests.command
    procfile = NativeSourceTests.procfile
    def setUp(self):
        NativeSourceTests.setUp(self)
        self.config = descriptor()
        self.cgroot = self.root / "cgroups"; self.cgroot.mkdir()
        self.group = self.cgroot / unit()["ControlGroup"].lstrip("/")
        self.group.mkdir(parents=True); (self.group / "cgroup.procs").write_text("100\n101\n102\n")
        self.source.isolation_descriptor = M.require_isolation_descriptor(self.config)
        self.source.cgroup_root = self.cgroot
        self.managed(100, exe="/usr/bin/launcher")
        self.managed(101, exe=M.COMPOSITOR, env=self.config["target_exec_environment"], full=True)
        self.managed(102, exe="/usr/bin/unrecognized-service-helper")
    def tearDown(self): NativeSourceTests.tearDown(self)
    def managed(self, pid, *, exe, env=None, full=False, group=None):
        p = self.procfile(pid, "compositor" if exe == M.COMPOSITOR else "browser", group=group)
        (p / "exe").unlink(); (p / "exe").symlink_to(exe)
        cols = ["S", "100" if pid != 100 else "1"] + ["0"] * 17 + [str(pid * 100)]
        (p / "stat").write_text(str(pid) + " (helper) " + " ".join(cols))
        env = env if env is not None else {"LD_PRELOAD": self.config["selector_path"], "PRIVATE_USER_VALUE": "never-log-this"}
        (p / "environ").write_bytes(b"".join(key.encode() + b"=" + value.encode() + b"\0" for key, value in env.items()))
        paths = [self.config["selector_path"]]
        if full: paths += [self.config["payload_path"], self.config["audit_path"]]
        (p / "maps").write_text("\n".join("1000-2000 r-xp 00000000 00:00 0 " + path for path in paths) + "\n")
        return p
    def report(self): return self.source.isolation(unit())

    def test_every_managed_executable_is_checked_with_only_sanitized_evidence(self):
        report = self.report(); M.require_isolation_report(report, self.config, unit())
        self.assertEqual(report["status"], "isolated")
        self.assertEqual({row["pid"] for row in report["processes"]}, {100, 101, 102})
        self.assertNotIn("never-log-this", json.dumps(report))
        self.assertFalse(report["runtime_environ_verified"])
        self.assertEqual(report["target_env_view"], "initial-exec-only")
        self.assertGreater(report["metadata_usage"]["metadata_bytes"], 0)

    def test_unknown_helper_environment_and_mappings_cannot_be_skipped_by_name(self):
        p = self.proc / "102"
        for case in ("preload", "audit", "flag", "payload_map", "audit_map", "deleted_map", "prefix"):
            original_env, original_maps = (p / "environ").read_bytes(), (p / "maps").read_bytes()
            if case == "preload": (p / "environ").write_bytes(b"LD_PRELOAD=/foreign.so:" + self.config["payload_path"].encode() + b"\0")
            if case == "audit": (p / "environ").write_bytes(b"LD_AUDIT=" + self.config["audit_path"].encode() + b"\0")
            if case == "flag": (p / "environ").write_bytes(b"FRAME_MASK_TRACE=0\0")
            if case == "prefix": (p / "environ").write_bytes(b"GHVR_FRAME_COMPOSITOR_RAW_UNREVIEWED=secret\0")
            if case.endswith("map"):
                key = "audit_path" if case == "audit_map" else "payload_path"
                (p / "maps").write_text("1000-2000 r-xp 00000000 00:00 0 " + self.config[key] + (" (deleted)" if case == "deleted_map" else "") + "\n")
            with self.subTest(case=case): self.assertEqual(self.report()["status"], "contaminated")
            (p / "environ").write_bytes(original_env); (p / "maps").write_bytes(original_maps)

    def test_descriptor_matches_exact_paths_and_target_initial_seven_values(self):
        target = self.proc / "101"
        original = (target / "environ").read_bytes()
        (target / "environ").write_bytes(original + b"FRAME_MASK_TRACE_DISABLE=1\0")
        self.assertEqual(self.report()["status"], "contaminated")
        (target / "environ").write_bytes(original.replace(b"FRAME_MASK_TRACE=1", b"FRAME_MASK_TRACE=0"))
        self.assertEqual(self.report()["status"], "contaminated")
        (target / "environ").write_bytes(original)
        p = self.proc / "102"
        (p / "environ").write_bytes(b"LD_PRELOAD=/unrelated/observer.so /unrelated/audit.so\0")
        (p / "maps").write_text("1000-2000 r-xp 00000000 00:00 0 /unrelated/observer.so\n")
        self.assertEqual(self.report()["status"], "isolated")

    def test_managed_descendants_are_included_and_unrelated_pid_metadata_is_not_read(self):
        child = self.group / "nested"; child.mkdir(); (child / "cgroup.procs").write_text("200\n")
        self.managed(200, exe="/usr/bin/nested-helper", group=unit()["ControlGroup"] + "/nested")
        unrelated = self.managed(201, exe="/unrelated/helper", group="/other.service")
        (unrelated / "environ").unlink()
        result = self.report()
        self.assertEqual(result["status"], "isolated")
        self.assertEqual({row["pid"] for row in result["processes"]}, {100, 101, 102, 200})

    def test_unknown_unreadable_managed_members_are_non_proof_never_skipped(self):
        for file in ("environ", "maps", "exe", "cgroup", "stat"):
            path = self.proc / "102" / file; rename = path.with_name(file + ".held")
            path.rename(rename)
            result = self.report()
            with self.subTest(file=file):
                self.assertEqual(result["status"], "not-isolated")
                self.assertTrue(any(error["pid"] == 102 for error in result["errors"]))
            rename.rename(path)
        (self.group / "cgroup.procs").write_text("100\n101\n102\n200\n")
        self.assertEqual(self.report()["status"], "not-isolated")

    def test_permission_denial_of_any_managed_helper_is_explicit_non_proof(self):
        original = self.source.isolation_read
        def deny(path, limit):
            if path == self.proc / "102" / "maps": raise PermissionError("managed mapping denied")
            return original(path, limit)
        with mock.patch.object(self.source, "isolation_read", side_effect=deny): result = self.report()
        self.assertEqual(result["status"], "not-isolated")
        self.assertIn({"pid": 102, "reason": "PermissionError"}, result["errors"])

    @unittest.skipUnless(sys.platform == "linux", "Linux proc initial environment contract")
    def test_real_proc_environment_is_initial_exec_input_not_late_runtime_getenv(self):
        # The actual kernel interface retains the initial range after unsetenv.
        # Reading it cannot certify that a compositor's late getenv is frozen.
        code = ("import os\nfrom pathlib import Path\nimport sys\n"
                "os.environ.pop('FRAME_MASK_TRACE')\nos.environ.pop('GHVR_FRAME_COMPOSITOR_RAW_OUTPUT')\n"
                "assert os.getenv('FRAME_MASK_TRACE') is None\n"
                "sys.stdout.buffer.write(Path('/proc/self/environ').read_bytes())\n")
        result = subprocess.run([sys.executable, "-c", code], env={"FRAME_MASK_TRACE": "1",
            "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT": "/reviewed/raw.jsonl"}, check=True, capture_output=True, timeout=5)
        evidence = self.source.isolation_environment(result.stdout, target=False)
        self.assertEqual(set(evidence["diagnostic_keys"]), {"FRAME_MASK_TRACE", "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"})
        self.assertIsNone(evidence["target_initial_environment_matches"])

    def test_uid_pid_start_exe_cgroup_and_inventory_are_bracketed(self):
        for variant in ("uid", "start", "exe", "group", "inventory", "unit"):
            with self.subTest(variant=variant):
                original = self.source.isolation_read
                changed = False
                def mutate(path, limit):
                    nonlocal changed
                    data = original(path, limit)
                    if path == self.proc / "102" / "maps" and not changed:
                        changed = True
                        if variant == "start":
                            p = self.proc / "102" / "stat"; p.write_text(p.read_text().replace("10200", "10201"))
                        if variant == "exe":
                            p = self.proc / "102" / "exe"; p.unlink(); p.symlink_to("/changed/helper")
                        if variant == "group": (self.proc / "102" / "cgroup").write_text("0::/other.service\n")
                        if variant == "inventory": (self.group / "cgroup.procs").write_text("100\n101\n")
                    return data
                if variant == "uid":
                    self.source.uid = os.getuid() + 1
                    result = self.report(); self.source.uid = os.getuid()
                elif variant == "unit":
                    with mock.patch.object(self.source, "unit", return_value=unit("c" * 32)): result = self.report()
                else:
                    with mock.patch.object(self.source, "isolation_read", side_effect=mutate): result = self.report()
                self.assertEqual(result["status"], "not-isolated")
                if variant in ("start", "exe", "group"):
                    for p in (self.proc / "102",):
                        for child in p.iterdir(): child.unlink()
                        p.rmdir()
                    self.managed(102, exe="/usr/bin/unrecognized-service-helper")
                (self.group / "cgroup.procs").write_text("100\n101\n102\n")

    def test_non_exec_fork_copies_are_ambiguous_and_not_called_unloaded(self):
        self.managed(200, exe=M.COMPOSITOR, env=self.config["target_exec_environment"], full=True)
        (self.group / "cgroup.procs").write_text("100\n101\n102\n200\n")
        result = self.report()
        self.assertEqual(result["status"], "not-isolated"); self.assertIn("fork", result["reason"])
        (self.proc / "200" / "exe").unlink(); (self.proc / "200" / "exe").symlink_to("/usr/bin/forked-shell")
        self.assertEqual(self.report()["status"], "contaminated")

    def test_source_checkpoint_preserves_budget_descriptor_and_never_resets_on_resume(self):
        self.source.baseline(); self.report()
        saved = json.loads(json.dumps(self.source.checkpoint()))
        resumed = M.NativeEvidenceSource(self.home, proc_root=self.proc, run=self.command, isolation=self.config, cgroup_root=self.cgroot)
        resumed.resume(saved)
        self.assertEqual(resumed.isolation_usage, saved["isolation_usage"])
        resumed.isolation(unit())
        self.assertGreater(resumed.isolation_usage["samples"], saved["isolation_usage"]["samples"])
        self.assertGreater(resumed.isolation_usage["metadata_bytes"], saved["isolation_usage"]["metadata_bytes"])
        for key, value in (("samples", M.ISOLATION_SAMPLE_LIMIT + 1), ("processes", True), ("metadata_bytes", -1)):
            bad = copy.deepcopy(saved); bad["isolation_usage"][key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError): resumed.resume(bad)
        bad = copy.deepcopy(saved); bad["isolation_descriptor"]["audit_path"] = "/unreviewed/audit.so"
        with self.assertRaises(RuntimeError): resumed.resume(bad)

    def test_cumulative_limits_and_per_process_extents_fail_without_metadata_leak(self):
        self.source.isolation_usage["samples"] = M.ISOLATION_SAMPLE_LIMIT
        with self.assertRaises(RuntimeError): self.report()
        self.source.isolation_usage["samples"] = 0
        self.source.isolation_usage["processes"] = M.ISOLATION_PROCESS_LIMIT
        self.assertEqual(self.report()["status"], "not-isolated")
        self.source.isolation_usage = {"samples": 0, "processes": 0, "metadata_bytes": M.ISOLATION_BYTE_LIMIT - 1}
        self.assertEqual(self.report()["status"], "not-isolated")
        self.assertEqual(self.source.isolation_usage["metadata_bytes"], M.ISOLATION_BYTE_LIMIT)
        self.source.isolation_usage = {"samples": 0, "processes": 0, "metadata_bytes": 0}
        (self.proc / "102" / "environ").write_bytes(b"PRIVATE=secret" + b"x" * M.ISOLATION_ENV_LIMIT + b"\0")
        report = self.report(); self.assertEqual(report["status"], "not-isolated")
        self.assertNotIn("secret", json.dumps(report))
        (self.proc / "102" / "environ").write_bytes(b"LD_PRELOAD=" + self.config["selector_path"].encode() + b"\0")
        (self.proc / "102" / "maps").write_bytes(b"x" * (M.ISOLATION_MAP_LIMIT + 1))
        self.assertEqual(self.report()["status"], "not-isolated")

    def test_cgroup_inventory_bounds_prevent_unbounded_process_or_directory_scan(self):
        (self.group / "cgroup.procs").write_text("\n".join(str(pid) for pid in range(1, M.MAX_PROCESSES + 2)) + "\n")
        result = self.report(); self.assertEqual(result["status"], "not-isolated")
        self.assertFalse(result["processes"])
        (self.group / "cgroup.procs").write_text("100\n101\n102\n")
        for index in range(M.ISOLATION_GROUP_LIMIT):
            child = self.group / str(index); child.mkdir(); (child / "cgroup.procs").write_text("")
        result = self.report(); self.assertEqual(result["status"], "not-isolated")
        self.assertFalse(result["processes"])

    def test_unsafe_descriptors_and_scope_paths_are_refused_without_guessing(self):
        for variant in ("path", "relative", "aliases", "keys", "loader", "flag", "foreign"):
            config = descriptor()
            if variant == "path": config["selector_path"] = "/reviewed/../selector.so"
            if variant == "relative": config["target_exe"] = "vrcompositor"
            if variant == "aliases": config["audit_path"] = config["payload_path"]
            if variant == "keys": config["target_exec_environment"].pop("LD_AUDIT")
            if variant == "loader": config["target_exec_environment"]["LD_AUDIT"] = "/unrelated/audit.so"
            if variant == "flag": config["target_exec_environment"]["FRAME_MASK_TRACE"] = "0"
            if variant == "foreign": config["extra"] = "unreviewed"
            with self.subTest(variant=variant), self.assertRaises(RuntimeError): M.require_isolation_descriptor(config)
        u = unit(); u["ControlGroup"] = "/user.slice/../steamvr.service"
        self.assertEqual(self.source.isolation(u)["status"], "not-isolated")
        original = self.group / "cgroup.procs"; original.unlink(); original.symlink_to(self.proc / "102" / "environ")
        self.assertEqual(self.report()["status"], "not-isolated")


if __name__ == "__main__": unittest.main()
