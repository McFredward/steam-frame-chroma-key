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
            "observer": {"sha256": M.digest(main), "bytes": len(main), "public_exports": sorted(M.PUBLIC_EXPORTS),
                "vulkan_functions": dict(M.VULKAN_FUNCTIONS), "standard_vulkan_exports": []},
            "audit": {"sha256": M.digest(audit), "bytes": len(audit), "native_rvas": dict(M.NATIVE_RVAS), "early_forward_only": True},
            "envelope": {"sha256": M.digest(envelope), "bytes": len(envelope), "schema": 1, "module_count": 4,
                "module_roles": [7, 3, 4, 5], "reviewed_observer_profile_identity": profiles[-1][-32:].hex()},
            "manifest": {"sha256": M.digest(layer), "bytes": len(layer), "library_path": str(self.session.library),
                "functions": dict(M.VULKAN_FUNCTIONS),
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
