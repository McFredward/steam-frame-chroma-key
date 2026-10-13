"""Local, mocked service/session controls; no Frame access or native reads.

Only this fixture replaces native/profile pins. Production has no bypass flag.
"""
import copy
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

class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="compositor-raw-session-fixture-")
        self.root = Path(self.temp.name)
        self.home, self.runtime = self.root / "home", self.root / "runtime"
        self.home.mkdir(mode=0o700); self.runtime.mkdir(mode=0o700)
        self.calls, self.fail_restart, self.interrupt_restart = [], False, False
        self.original = {"Environment": "NATIVE_SETTING=unchanged", "EnvironmentFiles": "", "DropInPaths": ""}
        self.manager = "NATIVE_ENV=yes\n"
        self.session = M.Session(self.home, self.runtime, self.command, environment={})
        self.native, self.artifacts, self.patches = {}, {}, []
        for module, name in ((M, M.HELPERS[0]), (M.base, M.HELPERS[1])):
            target = self.root / name; M.exclusive(target, Path(module.__file__).read_bytes())
            patch = mock.patch.object(module, "__file__", str(target)); patch.start(); self.patches.append(patch)
        pins, profiles, reports = {}, [], []
        for i, ((role, (name, _)), numeric) in enumerate(zip(M.NATIVE.items(), (7, 3, 4))):
            data = self.elf(i + 1); path = self.root / name; M.exclusive(path, data); self.native[role] = path
            pins[role] = (name, M.digest(data)); profile = self.profile(numeric, data); profiles.append(profile)
            reports.append({"role": numeric, "file_sha256": M.digest(data), "profile_sha256": profile[-32:].hex()})
        for key, value in (("NATIVE", pins), ("PROFILE_PINS", tuple(p[-32:].hex() for p in profiles))):
            patch = mock.patch.object(M, key, value); patch.start(); self.patches.append(patch)
        main, audit = self.elf(7), self.elf(8); profiles.append(self.profile(5, main))
        envelope = bytearray(8272); envelope[:8] = b"FCMRAW01"
        struct.pack_into("<II", envelope, 8, 1, 8272); envelope[16:48] = profiles[-1][-32:]
        struct.pack_into("<8Q", envelope, 48, *M.ENVELOPE_LIMITS)
        envelope[112:-32] = b"".join(profiles); envelope[-32:] = bytes.fromhex(M.digest(envelope[:-32]))
        layer = self.layer_bytes()
        for name, data in ((M.MAIN, main), (M.AUDIT, audit), (M.ENVELOPE, envelope), (M.LAYER, layer)):
            path = self.root / name; M.exclusive(path, bytes(data)); self.artifacts[name] = path
        self.plan = {"schema": M.SCHEMA, "prepared": True, "frame_directory": str(self.session.folder),
            "frame_user_home": str(self.home), "directory_mode": "0700", "environment": self.session.activation_values(), "native_profiles": reports,
            "observer_profile": {"role": 5, "file_sha256": M.digest(main), "profile_sha256": profiles[-1][-32:].hex()},
            "observer": {"sha256": M.digest(main), "bytes": len(main)},
            "audit": {"sha256": M.digest(audit), "bytes": len(audit), "native_rvas": dict(M.NATIVE_RVAS), "early_forward_only": True},
            "envelope": {"sha256": M.digest(envelope), "bytes": len(envelope), "schema": 1, "module_count": 4,
                "module_roles": [7, 3, 4, 5], "reviewed_observer_profile_identity": profiles[-1][-32:].hex()},
            "manifest": {"sha256": M.digest(layer), "bytes": len(layer), "library_path": str(self.session.library),
                "staged_path": str(self.session.folder / M.LAYER), "frame_path": str(self.session.layer)},
            "capture_limits": dict(M.CAPTURE_LIMITS), "raw_limits": dict(M.RAW_LIMITS), "journal_limits": dict(M.JOURNAL_LIMITS), "inventory": []}
        for flag in ("installed", "activated", "runtime_authorized", "frame_access", "network_access",
            "native_binary_or_script_changes", "shader_or_mask_activation", "source_frame_associated", "gpu_coherent", "automatic_restart", "continuous_connection_owned", "native_namespace_associated"):
            self.plan[flag] = False
        for name in (*self.artifacts, "audit-preparation.cpp"):
            data = self.artifacts[name].read_bytes() if name in self.artifacts else b"offline source"
            self.plan["inventory"].append({"name": name, "bytes": len(data), "sha256": M.digest(data), "frame_path":
                str(self.session.folder / name) if name in self.artifacts else None, "owned_source": True, "native_bytes": False, "mode": "0600"})
        self.plan_path = self.root / "deployment-plan.json"; self.save_plan()
        self.helpers = {M.HELPERS[0]: M.digest(Path(M.__file__).read_bytes()), M.HELPERS[1]: M.digest(Path(M.base.__file__).read_bytes())}

    @staticmethod
    def elf(value): return b"\x7fELF\x02\x01\x01" + b"\0" * 9 + b"\x03\0\xb7\0" + bytes([value]) * 44
    @staticmethod
    def profile(role, data):
        raw = bytearray(2032); struct.pack_into("<5Q", raw, 0, 1, role, 183, 3, len(data))
        raw[176:208] = bytes.fromhex(M.digest(data)); raw[-32:] = bytes.fromhex(M.digest(raw[:-32])); return raw
    def layer_bytes(self):
        return M.json_bytes({"file_format_version": "1.2.0", "layer": {"name": "VK_LAYER_FRAME_MASK_trace", "type": "GLOBAL",
            "library_path": str(self.session.library), "api_version": "1.3.0", "implementation_version": "1",
            "description": "Opt-in bounded passive compositor raw import metadata", "enable_environment": {"FRAME_MASK_TRACE": "1"},
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
        return subprocess.CompletedProcess(args, 0, text, "")
    def stage(self, **kwargs):
        opts = {"runtime_scope": True, "reviewed_helpers": self.helpers}; opts.update(kwargs)
        self.session.stage(self.plan_path, self.sha, self.native, self.artifacts, **opts)
    def activate(self, **kwargs):
        opts = {"runtime_scope": True, "restart_ready": True}; opts.update(kwargs); self.session.activate(**opts)
    def restart_count(self): return sum("restart" in call for call in self.calls)
    def alter_artifact(self, name, raw):
        self.artifacts[name].unlink(); M.exclusive(self.artifacts[name], raw)
        report = {M.MAIN: "observer", M.AUDIT: "audit", M.ENVELOPE: "envelope", M.LAYER: "manifest"}[name]
        self.plan[report].update(sha256=M.digest(raw), bytes=len(raw))
        next(i for i in self.plan["inventory"] if i["name"] == name).update(sha256=M.digest(raw), bytes=len(raw)); self.save_plan()

    def test_stage_does_not_enable_or_restart(self):
        self.stage(); self.assertFalse(self.session.layer.exists()); self.assertFalse(self.session.dropin.exists())
        self.assertEqual(self.restart_count(), 0); self.assertEqual(self.session.folder.stat().st_mode & 0o777, 0o700)
        self.assertFalse(self.session.readcheck()["active"])
        self.assertTrue(self.session.manifest()["scope_acknowledged"])
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
        self.assertEqual(json.loads(self.session.layer.read_bytes())["layer"]["library_path"], self.session.activation_values()["LD_PRELOAD"])
        self.assertTrue(all(call[0:2] == ("systemctl", "--user") for call in self.calls))
        self.assertTrue(all("steam.service" not in call for call in self.calls))
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

if __name__ == "__main__": unittest.main()
