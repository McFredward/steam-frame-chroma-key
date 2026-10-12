"""Owned startup-session safety controls; no Frame, network or real service.

Only these tests substitute canonical hashes with synthetic ELF/profile pins.
The production manager has no fixture or bypass command-line option.
"""
import errno
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

SPEC = importlib.util.spec_from_file_location("startup_session", Path(__file__).resolve().parents[1] / "tools/client_startup_session.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.production_api_pin = M.NATIVE['api'][1]
        self.production_api_profile_pin = M.PROFILE_PINS[1]
        self.temp = tempfile.TemporaryDirectory(prefix="client-startup-session-fixture-")
        self.root = Path(self.temp.name)
        self.home, self.runtime = self.root / "home", self.root / "runtime"
        self.home.mkdir(mode=0o700);self.runtime.mkdir(mode=0o700)
        self.calls, self.fail_restart, self.interrupt_restart = [], False, False
        self.original = {"Environment": "NATIVE_SETTING=unchanged", "EnvironmentFiles": "", "DropInPaths": ""}
        self.manager = "NATIVE_ENV=yes\n"
        self.session = M.Session(self.home, self.runtime, self.command, environment={})
        self.native, self.artifacts, self.patches = {}, {}, []
        # Developer worktrees need not satisfy deployed-helper private-parent
        # rules. Use byte-identical private copies, never relax those rules.
        for module, name in ((M, M.HELPERS[0]), (M.base, M.HELPERS[1])):
            source = Path(module.__file__);target = self.root / name
            M.exclusive(target, source.read_bytes())
            patch = mock.patch.object(module, "__file__", str(target));patch.start();self.patches.append(patch)
        pins, profiles, reports = {}, [], []
        for i, (role, (name, _)) in enumerate(M.NATIVE.items()):
            data = self.elf(i+1)
            path = self.root / name;M.exclusive(path, data);self.native[role] = path
            pins[role] = (name, M.digest(data))
            if i < 4:
                descriptor = self.profile(i+1, data);profiles.append(descriptor)
                reports.append({"role": i+1, "file_sha256": M.digest(data), "profile_sha256": descriptor[-32:].hex()})
        main, audit = self.elf(7), self.elf(8)
        profiles.append(self.profile(5, main));client = bytearray(1064)
        struct.pack_into("<QQ", client, 0, 1, len(self.native["client"].read_bytes()))
        client[72:104] = bytes.fromhex(pins["client"][1]);client[-32:] = bytes.fromhex(M.digest(client[:-32]))
        for key, value in (("NATIVE", pins), ("PROFILE_PINS", tuple(p[-32:].hex() for p in profiles[:4])),
                           ("CLIENT_PROFILE", client[-32:].hex())):
            patch = mock.patch.object(M, key, value);patch.start();self.patches.append(patch)
        envelope = bytearray(11800);envelope[:8] = b"FCSTART1"
        struct.pack_into("<II", envelope, 8, 1, 11800);envelope[16:48] = profiles[-1][-32:]
        name = b"/native/vrclient.so";envelope[48:48+len(name)] = name
        struct.pack_into("<QQ", envelope, 560, 0, 0x60699a)
        envelope[576:10736] = b"".join(profiles);envelope[10736:] = client
        for name, data in ((M.MAIN, main), (M.AUDIT, audit), (M.ENVELOPE, envelope)):
            path = self.root / name;M.exclusive(path, bytes(data));self.artifacts[name] = path
        self.plan = {"schema": M.SCHEMA, "prepared": True, "frame_directory": str(self.session.folder),
            "frame_user_home": str(self.home), "directory_mode": "0700", "environment": self.session.activation_values(),
            "native_profiles": reports, "client_profile": {"file_sha256": pins["client"][1], "profile_sha256": client[-32:].hex()},
            "observer_profile": {"role": 5, "file_sha256": M.digest(main), "profile_sha256": profiles[-1][-32:].hex()},
            "client_slot": {"exact_loader_name": "/native/vrclient.so", "rx_rva_begin": 0, "rx_rva_end": 0x60699a},
            "observer": {"sha256": M.digest(main)}, "audit": {"sha256": M.digest(audit), "native_api_rvas": dict(M.API_ENTRY_RVAS),
                                                               "native_mutex_rvas": dict(M.MUTEX_ENTRY_RVAS)},
            "libc_slot": {"exact_loader_name": "/native/libc.so.6", "rx_rva_begin": 0, "rx_rva_end": 1637944},
            "envelope": {"sha256": M.digest(envelope), "reviewed_observer_profile_identity": profiles[-1][-32:].hex()},
            "inventory": []}
        for flag in ("installed", "activated", "runtime_authorized", "frame_access", "network_access",
                     "native_binary_or_script_changes", "source_frame_associated", "gpu_coherent"):
            self.plan[flag] = False
        for name in (*self.artifacts, "audit-preparation.cpp"):
            data = self.artifacts[name].read_bytes() if name in self.artifacts else b"offline source"
            self.plan["inventory"].append({"name": name, "bytes": len(data), "sha256": M.digest(data),
                "frame_path": str(self.session.folder / name) if name in self.artifacts else None,
                "owned_source": True, "native_bytes": False, "mode": "0600"})
        self.plan_path = self.root / "deployment-plan.json";self.save_plan()
        self.helpers = {M.HELPERS[0]: M.digest(Path(M.__file__).read_bytes()),
                        M.HELPERS[1]: M.digest(Path(M.base.__file__).read_bytes())}

    @staticmethod
    def elf(value):
        return b"\x7fELF\x02\x01\x01" + b"\0"*9 + b"\x03\0\xb7\0" + bytes([value])*44

    @staticmethod
    def profile(role, data):
        raw = bytearray(2032);struct.pack_into("<5Q", raw, 0, 1, role, 183, 3, len(data))
        raw[176:208] = bytes.fromhex(M.digest(data));raw[-32:] = bytes.fromhex(M.digest(raw[:-32]));return raw

    def tearDown(self):
        for patch in reversed(self.patches): patch.stop()
        self.temp.cleanup()

    def save_plan(self):
        if self.plan_path.exists(): self.plan_path.unlink()
        M.exclusive(self.plan_path, M.json_bytes(self.plan));self.sha = M.digest(self.plan_path.read_bytes())

    def command(self, args, **options):
        self.calls.append(tuple(args));text = "active\n"
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
        opts = {"runtime_scope": True, "reviewed_helpers": self.helpers};opts.update(kwargs)
        self.session.stage(self.plan_path, self.sha, self.native, self.artifacts, **opts)

    def start(self, **kwargs):
        opts = {"runtime_scope": True, "restart_ready": True, "reviewed_helpers": self.helpers};opts.update(kwargs)
        self.session.start(self.plan_path, self.sha, self.native, self.artifacts, **opts)

    def restarts(self): return sum("restart" in c for c in self.calls)

    def connection_plan(self):
        path=self.artifacts[M.ENVELOPE];raw=bytearray(path.read_bytes())
        raw[:8]=b'FCCONN01';struct.pack_into('<I',raw,8,2);path.write_bytes(raw)
        sha=M.digest(raw);self.plan['envelope']['sha256']=sha
        for item in self.plan['inventory']:
            if item['name']==M.ENVELOPE:item['sha256']=sha
        self.plan.update(connection_register_observation=True,connection_private_bytes_read=0,continuous_connection_owned=False)
        self.plan['observer']['connection_register_observation']=True
        self.plan['observer']['audit_only_libc_routing']=True
        self.plan['observer']['public_mutex_aliases']={}
        self.plan['audit']['native_connection_rvas']={'lock':'0x87370','unlock':'0x88f74','connect':'0xebcb0'}
        self.save_plan()

    def test_connection_stage_and_activation_require_distinct_scope(self):
        self.connection_plan()
        with self.assertRaisesRegex(RuntimeError,'Separate register-only'):
            self.stage()
        self.assertFalse(self.session.folder.exists());self.assertEqual(self.calls,[])
        self.stage(connect_scope=True)
        with self.assertRaisesRegex(RuntimeError,'Separate register-only'):
            self.session.activate(runtime_scope=True,restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(),0)
        self.session.activate(runtime_scope=True,connect_scope=True,restart_ready=True)
        self.assertEqual(self.restarts(),1)
        self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(),2)

    def test_connection_start_forwards_scope_but_never_claims_lifetime(self):
        self.connection_plan();self.start(connect_scope=True)
        self.assertTrue(self.session.manifest()['connect_register_scope_acknowledged'])
        self.assertEqual(self.restarts(),1)
        self.session.rollback(restart_ready=True)

    def test_connection_scope_cannot_claim_extra_memory_or_lifetime(self):
        for key,value in (('connection_private_bytes_read',8),('continuous_connection_owned',True),('connection_register_observation','true')):
            self.connection_plan();self.plan[key]=value;self.save_plan()
            with self.subTest(key=key),self.assertRaises(RuntimeError):
                self.stage(connect_scope=True)
            self.assertFalse(self.session.folder.exists());self.assertEqual(self.calls,[])

    def test_connection_scope_rejects_old_global_interception_before_staging(self):
        for change in ({'audit_only_libc_routing':False},{'public_mutex_aliases':{'pthread_mutex_lock':0x1000}}):
            self.connection_plan();self.plan['observer'].update(change);self.save_plan()
            with self.subTest(change=change),self.assertRaises(RuntimeError):
                self.stage(connect_scope=True)
            self.assertFalse(self.session.folder.exists());self.assertEqual(self.calls,[])

    def test_missing_scope_and_readiness_cannot_create_any_session(self):
        for kwargs in ({"runtime_scope": False}, {"restart_ready": False}):
            with self.assertRaises(RuntimeError): self.start(**kwargs)
        self.assertFalse(self.session.folder.exists());self.assertEqual(self.calls, [])

    def test_production_api_identity_remains_the_single_steam_runtime_pin(self):
        self.assertEqual(self.production_api_pin,
                         '4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4')
        self.assertEqual(self.production_api_profile_pin,
                         '9ef296865d2583cd8136303727db150f6e4a349ffe7b175e3e0f651adfb81dcf')

    def test_old_opt_runtime_rvas_refused_before_creating_a_session(self):
        self.plan['audit']['native_api_rvas'] = {'init': '0x5ab8', 'shutdown': '0x5680'}
        self.save_plan()
        with self.assertRaisesRegex(RuntimeError, 'canonical Steam-runtime API'):
            self.stage()
        self.assertFalse(self.session.folder.exists())
        self.assertEqual(self.calls, [])

    def test_missing_or_wrong_mutex_route_and_libc_geometry_are_refused(self):
        audit = dict(self.plan['audit'])
        slot = dict(self.plan['libc_slot'])
        for field, value in (('native_mutex_rvas', None), ('native_mutex_rvas', {'init':'0x86980','destroy':'0x86a00'}),
                             ('exact_loader_name', ''), ('exact_loader_name', 'x' * 512),
                             ('rx_rva_begin', 1), ('rx_rva_end', 1637943)):
            self.plan['audit'] = dict(audit)
            self.plan['libc_slot'] = dict(slot)
            self.plan['audit' if field == 'native_mutex_rvas' else 'libc_slot'][field] = value
            self.save_plan()
            with self.subTest(field=field, value=value), self.assertRaises(RuntimeError):
                self.stage()
            self.assertFalse(self.session.folder.exists())
            self.assertEqual(self.calls, [])

    def test_stage_is_inactive_and_only_copies_owned_deliverables(self):
        self.stage();m = self.session.manifest()
        self.assertEqual(set(m["owned_files"]), {M.MAIN, M.AUDIT, M.ENVELOPE, "deployment-plan.json", *M.HELPERS})
        self.assertFalse((self.session.folder / "audit-preparation.cpp").exists())
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)
        self.assertEqual(m["original_service"], self.original)
        self.assertEqual(m["original_manager_environment"], {"NATIVE_ENV": "yes"})
        self.assertFalse(m["automatic_restart"])

    def test_disk_full_staging_keeps_ownership_record_and_cannot_activate(self):
        real = M.exclusive
        def fail(path, data):
            if path == self.session.folder / M.MAIN: raise OSError(errno.ENOSPC, "fixture full")
            return real(path, data)
        with mock.patch.object(M, "exclusive", side_effect=fail):
            with self.assertRaises(OSError): self.stage()
        self.assertEqual(self.session.manifest()["original_service"], self.original)
        with self.assertRaises(OSError): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertFalse(self.session.pending.exists());self.assertEqual(self.restarts(), 0)

    def test_plan_hash_and_helper_review_are_independent(self):
        with self.assertRaisesRegex(RuntimeError, "plan SHA"):
            self.session.validate_bundle(self.plan_path, "0"*64, self.native, self.artifacts, self.helpers)
        with self.assertRaisesRegex(RuntimeError, "helper SHA"):
            self.stage(reviewed_helpers={name: None for name in M.HELPERS})
        self.assertFalse(self.session.folder.exists())

    def test_activated_plan_and_source_deployment_are_refused(self):
        self.plan["runtime_authorized"] = True;self.save_plan()
        with self.assertRaises(RuntimeError): self.stage()
        self.plan["runtime_authorized"] = False;self.plan["inventory"][-1]["frame_path"] = str(self.session.folder / "audit-preparation.cpp")
        self.save_plan()
        with self.assertRaises(RuntimeError): self.stage()

    def test_duplicate_inventory_and_wrong_envelope_are_refused(self):
        self.plan["inventory"][-1] = dict(self.plan["inventory"][0]);self.save_plan()
        with self.assertRaises(RuntimeError): self.stage()

    def test_fresh_native_and_artifact_hashes_required(self):
        self.native["client"].write_bytes(self.elf(99))
        with self.assertRaisesRegex(RuntimeError, "native hash"): self.stage()
        self.assertFalse(self.session.folder.exists())

    def test_envelope_does_not_accept_boolean_owned_identity(self):
        raw = bytearray(self.artifacts[M.ENVELOPE].read_bytes());raw[592] ^= 1
        self.artifacts[M.ENVELOPE].write_bytes(raw)
        self.plan["envelope"]["sha256"] = M.digest(raw)
        self.plan["inventory"][2]["sha256"] = M.digest(raw);self.save_plan()
        with self.assertRaisesRegex(RuntimeError, "descriptor"): self.stage()

    def test_client_loader_name_and_geometry_must_match_envelope(self):
        self.plan["client_slot"]["exact_loader_name"] = "vrclient.so";self.save_plan()
        with self.assertRaisesRegex(RuntimeError, "slot"): self.stage()

    def test_foreign_caller_manager_and_service_environments_refused(self):
        for location, key in (("caller", "LD_AUDIT"), ("manager", "LD_PRELOAD"), ("service", "FRAME_IMPORT_ENABLED")):
            with self.subTest(location=location):
                self.session.environment = {key: "foreign"} if location == "caller" else {}
                self.manager = f"{key}=foreign\n" if location == "manager" else "NATIVE_ENV=yes\n"
                self.original["Environment"] = f"{key}=foreign" if location == "service" else "NATIVE_SETTING=unchanged"
                with self.assertRaisesRegex(RuntimeError, "Foreign"): self.stage()
        self.assertFalse(self.session.folder.exists())

    def test_native_environment_file_requires_exact_review_and_no_audit(self):
        path = self.root / "native.env";M.exclusive(path, b"NATIVE_FILE=good\n")
        self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        with self.assertRaises(RuntimeError): self.stage()
        path.write_bytes(b"LD_AUDIT=foreign\n")
        with self.assertRaises(RuntimeError): self.stage(reviewed_environment_files={str(path): M.digest(path.read_bytes())})

    def test_reviewed_native_environment_file_is_preserved_exactly(self):
        path = self.root / "native.env";raw = b'# original comments\nNATIVE_FILE="good"\n'
        M.exclusive(path, raw);self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        self.start(reviewed_environment_files={str(path): M.digest(raw)})
        self.session.rollback(restart_ready=True)
        self.assertEqual(path.read_bytes(), raw)
        receipt = json.loads((self.session.folder / "owned.json").read_text())
        self.assertEqual(receipt["original_environment_files"][str(path)]["bytes_hex"], raw.hex())

    def test_native_environment_changed_after_activation_cannot_claim_restored(self):
        path = self.root / "native.env";raw = b"NATIVE_FILE=good\n";M.exclusive(path, raw)
        self.original["EnvironmentFiles"] = f"{path} (ignore_errors=no)"
        self.start(reviewed_environment_files={str(path): M.digest(raw)})
        path.write_bytes(b"NATIVE_FILE=changed\n")
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_foreign_audit_in_original_dropin_is_refused(self):
        path = self.root / "native-dropin.conf";M.exclusive(path, b'[Service]\nEnvironment="LD_AUDIT=foreign"\n')
        self.original["DropInPaths"] = str(path)
        with self.assertRaisesRegex(RuntimeError, "drop-in"): self.stage()
        self.assertFalse(self.session.folder.exists());self.assertEqual(self.restarts(), 0)

    def test_eager_loader_binding_is_refused_before_activation(self):
        self.session.environment = {"LD_BIND_NOW": "1"}
        with self.assertRaises(RuntimeError): self.stage()
        self.assertFalse(self.session.folder.exists())

    def test_old_activation_and_symlink_refused_without_mutation(self):
        old = self.runtime / "systemd/user/steamvr.service.d/90-frame-mask-trace.conf"
        M.private_parents(old.parent);M.exclusive(old, b"old")
        with self.assertRaises(RuntimeError): self.stage()
        self.assertEqual(old.read_bytes(), b"old");old.unlink()
        M.private_parents(self.session.folder.parent);self.session.folder.symlink_to(self.root)
        with self.assertRaises(RuntimeError): self.stage()

    def test_foreign_pending_without_manifest_is_preserved_and_refused(self):
        M.private_parents(self.session.pending.parent);M.exclusive(self.session.pending, b"foreign")
        with self.assertRaises(RuntimeError): self.stage()
        with self.assertRaises(RuntimeError): self.session.rollback()
        self.assertEqual(self.session.pending.read_bytes(), b"foreign");self.assertEqual(self.restarts(), 0)

    def test_activate_needs_both_acknowledgments_again(self):
        self.stage()
        for kwargs in ({"runtime_scope": True}, {"restart_ready": True}, {}):
            with self.assertRaises(RuntimeError): self.session.activate(**kwargs)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_activation_is_one_restart_and_exact_environment(self):
        self.start();self.assertEqual(self.restarts(), 1)
        self.assertEqual(self.session.dropin.read_bytes(), self.session.activation())
        self.assertFalse(self.session.pending.exists());self.session.active_environment(self.session.manifest())
        self.assertFalse(any("sudo" in c or "start" in c for c in self.calls))

    def test_staged_native_or_owned_changes_prevent_activation(self):
        self.stage();(self.session.folder / M.MAIN).write_bytes(b"changed")
        with self.assertRaises(RuntimeError): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_native_hash_change_after_staging_prevents_activation(self):
        self.stage();self.native["receiver"].write_bytes(self.elf(99))
        with self.assertRaisesRegex(RuntimeError, "Native files changed"):
            self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_copied_file_unsafe_permissions_prevent_activation(self):
        self.stage();(self.session.folder / M.AUDIT).chmod(0o644)
        with self.assertRaises(RuntimeError): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists())

    def test_environment_changed_after_staging_refused(self):
        self.stage();self.manager = "NATIVE_ENV=changed\n"
        with self.assertRaisesRegex(RuntimeError, "changed after staging"):
            self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists())

    def test_restart_failure_disables_owned_override_without_automatic_retry(self):
        self.stage();self.fail_restart = True
        with self.assertRaises(subprocess.TimeoutExpired): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 1)
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_interrupt_disables_owned_override_without_automatic_retry(self):
        self.stage();self.interrupt_restart = True
        with self.assertRaises(KeyboardInterrupt): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 1)

    def test_disk_full_ledger_after_publication_does_not_keep_override(self):
        self.stage()
        with mock.patch.object(self.session, "event", side_effect=OSError(errno.ENOSPC, "fixture disk full")):
            with self.assertRaises(OSError): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_partial_pending_write_never_publishes_conf_or_restarts(self):
        self.stage();real = M.exclusive
        def full(path, data):
            if path == self.session.pending:
                real(path, data[:10]);raise OSError(errno.ENOSPC, "fixture disk full")
            return real(path, data)
        with mock.patch.object(M, "exclusive", side_effect=full):
            with self.assertRaisesRegex(RuntimeError, "pending"): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertTrue(self.session.pending.exists());self.assertEqual(self.restarts(), 0)

    def test_crash_between_link_and_unlink_is_recoverable_exact_pair(self):
        self.stage();M.private_parents(self.session.dropin.parent)
        M.exclusive(self.session.pending, self.session.activation());os.link(self.session.pending, self.session.dropin)
        self.session.rollback()
        self.assertFalse(self.session.pending.exists());self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_publication_race_does_not_replace_foreign_override(self):
        self.stage();link = os.link
        def foreign(source, dest, **kwargs):
            M.exclusive(dest, b"foreign");return link(source, dest, **kwargs)
        with mock.patch.object(M.os, "link", side_effect=foreign):
            with self.assertRaises(RuntimeError): self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertEqual(self.session.dropin.read_bytes(), b"foreign");self.assertEqual(self.restarts(), 0)

    def test_unowned_third_hardlink_is_preserved(self):
        self.stage();M.private_parents(self.session.dropin.parent)
        M.exclusive(self.session.pending, self.session.activation());os.link(self.session.pending, self.session.dropin)
        os.link(self.session.pending, self.root / "foreign-third-link")
        with self.assertRaises(RuntimeError): self.session.rollback()
        self.assertTrue(self.session.pending.exists());self.assertTrue(self.session.dropin.exists());self.assertEqual(self.restarts(), 0)

    def test_rollback_without_readiness_only_removes_owned_activation(self):
        self.start();self.session.rollback()
        self.assertFalse(self.session.dropin.exists());self.assertEqual(self.restarts(), 1)
        self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        self.assertIn("loaded_processes_may_remain", (self.session.folder / "session.jsonl").read_text())

    def test_manual_rollback_restores_snapshots_archives_bounded_capture(self):
        self.start();M.exclusive(self.session.capture, b'{"kind":"refusal"}\n')
        self.session.rollback(restart_ready=True);self.assertEqual(self.restarts(), 2)
        receipt = json.loads((self.session.folder / "rollback-complete.json").read_text())
        self.assertTrue(receipt["restored"]);self.assertFalse(receipt["native_mapping_unload_verified"])
        self.assertEqual((self.session.folder / "archive/client-startup.jsonl").read_bytes(), self.session.capture.read_bytes())
        self.session.rollback(restart_ready=True);self.assertEqual(self.restarts(), 2)

    def test_tampered_override_is_preserved_without_restart(self):
        self.start();self.session.dropin.write_bytes(b"foreign")
        with self.assertRaisesRegex(RuntimeError, "tampered"): self.session.rollback(restart_ready=True)
        self.assertEqual(self.session.dropin.read_bytes(), b"foreign");self.assertEqual(self.restarts(), 1)

    def test_rollback_can_disable_even_if_owned_library_tampered(self):
        self.start();(self.session.folder / M.MAIN).write_bytes(b"tampered")
        self.session.rollback();self.assertFalse(self.session.dropin.exists())

    def test_own_inherited_caller_environment_allowed_only_on_rollback(self):
        self.start();self.session.environment = self.session.activation_values()
        self.session.rollback(restart_ready=True)
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())

    def test_foreign_inherited_caller_and_changed_manager_cannot_claim_restoration(self):
        self.start();self.session.environment = {"LD_PRELOAD": "foreign"}
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_failed_archive_never_claims_complete_and_exact_retry_succeeds(self):
        self.start();real = M.exclusive
        def fail(path, raw):
            if path == self.session.folder / "archive/deployment-plan.json": raise OSError(errno.ENOSPC, "fixture full")
            return real(path, raw)
        with mock.patch.object(M, "exclusive", side_effect=fail):
            with self.assertRaises(OSError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertFalse((self.session.folder / "rollback-complete.json").exists())
        self.session.rollback(restart_ready=True)
        self.assertTrue((self.session.folder / "rollback-complete.json").exists())

    def test_oversize_capture_refused_after_disabling_without_false_receipt(self):
        self.start();M.exclusive(self.session.capture, b"x"*(M.CAPTURE_LIMIT+1))
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertFalse(self.session.dropin.exists());self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_complete_receipt_tampering_is_not_idempotent_success(self):
        self.start();self.session.rollback(restart_ready=True)
        path = self.session.folder / "rollback-complete.json";path.write_bytes(M.json_bytes({"restored": True}))
        with self.assertRaises(RuntimeError): self.session.rollback()

    def test_archived_foreign_files_are_preserved(self):
        self.start();destination = self.session.folder / "archive";destination.mkdir(mode=0o700)
        M.exclusive(destination / "foreign", b"retain")
        with self.assertRaises(RuntimeError): self.session.rollback(restart_ready=True)
        self.assertEqual((destination / "foreign").read_bytes(), b"retain")
        self.assertFalse(self.session.dropin.exists())

    def test_partial_archive_file_is_preserved_without_false_completion(self):
        self.start();destination = self.session.folder / "archive"
        # Simulate a truncated write only after the owned expected manifest.
        real = M.exclusive
        def partial(path, raw):
            if path == destination / "deployment-plan.json":
                real(path, raw[:20]);raise OSError(errno.ENOSPC, "fixture full")
            return real(path, raw)
        with mock.patch.object(M, "exclusive", side_effect=partial):
            with self.assertRaises(OSError): self.session.rollback(restart_ready=True)
        with self.assertRaisesRegex(RuntimeError, "Partial/tampered"): self.session.rollback(restart_ready=True)
        self.assertEqual((destination / "deployment-plan.json").stat().st_size, 20)
        self.assertFalse(self.session.dropin.exists());self.assertFalse((self.session.folder / "rollback-complete.json").exists())

    def test_idempotent_receipt_does_not_hide_later_environment_change(self):
        self.start();self.session.rollback(restart_ready=True);self.manager = "NATIVE_ENV=changed\n"
        with self.assertRaises(RuntimeError): self.session.rollback()

    def test_manager_audit_added_during_restart_is_detected_and_disabled(self):
        self.stage();original = self.session.command
        def command(*args):
            if "restart" in args: self.manager = "NATIVE_ENV=yes LD_AUDIT=foreign\n"
            return original(*args)
        with mock.patch.object(self.session, "command", side_effect=command):
            with self.assertRaisesRegex(RuntimeError, "Manager"):
                self.session.activate(runtime_scope=True, restart_ready=True)
        self.assertFalse(self.session.dropin.exists())


if __name__ == "__main__": unittest.main()
