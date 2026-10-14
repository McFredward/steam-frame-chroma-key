"""Local, mocked service/session controls; no Frame access or native reads.

Only this fixture replaces native/profile pins and the blocked legacy activation
guard. Production has no bypass flag; its guard is independently checked below.
"""
import copy
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("compositor_raw_session", Path(__file__).resolve().parents[1] / "tools/compositor_raw_session.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)

class ProductionLaunchGuardTests(unittest.TestCase):
    def test_known_unsafe_start_or_activation_cannot_write_or_restart(self):
        with tempfile.TemporaryDirectory(prefix="raw-launch-guard-") as folder:
            root = Path(folder); run = mock.Mock()
            session = M.Session(root / "home", root / "runtime", run, environment={})
            for mode in ("activate",):
                with self.subTest(mode=mode), self.assertRaisesRegex(RuntimeError, "activation blocked"):
                    getattr(session, mode)(runtime_scope=True, restart_ready=True)
            run.assert_not_called()
            self.assertFalse(session.folder.exists())
            self.assertFalse(session.dropin.exists())
            self.assertFalse(session.layer.exists())

    def test_legacy_manifest_cannot_bypass_the_freeze_package_guard(self):
        for manifest in (None, {}, {"schema": "frame-compositor-raw-owned-session-v1"},
                {"schema": M.SESSION_SCHEMA, "owned_files": {M.SELECTOR: "a" * 64}, "launch_freeze_abi": 1, "launch_provider_mask": 15}):
            with self.subTest(manifest=manifest), self.assertRaisesRegex(RuntimeError, "activation blocked"):
                M.require_reviewed_launch_isolation(manifest)

    def test_freeze_header_must_belong_to_current_start(self):
        with tempfile.TemporaryDirectory(prefix="raw-freeze-receipt-") as folder:
            root = Path(folder); session = M.Session(root, root / "runtime", environment={})
            session.health_source.clock = lambda: 3.0
            M.private_parents(session.folder)
            header = {"schema": 1, "capture": "compositor_raw_import", "pid": 100,
                "launch_environment_frozen": True, "launch_freeze_abi": 1, "launch_provider_mask": 63,
                "launch_frozen_owner_pid": 100, "launch_frozen_observed_ns": 2000000000}
            result = {"process_identities": {"compositor": {"pid": 100}}}
            encoded = lambda value: (json.dumps(value, separators=(",", ":")) + "\n").encode()
            M.exclusive(session.capture, encoded(header))
            session.verify_freeze_receipt({"monotonic": 1.0}, result)
            for field, value in (("pid", 101), ("launch_environment_frozen", False), ("launch_provider_mask", 31),
                    ("launch_frozen_owner_pid", 101), ("launch_frozen_observed_ns", 999999999),
                    ("launch_frozen_observed_ns", 4000000000), ("launch_frozen_observed_ns", 2**64)):
                changed = dict(header); changed[field] = value; session.capture.write_bytes(encoded(changed))
                with self.subTest(field=field), self.assertRaises(RuntimeError): session.verify_freeze_receipt({"monotonic": 1.0}, result)

class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="compositor-raw-session-fixture-")
        self.root = Path(self.temp.name)
        self.home, self.runtime = self.root / "home", self.root / "runtime"
        self.home.mkdir(mode=0o700); self.runtime.mkdir(mode=0o700)
        self.calls, self.fail_restart, self.interrupt_restart = [], False, False
        self.manager_after_restart = []
        self.original = {"Environment": "NATIVE_SETTING=unchanged", "EnvironmentFiles": "", "DropInPaths": ""}
        self.manager = "NATIVE_ENV=yes\n"
        self.health_source = mock.Mock()
        self.health_source.baseline.return_value = {"fixture_baseline": True}
        self.health_source.clock = lambda: 1.0
        self.health_source.sleep = lambda seconds: None
        self.session = M.Session(self.home, self.runtime, self.command, environment={}, health_source=self.health_source)
        self.native, self.artifacts, self.patches = {}, {}, []
        patch = mock.patch.object(M, "require_reviewed_launch_isolation", return_value=None)
        patch.start(); self.patches.append(patch)
        for module, name in ((M, M.HELPERS[0]), (M.base, M.HELPERS[1]), (M.health, M.HELPERS[2])):
            target = self.root / name; M.exclusive(target, Path(module.__file__).read_bytes())
            patch = mock.patch.object(module, "__file__", str(target)); patch.start(); self.patches.append(patch)
        pins, profiles, reports = {}, [], []
        for i, ((role, (name, _)), numeric) in enumerate(zip(M.NATIVE.items(), (7, 3, 4))):
            data = self.elf(i + 1); path = self.root / name; M.exclusive(path, data); self.native[role] = path
            pins[role] = (name, M.digest(data)); profile = self.profile(numeric, data); profiles.append(profile)
            reports.append({"role": numeric, "file_sha256": M.digest(data), "profile_sha256": profile[-32:].hex()})
        for key, value in (("NATIVE", pins), ("PROFILE_PINS", tuple(p[-32:].hex() for p in profiles))):
            patch = mock.patch.object(M, key, value); patch.start(); self.patches.append(patch)
        main, audit, selector = self.elf(7), self.elf(8), self.elf(9); profiles.append(self.profile(5, main))
        envelope = bytearray(8272); envelope[:8] = b"FCMRAW03"
        struct.pack_into("<II", envelope, 8, 3, 8272); envelope[16:48] = profiles[-1][-32:]
        struct.pack_into("<8Q", envelope, 48, *M.ENVELOPE_LIMITS)
        envelope[112:-32] = b"".join(profiles); envelope[-32:] = bytes.fromhex(M.digest(envelope[:-32]))
        layer = self.layer_bytes()
        for name, data in ((M.MAIN, main), (M.AUDIT, audit), (M.SELECTOR, selector), (M.ENVELOPE, envelope), (M.LAYER, layer)):
            path = self.root / name; M.exclusive(path, bytes(data)); self.artifacts[name] = path
        self.plan = {"schema": M.SCHEMA, "prepared": True, "frame_directory": str(self.session.folder),
            "frame_user_home": str(self.home), "directory_mode": "0700", "environment": self.session.activation_values(), "native_profiles": reports,
            "observer_profile": {"role": 5, "file_sha256": M.digest(main), "profile_sha256": profiles[-1][-32:].hex()},
            "observer": {"sha256": M.digest(main), "bytes": len(main), "public_exports": sorted(M.PUBLIC_EXPORTS),
                "scope_marker": {"symbol": "frame_compositor_raw_scope", "rva": 0x1080, "bytes": 16, "sha256": M.digest(M.SCOPE_MARKER)},
                "trace_sampling_marker": {"symbol": M.TRACE_SAMPLING_SYMBOL, "rva": 0x10a0, "bytes": 32,
                    "sha256": M.digest(M.TRACE_SAMPLING_MARKER)},
                "vulkan_functions": dict(M.VULKAN_FUNCTIONS), "standard_vulkan_exports": [], "cold_environment_freeze_abi": 1, "launch_provider_mask": 63,
                "raw_trace_sampling": dict(M.RAW_TRACE_SAMPLING)},
            "audit": {"sha256": M.digest(audit), "bytes": len(audit), "native_rvas": dict(M.NATIVE_RVAS), "early_forward_only": True},
            "envelope": {"sha256": M.digest(envelope), "bytes": len(envelope), "schema": 3, "module_count": 4,
                "module_roles": [7, 3, 4, 5], "reviewed_observer_profile_identity": profiles[-1][-32:].hex()},
            "manifest": {"sha256": M.digest(layer), "bytes": len(layer), "library_path": str(self.session.library),
                "functions": dict(M.VULKAN_FUNCTIONS),
                "staged_path": str(self.session.folder / M.LAYER), "frame_path": str(self.session.layer)},
            "capture_limits": dict(M.CAPTURE_LIMITS), "raw_limits": dict(M.RAW_LIMITS), "journal_limits": dict(M.JOURNAL_LIMITS), "inventory": []}
        self.plan["operator_arm"] = self.session.operator_arm_descriptor()
        self.plan["selector"] = {"sha256": M.digest(selector), "bytes": len(selector), "public_exports": sorted(M.SELECTOR_EXPORTS),
            "cold_environment_freeze_abi": 1, "launch_provider_mask": 63, "module_tls_bytes": 0, "standard_vulkan_exports": [],
            "configuration": {"target": str(self.native["compositor"]), "selector": str(self.session.selector), "payload": str(self.session.library),
                "audit": str(self.session.audit), "envelope": str(self.session.envelope), "output": str(self.session.capture), "journal": str(self.session.journal)}}
        self.plan["launch_isolation"] = {"schema": M.health.ISOLATION_SCHEMA, "target_exe": str(self.native["compositor"]),
            "selector_path": str(self.session.selector), "payload_path": str(self.session.library), "audit_path": str(self.session.audit),
            "target_exec_environment": self.session.target_environment()}
        for flag in ("installed", "activated", "runtime_authorized", "frame_access", "network_access",
            "native_binary_or_script_changes", "shader_or_mask_activation", "source_frame_associated", "gpu_coherent", "automatic_restart", "continuous_connection_owned", "native_namespace_associated"):
            self.plan[flag] = False
        for name in (*self.artifacts, "audit-preparation.cpp", "launch-configuration.c"):
            data = self.artifacts[name].read_bytes() if name in self.artifacts else b"offline source"
            self.plan["inventory"].append({"name": name, "bytes": len(data), "sha256": M.digest(data), "frame_path":
                str(self.session.folder / name) if name in self.artifacts else None, "owned_source": True, "native_bytes": False, "mode": "0600"})
        self.plan_path = self.root / "deployment-plan.json"; self.save_plan()
        self.helpers = {name: M.digest(Path(module.__file__).read_bytes()) for module, name in
                        ((M, M.HELPERS[0]), (M.base, M.HELPERS[1]), (M.health, M.HELPERS[2]))}
        self.ready_result = {"schema": M.health.SCHEMA, "status": "ready", "ready": True,
            "screen_pixels_verified": False, "automatic_restart": False, "fixture_health": True, "isolation_verified": True}
        patch = mock.patch.object(M.health, "wait_ready", return_value=self.ready_result)
        self.wait_ready = patch.start(); self.patches.append(patch)
        patch = mock.patch.object(self.session, "verify_freeze_receipt", return_value=None)
        patch.start(); self.patches.append(patch)

    @staticmethod
    def elf(value):
        raw = bytearray(576 if value == 7 else 192)
        raw[:20] = b"\x7fELF\x02\x01\x01" + b"\0" * 9 + b"\x03\0\xb7\0"
        struct.pack_into("<Q", raw, 32, 64)
        struct.pack_into("<3H", raw, 52, 64, 56, 1)
        struct.pack_into("<II6Q", raw, 64, 1, 4, 0, 0x1000, 0, len(raw), len(raw), 4096)
        raw[128:144] = M.SCOPE_MARKER
        raw[144:] = bytes([value]) * (len(raw) - 144)
        if value == 7:
            # Small real ELF tables authenticate the new own object; other
            # native/audit fixture profiles keep their original shape.
            struct.pack_into("<I", raw, 20, 1)
            struct.pack_into("<Q", raw, 40, 320)
            struct.pack_into("<3H", raw, 58, 64, 4, 0)
            raw[160:192] = M.TRACE_SAMPLING_MARKER
            strings = b"\0" + M.TRACE_SAMPLING_SYMBOL.encode("ascii") + b"\0"
            raw[192:192 + len(strings)] = strings
            raw[256:304] = bytes(48)
            struct.pack_into("<IBBHQQ", raw, 280, 1, 1, 0, 1, 0x10a0, 32)
            raw[320:] = bytes(256)
            struct.pack_into("<II4QII2Q", raw, 384, 0, 1, 2, 0x1080, 128, 64, 0, 0, 8, 0)
            struct.pack_into("<II4QII2Q", raw, 448, 0, 3, 0, 0, 192, len(strings), 0, 0, 1, 0)
            struct.pack_into("<II4QII2Q", raw, 512, 0, 2, 0, 0, 256, 48, 2, 2, 8, 24)
        return bytes(raw)
    @staticmethod
    def profile(role, data):
        raw = bytearray(2032); struct.pack_into("<5Q", raw, 0, 1, role, 183, 3, len(data))
        raw[176:208] = bytes.fromhex(M.digest(data)); raw[-32:] = bytes.fromhex(M.digest(raw[:-32])); return raw
    def layer_bytes(self):
        return M.json_bytes({"file_format_version": "1.2.0", "layer": {"name": "VK_LAYER_FRAME_MASK_trace", "type": "GLOBAL",
            "library_path": str(self.session.library), "api_version": "1.3.0", "implementation_version": "1",
            "description": "Opt-in bounded passive compositor raw import metadata", "enable_environment": {"FRAME_MASK_TRACE": "1"},
            "functions": dict(M.VULKAN_FUNCTIONS),
            "disable_environment": {"FRAME_MASK_TRACE_DISABLE": "1"}}})
    def tearDown(self):
        for patch in reversed(self.patches): patch.stop()
        self.temp.cleanup()
    def save_plan(self):
        if self.plan_path.exists(): self.plan_path.unlink()
        M.exclusive(self.plan_path, M.json_bytes(self.plan)); self.sha = M.digest(self.plan_path.read_bytes())
    def command(self, args, **options):
        self.calls.append(tuple(args)); text = "active\n"
        if "show-environment" in args: text = self.manager
        elif "show" in args:
            state = dict(self.original)
            if self.session.dropin.exists():
                state["Environment"] += " " + " ".join(shlex.quote(f"{k}={v}") for k, v in self.session.activation_values().items())
                state["DropInPaths"] = " ".join(filter(None, (state["DropInPaths"], str(self.session.dropin))))
            text = "\n".join(f"{k}={v}" for k, v in state.items()) + "\n"
        elif "restart" in args:
            if self.interrupt_restart: raise KeyboardInterrupt()
            if self.fail_restart: raise subprocess.TimeoutExpired(args, 45)
            if self.manager_after_restart: self.manager = self.manager_after_restart.pop(0)
        return subprocess.CompletedProcess(args, 0, text, "")
    def stage(self, **kwargs):
        opts = {"runtime_scope": True, "reviewed_helpers": self.helpers}; opts.update(kwargs)
        self.session.stage(self.plan_path, self.sha, self.native, self.artifacts, **opts)
    def activate(self, **kwargs):
        opts = {"runtime_scope": True, "restart_ready": True}; opts.update(kwargs); self.session.activate(**opts)
    def restart_count(self): return sum("restart" in call for call in self.calls)
    def alter_artifact(self, name, raw):
        self.artifacts[name].unlink(); M.exclusive(self.artifacts[name], raw)
        report = {M.MAIN: "observer", M.AUDIT: "audit", M.SELECTOR: "selector", M.ENVELOPE: "envelope", M.LAYER: "manifest"}[name]
        self.plan[report].update(sha256=M.digest(raw), bytes=len(raw))
        next(i for i in self.plan["inventory"] if i["name"] == name).update(sha256=M.digest(raw), bytes=len(raw)); self.save_plan()

    def promote_stream_image_plan(self):
        self.plan['schema'] = M.STREAM_IMAGE_SCHEMA
        self.plan['observer']['candidate_selection'] = copy.deepcopy(M.CANDIDATE_SELECTION)
        self.plan['observer']['scope_marker']['sha256'] = M.digest(M.STREAM_IMAGE_MARKER)
        main = bytearray(self.artifacts[M.MAIN].read_bytes()); main[128:144] = M.STREAM_IMAGE_MARKER
        SessionTests.alter_artifact(self, M.MAIN, bytes(main))
        profile = self.profile(5, main)
        self.plan['observer_profile'].update(file_sha256=M.digest(main), profile_sha256=profile[-32:].hex())
        self.plan['envelope'].update(schema=4, reviewed_observer_profile_identity=profile[-32:].hex())
        envelope = bytearray(self.artifacts[M.ENVELOPE].read_bytes())
        envelope[:16] = M.STREAM_IMAGE_MARKER; envelope[16:48] = profile[-32:]
        envelope[112 + 2032 * 3:-32] = profile; envelope[-32:] = bytes.fromhex(M.digest(envelope[:-32]))
        SessionTests.alter_artifact(self, M.ENVELOPE, bytes(envelope))

    def test_v5_current_image_plan_stages_and_readmits_without_changing_recovery(self):
        self.promote_stream_image_plan(); self.stage(); self.activate()
        self.assertTrue(self.session.readcheck()['active'])
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertTrue(json.loads((self.session.folder / 'rollback-complete.json').read_text())['restored'])

    def test_v5_requires_exact_typed_selection_scope_before_any_staging(self):
        self.promote_stream_image_plan(); original = copy.deepcopy(self.plan)
        variants = [None, {}, dict(M.CANDIDATE_SELECTION, even_extent=1),
            dict(M.CANDIDATE_SELECTION, thread_slots=17), dict(M.CANDIDATE_SELECTION, depth=True),
            dict(M.CANDIDATE_SELECTION, stream_origin_proven=True),
            dict(M.CANDIDATE_SELECTION, begin_early_private_replay=True),
            dict(M.CANDIDATE_SELECTION, public_formats=[50, 1000156013]),
            dict(M.CANDIDATE_SELECTION, private_acquisition='begin-after')]
        for selection in variants:
            self.plan = copy.deepcopy(original)
            self.plan['observer']['candidate_selection'] = selection; self.save_plan()
            with self.subTest(selection=selection), self.assertRaisesRegex(RuntimeError, 'public image selection'):
                self.stage()
            self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_v5_cannot_relabel_an_actual_first_begin_artifact_as_image_selection(self):
        self.promote_stream_image_plan()
        main = bytearray(self.artifacts[M.MAIN].read_bytes()); main[128:144] = M.SCOPE_MARKER
        self.alter_artifact(M.MAIN, bytes(main))
        with self.assertRaisesRegex(RuntimeError, 'acquisition marker'): self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_legacy_plan_cannot_admit_the_new_actual_image_artifact_or_selection_report(self):
        self.plan['observer']['candidate_selection'] = dict(M.CANDIDATE_SELECTION); self.save_plan()
        with self.assertRaisesRegex(RuntimeError, 'Legacy acquisition'): self.stage()
        self.plan['observer'].pop('candidate_selection'); self.save_plan()
        main = bytearray(self.artifacts[M.MAIN].read_bytes()); main[128:144] = M.STREAM_IMAGE_MARKER
        self.alter_artifact(M.MAIN, bytes(main))
        with self.assertRaisesRegex(RuntimeError, 'acquisition marker'): self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_activation_requires_current_ui_health_and_fault_removes_only_own_activation(self):
        self.stage()
        self.wait_ready.return_value = {"status": "fault", "ready": False,
            "reason": "vrwebhelper cannot allocate memory in static TLS block", "automatic_restart": False}
        with self.assertRaisesRegex(RuntimeError, "compositor/UI startup not ready"): self.activate()
        self.assertEqual(self.restart_count(), 1)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        result = json.loads((self.session.folder / "activation-startup-health.json").read_bytes())
        self.assertFalse(result["ready"]); self.assertIn("static TLS", result["reason"])
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_missing_recovery_baseline_does_not_prevent_original_restart(self):
        self.stage(); self.activate()
        self.health_source.baseline.side_effect = PermissionError("startup journal denied")
        with self.assertRaisesRegex(RuntimeError, "Original service restarted"):
            self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        result = json.loads((self.session.folder / "restoration-startup-health.json").read_bytes())
        self.assertFalse(result["ready"]); self.assertIn("baseline unavailable", result["reason"])

    def test_receipt_storage_failure_cannot_block_authorized_original_restart(self):
        self.stage(); self.activate()
        with mock.patch.object(self.session, "health_receipt", side_effect=OSError("No space left on device")):
            with self.assertRaisesRegex(RuntimeError, "Original service restarted"):
                self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_activation_baseline_is_captured_immediately_before_restart(self):
        self.stage()
        self.health_source.baseline.side_effect = lambda: {"restart_count_when_captured": self.restart_count()}
        self.activate()
        self.assertEqual(json.loads((self.session.folder / "activation-startup-baseline.json").read_bytes()),
                         {"restart_count_when_captured": 0})
        self.wait_ready.assert_called_once()
        self.assertFalse(self.session.readcheck()["startup_health_current_verified"])

    def test_restoration_health_failure_is_not_reported_as_completed(self):
        self.stage(); self.activate()
        self.wait_ready.return_value = {"status": "not-ready", "ready": False, "reason": "UI evidence unreadable"}
        with self.assertRaisesRegex(RuntimeError, "compositor/UI startup not ready"):
            self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_monitor_api_retains_failure_receipt_without_an_automatic_restart(self):
        self.stage(); self.activate()
        self.ready_result.update(gate_checkpoint={"fixture": True}, source_checkpoint={"fixture": True})
        self.session.health_receipt("activation-startup-health.json", self.ready_result)
        with mock.patch.object(M.health.StartupGate, "resume", return_value=mock.Mock()), \
             mock.patch.object(M.health, "monitor", return_value={"status": "fault", "ready": False,
                 "reason": "Required UI process exited", "automatic_restart": False}):
            with self.assertRaisesRegex(RuntimeError, "health lost"): self.session.monitor_startup(seconds=1)
        self.assertEqual(self.restart_count(), 1)
        result = json.loads((self.session.folder / "startup-monitor.json").read_bytes())
        self.assertFalse(result["automatic_restart"]); self.assertEqual(result["status"], "fault")

    def test_freeze_failure_invalidates_both_receipt_and_resumable_gate(self):
        self.stage()
        self.ready_result.update(gate_checkpoint={"ready": True, "ready_since": 1.0, "failed": None})
        self.session.verify_freeze_receipt.side_effect = RuntimeError("missing current freeze header")
        with self.assertRaisesRegex(RuntimeError, "startup not ready"): self.activate()
        result = json.loads((self.session.folder / "activation-startup-health.json").read_bytes())
        self.assertFalse(result["ready"]); self.assertFalse(result["launch_freeze_receipt_verified"])
        self.assertFalse(result["gate_checkpoint"]["ready"])
        self.assertIn("freeze header", result["gate_checkpoint"]["failed"])
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())

    def test_monitor_cannot_resume_a_failed_outer_startup_receipt(self):
        self.stage(); self.activate()
        failed = dict(self.ready_result, status="fault", ready=False,
            gate_checkpoint={"ready": True}, source_checkpoint={"fixture": True})
        self.session.health_receipt("activation-startup-health.json", failed)
        with mock.patch.object(M.health.StartupGate, "resume") as resume:
            with self.assertRaisesRegex(RuntimeError, "failed startup receipt"): self.session.monitor_startup(seconds=1)
            resume.assert_not_called()
        self.assertEqual(self.restart_count(), 1)

    def test_stage_does_not_enable_or_restart(self):
        self.stage(); self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 0); self.assertEqual(self.session.folder.stat().st_mode & 0o777, 0o700)
        self.assertFalse(self.session.readcheck()["active"])
        self.assertTrue(self.session.manifest()["scope_acknowledged"])

    def test_actual_observer_scope_is_read_only_and_matches_the_reviewed_contract(self):
        report = copy.deepcopy(self.plan["observer"]["scope_marker"])
        valid = self.artifacts[M.MAIN].read_bytes()
        self.session.validate_scope_marker(valid, report)
        variants = []
        raw = bytearray(valid); raw[128:144] = b"FCMRAW01" + struct.pack("<II", 1, 8272); variants.append(raw)
        for flags in (5, 6, 7):
            raw = bytearray(valid); struct.pack_into("<I", raw, 68, flags); variants.append(raw)
        raw = bytearray(valid); struct.pack_into("<Q", raw, 96, 128); variants.append(raw)
        raw = bytearray(valid); struct.pack_into("<H", raw, 56, 129); variants.append(raw)
        variants.append(valid[:100])
        for raw in variants:
            with self.subTest(raw=bytes(raw[:16])), self.assertRaises(RuntimeError):
                self.session.validate_scope_marker(bytes(raw), report)
        for key, value in (("rva", True), ("rva", 2**64), ("bytes", 15), ("sha256", "f" * 64)):
            bad = dict(report); bad[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                self.session.validate_scope_marker(valid, bad)
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_staging_rejects_relabelled_old_observer_even_with_a_reviewed_new_plan(self):
        raw = bytearray(self.artifacts[M.MAIN].read_bytes())
        raw[128:144] = b"FCMRAW01" + struct.pack("<II", 1, 8272)
        self.alter_artifact(M.MAIN, bytes(raw))
        with self.assertRaisesRegex(RuntimeError, "acquisition marker"): self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_activation_readmits_stored_scope_without_blocking_original_recovery(self):
        self.stage()
        plan = copy.deepcopy(self.plan); plan["schema"] = "frame-compositor-raw-offline-bundle-v2"
        path = self.session.folder / "deployment-plan.json"; path.unlink(); M.exclusive(path, M.json_bytes(plan))
        manifest_path = self.session.folder / "owned.json"
        manifest = json.loads(manifest_path.read_bytes()); manifest["plan_sha256"] = M.digest(path.read_bytes())
        manifest["owned_files"]["deployment-plan.json"] = manifest["plan_sha256"]
        manifest_path.unlink(); M.exclusive(manifest_path, M.json_bytes(manifest))
        with self.assertRaisesRegex(RuntimeError, "Stored acquisition scope"): self.activate()
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 0)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 1)

    def test_legacy_environment_or_unreviewed_freeze_cannot_stage(self):
        original = copy.deepcopy(self.plan)
        variants = (("schema", "frame-compositor-raw-offline-bundle-v1"),
            ("schema", "frame-compositor-raw-offline-bundle-v2"),
            ("raw_limits", dict(self.plan["raw_limits"], scalar_bytes=672, private_bytes=709)),
            ("environment", self.session.target_environment()),
            ("selector", dict(self.plan["selector"], launch_provider_mask=15)),
            ("launch_isolation", dict(self.plan["launch_isolation"], target_exe="/foreign/vrcompositor")))
        for key, value in variants:
            self.plan = copy.deepcopy(original); self.plan[key] = value; self.save_plan()
            with self.subTest(key=key), self.assertRaises(RuntimeError): self.stage()
            self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_startup_requires_runtime_isolation_in_addition_to_ui_readiness(self):
        self.stage(); self.ready_result["isolation_verified"] = False
        with self.assertRaisesRegex(RuntimeError, "isolation was not established"): self.activate()
        self.assertEqual(self.restart_count(), 1)
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def large_main(self, size):
        # The real final ARM artifact is 3,606,520 bytes. Preserve all modeled
        # reviewed profile/envelope relationships instead of widening a toy
        # file after staging. This tests lifecycle limits, not native ELF Ready.
        raw = self.elf(7) + bytes(size - 64)
        profile = self.profile(5, raw)
        envelope = bytearray(self.artifacts[M.ENVELOPE].read_bytes())
        envelope[16:48] = profile[-32:]
        envelope[112 + 3 * 2032:-32] = profile
        envelope[-32:] = bytes.fromhex(M.digest(envelope[:-32]))
        self.alter_artifact(M.MAIN, raw)
        self.plan["observer_profile"] = {"role": 5, "file_sha256": M.digest(raw), "profile_sha256": profile[-32:].hex()}
        self.plan["envelope"]["reviewed_observer_profile_identity"] = profile[-32:].hex()
        self.alter_artifact(M.ENVELOPE, bytes(envelope))
    def test_actual_sized_main_survives_stage_activate_readcheck_rollback(self):
        self.large_main(3606520); self.stage(); self.activate()
        self.assertTrue(self.session.readcheck()["active"])
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.readcheck()["active"])
    def test_large_main_limit_does_not_extend_audit_or_metadata(self):
        self.stage(); m = self.session.manifest()
        for name in (M.AUDIT, M.HELPERS[0]):
            path = self.session.folder / name; original = path.read_bytes()
            limit = 262144 if name == M.AUDIT else M.base.LIMIT
            large = original + bytes(limit + 1 - len(original))
            path.write_bytes(large); m["owned_files"][name] = M.digest(large)
            with self.subTest(name=name), self.assertRaises(RuntimeError): self.session.verify_owned(m)
            path.write_bytes(original); m["owned_files"][name] = M.digest(original)
    def test_scope_is_actual_boolean_not_plan_permission(self):
        for bad in (False, 1, "yes", None):
            with self.subTest(bad=bad), self.assertRaises(RuntimeError): self.stage(runtime_scope=bad)
        self.assertFalse(self.session.folder.exists())
    def test_missing_reviewed_helper_refused(self):
        for name in M.HELPERS:
            with self.subTest(name=name), self.assertRaises(RuntimeError): self.stage(reviewed_helpers={k: v for k, v in self.helpers.items() if k != name})
        self.assertFalse(self.session.folder.exists())
    def test_bad_reviewed_plan_hash_refused(self):
        with self.assertRaises(RuntimeError): self.session.stage(self.plan_path, "0" * 64, self.native, self.artifacts, runtime_scope=True, reviewed_helpers=self.helpers)
    def test_changed_reviewed_helper_refused(self):
        helpers = dict(self.helpers); helpers[M.HELPERS[0]] = "0" * 64
        with self.assertRaises(RuntimeError): self.stage(reviewed_helpers=helpers)
    def test_scope_flags_refused(self):
        for key in ("installed", "activated", "runtime_authorized", "frame_access", "network_access", "native_binary_or_script_changes",
            "shader_or_mask_activation", "source_frame_associated", "gpu_coherent", "automatic_restart", "continuous_connection_owned", "native_namespace_associated"):
            original = self.plan[key]
            with self.subTest(key=key):
                self.plan[key] = True; self.save_plan()
                with self.assertRaises(RuntimeError): self.stage()
                self.plan[key] = original
        self.assertFalse(self.session.folder.exists())
    def test_foreign_target_or_environment_refused(self):
        for key in ("frame_user_home", "frame_directory", "environment"):
            original = copy.deepcopy(self.plan[key]); self.plan[key] = "/foreign"; self.save_plan()
            with self.subTest(key=key), self.assertRaises(RuntimeError): self.stage()
            self.plan[key] = original
    def test_limits_cannot_be_widened_or_boolean(self):
        for key in ("capture_limits", "raw_limits", "journal_limits"):
            original = dict(self.plan[key])
            for name in original:
                for value in (original[name] + 1, True):
                    self.plan[key] = dict(original); self.plan[key][name] = value; self.save_plan()
                    with self.subTest(key=key, name=name, value=value), self.assertRaises(RuntimeError): self.stage()
            self.plan[key] = original
    def test_duplicate_or_native_inventory_refused(self):
        original = copy.deepcopy(self.plan["inventory"])
        for mode in ("duplicate", "native", "source", "path", "mode"):
            self.plan["inventory"] = copy.deepcopy(original)
            item = self.plan["inventory"][0]
            if mode == "duplicate": self.plan["inventory"][-1] = copy.deepcopy(item)
            if mode == "native": item["native_bytes"] = True
            if mode == "source": item["owned_source"] = False
            if mode == "path": item["frame_path"] = "/foreign/native"
            if mode == "mode": item["mode"] = "0644"
            self.save_plan()
            with self.subTest(mode=mode), self.assertRaises(RuntimeError): self.stage()
    def test_exact_canonical_native_required(self):
        path = self.native["compositor"]; path.write_bytes(self.elf(99))
        with self.assertRaises(RuntimeError): self.stage()
    def test_artifact_symlink_or_public_permissions_refused(self):
        path = self.artifacts[M.MAIN]; original = path.read_bytes()
        path.chmod(0o644)
        with self.assertRaises(RuntimeError): self.stage()
        path.chmod(0o600); path.unlink(); target = self.root / "alternate"; M.exclusive(target, original); path.symlink_to(target)
        with self.assertRaises(RuntimeError): self.stage()
    def test_foreign_layer_prevents_stage(self):
        M.private_parents(self.session.layer.parent); M.exclusive(self.session.layer.parent / "unrelated.json", b"{}")
        with self.assertRaises(RuntimeError): self.stage()
    def test_old_activation_prevents_stage(self):
        path = self.runtime / "systemd/user/steam.service.d/94-frame-client-startup.conf"
        M.private_parents(path.parent); M.exclusive(path, b"old")
        with self.assertRaises(RuntimeError): self.stage()
    def test_foreign_environments_prevent_stage(self):
        for key in ("LD_PRELOAD", "LD_AUDIT", "LD_BIND_NOW", "VK_INSTANCE_LAYERS", "FRAME_MASK_TRACE", "GHVR_FRAME_CLIENT_STARTUP_OUTPUT"):
            self.session.environment = {key: "foreign"}
            with self.subTest(key=key), self.assertRaises(RuntimeError): self.stage()
        self.session.environment = {}; self.manager += "LD_AUDIT=foreign\n"
        with self.assertRaises(RuntimeError): self.stage()
    def test_foreign_native_environment_file_prevents_stage(self):
        path = self.root / "native-env"; M.exclusive(path, b"LD_AUDIT=foreign\n")
        self.original["EnvironmentFiles"] = str(path) + " (ignore_errors=no)"
        with self.assertRaises(RuntimeError): self.stage(reviewed_environment_files={str(path): M.digest(path.read_bytes())})
    def test_envelope_schema_scope_and_pins_are_checked(self):
        original = self.artifacts[M.ENVELOPE].read_bytes()
        for offset in (0, 8, 48, 112, 112 + 176, 112 + 2032 * 3 + 176):
            raw = bytearray(original); raw[offset] ^= 1; raw[-32:] = bytes.fromhex(M.digest(raw[:-32]))
            self.alter_artifact(M.ENVELOPE, bytes(raw))
            with self.subTest(offset=offset), self.assertRaises(RuntimeError): self.stage()
        self.assertFalse(self.session.folder.exists())
    def test_layer_cannot_activate_different_library_or_shader(self):
        original = json.loads(self.artifacts[M.LAYER].read_bytes())
        for key, value in (("library_path", "/foreign.so"), ("name", "REPLACEMENT_SHADER"), ("disable_environment", {})):
            data = copy.deepcopy(original); data["layer"][key] = value; self.alter_artifact(M.LAYER, M.json_bytes(data))
            with self.subTest(key=key), self.assertRaises(RuntimeError): self.stage()
    def test_private_layer_function_mapping_is_required_and_exact(self):
        original = json.loads(self.artifacts[M.LAYER].read_bytes())
        maps = [None, {}, [], {key: key for key in M.VULKAN_FUNCTIONS},
            {**M.VULKAN_FUNCTIONS, "vkGetInstanceProcAddr": M.VULKAN_FUNCTIONS["vkGetDeviceProcAddr"]},
            {**M.VULKAN_FUNCTIONS, "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion"},
            {**M.VULKAN_FUNCTIONS, "vkCreateInstance": "frame_compositor_raw_vkCreateInstance"}]
        maps += [{key: value for key, value in M.VULKAN_FUNCTIONS.items() if key != missing} for missing in M.VULKAN_FUNCTIONS]
        for mapping in maps:
            data = copy.deepcopy(original)
            if mapping is None: data["layer"].pop("functions")
            else: data["layer"]["functions"] = mapping
            self.alter_artifact(M.LAYER, M.json_bytes(data))
            with self.subTest(mapping=mapping), self.assertRaises(RuntimeError): self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)
    def test_old_or_inconsistent_public_export_and_mapping_reports_cannot_stage(self):
        original = copy.deepcopy(self.plan)
        for section, field, value in (("observer", "public_exports", None),
            ("observer", "public_exports", ["frame_compositor_raw_bridge", *M.VULKAN_FUNCTIONS]),
            ("observer", "vulkan_functions", {}), ("observer", "standard_vulkan_exports", ["vkGetDeviceProcAddr"]),
            ("manifest", "functions", {})):
            self.plan = copy.deepcopy(original)
            if value is None: self.plan[section].pop(field)
            else: self.plan[section][field] = value
            self.save_plan()
            with self.subTest(section=section, field=field), self.assertRaises(RuntimeError): self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)
    def test_activate_requires_both_actual_acknowledgments(self):
        self.stage()
        for args in ({"runtime_scope": False}, {"restart_ready": False}, {"runtime_scope": 1}, {"restart_ready": 1}):
            with self.subTest(args=args), self.assertRaises(RuntimeError): self.activate(**args)
        self.assertEqual(self.restart_count(), 0)
    def test_activate_exact_layer_and_preload_manual_restart(self):
        self.stage(); self.activate(); self.assertEqual(self.restart_count(), 1)
        self.assertEqual(self.session.layer.read_bytes(), self.artifacts[M.LAYER].read_bytes())
        self.assertEqual(self.session.dropin.read_bytes(), self.session.activation())
        self.assertTrue(self.session.readcheck()["active"])
        self.assertEqual(json.loads(self.session.layer.read_bytes())["layer"]["library_path"], str(self.session.library))
        self.assertEqual(self.session.activation_values(), {"LD_PRELOAD": str(self.session.selector)})
        self.assertTrue(all(call[0:2] == ("systemctl", "--user") for call in self.calls))
        self.assertTrue(all("steam.service" not in call for call in self.calls))
    def graphical_manager(self, number, *, session_paths=False):
        values = {"NATIVE_ENV": "yes", "XDG_SESSION_ID": str(number),
            "XDG_SESSION_PATH": "/org/freedesktop/DisplayManager/Session" + str(number)}
        if session_paths:
            values.update(GAMESCOPE_LIMITER_FILE=str(self.runtime / ("gamescope-limiter-" + str(number))),
                MANGOHUD_CONFIGFILE=str(self.runtime / ("gamescope-mangoapp-" + str(number))))
        return values
    @staticmethod
    def manager_text(values):
        return "\n".join(key + "=" + shlex.quote(value) for key, value in values.items()) + "\n"
    def test_coordinated_restart_accepts_exact_graphical_metadata_and_session_paths(self):
        self.manager = self.manager_text(self.graphical_manager(1, session_paths=True))
        self.stage()
        current = self.manager_text(self.graphical_manager(2, session_paths=True))
        restored = self.manager_text(self.graphical_manager(3, session_paths=True))
        self.manager_after_restart = [current, restored]
        self.activate(); self.assertEqual(self.manager, current)
        self.assertTrue(self.session.readcheck()["active"])
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.manager, restored)
        result = json.loads((self.session.folder / "rollback-complete.json").read_bytes())
        self.assertFalse(result["exact_manager"])
        self.assertFalse(result["manager_matches_except_session_paths"])
        self.assertTrue(result["manager_matches_except_session_metadata"])
        self.assertEqual(result["permitted_metadata_changes"], ["XDG_SESSION_ID", "XDG_SESSION_PATH"])
        self.assertEqual(result["permitted_path_changes"], ["GAMESCOPE_LIMITER_FILE", "MANGOHUD_CONFIGFILE"])
        self.assertFalse(self.session.readcheck()["active"])
        self.assertTrue(all("set-environment" not in call and "import-environment" not in call for call in self.calls))
    def test_graphical_metadata_before_activation_remains_exact(self):
        self.manager = self.manager_text(self.graphical_manager(1)); self.stage()
        self.manager = self.manager_text(self.graphical_manager(2))
        with self.assertRaisesRegex(RuntimeError, "changed after staging"): self.activate()
        self.assertEqual(self.restart_count(), 0)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
    def test_merely_staged_readcheck_does_not_allow_graphical_metadata_changes(self):
        self.manager = self.manager_text(self.graphical_manager(1)); self.stage()
        self.manager = self.manager_text(self.graphical_manager(2))
        with self.assertRaises(RuntimeError): self.session.readcheck()
        self.assertEqual(self.restart_count(), 0)
    def test_invalid_restoration_receipt_cannot_enable_post_restart_comparison(self):
        self.manager = self.manager_text(self.graphical_manager(1)); self.stage()
        self.manager = self.manager_text(self.graphical_manager(2))
        M.exclusive(self.session.folder / "rollback-complete.json", M.json_bytes({"restored": True}))
        with self.assertRaisesRegex(RuntimeError, "Invalid restoration record"): self.session.readcheck()
        self.assertEqual(self.restart_count(), 0)
    def test_malformed_or_unrelated_post_restart_manager_changes_fail_closed(self):
        initial = self.graphical_manager(1)
        self.manager = self.manager_text(initial); self.stage(); self.activate()
        variants = [("XDG_SESSION_ID", "c2"), ("XDG_SESSION_ID", "02"), ("XDG_SESSION_ID", "1" * 17),
            ("XDG_SESSION_ID", "２"), ("XDG_SESSION_PATH", "/tmp/Session2"),
            ("XDG_SESSION_PATH", "/org/freedesktop/DisplayManager/Session02"),
            ("XDG_SESSION_PATH", "/org/freedesktop/DisplayManager/Session2\nforeign"),
            ("XDG_SESSION_COOKIE", "new"), ("NATIVE_ENV", "changed"),
            ("LD_AUDIT", "foreign"), ("VK_INSTANCE_LAYERS", "foreign")]
        for key, value in variants:
            current = dict(initial); current[key] = value; self.manager = self.manager_text(current)
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError): self.session.readcheck()
        self.manager = self.manager_text(initial); self.session.rollback(restart_ready=True)
    def test_new_graphical_metadata_after_restart_is_not_a_permitted_addition(self):
        self.stage(); self.activate()
        self.manager = self.manager_text(self.graphical_manager(2))
        with self.assertRaises(RuntimeError): self.session.readcheck()
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
    def test_bad_original_graphical_metadata_cannot_become_an_allowed_changed_value(self):
        original = self.graphical_manager(1); original["XDG_SESSION_ID"] = "c1"
        self.manager = self.manager_text(original); self.stage(); self.activate()
        self.manager = self.manager_text(self.graphical_manager(2))
        with self.assertRaises(RuntimeError): self.session.readcheck()
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
    def test_changed_native_or_staged_helper_prevents_activation(self):
        self.stage(); self.native["libc"].write_bytes(self.elf(99))
        with self.assertRaises(RuntimeError): self.activate()
        self.assertEqual(self.restart_count(), 0)
    def test_changed_environment_after_stage_prevents_activation(self):
        self.stage(); self.original["Environment"] += " CHANGED=yes"
        with self.assertRaises(RuntimeError): self.activate()
        self.assertFalse(self.session.layer.exists())
    def test_existing_foreign_override_is_not_replaced(self):
        self.stage(); M.private_parents(self.session.dropin.parent); M.exclusive(self.session.dropin, b"foreign")
        with self.assertRaises(RuntimeError): self.activate()
        self.assertEqual(self.session.dropin.read_bytes(), b"foreign")
    def test_activation_restart_failure_does_not_restart_automatically(self):
        self.stage(); self.fail_restart = True
        with self.assertRaises(subprocess.TimeoutExpired): self.activate()
        self.assertEqual(self.restart_count(), 1); self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def test_activation_interruption_disables_owned_files_without_extra_restart(self):
        self.stage(); self.interrupt_restart = True
        with self.assertRaises(KeyboardInterrupt): self.activate()
        self.assertEqual(self.restart_count(), 1); self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def test_rollback_without_readiness_only_disables_activation(self):
        self.stage(); self.activate(); self.session.rollback()
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 1); self.assertFalse((self.session.folder / "rollback-complete.json").exists())
    def test_manual_rollback_restores_and_is_idempotent(self):
        self.stage(); self.activate(); self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2); self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.session.rollback(restart_ready=True); self.assertEqual(self.restart_count(), 2)
        proof = json.loads((self.session.folder / "rollback-complete.json").read_bytes())
        self.assertTrue(proof["restored"]); self.assertFalse(proof["native_mapping_unload_verified"])
    def test_tampered_layer_preserved_but_owned_override_removed(self):
        self.stage(); self.activate(); self.session.layer.write_bytes(b"foreign")
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertEqual(self.session.layer.read_bytes(), b"foreign"); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 1)
    def test_partial_pending_preserved_but_other_owned_activations_removed(self):
        self.stage(); self.activate(); M.exclusive(self.session.dropin.with_suffix(".pending"), b"partial")
        with self.assertRaises(RuntimeError): self.session.rollback()
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.session.dropin.with_suffix(".pending").read_bytes(), b"partial")
    def test_interrupted_hard_link_publication_is_recoverable(self):
        self.stage(); self.activate(); pending = self.session.dropin.with_suffix(".pending")
        os.link(self.session.dropin, pending)
        self.session.rollback(restart_ready=True)
        self.assertFalse(pending.exists()); self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def test_full_ledger_does_not_prevent_unloading_owned_activation(self):
        self.stage(); self.activate(); (self.session.folder / "session.jsonl").write_bytes(b"x" * M.base.LIMIT)
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 2)
    def test_reactivation_after_rollback_refused(self):
        self.stage(); self.activate(); self.session.rollback(restart_ready=True)
        with self.assertRaises(RuntimeError): self.activate()
        self.assertEqual(self.restart_count(), 2)
    def test_unknown_archive_is_preserved(self):
        self.stage(); self.activate(); archive = self.session.folder / "archive"; archive.mkdir(mode=0o700)
        M.exclusive(archive / "foreign", b"foreign")
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertEqual((archive / "foreign").read_bytes(), b"foreign")
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def test_capture_archive_limits_enforced_after_safe_disable(self):
        self.stage(); self.activate(); M.exclusive(self.session.capture, b"x" * (65536 + 1))
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
    def test_unload_verification_checks_actual_owned_mapping_metadata(self):
        self.stage(); self.activate(); self.session.rollback(restart_ready=True)
        proc = self.root / "proc"; proc.mkdir(mode=0o700); self.session.proc = proc
        process = proc / "456"; process.mkdir(mode=0o700)
        M.exclusive(process / "stat", b"456 (vrcompositor) S " + b"1 " * 19 + b"\n")
        M.exclusive(process / "cgroup", b"0::/user.slice/steamvr.service\n")
        M.exclusive(process / "maps", b"1000-2000 r-xp 00000000 00:01 3 /native/vrcompositor\n")
        proof = self.session.verify_unloaded(); self.assertTrue(proof["native_mapping_unload_verified"])
        self.assertEqual(proof["steamvr_processes_checked"], 1)
        (process / "maps").write_text("1000-2000 r-xp 00000000 00:01 3 " + str(self.session.library) + " (deleted)\n")
        with self.assertRaises(RuntimeError): self.session.verify_unloaded()
    def test_unload_verification_requires_managed_process_and_restoration(self):
        self.stage()
        with self.assertRaises(RuntimeError): self.session.verify_unloaded()
        self.activate(); self.session.rollback(restart_ready=True)
        proc = self.root / "proc"; proc.mkdir(mode=0o700); self.session.proc = proc
        with self.assertRaises(RuntimeError): self.session.verify_unloaded()
    def test_no_scope_bypass_or_timers_in_cli(self):
        source = Path(M.__file__).read_text()
        self.assertNotIn('"watchdog"', source); self.assertNotIn('"sudo"', source); self.assertNotIn('"timer"', source)
        with mock.patch.object(M.platform, "machine", return_value="x86_64"), self.assertRaises(SystemExit): M.main(["stage"])

