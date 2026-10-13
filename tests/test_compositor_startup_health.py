"""Injected startup evidence and local proc/log fixtures; no Frame access."""
import copy
import datetime
import importlib.util
import json
import os
from pathlib import Path
import subprocess
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

    def test_old_invocation_pid_or_pre_baseline_journal_errors_are_not_current(self):
        for updates in ({"_SYSTEMD_INVOCATION_ID": "a" * 32}, {"_PID": "10"}, {"__MONOTONIC_TIMESTAMP": "999999"}):
            s = sample(); row = {"MESSAGE": "segmentation fault core dumped", "_PID": "102", "__MONOTONIC_TIMESTAMP": "2000000"}; row.update(updates)
            s["journal"] = [row]
            with self.subTest(updates=updates): self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

    def test_old_core_exit_report_from_manager_is_not_new_ui_crash(self):
        s = sample(); s["journal"] = [{"MESSAGE": "Process 10 (vrcompositor) core dumped", "_PID": "1592", "__MONOTONIC_TIMESTAMP": "2000000"}]
        self.assertTrue(M.StartupGate(baseline()).consume(s, stable_seconds=0)["ready"])

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
    def test_resume_rejects_malformed_offsets(self):
        self.source.baseline(); checkpoint = self.source.checkpoint()
        checkpoint["log_offsets"]["vrserver"]["offset"] = -1
        with self.assertRaises(RuntimeError): self.source.resume(checkpoint)

if __name__ == "__main__": unittest.main()
