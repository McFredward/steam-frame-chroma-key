#!/usr/bin/env python3
"""Explicitly authorized, reversible USER-service compositor-raw diagnostic session.

Offline artifacts/receipts are never permission. No network, timers, sudo,
native patches, shader/mask activation or automatic recovery restart.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import stat
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
_base_spec = importlib.util.spec_from_file_location("client_session_safe_primitives",
    Path(__file__).with_name("receiver_observer_session.py"))
base = importlib.util.module_from_spec(_base_spec)
_base_spec.loader.exec_module(base)
_health_spec = importlib.util.spec_from_file_location("compositor_startup_health",
    Path(__file__).with_name("compositor_startup_health.py"))
health = importlib.util.module_from_spec(_health_spec)
_health_spec.loader.exec_module(health)
safe_path, read_regular, exclusive = base.safe_path, base.read_regular, base.exclusive
digest, json_bytes, private_parents = base.digest, base.json_bytes, base.private_parents
UNIT = "steamvr.service"
SCHEMA = "frame-compositor-raw-offline-bundle-v2"
SESSION_SCHEMA = "frame-compositor-raw-owned-session-v2"
MAIN, AUDIT, ENVELOPE = "libVkLayer_frame_mask_trace.so", "libframe_compositor_raw_audit.so", "compositor-raw-envelope.bin"
SELECTOR = "libframe_compositor_launch_selector.so"
SELECTOR_EXPORTS = {"execve", "execv", "execvp", "execvpe", "posix_spawn", "posix_spawnp", "getenv", "secure_getenv",
    "frame_compositor_launch_freeze_environment_v1", "frame_compositor_launch_frozen_status_v1", "frame_compositor_launch_provider_mask"}
LAYER = "VkLayer_FRAME_compositor_raw.json"
VULKAN_FUNCTIONS = {
    "vkGetInstanceProcAddr": "frame_compositor_raw_vkGetInstanceProcAddr",
    "vkGetDeviceProcAddr": "frame_compositor_raw_vkGetDeviceProcAddr",
    "vkNegotiateLoaderLayerInterfaceVersion": "frame_compositor_raw_vkNegotiateLoaderLayerInterfaceVersion"}
PUBLIC_EXPORTS = {"frame_compositor_raw_bridge", *VULKAN_FUNCTIONS.values()}
HELPERS = ("compositor_raw_session.py", "receiver_observer_session.py", "compositor_startup_health.py")
NATIVE = {
    "compositor": ("vrcompositor", "0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96"),
    "libc": ("libc.so.6", "be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b"),
    "loader": ("ld-linux-aarch64.so.1", "2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e"),
}
PROFILE_PINS = ("a6c63d91e7da71328088cf912241b737e7529a2adf383d5227bb43f5bf0a8c6e",
    "48edb257be746f755a00ff564c6db4284526b15ffc7e6bad5b8d6723b1891512",
    "51af1fa58396e7a1ab48760ce90d90e919db3e42653504ad550eb54e129a7405")
PREFIXES = ("FRAME_RECEIVER_", "FRAME_MASK_TRACE", "FRAME_IMPORT_", "FRAME_CLIENT_",
    "GHVR_FRAME_CLIENT_STARTUP_", "GHVR_FRAME_COMPOSITOR_RAW_", "MRCHROMA_")
BLOCKED = {"LD_PRELOAD", "LD_AUDIT", "LD_BIND_NOW", "LD_BIND_NOT", "LD_DEBUG", "LD_DEBUG_OUTPUT", "LD_PROFILE",
    "LD_PROFILE_OUTPUT", "VK_LOADER_DEBUG", "VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH", "VK_ADD_IMPLICIT_LAYER_PATH", "VK_LOADER_LAYERS_ENABLE", "VK_LOADER_LAYERS_DISABLE", "VK_LOADER_LAYERS_ALLOW"}
MAX_INPUT = 8 * 1024 * 1024
CAPTURE_LIMITS = {"records": 32, "bytes": 65536, "seconds": 120}
JOURNAL_LIMITS = {"bytes": 1048576, "call_records": 512, "binding_records": 32}
RAW_LIMITS = {"transactions": 1, "writes": 8, "scalar_bytes": 672, "wire_bytes": 21,
    "parent_bytes": 16, "private_bytes": 709, "bootstrap_poll_bytes": 0}
NATIVE_RVAS = {"lock": "0x87370", "unlock": "0x88f74", "write": "0xdfee0"}
ENVELOPE_LIMITS = (672, 21, 16, 8, 32, 65536, 120, 4)
STARTUP_RECEIPTS = frozenset({"activation-startup-baseline.json", "activation-startup-health.json",
    "restoration-startup-baseline.json", "restoration-startup-health.json", "startup-monitor.json"})


def require_reviewed_launch_isolation(manifest=None):
    # No flag admits the old service-wide audit/preload. The versioned package
    # must have a reviewed cold-freeze selector and a runtime isolation probe.
    if (not isinstance(manifest, dict) or manifest.get("schema") != SESSION_SCHEMA or
            SELECTOR not in manifest.get("owned_files", {}) or manifest.get("launch_freeze_abi") != 1 or
            manifest.get("launch_provider_mask") != 63 or not manifest.get("launch_isolation")):
        raise RuntimeError("Compositor activation blocked: reviewed cold-freeze launch isolation is required")
    health.require_isolation_descriptor(manifest["launch_isolation"])


def blocked(key, value):
    return bool(value) and (key in BLOCKED or key.startswith(PREFIXES))

def directory_sync(path):
    fd = os.open(safe_path(path), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try: os.fsync(fd)
    finally: os.close(fd)

class Session(base.Session):
    def __init__(self, home, runtime, run=subprocess.run, environment=None, proc_root=Path("/proc"), *, health_source=None):
        super().__init__(home, runtime, run, environment, proc_root)
        self.folder = self.home / ".local/share/GloomhavenVR/frame-compositor-raw"
        self.dropin = self.runtime / "systemd/user/steamvr.service.d/96-frame-compositor-raw.conf"
        self.layer = self.home / ".local/share/vulkan/implicit_layer.d/frame_compositor_raw.json"
        self.library, self.audit, self.envelope = (self.folder / name for name in (MAIN, AUDIT, ENVELOPE))
        self.selector = self.folder / SELECTOR
        self.capture = self.folder / "compositor-raw.jsonl"
        self.journal = self.folder / "compositor-journal.jsonl"
        self._owned_caller = None
        self.health_source = health_source or health.NativeEvidenceSource(self.home, proc_root=self.proc, run=self.run)
        self.native_layer_node_drift = {}

    def state(self):
        data = self.command("show", UNIT, "--property=Environment,EnvironmentFiles,DropInPaths")
        result = dict(line.split("=", 1) for line in data.splitlines() if "=" in line)
        if set(result) != {"Environment", "EnvironmentFiles", "DropInPaths"}:
            raise RuntimeError("Incomplete SteamVR environment inspection")
        return result

    def activation_values(self):
        return {"LD_PRELOAD": str(self.selector)}

    def target_environment(self):
        return {"LD_PRELOAD": str(self.selector) + ":" + str(self.library), "LD_AUDIT": str(self.audit),
            "GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE": str(self.envelope),
            "GHVR_FRAME_COMPOSITOR_RAW_OUTPUT": str(self.capture), "FRAME_MASK_TRACE": "1",
            "FRAME_MASK_TRACE_BINDINGS": "1", "FRAME_MASK_TRACE_OUTPUT": str(self.journal)}

    def activation(self):
        values = self.activation_values()
        if any(not re.fullmatch(r"[A-Za-z0-9_./-]+", v) for v in values.values()):
            raise RuntimeError("Unsupported activation path characters")
        return ("[Service]\n" + "".join(f'Environment="{k}={v}"\n' for k, v in values.items())).encode()

    def reject_environment(self, state):
        manager = self.env_values(self.command("show-environment"))
        caller = dict(self.environment)
        if self._owned_caller is not None:
            for key, value in self._owned_caller.items():
                if caller.get(key) == value: caller.pop(key)
        for values in (caller, manager, self.env_values(state["Environment"])):
            if any(blocked(k, v) for k, v in values.items()):
                raise RuntimeError("Foreign preload/audit/tracing environment")
        tokens = [k.encode() for k in sorted(BLOCKED)] + [p.encode() for p in PREFIXES]
        for path in shlex.split(state["DropInPaths"]):
            raw = read_regular(Path(path), 65536)
            if any(token in raw for token in tokens): raise RuntimeError("Foreign tracing/service drop-in")
        return manager

    def native_environment_files(self, state, reviewed):
        snapshots = super().native_environment_files(state, reviewed)
        if any(blocked(k, v) for snapshot in snapshots.values() for k, v in snapshot["values"].items()):
            raise RuntimeError("Foreign audit/tracing in native EnvironmentFile")
        return snapshots

    @staticmethod
    def layer_stat(info):
        return {key: getattr(info, "st_" + key) for key in
                ("dev", "ino", "mode", "uid", "gid", "nlink", "size", "mtime_ns", "ctime_ns")}

    def native_layer_snapshots(self, reviewed, allow_own=False):
        # Each exact file has its own independently reviewed digest. Filenames
        # or vendor names alone never admit an implicit layer. The current
        # native Steam manifests are regular files; symlinks are not followed.
        if not isinstance(reviewed, dict) or len(reviewed) > 64:
            raise RuntimeError("Explicit bounded native layer reviews required")
        safe_path(self.layer.parent)
        expected = {}
        for name, sha in reviewed.items():
            node = Path(name)
            if (not isinstance(name, str) or str(node) != name or node.parent != self.layer.parent or
                node == self.layer or node.suffix != ".json" or not re.fullmatch(r"[0-9a-f]{64}", sha or "")):
                raise RuntimeError("Exact native layer node/digest required")
            expected[name] = sha
        entries = list(self.layer.parent.iterdir()) if self.layer.parent.exists() else []
        if len(entries) > 65: raise RuntimeError("User layer directory bound exceeded")
        actual = {str(path) for path in entries if not (allow_own and path == self.layer)}
        if actual != set(expected): raise RuntimeError("New, missing or unreviewed user implicit layer")
        result, total = {}, 0
        for name, sha in sorted(expected.items()):
            path = safe_path(name); before = path.lstat()
            if not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid() or before.st_mode & 0o022:
                raise RuntimeError("Unsafe native layer ownership/type/mode")
            raw = read_regular(path, 16384); after = path.lstat(); total += len(raw)
            if self.layer_stat(before) != self.layer_stat(after) or total > 65536 or digest(raw) != sha:
                raise RuntimeError("Reviewed native layer changed or exceeded bound")
            document = json.loads(raw)
            layer = document.get("layer") if isinstance(document, dict) else None
            if (not isinstance(layer, dict) or not isinstance(layer.get("name"), str) or
                layer["name"] == "VK_LAYER_FRAME_MASK_trace" or not isinstance(layer.get("library_path"), str) or
                layer["library_path"].startswith(str(self.folder))):
                raise RuntimeError("Native layer cannot be a diagnostic or malformed manifest")
            enables = layer.get("enable_environment")
            if (not isinstance(enables, dict) or not 1 <= len(enables) <= 8 or
                any(not isinstance(key, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) or
                    not isinstance(value, str) or blocked(key, value) for key, value in enables.items())):
                raise RuntimeError("Native layers require explicit inactive enable gates")
            result[name] = {"sha256": sha, "bytes_hex": raw.hex(), "node": self.layer_stat(after),
                            "enable_environment": enables, "library_path": layer["library_path"], "layer_name": layer["name"]}
        return result

    def native_layers_inactive(self, snapshots, state, manager, environment_files):
        values = [self.environment, manager, self.env_values(state["Environment"])]
        values.extend(item["values"] for item in environment_files.values())
        keys = {key for item in snapshots.values() for key in item["enable_environment"]}
        if any(key in scope for scope in values for key in keys):
            raise RuntimeError("Reviewed native implicit layer enable variable is present")

    def revalidate_native_layers(self, m, *, allow_own=False, state=None, manager=None, environment_files=None, allow_node_rewrite=False):
        baseline = m.get("original_native_layers")
        if not isinstance(baseline, dict): raise RuntimeError("Owned native layer baseline missing")
        current = self.native_layer_snapshots({name: item["sha256"] for name, item in baseline.items()}, allow_own)
        if type(allow_node_rewrite) is not bool: raise RuntimeError("Explicit native-layer comparison phase required")
        drift = {}
        for name, original in baseline.items():
            item = current[name]
            if item == original: continue
            changed = {key for key in item["node"] if item["node"][key] != original["node"].get(key)}
            same_manifest = {k: v for k, v in item.items() if k != "node"} == {k: v for k, v in original.items() if k != "node"}
            if not allow_node_rewrite or not same_manifest or not changed <= {"dev", "ino", "mtime_ns", "ctime_ns"}:
                raise RuntimeError("Native layer node/content baseline changed")
            drift[name] = {"changed_node_fields": sorted(changed), "original": original["node"], "current": item["node"]}
        self.native_layer_node_drift = drift
        if state is None: state = self.state()
        if manager is None: manager = self.env_values(self.command("show-environment"))
        if environment_files is None:
            environment_files = self.native_environment_files(state, {name: item["sha256"] for name, item in m["original_environment_files"].items()})
        self.native_layers_inactive(current, state, manager, environment_files)
        return current

    def reject_old_activation(self, reviewed_native_layers=None, allow_own=False):
        for path in (self.runtime / "systemd/user/steam.service.d/94-frame-client-startup.conf",
            self.runtime / "systemd/user/steam.service.d/91-frame-receiver-observer.conf",
            self.runtime / "systemd/user/steamvr.service.d/91-frame-receiver-observer.conf",
            self.runtime / "systemd/user/steamvr.service.d/90-frame-mask-trace.conf"):
            safe_path(path)
            if path.exists() or path.is_symlink(): raise RuntimeError("Prior diagnostic activation remains")
        return self.native_layer_snapshots(reviewed_native_layers or {}, allow_own)

    @staticmethod
    def elf(data):
        return len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01" and data[16:20] == b"\x03\x00\xb7\x00"

    @staticmethod
    def validate_envelope(data, plan, files, observer):
        if (len(data) != 8272 or data[:8] != b"FCMRAW01" or struct.unpack_from("<II", data, 8) != (1, 8272) or
            struct.unpack_from("<8Q", data, 48) != ENVELOPE_LIMITS or digest(data[:-32]) != data[-32:].hex()):
            raise RuntimeError("Exact raw envelope schema/limits/checksum required")
        reviewed = data[16:48].hex()
        if plan.get("envelope", {}).get("reviewed_observer_profile_identity") != reviewed:
            raise RuntimeError("Reviewed observer profile missing")
        reports = plan.get("native_profiles")
        if not isinstance(reports, list) or len(reports) != 3: raise RuntimeError("Exact native profile reports required")
        for i, (role, numeric) in enumerate((("compositor", 7), ("libc", 3), ("loader", 4), ("observer", 5))):
            profile = data[112 + 2032 * i:112 + 2032 * (i + 1)]
            pin = PROFILE_PINS[i] if i < 3 else reviewed
            sha, size = (files[role]["sha256"], files[role]["bytes"]) if i < 3 else (digest(observer), len(observer))
            report = reports[i] if i < 3 else plan.get("observer_profile", {})
            if (struct.unpack_from("<4Q", profile) != (1, numeric, 183, 3) or
                digest(profile[:-32]) != pin or profile[-32:].hex() != pin or
                profile[176:208].hex() != sha or struct.unpack_from("<Q", profile, 32)[0] != size or
                report.get("role") != numeric or report.get("file_sha256") != sha or report.get("profile_sha256") != pin):
                raise RuntimeError("Fresh native file/descriptor or observer profile changed")

    def validate_bundle(self, path, expected, native, artifacts, reviewed_helpers):
        raw = read_regular(path, private=True)
        if not re.fullmatch(r"[0-9a-f]{64}", expected or "") or digest(raw) != expected:
            raise RuntimeError("Separately reviewed plan SHA required")
        plan = json.loads(raw)
        if (plan.get("schema") != SCHEMA or plan.get("prepared") is not True or
            plan.get("frame_directory") != str(self.folder) or plan.get("frame_user_home") != str(self.home) or
            plan.get("directory_mode") != "0700" or plan.get("environment") != self.activation_values()):
            raise RuntimeError("Unsupported compositor raw bundle scope/paths")
        for key in ("installed", "activated", "runtime_authorized", "frame_access", "network_access",
            "native_binary_or_script_changes", "shader_or_mask_activation", "source_frame_associated", "gpu_coherent", "automatic_restart", "continuous_connection_owned", "native_namespace_associated"):
            if plan.get(key) is not False: raise RuntimeError("Passive offline plan exceeds approved scope")
        if set(native) != set(NATIVE) or set(artifacts) != {MAIN, AUDIT, SELECTOR, ENVELOPE, LAYER} or set(reviewed_helpers) != set(HELPERS):
            raise RuntimeError("Exact fresh natives, five own artifacts and reviewed helpers required")
        if (plan.get("capture_limits") != CAPTURE_LIMITS or plan.get("raw_limits") != RAW_LIMITS or plan.get("journal_limits") != JOURNAL_LIMITS or
            any(type(v) is not int for v in plan.get("capture_limits", {}).values()) or
            any(type(v) is not int for v in plan.get("raw_limits", {}).values()) or
            any(type(v) is not int for v in plan.get("journal_limits", {}).values())):
            raise RuntimeError("Exact bounded raw diagnostic limits required")
        inventory = plan.get("inventory")
        sources = {"audit-preparation.cpp", "launch-configuration.c"}
        names = {MAIN, AUDIT, SELECTOR, ENVELOPE, LAYER} | sources
        if not isinstance(inventory, list) or len(inventory) != len(names): raise RuntimeError("Exact bundle inventory required")
        entries = {item.get("name"): item for item in inventory if isinstance(item, dict)}
        if set(entries) != names: raise RuntimeError("Unexpected or duplicate inventory entry")
        for name, item in entries.items():
            if (item.get("owned_source") is not True or item.get("native_bytes") is not False or item.get("mode") != "0600" or
                item.get("frame_path") != (None if name in sources else str(self.folder / name))):
                raise RuntimeError("Source/native or foreign artifact may not be deployed")
        files, payloads = {}, {}
        for role, source in native.items():
            data = read_regular(source, MAX_INPUT)
            if Path(source).name != NATIVE[role][0] or digest(data) != NATIVE[role][1] or not self.elf(data):
                raise RuntimeError("Fresh canonical native hash/ABI mismatch")
            files[role] = {"path": str(safe_path(source)), "sha256": digest(data), "bytes": len(data)}
        for name, source in artifacts.items():
            data = read_regular(source, 256 * 1024 if name in (AUDIT, SELECTOR) else MAX_INPUT, private=True)
            if entries[name].get("sha256") != digest(data) or entries[name].get("bytes") != len(data):
                raise RuntimeError("Reviewed own artifact changed")
            if name in (MAIN, AUDIT, SELECTOR) and not self.elf(data): raise RuntimeError("Own artifact ABI changed")
            payloads[name] = data
        for report_name, name in (("observer", MAIN), ("audit", AUDIT), ("selector", SELECTOR), ("envelope", ENVELOPE), ("manifest", LAYER)):
            report = plan.get(report_name, {})
            if report.get("sha256") != digest(payloads[name]) or report.get("bytes") != len(payloads[name]):
                raise RuntimeError("Artifact report changed")
        if plan["audit"].get("native_rvas") != NATIVE_RVAS or plan["audit"].get("early_forward_only") is not True:
            raise RuntimeError("Canonical audited providers required")
        if (plan["observer"].get("public_exports") != sorted(PUBLIC_EXPORTS) or
            plan["observer"].get("vulkan_functions") != VULKAN_FUNCTIONS or
            plan["observer"].get("standard_vulkan_exports") != []):
            raise RuntimeError("Reviewed combined DSO must expose only the private layer entries and bridge")
        if (plan["observer"].get("cold_environment_freeze_abi") != 1 or plan["observer"].get("launch_provider_mask") != 63 or
                plan["selector"].get("public_exports") != sorted(SELECTOR_EXPORTS) or
                plan["selector"].get("cold_environment_freeze_abi") != 1 or plan["selector"].get("launch_provider_mask") != 63 or
                plan["selector"].get("module_tls_bytes") != 0 or plan["selector"].get("standard_vulkan_exports") != []):
            raise RuntimeError("Exact cold-freeze observer/selector ABI required")
        isolation = health.require_isolation_descriptor(plan.get("launch_isolation"))
        expected_isolation = {"schema": health.ISOLATION_SCHEMA, "target_exe": files["compositor"]["path"],
            "selector_path": str(self.selector), "payload_path": str(self.library), "audit_path": str(self.audit),
            "target_exec_environment": self.target_environment()}
        expected_config = {"target": files["compositor"]["path"], "selector": str(self.selector), "payload": str(self.library),
            "audit": str(self.audit), "envelope": str(self.envelope), "output": str(self.capture), "journal": str(self.journal)}
        if isolation != expected_isolation or plan["selector"].get("configuration") != expected_config:
            raise RuntimeError("Reviewed exact launch/isolation paths differ from staged natives/artifacts")
        if (plan["envelope"].get("schema") != 1 or plan["envelope"].get("module_count") != 4 or
            plan["envelope"].get("module_roles") != [7, 3, 4, 5]):
            raise RuntimeError("Exact envelope module report required")
        self.validate_envelope(payloads[ENVELOPE], plan, files, payloads[MAIN])
        self.validate_layer(payloads[LAYER])
        if (plan["manifest"].get("frame_path") != str(self.layer) or
            plan["manifest"].get("staged_path") != str(self.folder / LAYER) or
            plan["manifest"].get("library_path") != str(self.library) or
            plan["manifest"].get("functions") != VULKAN_FUNCTIONS):
            raise RuntimeError("Same owned combined DSO and exact layer paths required")
        for name, source in ((HELPERS[0], Path(__file__)), (HELPERS[1], Path(base.__file__)), (HELPERS[2], Path(health.__file__))):
            data = read_regular(source)
            if not re.fullmatch(r"[0-9a-f]{64}", reviewed_helpers[name] or "") or digest(data) != reviewed_helpers[name]:
                raise RuntimeError("Separately reviewed helper SHA required")
            payloads[name] = data
        payloads["deployment-plan.json"] = raw
        return files, payloads

    def validate_layer(self, data):
        actual = json.loads(data)
        expected = {"file_format_version": "1.2.0", "layer": {
            "name": "VK_LAYER_FRAME_MASK_trace", "type": "GLOBAL", "library_path": str(self.library),
            "api_version": "1.3.0", "implementation_version": "1",
            "description": "Opt-in bounded passive compositor raw import metadata",
            "functions": dict(VULKAN_FUNCTIONS),
            "enable_environment": {"FRAME_MASK_TRACE": "1"}, "disable_environment": {"FRAME_MASK_TRACE_DISABLE": "1"}}}
        if actual != expected: raise RuntimeError("Passive implicit layer shape or same-DSO identity changed")

    def activation_files(self, layer_data):
        return {self.dropin: self.activation(), self.layer: layer_data}

    def stage(self, path, expected, native, artifacts, *, runtime_scope=False, reviewed_helpers=None, reviewed_environment_files=None, reviewed_native_layers=None):
        if runtime_scope is not True: raise RuntimeError("Separate compositor-private-scope acknowledgment required")
        native_layers = self.reject_old_activation(reviewed_native_layers)
        for target in (self.folder, self.dropin, self.layer, self.dropin.with_suffix(".pending"), self.layer.with_suffix(".pending")):
            safe_path(target)
            if target.exists() or target.is_symlink(): raise RuntimeError("Existing session/activation refused")
        files, payloads = self.validate_bundle(path, expected, native, artifacts, reviewed_helpers or {})
        self.command("is-active", UNIT); original = self.state()
        manager = self.reject_environment(original)
        snapshots = self.native_environment_files(original, reviewed_environment_files or {})
        self.native_layers_inactive(native_layers, original, manager, snapshots)
        private_parents(self.folder.parent); self.folder.mkdir(mode=0o700); self.folder.chmod(0o700)
        exclusive(self.folder / "operation.lock", b""); exclusive(self.folder / "session.jsonl", b"")
        with self.lock():
            activation = self.activation_files(payloads[LAYER])
            manifest = {"schema": SESSION_SCHEMA, "unit": UNIT, "scope_acknowledged": True,
                "plan_sha256": expected, "original_service": original, "original_manager_environment": manager,
                "original_environment_files": snapshots, "original_native_layers": native_layers, "native_files": files,
                "activation": {str(p): {"sha256": digest(raw), "bytes_hex": raw.hex()} for p, raw in activation.items()},
                "owned_files": {name: digest(data) for name, data in payloads.items()}, "automatic_restart": False}
            plan = json.loads(payloads["deployment-plan.json"])
            manifest.update(launch_isolation=plan["launch_isolation"], launch_freeze_abi=1, launch_provider_mask=63)
            exclusive(self.folder / "owned.json", json_bytes(manifest))
            for name, data in payloads.items(): exclusive(self.folder / name, data)
            exclusive(self.folder / "staged.json", json_bytes({"staged": True, "activated": False}))
            directory_sync(self.folder)
            self.event("staged", activated=False, permission_from_plan=False, scope_acknowledged=True)

    def manifest(self):
        self.private_folder(); m = json.loads(read_regular(self.folder / "owned.json", private=True))
        if (m.get("schema") != SESSION_SCHEMA or m.get("unit") != UNIT or m.get("scope_acknowledged") is not True or
            m.get("automatic_restart") is not False or set(m.get("activation", {})) != {str(self.dropin), str(self.layer)} or
            set(m.get("owned_files", {})) != {MAIN, AUDIT, SELECTOR, ENVELOPE, LAYER, "deployment-plan.json", *HELPERS} or
            set(m.get("native_files", {})) != set(NATIVE) or
            m["owned_files"].get("deployment-plan.json") != m.get("plan_sha256")):
            raise RuntimeError("Invalid owned compositor session manifest")
        require_reviewed_launch_isolation(m)
        plan_raw = read_regular(self.folder / "deployment-plan.json", private=True)
        if digest(plan_raw) != m["plan_sha256"] or json.loads(plan_raw).get("launch_isolation") != m["launch_isolation"]:
            raise RuntimeError("Owned launch isolation differs from the separately reviewed plan")
        if not isinstance(m.get("original_native_layers"), dict) or len(m["original_native_layers"]) > 64:
            raise RuntimeError("Invalid owned native layer baseline")
        for name, sha in m["owned_files"].items():
            if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{64}", sha): raise RuntimeError("Invalid owned fingerprint")
        for path, item in m["activation"].items():
            if not isinstance(item, dict) or set(item) != {"sha256", "bytes_hex"}: raise RuntimeError("Invalid activation receipt")
            raw = bytes.fromhex(item["bytes_hex"])
            if len(raw) > 65536 or digest(raw) != item["sha256"]: raise RuntimeError("Invalid activation fingerprint")
            if path == str(self.dropin) and raw != self.activation(): raise RuntimeError("Owned override changed")
            if path == str(self.layer):
                self.validate_layer(raw)
                if digest(raw) != m["owned_files"][LAYER]: raise RuntimeError("Owned layer snapshot changed")
        if (set(m.get("original_service", {})) != {"Environment", "EnvironmentFiles", "DropInPaths"} or
            any(not isinstance(v, str) for v in m["original_service"].values()) or
            not isinstance(m.get("original_manager_environment"), dict) or not isinstance(m.get("original_environment_files"), dict)):
            raise RuntimeError("Invalid native environment snapshots")
        for role, item in m["native_files"].items():
            if (not isinstance(item, dict) or set(item) != {"path", "sha256", "bytes"} or
                not isinstance(item["path"], str) or Path(item["path"]).name != NATIVE[role][0] or item["sha256"] != NATIVE[role][1] or
                type(item["bytes"]) is not int or not 64 <= item["bytes"] <= MAX_INPUT):
                raise RuntimeError("Invalid canonical native snapshot")
        return m

    def verify_owned(self, m):
        for name, sha in m["owned_files"].items():
            # The final combined DSO is larger than the metadata/helper cap.
            # Its independent reviewed hash still applies; no other input gets
            # a wider allowance merely because it is in the owned folder.
            limit = MAX_INPUT if name == MAIN else 262144 if name in (AUDIT, SELECTOR) else base.LIMIT
            if digest(read_regular(self.folder / name, limit, private=True)) != sha: raise RuntimeError("Owned staged artifact/helper/plan changed")
        for role, item in m["native_files"].items():
            data = read_regular(Path(item["path"]), MAX_INPUT)
            if digest(data) != item["sha256"] or len(data) != item["bytes"]: raise RuntimeError("Native identity changed since staging")

    def activate(self, *, runtime_scope=False, restart_ready=False):
        if runtime_scope is not True or restart_ready is not True: raise RuntimeError("Actual scope and coordinated SteamVR restart required")
        if not self.folder.exists(): require_reviewed_launch_isolation()
        with self.lock():
            m = self.manifest(); self.verify_owned(m)
            require_reviewed_launch_isolation(m)
            self.reject_old_activation({name: item["sha256"] for name, item in m["original_native_layers"].items()})
            self.revalidate_native_layers(m)
            if (self.folder / "rollback-complete.json").exists(): raise RuntimeError("Restored session cannot reactivate")
            for name in m["activation"]:
                path = safe_path(name)
                if path.exists() or path.is_symlink() or path.with_suffix(".pending").exists() or path.with_suffix(".pending").is_symlink():
                    raise RuntimeError("Existing activation cannot be replaced")
            current = self.state(); manager = self.reject_environment(current)
            snapshots = self.native_environment_files(current, {n: v["sha256"] for n, v in m["original_environment_files"].items()})
            if current != m["original_service"] or manager != m["original_manager_environment"] or snapshots != m["original_environment_files"]:
                raise RuntimeError("Native environment changed after staging")
            if json.loads(read_regular(self.folder / "staged.json", private=True)) != {"staged": True, "activated": False}:
                raise RuntimeError("Staging incomplete")
            try:
                # Complete private temporary bytes first, then exclusive hard
                # link publication. Never replace existing/partial foreign data.
                for name in (str(self.layer), str(self.dropin)):
                    path = Path(name); pending = path.with_suffix(".pending")
                    private_parents(path.parent)
                    exclusive(pending, bytes.fromhex(m["activation"][name]["bytes_hex"]))
                    os.link(pending, path, follow_symlinks=False); pending.unlink(); directory_sync(path.parent)
                self.event("activation_created", restart_readiness=True)
                self.command("daemon-reload")
                self.configure_startup_isolation(m, active=True)
                baseline = self.health_source.baseline()
                self.health_receipt("activation-startup-baseline.json", baseline)
                self.command("restart", UNIT); self.command("is-active", UNIT)
                self.active_environment(m, post_restart=True)
                self.check_startup(baseline, "activation-startup-health.json")
                self.event("activated", startup_health_ready=True, startup_screen_pixels_verified=False,
                           runtime_loaded_verified=False, capture_verified=False)
            except BaseException:
                self.remove_activation(m); self.recovery_event("activation_interrupted", native_restart_still_required=True)
                raise

    def active_environment(self, m, *, post_restart=False):
        current = self.state();actual = self.env_values(current["Environment"])
        expected = self.env_values(m["original_service"]["Environment"]);expected.update(self.activation_values())
        paths = shlex.split(current["DropInPaths"])
        if (actual != expected or current["EnvironmentFiles"] != m["original_service"]["EnvironmentFiles"] or
            len(paths) != len(set(paths)) or set(paths) != set(shlex.split(m["original_service"]["DropInPaths"])) | {str(self.dropin)}):
            raise RuntimeError("Service did not acquire the exact owned environment")
        snapshots = self.native_environment_files(current, {n: v["sha256"] for n, v in m["original_environment_files"].items()})
        if snapshots != m["original_environment_files"]: raise RuntimeError("Native environment files changed")
        manager = self.env_values(self.command("show-environment"))
        if any(blocked(k, v) for k, v in manager.items()) or not base.compare_manager(
                m["original_manager_environment"], manager, self.runtime,
                allow_graphical_session_metadata=True)["manager_matches_except_session_metadata"]:
            raise RuntimeError("Manager environment changed during activation")
        self.revalidate_native_layers(m, allow_own=True, state=current, manager=manager, environment_files=snapshots, allow_node_rewrite=post_restart)
        return current

    def health_receipt(self, name, value):
        if name not in STARTUP_RECEIPTS:
            raise RuntimeError("Unsupported startup receipt")
        raw = json_bytes(value)
        if len(raw) > 262144: raise RuntimeError("Startup receipt bound exceeded")
        path = self.folder / name
        pending = path.with_suffix(".pending")
        if path.exists(): read_regular(path, 262144, private=True)
        exclusive(pending, raw)
        os.replace(pending, path); directory_sync(self.folder)

    def check_startup(self, baseline, receipt):
        result = health.wait_ready(self.health_source, baseline, clock=self.health_source.clock, sleep=self.health_source.sleep)
        if receipt.startswith("activation") and result.get("ready") is True:
            try:
                if result.get("isolation_verified") is not True: raise RuntimeError("Managed process isolation was not established")
                self.verify_freeze_receipt(baseline, result)
                result["launch_freeze_receipt_verified"] = True
            except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
                self.deny_startup_result(result, "Cold-freeze startup evidence refused: " + str(error)[:384])
        self.health_receipt(receipt, result)
        if result.get("ready") is not True or result.get("status") != "ready":
            self.recovery_event("startup_not_ready", startup_result=result.get("status"), reason=result.get("reason"),
                                native_restart_still_required=receipt.startswith("activation"))
            raise RuntimeError("SteamVR compositor/UI startup not ready: " + str(result.get("reason")))
        return result

    @staticmethod
    def deny_startup_result(result, reason):
        result.update(status="fault", ready=False, reason=reason, launch_freeze_receipt_verified=False)
        checkpoint = result.get("gate_checkpoint")
        if isinstance(checkpoint, dict):
            checkpoint.update(ready=False, ready_since=None, failed=reason)

    def configure_startup_isolation(self, manifest, *, active):
        if isinstance(self.health_source, health.NativeEvidenceSource):
            self.health_source.isolation_descriptor = health.require_isolation_descriptor(manifest["launch_isolation"] if active else None)

    def verify_freeze_receipt(self, baseline, result):
        # The header is immutable; its writer may still append later records.
        # Require a stable owned node and bounded file, not a frozen mtime.
        path = safe_path(self.capture)
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            before = os.fstat(stream.fileno())
            node = lambda item: (item.st_dev, item.st_ino, item.st_mode, item.st_uid, item.st_nlink)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid() or
                    stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1 or not 0 < before.st_size <= 65536):
                raise RuntimeError("Private bounded raw startup file required")
            raw = stream.read(8193)
            after, current = os.fstat(stream.fileno()), path.lstat()
            if (node(before) != node(after) or node(before) != node(current) or
                    after.st_size < len(raw) or after.st_size < before.st_size or after.st_size > 65536 or current.st_size > 65536):
                raise RuntimeError("Raw startup node changed or truncated")
        first = raw.split(b"\n", 1)[0]
        if len(first) > 8192 or b"\n" not in raw: raise RuntimeError("Bounded complete raw startup header required")
        header = json.loads(first)
        if not isinstance(header, dict): raise RuntimeError("Raw startup header must be an object")
        pid = result["process_identities"]["compositor"]["pid"]
        if (header.get("schema") != 1 or header.get("capture") != "compositor_raw_import" or
                type(header.get("pid")) is not int or header["pid"] != pid or
                header.get("launch_environment_frozen") is not True or
                any(type(header.get(key)) is not int for key in ("launch_freeze_abi", "launch_provider_mask", "launch_frozen_owner_pid")) or
                header.get("launch_freeze_abi") != 1 or header.get("launch_provider_mask") != 63 or header.get("launch_frozen_owner_pid") != pid or
                type(header.get("launch_frozen_observed_ns")) is not int or
                not 0 < header["launch_frozen_observed_ns"] < 2**64 or
                not baseline["monotonic"] * 1000000000 < header["launch_frozen_observed_ns"] <= self.health_source.clock() * 1000000000):
            raise RuntimeError("Fresh current-compositor cold-freeze receipt required")

    def monitor_startup(self, *, seconds=10.0):
        with self.lock():
            m = self.manifest(); self.verify_owned(m)
            active = self.dropin.exists() or self.layer.exists()
            self.configure_startup_isolation(m, active=active)
            if active: self.active_environment(m, post_restart=True)
            else:
                if not self.completed_restoration(): raise RuntimeError("Completed startup/restoration required before monitoring")
                self.restoration_state(m, post_restart=True)
            name = "activation-startup-health.json" if active else "restoration-startup-health.json"
            saved = json.loads(read_regular(self.folder / name, 262144, private=True))
            previous = self.folder / "startup-monitor.json"
            if previous.exists():
                observed = json.loads(read_regular(previous, 262144, private=True))
                if observed.get("startup_receipt") == name: saved = observed
            if saved.get("status") != "ready" or saved.get("ready") is not True:
                raise RuntimeError("A failed startup receipt cannot authorize monitoring")
            if active and (saved.get("isolation_verified") is not True or saved.get("launch_freeze_receipt_verified") is not True):
                raise RuntimeError("Verified cold-freeze isolation required before monitoring")
            gate = health.StartupGate.resume(saved["gate_checkpoint"])
            self.health_source.resume(saved["source_checkpoint"])
            if active: self.verify_freeze_receipt(gate.baseline, saved)
            result = health.monitor(self.health_source, gate, seconds=seconds, clock=self.health_source.clock, sleep=self.health_source.sleep)
            result["startup_receipt"] = name
            result["launch_freeze_receipt_verified"] = active
            self.health_receipt("startup-monitor.json", result)
            if result.get("ready") is not True:
                self.recovery_event("startup_monitor_not_ready", reason=result.get("reason"), automatic_restart=False)
                raise RuntimeError("SteamVR compositor/UI health lost: " + str(result.get("reason")))
            return result

    def start(self, *args, runtime_scope=False, restart_ready=False, **kwargs):
        if runtime_scope is not True or restart_ready is not True: raise RuntimeError("Scope and coordinated restart required")
        self.stage(*args, runtime_scope=True, **kwargs)
        self.activate(runtime_scope=True, restart_ready=True)

    def remove_activation(self, m):
        refused = []
        for name in (str(self.layer), str(self.dropin)):
            live = Path(name); pending = live.with_suffix(".pending"); owned = bytes.fromhex(m["activation"][name]["bytes_hex"])
            if pending.exists() or pending.is_symlink():
                try:
                    self.remove_pending(pending, live, owned)
                except (OSError, RuntimeError) as exc: refused.append(str(exc))
            if live.exists() or live.is_symlink():
                try:
                    if read_regular(live, private=True) != owned: raise RuntimeError("Foreign/tampered activation preserved")
                    live.unlink(); directory_sync(live.parent)
                except (OSError, RuntimeError) as exc: refused.append(str(exc))
        if refused: raise RuntimeError("Manual activation recovery required: " + "; ".join(refused))

    @staticmethod
    def remove_pending(pending, live, owned):
        path = safe_path(pending)
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        with os.fdopen(fd, "rb") as stream:
            before = os.fstat(stream.fileno()); current = path.lstat()
            fields = lambda x: (x.st_dev, x.st_ino, x.st_size, x.st_mtime_ns, x.st_mode, x.st_uid, x.st_nlink)
            pair = live.lstat() if live.exists() and not live.is_symlink() else None
            two_owned = before.st_nlink == 2 and pair is not None and (pair.st_dev, pair.st_ino) == (before.st_dev, before.st_ino)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid() or stat.S_IMODE(before.st_mode) != 0o600 or
                (before.st_nlink != 1 and not two_owned) or before.st_size != len(owned) or stream.read(len(owned) + 1) != owned or
                fields(before) != fields(current) or fields(os.fstat(stream.fileno())) != fields(before) or fields(path.lstat()) != fields(current)):
                raise RuntimeError("Partial/foreign pending activation preserved")
        pending.unlink()

    def readcheck(self):
        with self.lock():
            m = self.manifest(); self.verify_owned(m)
            active = self.dropin.exists() or self.layer.exists()
            if active:
                for name, item in m["activation"].items():
                    if read_regular(Path(name), private=True) != bytes.fromhex(item["bytes_hex"]): raise RuntimeError("Owned activation changed")
                self.active_environment(m, post_restart=(self.folder / "activation-startup-baseline.json").exists())
            else: self.restoration_state(m, post_restart=self.completed_restoration())
            return {"owned_files_verified": True, "native_files_verified": True, "service_environment_verified": True,
                    "active": active, "startup_health_current_verified": False, "capture_verified": False, "native_mapping_unload_verified": False,
                    "native_layer_node_drift": self.native_layer_node_drift}

    def rollback(self, *, restart_ready=False):
        if not self.folder.exists():
            for path in (self.dropin, self.layer, self.dropin.with_suffix(".pending"), self.layer.with_suffix(".pending")):
                if path.exists() or path.is_symlink(): raise RuntimeError("Foreign activation without owned session")
            return
        with self.lock():
            m = self.manifest(); complete = self.folder / "rollback-complete.json"
            if complete.exists():
                recorded = json.loads(read_regular(complete, private=True))
                if any(recorded.get(k) is not True for k in ("restored", "runtime_activation_absent", "original_service_matches", "native_environment_files_match", "startup_health_ready")):
                    raise RuntimeError("Invalid restoration record")
                for path in (self.dropin, self.layer, self.dropin.with_suffix(".pending"), self.layer.with_suffix(".pending")):
                    if path.exists() or path.is_symlink(): raise RuntimeError("Activation appeared after restoration")
                self.restoration_state(m, post_restart=True); return
            self.remove_activation(m)
            if not (self.folder / "archive").exists(): self.recovery_event("activation_removed", restart_readiness=restart_ready)
            if restart_ready is not True:
                self.recovery_event("restart_pending", loaded_processes_may_remain=True)
                self.revalidate_native_layers(m, allow_node_rewrite=True)
                return
            # Once exact own activation bytes are removed, an authorized
            # original-service restart takes priority over baseline reports.
            # Changed native manifests are preserved, never repaired here.
            # They still prevent a successful restoration claim below.
            self.command("daemon-reload")
            self.configure_startup_isolation(m, active=False)
            try:
                baseline = self.health_source.baseline()
                self.health_receipt("restoration-startup-baseline.json", baseline)
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
                # Missing evidence must never prevent the already-authorized
                # original-service restart after own activation is removed.
                # It does prevent declaring the recovered UI healthy.
                baseline = None
                try:
                    self.health_receipt("restoration-startup-health.json", {"status": "not-ready", "ready": False,
                        "reason": "Restoration baseline unavailable: " + str(error)[:384], "automatic_restart": False})
                except (OSError, RuntimeError, ValueError):
                    # ENOSPC/read-only or an existing foreign pending receipt
                    # can defeat both writes. Preserve it, still restart the
                    # original service, and report unavailable proof below.
                    pass
            self.command("restart", UNIT); self.command("is-active", UNIT)
            comparison = self.restoration_state(m, post_restart=True)
            if baseline is None: raise RuntimeError("Original service restarted; fresh UI startup evidence unavailable")
            self.check_startup(baseline, "restoration-startup-health.json")
            archive = self.archive()
            exclusive(complete, json_bytes({"restored": True, "runtime_activation_absent": True, "original_service_matches": True,
                "native_environment_files_match": True, "startup_health_ready": True, "startup_screen_pixels_verified": False, "native_mapping_unload_verified": False, "time": time.time(), "archive": archive, **comparison}))
            directory_sync(self.folder)
            self.recovery_event("rollback_complete", native_mapping_unload_verified=False)

    def completed_restoration(self):
        path = self.folder / "rollback-complete.json"
        if not path.exists() and not path.is_symlink(): return False
        recorded = json.loads(read_regular(path, private=True))
        if not isinstance(recorded, dict) or any(recorded.get(key) is not True for key in
               ("restored", "runtime_activation_absent", "original_service_matches", "native_environment_files_match", "startup_health_ready")):
            raise RuntimeError("Invalid restoration record")
        return True

    def restoration_state(self, m, *, post_restart=False):
        if type(post_restart) is not bool: raise RuntimeError("Explicit post-restart comparison phase required")
        restored = self.state();self._owned_caller = self.activation_values()
        try: manager = self.reject_environment(restored)
        finally: self._owned_caller = None
        snapshots = self.native_environment_files(restored, {n: v["sha256"] for n, v in m["original_environment_files"].items()})
        # SteamVR can recreate the graphical session during the coordinated
        # restart. Accept only the separately bounded metadata grammar here;
        # never write the old manager values back or relax pre-restart checks.
        comparison = base.compare_manager(m["original_manager_environment"], manager, self.runtime,
                                          allow_graphical_session_metadata=post_restart)
        if (restored != m["original_service"] or snapshots != m["original_environment_files"] or
            not comparison["manager_matches_except_session_metadata"]):
            raise RuntimeError("Original service/manager/native-file snapshot not restored")
        self.revalidate_native_layers(m, state=restored, manager=manager, environment_files=snapshots, allow_node_rewrite=post_restart)
        return {**comparison, "native_layer_nodes_exact": not self.native_layer_node_drift, "native_layer_node_drift": self.native_layer_node_drift}

    def verify_unloaded(self):
        # Explicit /proc metadata only. Neither mapped memory nor map_files is
        # read. Configuration restoration alone never claims library unload.
        with self.lock():
            m = self.manifest()
            if not self.completed_restoration(): raise RuntimeError("Manual restoration required before unload verification")
            self.restoration_state(m, post_restart=True)
            entries = list(self.proc.iterdir())
            if len(entries) > 8192: raise RuntimeError("Process directory bound exceeded")
            owned, managed = 0, 0
            for process in entries:
                if not process.name.isdigit(): continue
                try:
                    before = process.lstat()
                    if not stat.S_ISDIR(before.st_mode) or before.st_uid != os.getuid(): continue
                    owned += 1
                    if owned > 512: raise RuntimeError("Owned process bound exceeded")
                    def identity():
                        text = self.proc_bytes(process / "stat", 8192).decode("ascii")
                        end = text.rfind(")"); columns = text[end + 2:].split()
                        if end < 0 or text.split(" ", 1)[0] != process.name or len(columns) < 20:
                            raise RuntimeError("Unsupported process start identity")
                        return int(columns[19])
                    original = identity()
                    groups = self.proc_bytes(process / "cgroup", 8192).decode("ascii").splitlines()
                    relevant = any(len(row.split(":", 2)) == 3 and UNIT in Path(row.split(":", 2)[2]).parts for row in groups)
                    managed += bool(relevant)
                    maps = self.proc_bytes(process / "maps", 1048576).decode("utf-8").splitlines()
                    if len(maps) > 8192: raise RuntimeError("Process mapping bound exceeded")
                    for row in maps:
                        columns = row.split(None, 5)
                        if len(columns) == 6 and (columns[5].removesuffix(" (deleted)") in (str(self.library), str(self.audit), str(self.selector)) or
                            columns[5].startswith(str(self.folder) + "/")):
                            raise RuntimeError("Diagnostic mapping remains in an owned process")
                    if identity() != original:
                        raise RuntimeError("Process identity changed during unload verification")
                except FileNotFoundError:
                    # A process that exited cannot still retain a mapping.
                    continue
            if not managed: raise RuntimeError("No original SteamVR service process observed")
            return {"native_mapping_unload_verified": True, "owned_processes_checked": owned,
                    "steamvr_processes_checked": managed, "verification_scope": "bounded current proc metadata; not continuous lifetime proof"}

    def archive(self):
        destination = self.folder / "archive"; safe_path(destination)
        if not destination.exists(): destination.mkdir(mode=0o700)
        info = destination.lstat()
        if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700 or not stat.S_ISDIR(info.st_mode):
            raise RuntimeError("Archive directory ownership changed")
        names = {"owned.json", "deployment-plan.json", "session.jsonl", "compositor-raw.jsonl", "compositor-journal.jsonl"} | STARTUP_RECEIPTS
        if any(p.name not in names | {"expected.json"} for p in destination.iterdir()): raise RuntimeError("Foreign archive preserved")
        payloads, items = {}, {}
        for name in sorted(names):
            source = self.folder / name
            if not source.exists(): continue
            limit = 65536 if name == "compositor-raw.jsonl" else 262144 if name in STARTUP_RECEIPTS else base.LIMIT
            raw = read_regular(source, limit, private=True)
            payloads[name] = raw; items[name] = {"bytes": len(raw), "sha256": digest(raw)}
        expected = destination / "expected.json"
        if expected.exists():
            if json.loads(read_regular(expected, private=True)) != items: raise RuntimeError("Archive source changed")
        elif any(destination.iterdir()): raise RuntimeError("Archive lacks owned snapshot")
        else: exclusive(expected, json_bytes(items))
        for name, raw in payloads.items():
            target = destination / name
            if target.exists():
                if read_regular(target, base.LIMIT, private=True) != raw: raise RuntimeError("Partial/tampered archive preserved")
            else: exclusive(target, raw)
        directory_sync(destination); return items


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("stage", "readcheck", "activate", "start", "rollback", "verify-unloaded", "monitor-startup"))
    p.add_argument("--reviewed-plan", type=Path); p.add_argument("--reviewed-plan-sha256")
    for role in NATIVE: p.add_argument("--native-" + role, type=Path)
    for artifact in ("observer", "audit", "selector", "envelope", "layer"): p.add_argument("--" + artifact, type=Path)
    p.add_argument("--reviewed-session-helper-sha256"); p.add_argument("--reviewed-receiver-helper-sha256")
    p.add_argument("--reviewed-startup-health-helper-sha256"); p.add_argument("--monitor-seconds", type=float, default=10.0)
    p.add_argument("--reviewed-environment-file", action="append", default=[], metavar="PATH=SHA256")
    p.add_argument("--reviewed-native-layer", action="append", default=[], metavar="PATH=SHA256")
    p.add_argument("--ack-compositor-raw-diagnostic-scope", action="store_true")
    p.add_argument("--ack-steamvr-restart-readiness", action="store_true")
    a = p.parse_args(argv)
    try:
        if platform.system() != "Linux" or platform.machine().lower() not in ("aarch64", "arm64"):
            raise RuntimeError("Native Linux/AArch64 user session required")
        runtime = Path(os.environ.get("XDG_RUNTIME_DIR", ""))
        if runtime != Path(f"/run/user/{os.getuid()}"): raise RuntimeError("Native user runtime directory required")
        session = Session(Path.home(), runtime)
        if a.mode in ("stage", "start"):
            native = {role: getattr(a, "native_" + role) for role in NATIVE}
            if any(v is None for v in (*native.values(), a.reviewed_plan, a.reviewed_plan_sha256, a.observer, a.audit, a.selector, a.envelope, a.layer)):
                raise RuntimeError("Reviewed plan and every fresh native/own artifact path required")
            reviewed = dict(entry.rsplit("=", 1) for entry in a.reviewed_environment_file)
            if len(reviewed) != len(a.reviewed_environment_file): raise RuntimeError("Duplicate reviewed environment file")
            layers = dict(entry.rsplit("=", 1) for entry in a.reviewed_native_layer)
            if len(layers) != len(a.reviewed_native_layer): raise RuntimeError("Duplicate reviewed native layer")
            kwargs = {"runtime_scope": a.ack_compositor_raw_diagnostic_scope,
                "reviewed_helpers": {HELPERS[0]: a.reviewed_session_helper_sha256, HELPERS[1]: a.reviewed_receiver_helper_sha256, HELPERS[2]: a.reviewed_startup_health_helper_sha256},
                "reviewed_environment_files": reviewed, "reviewed_native_layers": layers}
            if a.mode == "start": kwargs["restart_ready"] = a.ack_steamvr_restart_readiness
            getattr(session, a.mode)(a.reviewed_plan, a.reviewed_plan_sha256, native,
                {MAIN: a.observer, AUDIT: a.audit, SELECTOR: a.selector, ENVELOPE: a.envelope, LAYER: a.layer}, **kwargs)
        elif a.mode == "activate": session.activate(runtime_scope=a.ack_compositor_raw_diagnostic_scope,
            restart_ready=a.ack_steamvr_restart_readiness)
        elif a.mode == "rollback": session.rollback(restart_ready=a.ack_steamvr_restart_readiness)
        elif a.mode == "monitor-startup": print(json.dumps(session.monitor_startup(seconds=a.monitor_seconds), sort_keys=True))
        elif a.mode == "verify-unloaded": print(json.dumps(session.verify_unloaded(), sort_keys=True))
        else: print(json.dumps(session.readcheck(), sort_keys=True))
    except (OSError, RuntimeError, ValueError, TypeError, KeyError, struct.error, subprocess.SubprocessError) as error:
        p.exit(1, f"Compositor raw session refused: {type(error).__name__}: {error}\n")
    print("Manual operation completed; capture, mapping unload and passthrough are never inferred.")
    return 0

if __name__ == "__main__": raise SystemExit(main())