class OperatorArmTests(unittest.TestCase):
    """Actual flock/node checks with synthetic, bounded proc records; no Frame access."""
    def setUp(self):
        SessionTests.setUp(self)
        self.descriptors = []

    def tearDown(self):
        for fd in self.descriptors: os.close(fd)
        SessionTests.tearDown(self)

    elf, profile = staticmethod(SessionTests.elf), staticmethod(SessionTests.profile)
    layer_bytes, save_plan, command = SessionTests.layer_bytes, SessionTests.save_plan, SessionTests.command
    stage, activate, restart_count = SessionTests.stage, SessionTests.activate, SessionTests.restart_count

    def test_v5_arm_requires_the_matching_live_selection_header(self):
        SessionTests.promote_stream_image_plan(self); self.live_fixture()
        with self.assertRaisesRegex(RuntimeError, 'public image selection'):
            self.session.validate_prearm_outputs([self.header], [self.journal_header], self.session.manifest(), self.current)
        self.header['candidate_selection'] = dict(M.CANDIDATE_SELECTION)
        self.save_live_records([self.header], [self.journal_header])
        receipt = self.session.arm(stream_start_intent=True)
        self.assertTrue(receipt['operator_arm_requested'])
        self.assertFalse(receipt['source_frame_associated'])

    def live_fixture(self):
        self.stage(); self.activate()
        self.pid = os.getpid()
        self.current = {"status": "ready", "ready": True, "isolation_verified": True,
            "launch_freeze_receipt_verified": True, "invocation": "a" * 32,
            "process_identities": {"compositor": {"pid": self.pid, "start_ticks": 500}}}
        self.proc = self.root / "proc"; self.proc.mkdir(mode=0o700)
        self.session.proc = self.proc
        process = self.proc / str(self.pid); process.mkdir(mode=0o700)
        os.symlink(self.native["compositor"], process / "exe")
        os.symlink("/", process / "root")
        values = ["S"] + ["0"] * 18 + ["500"]
        (process / "stat").write_text(str(self.pid) + " (vrcompositor) " + " ".join(values) + "\n")
        (process / "cgroup").write_text("0::/app/steamvr.service\n")
        maprows = []
        for index, path in enumerate((self.session.library, self.session.audit, self.session.selector)):
            info = path.stat()
            maprows.append(f"{4096 + index * 4096:x}-{8192 + index * 4096:x} r-xp 0000 {os.major(info.st_dev):x}:{os.minor(info.st_dev):x} {info.st_ino} {path}")
        (process / "maps").write_text("\n".join(maprows) + "\n")
        self.header = {"schema": 1, "capture": "compositor_raw_import", "pid": self.pid,
            "operator_arm_required": True, "operator_armed": False, "pre_arm_private_bytes": 0,
            "launch_environment_frozen": True, "launch_freeze_abi": 1, "launch_provider_mask": 63,
            "launch_frozen_owner_pid": self.pid, "launch_frozen_observed_ns": 1000000000,
            "capture_started_ns": 1000000000, "capture_deadline_ns": 121000000000,
            "transactions": 1, "parent_byte_limit": 16, "scalar_byte_limit": 720, "wire_byte_limit": 21,
            "private_byte_limit": 757, "write_limit": 8, "record_limit": 32, "byte_limit": 65536,
            "seconds_limit": 120, "observer_profile": self.plan["envelope"]["reviewed_observer_profile_identity"],
            "native_cross_endpoint_associated": False, "native_namespace_associated": False,
            "source_frame_associated": False, "uv_owned": False, "accepted_submit": False,
            "gpu_consumed": False, "mask_enabled": False}
        self.header["modules"] = [{"role": role, "file_sha256": M.NATIVE[name][1], "profile_sha256": M.PROFILE_PINS[index]}
            for index, (name, role) in enumerate((("compositor", 7), ("libc", 3), ("loader", 4)))]
        self.header["modules"].append({"role": 5, "file_sha256": self.plan["observer"]["sha256"],
            "profile_sha256": self.plan["envelope"]["reviewed_observer_profile_identity"]})
        self.journal_header = {"event": "header", "version": 1, "layer": "VK_LAYER_FRAME_MASK_trace",
            "scope": "passive_pipeline_metadata", "limit_bytes": 1048576,
            "binding_metadata_version": 1, "limit_binding_snapshots": 32,
            "binding_arm_required": True, "pre_arm_draws_recorded": False,
            "binding_ready_at_header": False, "deadline_seconds": 120,
            "raw_trace_sampling": dict(M.RAW_TRACE_SAMPLING)}
        self.save_live_records([self.header], [self.journal_header], create=True)
        rows = []
        for index, path in enumerate((self.session.capture, self.session.journal)):
            fd = os.open(path, os.O_RDWR); self.descriptors.append(fd)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            info = path.stat()
            rows.append(f"{index}: FLOCK ADVISORY WRITE {self.pid} {os.major(info.st_dev):x}:{os.minor(info.st_dev):x}:{info.st_ino} 0 EOF")
        (self.proc / "locks").write_text("\n".join(rows) + "\n")
        patch = mock.patch.object(self.session, "current_arm_health", return_value=self.current)
        patch.start(); self.patches.append(patch)
        self.health_source.clock = lambda: 3.0

    def save_live_records(self, raw, journal, create=False):
        for path, value in ((self.session.capture, raw), (self.session.journal, journal)):
            data = b"".join((json.dumps(row, separators=(",", ":")) + "\n").encode() for row in value)
            if create: M.exclusive(path, data)
            else: path.write_bytes(data)

    def test_protected_signal_arms_once_without_restart_or_extra_environment(self):
        self.live_fixture(); prior = self.restart_count()
        result = self.session.arm(stream_ready=True)
        self.assertEqual(result["schema"], M.ARM_RECEIPT_SCHEMA)
        self.assertEqual(result["stream_selection_phase"], "visible")
        self.assertTrue(result["stream_ready_acknowledged"])
        self.assertFalse(result["stream_start_intent_acknowledged"])
        self.assertEqual(self.session.marker.read_bytes(), M.ARM_CONTENTS)
        self.assertEqual(self.session.marker.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.restart_count(), prior)
        self.assertEqual(result["process"]["pid"], self.pid)
        self.assertFalse(result["source_frame_associated"]); self.assertFalse(result["mask_enabled"])
        self.assertFalse(result["continuous_transport_loss_proven"])
        self.assertEqual(result["capture_deadline_ns"] - result["capture_started_ns"], 120000000000)
        self.assertEqual(json.loads(self.session.arm_receipt.read_bytes()), result)
        with self.assertRaisesRegex(RuntimeError, "single-use"): self.session.arm(stream_ready=True)
        self.assertEqual(self.restart_count(), prior)
        self.assertEqual(len(self.session.target_environment()), 7)

    def test_arm_acknowledgment_is_an_actual_boolean_before_any_io(self):
        for value in (False, 0, 1, None, "yes"):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "acknowledgment"):
                self.session.arm(stream_ready=value)
        self.assertEqual(self.restart_count(), 0); self.assertFalse(self.session.folder.exists())

    def test_arm_cli_requires_visible_stream_ack_and_does_not_request_a_restart(self):
        fake = mock.Mock(); fake.arm.return_value = {"operator_arm_requested": True}
        with mock.patch.object(M.platform, "machine", return_value="aarch64"), \
             mock.patch.dict(M.os.environ, {"XDG_RUNTIME_DIR": f"/run/user/{os.getuid()}"}), \
             mock.patch.object(M, "Session", return_value=fake), mock.patch("builtins.print"):
            M.main(["arm", "--ack-pc-vr-stream-visible"])
            fake.arm.assert_called_once_with(stream_ready=True, stream_start_intent=False)
            fake.activate.assert_not_called(); fake.command.assert_not_called()
            fake.arm.reset_mock()
            M.main(["arm"])
            fake.arm.assert_called_once_with(stream_ready=False, stream_start_intent=False)

    def test_pre_stream_intent_arms_after_fresh_health_without_claiming_visible_stream(self):
        self.live_fixture(); prior = self.restart_count()
        def fresh_health():
            self.assertFalse(self.session.marker.exists())
            self.assertFalse(self.session.arm_receipt.exists())
            return self.current
        self.session.current_arm_health.side_effect = fresh_health
        result = self.session.arm(stream_start_intent=True)
        self.session.current_arm_health.assert_called_once_with()
        self.assertEqual(self.session.marker.read_bytes(), M.ARM_CONTENTS)
        self.assertEqual(result["schema"], M.ARM_RECEIPT_SCHEMA)
        self.assertEqual(result["stream_selection_phase"], "start-intent")
        self.assertFalse(result["stream_ready_acknowledged"])
        self.assertTrue(result["stream_start_intent_acknowledged"])
        self.assertFalse(result["source_frame_associated"])
        self.assertFalse(result["accepted_submit"])
        self.assertFalse(result["gpu_consumed"])
        self.assertEqual(result["operator_arm_ns"], 3000000000)
        self.assertEqual(result["capture_started_ns"], 1000000000)
        self.assertEqual(result["capture_deadline_ns"], 121000000000)
        self.assertEqual(self.restart_count(), prior)
        with self.assertRaisesRegex(RuntimeError, "single-use"):
            self.session.arm(stream_ready=True)

    def test_pre_stream_acknowledgment_rejects_conflicts_and_non_booleans_before_io(self):
        for ready, intent in ((True, True), (False, False), (False, 1), (1, False),
                (True, None), (False, "yes"), (0, True)):
            with self.subTest(ready=ready, intent=intent), self.assertRaisesRegex(RuntimeError, "Exactly one"):
                self.session.arm(stream_ready=ready, stream_start_intent=intent)
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.calls, [])

    def test_pre_stream_cli_is_a_distinct_mutually_exclusive_acknowledgment(self):
        fake = mock.Mock(); fake.arm.return_value = {"operator_arm_requested": True}
        with mock.patch.object(M.platform, "machine", return_value="aarch64"), \
             mock.patch.dict(M.os.environ, {"XDG_RUNTIME_DIR": f"/run/user/{os.getuid()}"}), \
             mock.patch.object(M, "Session", return_value=fake) as constructor, mock.patch("builtins.print"):
            M.main(["arm", "--ack-pc-vr-stream-start-intent"])
            fake.arm.assert_called_once_with(stream_ready=False, stream_start_intent=True)
            fake.activate.assert_not_called(); fake.command.assert_not_called()
            constructor.reset_mock(); fake.arm.reset_mock()
            with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as error:
                M.main(["arm", "--ack-pc-vr-stream-visible", "--ack-pc-vr-stream-start-intent"])
            self.assertEqual(error.exception.code, 2)
            constructor.assert_not_called(); fake.arm.assert_not_called()

    def test_legacy_visible_descriptor_and_receipt_still_restore(self):
        self.plan["operator_arm"] = self.session.operator_arm_descriptor(schema=M.LEGACY_ARM_RECEIPT_SCHEMA)
        self.plan["observer"].pop("raw_trace_sampling")
        self.save_plan(); self.live_fixture()
        self.journal_header.pop("raw_trace_sampling")
        self.save_live_records([self.header], [self.journal_header])
        result = self.session.arm(stream_ready=True)
        self.assertEqual(result["schema"], M.LEGACY_ARM_RECEIPT_SCHEMA)
        self.assertTrue(result["stream_ready_acknowledged"])
        self.assertNotIn("stream_selection_phase", result)
        self.assertNotIn("stream_start_intent_acknowledged", result)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertEqual(json.loads((self.session.folder / "archive" / self.session.arm_receipt.name).read_bytes()), result)

    def test_v2_sampling_metadata_must_be_exact_before_staging(self):
        original = copy.deepcopy(self.plan)
        variants = (None, {}, dict(M.RAW_TRACE_SAMPLING, intentional=False),
            dict(M.RAW_TRACE_SAMPLING, draw_call_limit=33), dict(M.RAW_TRACE_SAMPLING, draw_call_limit=32.0),
            dict(M.RAW_TRACE_SAMPLING, execute_child_limit=17), dict(M.RAW_TRACE_SAMPLING, submit_command_limit=17),
            dict(M.RAW_TRACE_SAMPLING, binding_snapshots_require_import_request=False),
            dict(M.RAW_TRACE_SAMPLING, lifecycle_survives_trace_cap=False),
            dict(M.RAW_TRACE_SAMPLING, lifecycle_survives_trace_cap=1), dict(M.RAW_TRACE_SAMPLING, foreign_cap=1))
        for metadata in variants:
            self.plan = copy.deepcopy(original)
            if metadata is None: self.plan["observer"].pop("raw_trace_sampling")
            else: self.plan["observer"]["raw_trace_sampling"] = metadata
            self.save_plan()
            with self.subTest(metadata=metadata), self.assertRaisesRegex(RuntimeError, "sampling metadata.*observer"):
                self.stage()
            self.assertFalse(self.session.folder.exists())
            self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
            self.assertEqual(self.restart_count(), 0)

    def test_actual_v2_sampling_marker_checks_bounded_hidden_object_and_file_bytes(self):
        original = self.artifacts[M.MAIN].read_bytes()
        self.session.validate_trace_sampling_marker(original, self.plan)
        variants = []
        for offset, content in ((160, b"FCMSMP00"), (280, struct.pack("<I", 0)),
                (284, b"\x02"), (284, b"\x11"), (288, struct.pack("<Q", 0x10a8)),
                (296, struct.pack("<Q", 31)), (516, struct.pack("<I", 11)),
                (392, struct.pack("<Q", 3))):
            raw = bytearray(original); raw[offset:offset + len(content)] = content; variants.append(bytes(raw))
        for flags in (5, 6, 7):
            raw = bytearray(original); struct.pack_into("<I", raw, 68, flags); variants.append(bytes(raw))
        raw = bytearray(original); struct.pack_into("<Q", raw, 96, 176); variants.append(bytes(raw))
        raw = bytearray(original); struct.pack_into("<Q", raw, 40, len(raw) - 1); variants.append(bytes(raw))
        raw = bytearray(original); struct.pack_into("<H", raw, 60, 4097); variants.append(bytes(raw))
        raw = bytearray(original); raw.extend(original[64:120] * 2)
        struct.pack_into("<Q", raw, 32, len(original)); struct.pack_into("<H", raw, 56, 2); variants.append(bytes(raw))
        for index, raw in enumerate(variants):
            with self.subTest(index=index), self.assertRaisesRegex(RuntimeError, "[Tt]race sampling"):
                self.session.validate_trace_sampling_marker(raw, self.plan)
        for key, value in (("rva", True), ("rva", 0x1080), ("bytes", 32.0), ("bytes", 31),
                ("sha256", "f" * 64), ("symbol", "frame_compositor_raw_scope")):
            plan = copy.deepcopy(self.plan); plan["observer"]["trace_sampling_marker"][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "[Tt]race sampling"):
                self.session.validate_trace_sampling_marker(original, plan)
        hidden = bytearray(original); hidden[284:286] = bytes((0x11, 2))
        self.session.validate_trace_sampling_marker(bytes(hidden), self.plan)

    def test_old_acquisition_artifact_cannot_be_relabelled_as_v2_sampling_before_stage(self):
        old = bytearray(self.artifacts[M.MAIN].read_bytes()); old[160:192] = bytes(32)
        SessionTests.alter_artifact(self, M.MAIN, bytes(old))
        with self.assertRaisesRegex(RuntimeError, "Actual trace sampling marker changed"):
            self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())

    def test_missing_v2_sampling_object_report_is_refused_before_stage(self):
        self.plan["observer"].pop("trace_sampling_marker"); self.save_plan()
        with self.assertRaisesRegex(RuntimeError, "actual observer trace sampling marker"):
            self.stage()
        self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_old_actual_sampling_artifact_refuses_activation_without_blocking_recovery(self):
        self.stage()
        old = bytearray(self.session.library.read_bytes()); old[160:192] = bytes(32)
        self.session.library.unlink(); M.exclusive(self.session.library, bytes(old))
        manifest_path = self.session.folder / "owned.json"
        manifest = json.loads(manifest_path.read_bytes()); manifest["owned_files"][M.MAIN] = M.digest(old)
        manifest_path.unlink(); M.exclusive(manifest_path, M.json_bytes(manifest))
        with self.assertRaisesRegex(RuntimeError, "Actual trace sampling marker changed"):
            self.activate()
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 0)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 1)

    def test_legacy_sampling_guard_does_not_require_the_new_artifact_marker(self):
        plan = copy.deepcopy(self.plan)
        plan["operator_arm"] = self.session.operator_arm_descriptor(schema=M.LEGACY_ARM_RECEIPT_SCHEMA)
        plan["observer"].pop("trace_sampling_marker")
        self.session.validate_trace_sampling_marker(self.elf(6), plan)

    def test_live_v2_journal_sampling_must_match_reviewed_report_before_arming(self):
        self.live_fixture(); prior = self.restart_count()
        for metadata in (None, {}, dict(M.RAW_TRACE_SAMPLING, intentional=1),
                dict(M.RAW_TRACE_SAMPLING, draw_call_limit=512), dict(M.RAW_TRACE_SAMPLING, execute_child_limit=16.0),
                dict(M.RAW_TRACE_SAMPLING, submit_command_limit=17),
                dict(M.RAW_TRACE_SAMPLING, binding_snapshots_require_import_request=False),
                dict(M.RAW_TRACE_SAMPLING, lifecycle_survives_trace_cap=False),
                dict(M.RAW_TRACE_SAMPLING, unknown_flag=True)):
            header = dict(self.journal_header)
            if metadata is None: header.pop("raw_trace_sampling")
            else: header["raw_trace_sampling"] = metadata
            self.save_live_records([self.header], [header])
            with self.subTest(metadata=metadata), self.assertRaisesRegex(RuntimeError, "sampling metadata.*protected journal header"):
                self.session.arm(stream_start_intent=True)
            self.assertFalse(self.session.marker.exists()); self.assertFalse(self.session.arm_receipt.exists())
            self.assertEqual(self.restart_count(), prior)

    def test_invalid_stored_v2_sampling_refuses_arm_without_obstructing_native_recovery(self):
        self.live_fixture()
        plan = copy.deepcopy(self.plan)
        plan["observer"]["raw_trace_sampling"]["lifecycle_survives_trace_cap"] = False
        path = self.session.folder / "deployment-plan.json"
        path.unlink(); M.exclusive(path, M.json_bytes(plan))
        manifest_path = self.session.folder / "owned.json"
        manifest = json.loads(manifest_path.read_bytes()); manifest["plan_sha256"] = M.digest(path.read_bytes())
        manifest["owned_files"]["deployment-plan.json"] = manifest["plan_sha256"]
        manifest_path.unlink(); M.exclusive(manifest_path, M.json_bytes(manifest))
        with self.assertRaisesRegex(RuntimeError, "sampling metadata.*observer"):
            self.session.arm(stream_start_intent=True)
        self.assertFalse(self.session.marker.exists()); self.assertFalse(self.session.arm_receipt.exists())
        self.assertEqual(self.restart_count(), 1)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())

    def test_legacy_descriptor_cannot_silently_admit_pre_stream_intent(self):
        self.plan["operator_arm"] = self.session.operator_arm_descriptor(schema=M.LEGACY_ARM_RECEIPT_SCHEMA)
        self.save_plan(); self.live_fixture(); before = list(self.calls)
        with self.assertRaisesRegex(RuntimeError, "separately reviewed v2"):
            self.session.arm(stream_start_intent=True)
        self.session.current_arm_health.assert_not_called()
        self.assertEqual(self.calls, before)
        self.assertFalse(self.session.marker.exists()); self.assertFalse(self.session.arm_receipt.exists())

    def test_pre_stream_receipt_is_preserved_after_native_recovery(self):
        self.live_fixture(); result = self.session.arm(stream_start_intent=True)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(json.loads((self.session.folder / "archive" / self.session.arm_receipt.name).read_bytes()), result)

    def test_inconsistent_pre_stream_receipt_is_refused_after_native_recovery(self):
        self.live_fixture(); result = self.session.arm(stream_start_intent=True)
        result["stream_ready_acknowledged"] = True
        self.session.arm_receipt.write_bytes(M.json_bytes(result))
        with self.assertRaisesRegex(RuntimeError, "receipt preserved after native recovery"):
            self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(json.loads(self.session.arm_receipt.read_bytes()), result)

    def test_new_receipt_phase_version_and_booleans_cannot_be_relabelled(self):
        self.live_fixture(); result = self.session.arm(stream_start_intent=True)
        payloads = {"owned.json": (self.session.folder / "owned.json").read_bytes(),
            "deployment-plan.json": (self.session.folder / "deployment-plan.json").read_bytes(),
            self.session.marker.name: M.ARM_CONTENTS, self.session.arm_receipt.name: M.json_bytes(result)}
        self.session.validate_arm_archive(payloads)
        for field, value in (("schema", M.LEGACY_ARM_RECEIPT_SCHEMA), ("stream_selection_phase", "visible"),
                ("stream_selection_phase", "connected"), ("stream_ready_acknowledged", 0),
                ("stream_start_intent_acknowledged", 1), ("source_frame_associated", True)):
            changed = dict(result); changed[field] = value
            with self.subTest(field=field, value=value), self.assertRaisesRegex(RuntimeError, "preserved after native recovery"):
                self.session.validate_arm_archive(dict(payloads, **{self.session.arm_receipt.name: M.json_bytes(changed)}))
        self.assertEqual(self.session.marker.read_bytes(), M.ARM_CONTENTS)
        self.assertEqual(json.loads(self.session.arm_receipt.read_bytes()), result)

    def test_pre_stream_intent_still_requires_healthy_passed_invocation(self):
        self.live_fixture(); prior = self.restart_count()
        self.session.current_arm_health.side_effect = RuntimeError("Current compositor/UI health or invocation changed")
        with self.assertRaisesRegex(RuntimeError, "health or invocation changed"):
            self.session.arm(stream_start_intent=True)
        self.assertFalse(self.session.marker.exists()); self.assertFalse(self.session.arm_receipt.exists())
        self.assertEqual(self.restart_count(), prior)

    def test_pre_stream_intent_does_not_extend_expired_absolute_deadline(self):
        self.live_fixture(); self.health_source.clock = lambda: 121.0
        with self.assertRaisesRegex(RuntimeError, "deadline|expired"):
            self.session.arm(stream_start_intent=True)
        self.assertFalse(self.session.marker.exists()); self.assertFalse(self.session.arm_receipt.exists())
        self.assertEqual(self.header["capture_deadline_ns"], 121000000000)
        self.assertEqual(self.restart_count(), 1)

    def test_pre_stream_receipt_write_fault_cannot_retry_or_rearm_as_visible(self):
        self.live_fixture(); original = M.exclusive
        def failing(path, value):
            if path == self.session.arm_receipt: raise OSError("receipt storage unavailable")
            return original(path, value)
        with mock.patch.object(M, "exclusive", side_effect=failing), self.assertRaises(OSError):
            self.session.arm(stream_start_intent=True)
        self.assertEqual(self.session.marker.read_bytes(), M.ARM_CONTENTS)
        for kwargs in ({"stream_start_intent": True}, {"stream_ready": True}):
            with self.subTest(kwargs=kwargs), self.assertRaisesRegex(RuntimeError, "single-use"):
                self.session.arm(**kwargs)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())

    def test_missing_arm_descriptor_or_legacy_unarmed_artifact_cannot_stage(self):
        original = copy.deepcopy(self.plan)
        variants = (None, {}, dict(original["operator_arm"], required=False),
            dict(original["operator_arm"], required=1), dict(original["operator_arm"], marker_path="/foreign"),
            dict(original["operator_arm"], deadline_reset_allowed=True),
            dict(original["operator_arm"], marker_sha256="f" * 64),
            dict(original["operator_arm"], pc_vr_stream_acknowledgment_required=False),
            dict(original["operator_arm"], allowed_stream_selection_phases=["visible"]),
            dict(original["operator_arm"], allowed_stream_selection_phases=["visible", "start-intent", "automatic"]),
            dict(original["operator_arm"], schema="frame-compositor-operator-arm-v3"))
        for descriptor in variants:
            self.plan = copy.deepcopy(original)
            if descriptor is None: self.plan.pop("operator_arm")
            else: self.plan["operator_arm"] = descriptor
            self.save_plan()
            with self.subTest(descriptor=descriptor), self.assertRaisesRegex(RuntimeError, "operator-arm descriptor"):
                self.stage()
            self.assertFalse(self.session.folder.exists()); self.assertEqual(self.restart_count(), 0)

    def test_staged_marker_or_captures_cannot_be_reused_on_activation(self):
        self.stage()
        for path in (self.session.marker, self.session.arm_receipt, self.session.capture, self.session.journal):
            M.exclusive(path, b"stale")
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, "cannot reset"):
                self.activate()
            path.unlink()
        self.assertEqual(self.restart_count(), 0)

    def test_stage_refuses_existing_arm_marker_before_any_installation_mutation(self):
        M.private_parents(self.session.folder)
        M.exclusive(self.session.marker, M.ARM_CONTENTS)
        original = {path.name: path.read_bytes() for path in self.session.folder.iterdir()}
        with self.assertRaisesRegex(RuntimeError, "cannot reset"):
            self.stage()
        self.assertEqual({path.name: path.read_bytes() for path in self.session.folder.iterdir()}, original)
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())
        self.assertFalse((self.session.folder / "operation.lock").exists())
        self.assertEqual(self.restart_count(), 0)

    def test_prearm_journal_accepts_only_actual_complete_shader_and_pipeline_lifecycle_events(self):
        self.live_fixture()
        source = (Path(__file__).resolve().parents[1] / "layer/passive_pipeline_trace.cpp").read_text()
        serialized_names = ('"shader", "pipeline", "draw_recorded", "execute_recorded",',
            '"submit_accepted", "incomplete", "bindings_recorded", "bindings_armed"')
        for text in serialized_names: self.assertIn(text, source)
        for event in ("shader", "pipeline"):
            rows = [self.journal_header, {"event": event, "complete": True, "reason": 0}]
            evidence = self.session.validate_prearm_outputs([self.header], rows, self.session.manifest(), self.current)
            self.assertTrue(evidence["journal_no_recorded_loss"])
        forbidden = ("draw_recorded", "execute_recorded", "submit_accepted", "incomplete", "bindings_recorded",
            "bindings_armed", "header", "footer", "draw", "binding_snapshot", "unknown", None)
        for event in forbidden:
            self.save_live_records([self.header], [self.journal_header, {"event": event, "complete": True, "reason": 0}])
            with self.subTest(event=event), self.assertRaisesRegex(RuntimeError, "journal armed"):
                self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists()); self.assertEqual(self.restart_count(), 1)
        for change in ({"complete": False}, {"complete": 1}, {"reason": 1}, {"reason": False},
                {"lost_binding_state": 1}, {"io_ok": False}):
            row = {"event": "shader", "complete": True, "reason": 0, **change}
            self.save_live_records([self.header], [self.journal_header, row])
            with self.subTest(change=change), self.assertRaisesRegex(RuntimeError, "journal armed"):
                self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_raw_deadline_owner_limits_pins_and_retired_states_refuse_without_marker(self):
        self.live_fixture()
        variants = (("operator_arm_required", False), ("operator_armed", True), ("pre_arm_private_bytes", 1),
            ("pid", self.pid + 1), ("launch_frozen_owner_pid", self.pid + 1), ("private_byte_limit", 758),
            ("seconds_limit", 121), ("capture_deadline_ns", 3000000000),
            ("capture_started_ns", 4000000000), ("capture_deadline_ns", 121000000001),
            ("launch_frozen_observed_ns", 1000000001), ("modules", []), ("capture_started_ns", True))
        for field, value in variants:
            bad = dict(self.header); bad[field] = value
            self.save_live_records([bad], [self.journal_header])
            with self.subTest(field=field), self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())
        for row in ({"schema": 1, "end": "deadline"}, {"schema": 1, "operator_armed": True},
                {"schema": 1, "kind": 3, "refusal": 0}, {"schema": 1, "kind": 8, "refusal": 12}):
            self.save_live_records([self.header, row], [self.journal_header])
            with self.subTest(row=row), self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_journal_loss_footer_prearm_draws_and_wrong_protocol_refuse(self):
        self.live_fixture()
        for row in ({"event": "footer"}, {"event": "bindings_armed"}, {"event": "draw"},
                {"event": "submit_accepted"}, {"event": "state", "lost_binding_state": 1},
                {"event": "state", "complete": False}):
            self.save_live_records([self.header], [self.journal_header, row])
            with self.subTest(row=row), self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())
        for field, value in (("binding_arm_required", False), ("binding_ready_at_header", True),
                ("deadline_seconds", 121), ("pre_arm_draws_recorded", True)):
            bad = dict(self.journal_header); bad[field] = value
            self.save_live_records([self.header], [bad])
            with self.subTest(field=field), self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())

    def test_closed_or_wrong_owner_writer_cannot_be_armed(self):
        self.live_fixture()
        rows = (self.proc / "locks").read_text()
        (self.proc / "locks").write_text(rows.replace(f"WRITE {self.pid} ", f"WRITE {self.pid + 1} "))
        with self.assertRaisesRegex(RuntimeError, "admitted compositor"): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())
        (self.proc / "locks").write_text(rows)
        fcntl.flock(self.descriptors[1], fcntl.LOCK_UN)
        with self.assertRaisesRegex(RuntimeError, "writer is closed"): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())

    def test_mapping_missing_changed_or_pid_reused_refuses(self):
        self.live_fixture()
        process = self.proc / str(self.pid); maps = (process / "maps").read_text()
        for content in ("", maps.replace(str(self.session.library), "/other.so"),
                maps.replace(" r-xp ", " rw-p ")):
            (process / "maps").write_text(content)
            with self.subTest(content=content), self.assertRaises(RuntimeError): self.session.arm(stream_ready=True)
            self.assertFalse(self.session.marker.exists())
        (process / "maps").write_text(maps)
        original = (process / "stat").read_text()
        (process / "stat").write_text(original.replace("500", "501"))
        with self.assertRaisesRegex(RuntimeError, "PID/start"): self.session.arm(stream_ready=True)
        self.assertFalse(self.session.marker.exists())

    def test_arm_marker_or_receipt_foreign_nodes_are_not_replaced(self):
        self.live_fixture()
        for path in (self.session.marker, self.session.arm_receipt):
            os.symlink(self.session.capture, path)
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                self.session.arm(stream_ready=True)
            self.assertTrue(path.is_symlink()); path.unlink()
        self.assertFalse(self.session.marker.exists())

    def test_arm_dynamic_evidence_is_archived_after_safe_original_restart(self):
        self.live_fixture(); result = self.session.arm(stream_ready=True)
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 2)
        self.assertEqual((self.session.folder / "archive" / self.session.marker.name).read_bytes(), M.ARM_CONTENTS)
        self.assertEqual(json.loads((self.session.folder / "archive" / self.session.arm_receipt.name).read_bytes()), result)

    def test_missing_receipt_and_tampered_signal_never_block_authorized_recovery(self):
        self.live_fixture(); self.session.arm(stream_ready=True)
        self.session.arm_receipt.unlink(); self.session.marker.write_bytes(b"foreign")
        with self.assertRaisesRegex(RuntimeError, "marker preserved"):
            self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 2)
        self.assertEqual(self.session.marker.read_bytes(), b"foreign")

    def test_receipt_storage_failure_keeps_a_valid_one_shot_for_safe_recovery(self):
        self.live_fixture(); original = M.exclusive
        def failing(path, value):
            if path == self.session.arm_receipt: raise OSError("receipt storage unavailable")
            return original(path, value)
        with mock.patch.object(M, "exclusive", side_effect=failing), self.assertRaises(OSError):
            self.session.arm(stream_ready=True)
        self.assertEqual(self.session.marker.read_bytes(), M.ARM_CONTENTS)
        with self.assertRaisesRegex(RuntimeError, "single-use"): self.session.arm(stream_ready=True)
        self.session.rollback(restart_ready=True)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.layer.exists())

    def test_current_arm_health_uses_one_fresh_sample_without_wait_or_restart(self):
        self.stage(); self.activate()
        pinned = {"compositor": {"pid": 123, "start_ticks": 500}}
        saved = dict(self.ready_result, invocation="a" * 32, process_identities=pinned,
            launch_freeze_receipt_verified=True, gate_checkpoint={"fixture": True}, source_checkpoint={"fixture": True})
        self.session.health_receipt("activation-startup-health.json", saved)
        gate = mock.Mock(); gate.baseline = {"monotonic": 1.0}
        gate.consume.return_value = dict(saved); gate.checkpoint.return_value = {"fixture": True}
        self.health_source.sample.return_value = {"fresh": True}
        self.health_source.checkpoint.return_value = {"continued": True}
        before = self.restart_count()
        with mock.patch.object(M.health.StartupGate, "resume", return_value=gate):
            result = self.session.current_arm_health()
        gate.consume.assert_called_once_with({"fresh": True}, stable_seconds=0)
        self.assertEqual(result["invocation"], saved["invocation"])
        self.assertEqual(self.restart_count(), before)
        self.assertEqual(json.loads((self.session.folder / "startup-monitor.json").read_bytes())["source_checkpoint"], {"continued": True})

    def test_lost_current_ui_or_new_invocation_cannot_authorize_signal(self):
        self.stage(); self.activate()
        saved = dict(self.ready_result, invocation="a" * 32,
            process_identities={"compositor": {"pid": 123, "start_ticks": 500}},
            launch_freeze_receipt_verified=True, gate_checkpoint={"fixture": True}, source_checkpoint={"fixture": True})
        gate = mock.Mock(); gate.baseline = {"monotonic": 1.0}; gate.checkpoint.return_value = {"fixture": True}
        self.health_source.checkpoint.return_value = {"continued": True}
        for change in ({"ready": False}, {"isolation_verified": False}, {"invocation": "b" * 32},
                {"process_identities": {"compositor": {"pid": 124, "start_ticks": 501}}}):
            self.session.health_receipt("activation-startup-health.json", saved)
            monitor = self.session.folder / "startup-monitor.json"
            if monitor.exists(): monitor.unlink()
            gate.consume.return_value = dict(saved, **change)
            with self.subTest(change=change), mock.patch.object(M.health.StartupGate, "resume", return_value=gate), \
                    self.assertRaisesRegex(RuntimeError, "health or invocation changed"):
                self.session.current_arm_health()
            self.assertFalse(self.session.marker.exists())

    def test_failed_arm_sample_is_recorded_and_cannot_reuse_passed_budget(self):
        self.stage(); self.activate()
        saved = dict(self.ready_result, invocation="a" * 32,
            process_identities={"compositor": {"pid": 123, "start_ticks": 500}},
            launch_freeze_receipt_verified=True, gate_checkpoint={"fixture": True}, source_checkpoint={"fixture": True})
        self.session.health_receipt("activation-startup-health.json", saved)
        gate = mock.Mock(); gate.baseline = {"monotonic": 1.0}
        gate.result.return_value = dict(saved, status="not-ready", ready=False, reason="evidence refused")
        gate.checkpoint.return_value = {"failed": "recorded"}
        self.health_source.sample.side_effect = RuntimeError("Startup evidence byte bound exceeded")
        self.health_source.checkpoint.return_value = {"total": 457829}
        before = self.restart_count()
        with mock.patch.object(M.health.StartupGate, "resume", return_value=gate), \
                self.assertRaisesRegex(RuntimeError, "health or invocation changed"):
            self.session.current_arm_health()
        recorded = json.loads((self.session.folder / "startup-monitor.json").read_bytes())
        self.assertFalse(recorded["ready"])
        self.assertEqual(recorded["source_checkpoint"], {"total": 457829})
        self.assertIn("evidence byte bound", gate.failed)
        self.assertFalse(self.session.marker.exists())
        self.assertEqual(self.restart_count(), before)
        calls = self.health_source.sample.call_count
        with self.assertRaisesRegex(RuntimeError, "Passed cold-freeze startup"):
            self.session.current_arm_health()
        self.assertEqual(self.health_source.sample.call_count, calls)

    def test_large_unterminated_log_still_persists_denied_arm_checkpoint(self):
        self.stage(); self.activate()
        saved = dict(self.ready_result, invocation="a" * 32,
            process_identities={"compositor": {"pid": 123, "start_ticks": 500}},
            launch_freeze_receipt_verified=True, gate_checkpoint={"fixture": True}, source_checkpoint={"fixture": True})
        self.session.health_receipt("activation-startup-health.json", saved)
        actual = M.health.NativeEvidenceSource(self.home)
        actual.cursor = "s=fixture"
        for name, path in actual.logs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b""); actual.log_read(name, initial=True)
        actual.logs["vrserver"].write_bytes(b"x" * (300 * 1024))
        gate = mock.Mock(); gate.baseline = {"monotonic": 1.0}
        gate.result.return_value = dict(saved, status="not-ready", ready=False, reason="line bound exceeded")
        gate.checkpoint.return_value = {"failed": "recorded"}
        self.health_source.sample.side_effect = lambda: {"logs": {"vrserver": actual.log_read("vrserver")}}
        self.health_source.checkpoint.side_effect = actual.checkpoint
        before = self.restart_count()
        with mock.patch.object(M.health.StartupGate, "resume", return_value=gate), \
                self.assertRaisesRegex(RuntimeError, "health or invocation changed"):
            self.session.current_arm_health()
        path = self.session.folder / "startup-monitor.json"
        self.assertLess(path.stat().st_size, 262144)
        recorded = json.loads(path.read_bytes())
        self.assertFalse(recorded["ready"])
        self.assertEqual(recorded["source_checkpoint"]["total"], 300 * 1024)
        self.assertEqual(recorded["source_checkpoint"]["pending_lines"]["vrserver"], "")
        self.assertIn("line bound exceeded", gate.failed)
        with self.assertRaisesRegex(RuntimeError, "Passed cold-freeze startup"):
            self.session.current_arm_health()
        self.assertEqual(self.health_source.sample.call_count, 1)
        self.assertFalse(self.session.marker.exists())
        self.assertEqual(self.restart_count(), before)

class NativeLayerTests(unittest.TestCase):
    setUp, tearDown = SessionTests.setUp, SessionTests.tearDown
    elf, profile = staticmethod(SessionTests.elf), staticmethod(SessionTests.profile)
    layer_bytes, save_plan, command = SessionTests.layer_bytes, SessionTests.save_plan, SessionTests.command
    stage, activate, restart_count = SessionTests.stage, SessionTests.activate, SessionTests.restart_count

    def native_layer(self, name="native.json", document=None):
        M.private_parents(self.session.layer.parent)
        path = self.session.layer.parent / name
        raw = M.json_bytes(document or {"file_format_version": "1.0.0", "layer": {
            "name": "VK_LAYER_VENDOR_native", "type": "GLOBAL", "library_path": "/usr/lib/libNativeLayer.so",
            "enable_environment": {"ENABLE_NATIVE_LAYER": "1"}}})
        M.exclusive(path, raw); path.chmod(0o644)
        return path, {str(path): M.digest(raw)}

    def test_native_layer_node_only_rewrite_allows_original_restart_and_is_recorded(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed); self.activate()
        original = path.read_bytes(); original_inode = path.stat().st_ino
        path.rename(self.root / "retired-native-layer")
        M.exclusive(path, original); path.chmod(0o644)
        rewritten_inode = path.stat().st_ino
        self.assertNotEqual(original_inode, rewritten_inode)
        self.assertTrue(self.session.readcheck()["active"])
        self.session.rollback(restart_ready=True)
        result = json.loads((self.session.folder / "rollback-complete.json").read_bytes())
        self.assertFalse(result["native_layer_nodes_exact"])
        self.assertIn("ino", result["native_layer_node_drift"][str(path)]["changed_node_fields"])
        self.assertTrue(result["startup_health_ready"])
        self.assertEqual(path.read_bytes(), original); self.assertEqual(path.stat().st_ino, rewritten_inode)
        self.assertEqual(self.restart_count(), 2)
        self.assertFalse(self.session.readcheck()["active"])

    def test_recovery_rejects_changed_layer_content_mode_owner_links_or_gate(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed); self.activate()
        raw = path.read_bytes(); info = path.stat()
        for index, variant in enumerate(("content", "mode", "links", "gate")):
            if variant == "content": path.write_bytes(raw + b" ")
            if variant == "mode": path.chmod(0o600)
            if variant == "links": os.link(path, self.root / "extra-layer-link")
            if variant == "gate": self.manager += "ENABLE_NATIVE_LAYER=1\n"
            with self.subTest(variant=variant), self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
            self.assertEqual(self.restart_count(), index + 2)
            self.assertTrue(path.exists()); self.assertFalse((self.session.folder / "rollback-complete.json").exists())
            if variant == "content": path.write_bytes(raw)
            if variant == "mode": path.chmod(info.st_mode & 0o777)
            if variant == "links": (self.root / "extra-layer-link").unlink()
            if variant == "gate": self.manager = "NATIVE_ENV=yes\n"
        self.session.rollback(restart_ready=True)
        self.assertEqual(path.read_bytes(), raw)

    def test_startup_health_receipts_survive_the_rollback_archive(self):
        self.stage(); self.activate()
        self.session.health_receipt("startup-monitor.json", {"ready": False, "reason": "UI lost"})
        self.session.rollback(restart_ready=True)
        archived = json.loads((self.session.folder / "archive/expected.json").read_bytes())
        for name in M.STARTUP_RECEIPTS:
            self.assertIn(name, archived)
            self.assertEqual((self.session.folder / name).read_bytes(), (self.session.folder / "archive" / name).read_bytes())

    def test_native_layer_exact_reviewed_baseline_preserved(self):
        path, reviewed = self.native_layer(); original = path.read_bytes(); before = path.stat()
        self.stage(reviewed_native_layers=reviewed)
        baseline = self.session.manifest()["original_native_layers"][str(path)]
        self.assertEqual(baseline["sha256"], reviewed[str(path)])
        self.assertEqual(baseline["node"]["ino"], before.st_ino)
        self.assertEqual(bytes.fromhex(baseline["bytes_hex"]), original)
        self.activate(); self.assertTrue(self.session.readcheck()["active"])
        self.session.rollback(restart_ready=True); self.assertFalse(self.session.readcheck()["active"])
        self.assertEqual(path.read_bytes(), original); self.assertEqual(path.stat().st_ino, before.st_ino)
        self.assertEqual(path.stat().st_mode, before.st_mode)

    def test_native_layer_review_required_no_filename_whitelist(self):
        path, _ = self.native_layer("steamfossilize_arm64.json")
        with self.assertRaises(RuntimeError): self.stage()
        self.assertTrue(path.exists()); self.assertFalse(self.session.folder.exists())

    def test_native_layer_wrong_or_extra_digest_refused(self):
        path, reviewed = self.native_layer()
        for value in ("0" * 64, "not-a-sha", "", None):
            with self.subTest(value=value), self.assertRaises(RuntimeError): self.stage(reviewed_native_layers={str(path): value})
        reviewed[str(self.session.layer.parent / "missing.json")] = "0" * 64
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)

    def test_native_layer_symlink_targets_are_never_followed(self):
        path, reviewed = self.native_layer(); original = path.read_bytes(); path.unlink()
        target = self.root / "native-target.json"; M.exclusive(target, original)
        for selected in (target, self.root / "missing-target", self.artifacts[M.MAIN]):
            path.symlink_to(selected)
            with self.subTest(target=selected), self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)
            path.unlink()
        self.assertEqual(target.read_bytes(), original)

    def test_native_layer_nonregular_or_shared_nodes_refused(self):
        path, reviewed = self.native_layer(); original = path.read_bytes(); path.unlink(); path.mkdir()
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)
        path.rmdir(); os.mkfifo(path)
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)
        path.unlink(); M.exclusive(path, original); os.link(path, self.root / "shared-layer")
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)

    def test_native_layer_world_writable_refused(self):
        path, reviewed = self.native_layer(); path.chmod(0o666)
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)

    def test_native_layer_enable_gate_must_be_absent_in_all_scopes(self):
        path, reviewed = self.native_layer()
        for where in ("caller", "manager", "service", "file"):
            self.session.environment = {}; self.manager = "NATIVE_ENV=yes\n"
            self.original["Environment"] = "NATIVE_SETTING=unchanged"; self.original["EnvironmentFiles"] = ""
            opts = {"reviewed_native_layers": reviewed}
            if where == "caller": self.session.environment = {"ENABLE_NATIVE_LAYER": ""}
            if where == "manager": self.manager += "ENABLE_NATIVE_LAYER=1\n"
            if where == "service": self.original["Environment"] += " ENABLE_NATIVE_LAYER=1"
            if where == "file":
                source = self.root / "native-enable-env"; M.exclusive(source, b"ENABLE_NATIVE_LAYER=1\n")
                self.original["EnvironmentFiles"] = str(source) + " (ignore_errors=no)"
                opts["reviewed_environment_files"] = {str(source): M.digest(source.read_bytes())}
            with self.subTest(where=where), self.assertRaises(RuntimeError): self.stage(**opts)
        self.assertTrue(path.exists())

    def test_native_layer_gate_or_diagnostic_injection_refused(self):
        path, _ = self.native_layer(); path.unlink()
        for changed in ("missing_gate", "diagnostic_name", "diagnostic_library", "diagnostic_gate", "malformed"):
            document = {"file_format_version": "1.0.0", "layer": {"name": "VK_LAYER_VENDOR_native",
                "library_path": "/usr/lib/libNativeLayer.so", "enable_environment": {"ENABLE_NATIVE_LAYER": "1"}}}
            if changed == "missing_gate": document["layer"].pop("enable_environment")
            if changed == "diagnostic_name": document["layer"]["name"] = "VK_LAYER_FRAME_MASK_trace"
            if changed == "diagnostic_library": document["layer"]["library_path"] = str(self.session.library)
            if changed == "diagnostic_gate": document["layer"]["enable_environment"] = {"FRAME_MASK_TRACE": "1"}
            if changed == "malformed": document["layer"] = []
            _, reviewed = self.native_layer(document=document)
            with self.subTest(changed=changed), self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)
            path.unlink()

    def test_native_layer_changes_prevent_activation(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed)
        before = path.read_bytes(); path.write_bytes(before + b" ")
        with self.assertRaises(RuntimeError): self.activate()
        self.assertEqual(self.restart_count(), 0); self.assertFalse(self.session.dropin.exists())

    def test_native_layer_removed_or_new_entry_prevents_activation(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed)
        original = path.read_bytes(); path.unlink()
        with self.assertRaises(RuntimeError): self.activate()
        M.exclusive(path, original); extra = self.session.layer.parent / "extra.json"; M.exclusive(extra, original)
        with self.assertRaises(RuntimeError): self.activate()
        self.assertTrue(extra.exists())

    def test_native_layer_equal_content_replacement_is_not_same_node(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed)
        original = path.read_bytes(); path.rename(self.root / "old-layer")
        M.exclusive(path, original); path.chmod(0o644)
        with self.assertRaises(RuntimeError): self.activate()

    def test_native_layer_change_active_readcheck_refused(self):
        path, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed); self.activate()
        path.write_bytes(path.read_bytes() + b" ")
        with self.assertRaises(RuntimeError): self.session.readcheck()
        with self.assertRaises(RuntimeError): self.session.rollback()
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists()); self.assertTrue(path.exists())

    def test_native_layer_enable_variable_after_activation_refused(self):
        _, reviewed = self.native_layer(); self.stage(reviewed_native_layers=reviewed); self.activate()
        self.manager += "ENABLE_NATIVE_LAYER=1\n"
        with self.assertRaises(RuntimeError): self.session.readcheck()
        with self.assertRaises(RuntimeError): self.session.rollback()
        self.assertFalse(self.session.dropin.exists()); self.assertFalse(self.session.layer.exists())

    def test_native_layer_live_diagnostic_is_not_native_baseline(self):
        path, reviewed = self.native_layer(); reviewed[str(self.session.layer)] = "0" * 64
        with self.assertRaises(RuntimeError): self.stage(reviewed_native_layers=reviewed)
        self.assertTrue(path.exists())

if __name__ == "__main__": unittest.main()
