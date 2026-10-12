#!/usr/bin/env python3
"""Explicitly authorized, reversible USER-service startup-diagnostic session.

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
safe_path, read_regular, exclusive = base.safe_path, base.read_regular, base.exclusive
digest, json_bytes, private_parents = base.digest, base.json_bytes, base.private_parents
UNIT = "steam.service"
SCHEMA = "frame-client-startup-offline-bundle-v1"
SESSION_SCHEMA = "frame-client-startup-owned-session-v1"
MAIN, AUDIT, ENVELOPE = "libframe_client_startup.so", "libframe_client_startup_audit.so", "client-startup-envelope.bin"
HELPERS = ("client_startup_session.py", "receiver_observer_session.py")
RESOURCE_SCOPE = "frame_client_resource_census_scope"
RESOURCE_MARKER = b"FCCENS01" + struct.pack("<II", 3, 11800)
RESOURCE_THREAD_SCOPE = "frame_client_resource_thread_scope"
RESOURCE_THREAD_MARKER = b"FCSTAK01" + struct.pack("<II", 4, 11800)
RESOURCE_THREAD_START_RVA = "0x328d0"
EPOLL_WAIT_RVA = "0xe9e10"
RESOURCE_BOUNDARIES = {"begin":"0x3623c0", "pending":"0x3624c4", "reference":"0x362d58"}
CAPTURE_LIMITS = {"records":32, "bytes":65536, "seconds":120}


def resource_marker(data,*,thread_preparation=False):
    """Read bounded own ELF scope metadata without loading or native reads."""
    scope_name=RESOURCE_THREAD_SCOPE if thread_preparation else RESOURCE_SCOPE
    marker=RESOURCE_THREAD_MARKER if thread_preparation else RESOURCE_MARKER
    if len(data)<64:raise RuntimeError("Resource census ELF metadata missing")
    h=struct.unpack_from("<16sHHIQQQIHHHHHH",data)
    if h[0][:7]!=b"\x7fELF\x02\x01\x01" or h[1:4]!=(3,183,1) or h[8:10]!=(64,56) or h[11]!=64:
        raise RuntimeError("Resource census ELF ABI changed")
    if not 0<h[10]<=16 or not 0<h[12]<=4096 or h[5]+56*h[10]>len(data) or h[6]+64*h[12]>len(data):
        raise RuntimeError("Resource census ELF table bounds")
    phdrs=[struct.unpack_from("<IIQQQQQQ",data,h[5]+56*i) for i in range(h[10])]
    sections=[struct.unpack_from("<IIQQQQIIQQ",data,h[6]+64*i) for i in range(h[12])]
    matches=[]
    for s in sections:
        if s[1] not in (2,11):continue
        if s[9]!=24 or s[5]%24 or s[5]//24>65536 or s[4]+s[5]>len(data) or s[6]>=len(sections):
            raise RuntimeError("Resource census ELF symbol bounds")
        strings=sections[s[6]]
        if strings[1]!=3 or strings[4]+strings[5]>len(data):raise RuntimeError("Resource census ELF string bounds")
        for i in range(s[5]//24):
            name,info,other,index,rva,size=struct.unpack_from("<IBBHQQ",data,s[4]+24*i)
            if name>=strings[5]:raise RuntimeError("Resource census ELF string offset")
            begin=strings[4]+name;end=data.find(b"\0",begin,min(strings[4]+strings[5],begin+512))
            if end<0:raise RuntimeError("Resource census ELF unterminated symbol")
            if data[begin:end]!=scope_name.encode():continue
            if not index or info&15!=1 or (info>>4!=0 and other&3!=2) or other&3 not in (0,2):
                raise RuntimeError("Resource census opt-in is not a hidden object")
            matches.append((rva,size))
    if len(set(matches))!=1:raise RuntimeError("Missing/ambiguous resource census opt-in object")
    rva,size=matches[0]
    spans=[ph for ph in phdrs if ph[0]==1 and ph[1]==4 and size==16 and ph[3]<=rva and
           rva+size<=ph[3]+ph[5] and ph[2]+ph[5]<=len(data)]
    if len(spans)!=1:raise RuntimeError("Resource census opt-in is not read-only file-backed metadata")
    offset=spans[0][2]+rva-spans[0][3]
    if data[offset:offset+size]!=marker:raise RuntimeError("Resource census opt-in magic/schema/size mismatch")
    return {"symbol":scope_name,"rva":rva,"bytes":size,"sha256":digest(marker)}


def diagnostic_mode(plan):
    connection=plan.get("connection_register_observation",False)
    census=plan.get("resource_register_census",False)
    thread=plan.get("resource_thread_preparation",False)
    if (any(type(value) is not bool for value in (connection,census,thread)) or
        (census and not connection) or (thread and not census)):
        raise RuntimeError("Resource census requires explicit Boolean connection observation")
    return "resource-thread" if thread else "resource-census" if census else "connection" if connection else "startup"


NATIVE = {
    "receiver": ("vrlink", "31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153"),
    "api": ("libopenvr_api.so", "4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4"),
    "libc": ("libc.so.6", "be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b"),
    "loader": ("ld-linux-aarch64.so.1", "2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e"),
    "client": ("vrclient.so", "19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0"),
}
PROFILE_PINS = ("2f5ccf52a5ebbb4b7d36a9508451247bd370b03c0c1b3a4d4b18dcb9322b655c",
    "9ef296865d2583cd8136303727db150f6e4a349ffe7b175e3e0f651adfb81dcf",
    "48edb257be746f755a00ff564c6db4284526b15ffc7e6bad5b8d6723b1891512",
    "51af1fa58396e7a1ab48760ce90d90e919db3e42653504ad550eb54e129a7405")
CLIENT_PROFILE = "1da2390f9d65a5271f0750d9d5558e90716c35629f2f39583ba5aeef2d451b71"
PREFIXES = ("FRAME_RECEIVER_", "FRAME_MASK_TRACE", "FRAME_IMPORT_", "FRAME_CLIENT_",
            "GHVR_FRAME_CLIENT_STARTUP_", "MRCHROMA_")
BLOCKED = {"LD_PRELOAD", "LD_AUDIT", "LD_BIND_NOW", "LD_BIND_NOT", "LD_DEBUG", "LD_DEBUG_OUTPUT", "LD_PROFILE",
           "LD_PROFILE_OUTPUT", "VK_LOADER_DEBUG", "VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH"}
MAX_INPUT, CAPTURE_LIMIT = 8 * 1024 * 1024, 65536
API_ENTRY_RVAS = {"init": "0x5768", "shutdown": "0x5330"}
MUTEX_ENTRY_RVAS = {"init": "0x86a00", "destroy": "0x86980"}

def blocked(key, value):
    return bool(value) and (key in BLOCKED or key.startswith(PREFIXES))

def directory_sync(path):
    fd = os.open(safe_path(path), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try: os.fsync(fd)
    finally: os.close(fd)

class Session(base.Session):
    def __init__(self, home, runtime, run=subprocess.run, environment=None):
        super().__init__(home, runtime, run, environment)
        self.folder = self.home / ".local/share/GloomhavenVR/frame-client-startup"
        self.dropin = self.runtime / "systemd/user/steam.service.d/94-frame-client-startup.conf"
        self.library, self.audit = self.folder / MAIN, self.folder / AUDIT
        self.capture, self.envelope = self.folder / "client-startup.jsonl", self.folder / ENVELOPE
        self.pending = self.dropin.with_suffix(".pending")
        self._owned_caller = None

    def activation_values(self):
        return {"LD_PRELOAD": str(self.library), "LD_AUDIT": str(self.audit),
                "GHVR_FRAME_CLIENT_STARTUP_ENVELOPE": str(self.envelope),
                "GHVR_FRAME_CLIENT_STARTUP_OUTPUT": str(self.capture)}

    def activation(self):
        values = self.activation_values()
        if any(re.search(r'[\s%\\"\x00-\x1f]', v) for v in values.values()):
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

    def reject_old_activation(self):
        paths = [self.runtime / f"systemd/user/{unit}.d/{name}" for unit, name in (
            ("steam.service", "91-frame-receiver-observer.conf"),
            ("steamvr.service", "91-frame-receiver-observer.conf"),
            ("steamvr.service", "90-frame-mask-trace.conf"))]
        paths.append(self.home / ".local/share/vulkan/implicit_layer.d/frame_mask_trace.json")
        for path in paths:
            safe_path(path)
            if path.exists() or path.is_symlink(): raise RuntimeError("Old receiver/trace activation must be restored first")

    @staticmethod
    def elf(data):
        return len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01" and data[16:20] == b"\x03\x00\xb7\x00"

    def validate_bundle(self, path, expected, native, artifacts, reviewed_helpers):
        raw = read_regular(path, private=True)
        if not re.fullmatch(r"[0-9a-f]{64}", expected or "") or digest(raw) != expected:
            raise RuntimeError("Independently reviewed plan SHA required")
        plan = json.loads(raw)
        if (plan.get("schema") != SCHEMA or plan.get("prepared") is not True or
            plan.get("frame_directory") != str(self.folder) or plan.get("frame_user_home") != str(self.home) or
            plan.get("directory_mode") != "0700" or plan.get("environment") != self.activation_values()):
            raise RuntimeError("Unsupported bundle scope or paths")
        for flag in ("installed", "activated", "runtime_authorized", "frame_access", "network_access",
                     "native_binary_or_script_changes", "source_frame_associated", "gpu_coherent"):
            if plan.get(flag) is not False: raise RuntimeError("Offline plan exceeds this passive scope")
        if set(native) != set(NATIVE) or set(artifacts) != {MAIN, AUDIT, ENVELOPE} or set(reviewed_helpers) != set(HELPERS):
            raise RuntimeError("Exact fresh native files, three own artifacts and reviewed helpers required")
        inventory = plan.get("inventory", [])
        if not isinstance(inventory, list) or len(inventory) != 4:
            raise RuntimeError("Exact bundle inventory required")
        entries = {item.get("name"): item for item in inventory if isinstance(item, dict)}
        if set(entries) != {MAIN, AUDIT, ENVELOPE, "audit-preparation.cpp"}:
            raise RuntimeError("Unexpected or duplicate inventory entry")
        for name, item in entries.items():
            if (item.get("owned_source") is not True or item.get("native_bytes") is not False or item.get("mode") != "0600" or
                item.get("frame_path") != (None if name == "audit-preparation.cpp" else str(self.folder / name))):
                raise RuntimeError("Native/source artifact may not be deployed")
        files, payloads = {}, {}
        for role, source in native.items():
            data = read_regular(source, MAX_INPUT)
            if Path(source).name != NATIVE[role][0] or digest(data) != NATIVE[role][1] or not self.elf(data):
                raise RuntimeError("Fresh canonical native hash/ABI mismatch")
            files[role] = {"path": str(safe_path(source)), "sha256": digest(data), "bytes": len(data)}
        for name, source in artifacts.items():
            data = read_regular(source, 256 * 1024 if name == AUDIT else MAX_INPUT, private=True)
            if entries[name].get("sha256") != digest(data) or entries[name].get("bytes") != len(data):
                raise RuntimeError("Reviewed own artifact changed")
            if name != ENVELOPE and not self.elf(data): raise RuntimeError("Own artifact ABI mismatch")
            payloads[name] = data
        if (plan.get("observer", {}).get("sha256") != digest(payloads[MAIN]) or
            plan.get("audit", {}).get("sha256") != digest(payloads[AUDIT]) or
            plan.get("envelope", {}).get("sha256") != digest(payloads[ENVELOPE])):
            raise RuntimeError("Plan/artifact report mismatch")
        if plan.get("audit", {}).get("native_api_rvas") != API_ENTRY_RVAS:
            raise RuntimeError("Audit route differs from the canonical Steam-runtime API")
        if plan.get("audit", {}).get("native_mutex_rvas") != MUTEX_ENTRY_RVAS:
            raise RuntimeError("Audit mutex route differs from canonical libc")
        libc_slot = plan.get("libc_slot", {})
        libc_name = libc_slot.get("exact_loader_name")
        if (not isinstance(libc_name, str) or not 0 < len(libc_name.encode('utf-8')) < 512 or
            any(ord(c) < 32 for c in libc_name) or
            (libc_slot.get("rx_rva_begin"), libc_slot.get("rx_rva_end")) != (0, 1637944)):
            raise RuntimeError("Explicit libc loader name and canonical RX geometry required")
        self.validate_envelope(payloads[ENVELOPE], plan, files, payloads[MAIN])
        for name, source in ((HELPERS[0], Path(__file__)), (HELPERS[1], Path(base.__file__))):
            data = read_regular(source)
            if not re.fullmatch(r"[0-9a-f]{64}", reviewed_helpers[name] or "") or digest(data) != reviewed_helpers[name]:
                raise RuntimeError("Separately reviewed helper SHA required")
            payloads[name] = data
        payloads["deployment-plan.json"] = raw
        return files, payloads

    @staticmethod
    def validate_envelope(data, plan, files, observer):
        scope=diagnostic_mode(plan)
        mode=plan.get("connection_register_observation",False)
        census=scope in ("resource-census","resource-thread");thread=scope=="resource-thread"
        magic, schema=(b"FCSTAK01",4) if thread else (b"FCCENS01",3) if census else (b"FCCONN01",2) if mode else (b"FCSTART1",1)
        # Refuse a broader retained ELF opt-in even when plan metadata is
        # downgraded. Raw symbol presence grants no authority; it only refuses.
        if not thread and RESOURCE_THREAD_SCOPE.encode()+b"\0" in observer:
            raise RuntimeError("Resource thread artifact requires its distinct explicit mode")
        if mode and (plan.get("connection_private_bytes_read") != 0 or plan.get("continuous_connection_owned") is not False or
                     plan.get("audit",{}).get("native_connection_rvas") != {"lock":"0x87370","unlock":"0x88f74","connect":"0xebcb0"} or
                     plan.get("observer",{}).get("connection_register_observation") is not True or
                     plan.get("observer",{}).get("audit_only_libc_routing") is not True or
                     plan.get("observer",{}).get("public_mutex_aliases") != {}):
            raise RuntimeError("Connection scope differs from reviewed register-only observation")
        if not mode and plan.get("audit",{}).get("native_connection_rvas",{}):
            raise RuntimeError("Startup-only scope cannot acquire connection providers")
        resource_keys={"resource_register_census","resource_private_bytes_read","resource_transaction_accepted","resource_namespace_associated",
                       "resource_thread_preparation","resource_thread_private_bytes_read","resource_thread_start_boundary_rva"}
        for metadata,extra in ((plan,{"resource_source_boundaries"}),(plan.get("observer",{}),{"resource_scope_marker","resource_thread_scope_marker"})):
            if any(key.startswith("resource_") and key not in resource_keys|extra for key in metadata):
                raise RuntimeError("Resource census cannot acquire extra proof flags")
            if census:
                if (metadata.get("resource_register_census") is not True or
                    type(metadata.get("resource_private_bytes_read")) is not int or metadata["resource_private_bytes_read"]!=0 or
                    metadata.get("resource_transaction_accepted") is not False or metadata.get("resource_namespace_associated") is not False):
                    raise RuntimeError("Resource census exceeds reviewed register-only scope")
            elif (metadata.get("resource_register_census",False) is not False or
                  type(metadata.get("resource_private_bytes_read",0)) is not int or metadata.get("resource_private_bytes_read",0)!=0 or
                  metadata.get("resource_transaction_accepted",False) is not False or metadata.get("resource_namespace_associated",False) is not False):
                raise RuntimeError("Non-census scope cannot acquire resource observation")
            if thread:
                if (metadata.get("resource_thread_preparation") is not True or
                    type(metadata.get("resource_thread_private_bytes_read")) is not int or metadata["resource_thread_private_bytes_read"]!=0 or
                    metadata.get("resource_thread_start_boundary_rva")!=RESOURCE_THREAD_START_RVA or metadata.get("native_epoll_wait_rva")!=EPOLL_WAIT_RVA):
                    raise RuntimeError("Resource thread preparation exceeds reviewed one-time scope")
            elif (metadata.get("resource_thread_preparation",False) is not False or
                  type(metadata.get("resource_thread_private_bytes_read",0)) is not int or metadata.get("resource_thread_private_bytes_read",0)!=0 or
                  metadata.get("resource_thread_start_boundary_rva") is not None or metadata.get("native_epoll_wait_rva") is not None):
                raise RuntimeError("Non-thread scope cannot acquire decoder-thread preparation")
        if census:
            if (plan.get("resource_source_boundaries")!=RESOURCE_BOUNDARIES or plan.get("capture_limits")!=CAPTURE_LIMITS or
                any(type(value) is not int for value in plan.get("capture_limits",{}).values()) or
                plan.get("observer",{}).get("resource_scope_marker")!=resource_marker(observer,thread_preparation=thread)):
                raise RuntimeError("Resource census artifact/boundaries/caps differ from explicit scope")
        elif plan.get("resource_source_boundaries",{}) or plan.get("observer",{}).get("resource_scope_marker") is not None:
            raise RuntimeError("Non-census scope cannot acquire resource boundaries")
        if thread:
            if (plan.get("observer",{}).get("resource_thread_scope_marker")!=resource_marker(observer,thread_preparation=True) or
                plan.get("audit",{}).get("native_epoll_wait_rva")!=EPOLL_WAIT_RVA):
                raise RuntimeError("Resource thread marker/provider route differs from explicit scope")
        elif plan.get("observer",{}).get("resource_thread_scope_marker") is not None or plan.get("audit",{}).get("native_epoll_wait_rva") is not None:
            raise RuntimeError("Non-thread scope cannot acquire native event-wait routing")
        if len(data) != 11800 or data[:8] != magic or struct.unpack_from("<II", data, 8) != (schema, 11800):
            raise RuntimeError("Envelope schema changed")
        if "schema" in plan.get("envelope",{}) and plan["envelope"]["schema"]!=schema:
            raise RuntimeError("Envelope report schema changed")
        reviewed = data[16:48]
        if reviewed.hex() != plan["envelope"].get("reviewed_observer_profile_identity"):
            raise RuntimeError("Observer profile review identity mismatch")
        name = data[48:560];nul = name.find(b"\0")
        if nul <= 0 or any(name[nul:]): raise RuntimeError("Malformed exact client loader name")
        slot = plan.get("client_slot", {})
        if (name[:nul].decode("ascii") != slot.get("exact_loader_name") or
            struct.unpack_from("<QQ", data, 560) != (slot.get("rx_rva_begin"), slot.get("rx_rva_end")) or
            (slot.get("rx_rva_begin"), slot.get("rx_rva_end")) != (0, 0x60699a)):
            raise RuntimeError("Client slot differs from canonical source geometry")
        reports = plan.get("native_profiles", [])
        if not isinstance(reports, list) or len(reports) != 4: raise RuntimeError("Native profiles missing")
        for i, role in enumerate(("receiver", "api", "libc", "loader", "observer")):
            profile = data[576 + 2032*i:576 + 2032*(i+1)]
            pin = PROFILE_PINS[i] if i < 4 else reviewed.hex()
            if (struct.unpack_from("<4Q", profile) != (1, i+1, 183, 3) or digest(profile[:-32]) != pin or profile[-32:].hex() != pin):
                raise RuntimeError("Module descriptor is not independently pinned")
            sha, size = (files[role]["sha256"], files[role]["bytes"]) if i < 4 else (digest(observer), len(observer))
            if profile[176:208].hex() != sha or struct.unpack_from("<Q", profile, 32)[0] != size:
                raise RuntimeError("Fresh file differs from envelope descriptor")
            if i < 4 and (reports[i].get("role") != i+1 or reports[i].get("file_sha256") != sha or reports[i].get("profile_sha256") != pin):
                raise RuntimeError("Native report differs from canonical files")
            if i == 4 and (plan.get("observer_profile", {}).get("role") != 5 or
                plan["observer_profile"].get("file_sha256") != sha or plan["observer_profile"].get("profile_sha256") != pin):
                raise RuntimeError("Observer report differs from reviewed descriptor")
        client = data[10736:]
        if (digest(client[:-32]) != CLIENT_PROFILE or client[-32:].hex() != CLIENT_PROFILE or
            client[72:104].hex() != files["client"]["sha256"] or struct.unpack_from("<Q", client, 8)[0] != files["client"]["bytes"] or
            plan.get("client_profile", {}).get("file_sha256") != files["client"]["sha256"] or
            plan["client_profile"].get("profile_sha256") != CLIENT_PROFILE):
            raise RuntimeError("Client descriptor/fresh file mismatch")

    def stage(self, path, expected, native, artifacts, *, runtime_scope=False, connect_scope=False, resource_scope=False, resource_thread_scope=False, reviewed_helpers=None,
              reviewed_environment_files=None):
        if type(resource_scope) is not bool:raise RuntimeError("Resource scope acknowledgment must be an explicit Boolean")
        if type(resource_thread_scope) is not bool:raise RuntimeError("Resource thread scope acknowledgment must be an explicit Boolean")
        if not runtime_scope: raise RuntimeError("Separate startup diagnostic scope acknowledgment required")
        self.reject_old_activation();safe_path(self.folder);safe_path(self.dropin)
        if (self.folder.exists() or self.dropin.exists() or self.dropin.is_symlink() or self.pending.exists() or self.pending.is_symlink()):
            raise RuntimeError("Existing session/activation refused")
        files, payloads = self.validate_bundle(path, expected, native, artifacts, reviewed_helpers or {})
        plan=json.loads(payloads["deployment-plan.json"]);scope=diagnostic_mode(plan)
        connection_mode=plan.get("connection_register_observation",False)
        if connection_mode and not connect_scope:
            raise RuntimeError("Separate register-only connect diagnostic scope acknowledgment required")
        if (scope in ("resource-census","resource-thread") and (resource_scope is not True or runtime_scope is not True or connect_scope is not True)) or (scope not in ("resource-census","resource-thread") and resource_scope):
            raise RuntimeError("Separate resource register census scope acknowledgment must match the reviewed mode")
        if (scope=="resource-thread") is not resource_thread_scope:
            raise RuntimeError("Separate resource thread preparation acknowledgment must match the reviewed mode")
        self.command("is-active", UNIT);original = self.state()
        manager = self.reject_environment(original)
        snapshots = self.native_environment_files(original, reviewed_environment_files or {})
        private_parents(self.folder.parent);self.folder.mkdir(mode=0o700);self.folder.chmod(0o700)
        exclusive(self.folder / "operation.lock", b"");exclusive(self.folder / "session.jsonl", b"")
        with self.lock():
            activation = self.activation()
            manifest = {"schema": SESSION_SCHEMA, "unit": UNIT, "scope_acknowledged": True, "plan_sha256": expected,
                "connect_register_scope_acknowledged":bool(connection_mode and connect_scope),
                "diagnostic_mode":scope,"resource_register_scope_acknowledged":bool(scope in ("resource-census","resource-thread") and resource_scope),
                "resource_thread_scope_acknowledged":resource_thread_scope,
                "original_service": original, "original_manager_environment": manager, "original_environment_files": snapshots,
                "native_files": files, "activation": {"path": str(self.dropin), "bytes_hex": activation.hex(), "sha256": digest(activation)},
                "owned_files": {name: digest(data) for name, data in payloads.items()}, "automatic_restart": False}
            # Ownership/snapshots precede even staging completion; interruption
            # cannot leave an enabled override without an exact recovery record.
            exclusive(self.folder / "owned.json", json_bytes(manifest))
            for name, data in payloads.items(): exclusive(self.folder / name, data)
            exclusive(self.folder / "staged.json", json_bytes({"staged": True, "activated": False}))
            directory_sync(self.folder)
            self.event("staged", scope_acknowledged=True, permission_from_plan=False, activated=False)

    def manifest(self):
        self.private_folder();m = json.loads(read_regular(self.folder / "owned.json", private=True))
        activation = self.activation()
        if (m.get("schema") != SESSION_SCHEMA or m.get("unit") != UNIT or m.get("scope_acknowledged") is not True or
            m.get("automatic_restart") is not False or m.get("activation") != {"path": str(self.dropin), "bytes_hex": activation.hex(), "sha256": digest(activation)} or
            set(m.get("owned_files", {})) != {MAIN, AUDIT, ENVELOPE, "deployment-plan.json", *HELPERS} or
            set(m.get("native_files", {})) != set(NATIVE)):
            raise RuntimeError("Invalid owned session manifest")
        if (not re.fullmatch(r"[0-9a-f]{64}", m.get("plan_sha256", "")) or
            m["owned_files"]["deployment-plan.json"] != m["plan_sha256"] or
            any(not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{64}", sha) for sha in m["owned_files"].values()) or
            set(m.get("original_service", {})) != {"Environment", "EnvironmentFiles", "DropInPaths"} or
            any(not isinstance(value, str) for value in m["original_service"].values()) or
            not isinstance(m.get("original_manager_environment"), dict) or not isinstance(m.get("original_environment_files"), dict)):
            raise RuntimeError("Invalid owned snapshot/fingerprint")
        for role, info in m["native_files"].items():
            if (not isinstance(info, dict) or set(info) != {"path", "sha256", "bytes"} or
                not isinstance(info["path"], str) or Path(info["path"]).name != NATIVE[role][0] or
                info["sha256"] != NATIVE[role][1] or type(info["bytes"]) is not int or not 64 <= info["bytes"] <= MAX_INPUT):
                raise RuntimeError("Invalid canonical native snapshot")
        return m

    def verify_owned(self, m):
        for name, sha in m["owned_files"].items():
            if Path(name).name != name or digest(read_regular(self.folder / name, private=True)) != sha:
                raise RuntimeError("Owned staged artifact/plan/helper changed")
        for role, info in m["native_files"].items():
            data = read_regular(Path(info["path"]), MAX_INPUT)
            if digest(data) != NATIVE[role][1] or digest(data) != info["sha256"] or len(data) != info["bytes"]:
                raise RuntimeError("Native files changed since reviewed staging")

    def activate(self, *, runtime_scope=False, connect_scope=False, resource_scope=False, resource_thread_scope=False, restart_ready=False):
        if type(resource_scope) is not bool:raise RuntimeError("Resource scope acknowledgment must be an explicit Boolean")
        if type(resource_thread_scope) is not bool:raise RuntimeError("Resource thread scope acknowledgment must be an explicit Boolean")
        if not runtime_scope or not restart_ready: raise RuntimeError("Scope and coordinated restart readiness required again")
        with self.lock():
            m = self.manifest();self.verify_owned(m);self.reject_old_activation()
            plan=json.loads(read_regular(self.folder / "deployment-plan.json",private=True));scope=diagnostic_mode(plan)
            connection_mode=plan.get("connection_register_observation",False)
            if connection_mode and (not connect_scope or m.get("connect_register_scope_acknowledged") is not True):
                raise RuntimeError("Separate register-only connect scope and its staged acknowledgment required")
            if (m.get("diagnostic_mode",scope if scope not in ("resource-census","resource-thread") else None)!=scope or
                (scope in ("resource-census","resource-thread") and (resource_scope is not True or runtime_scope is not True or connect_scope is not True or
                                             restart_ready is not True or m.get("resource_register_scope_acknowledged") is not True)) or
                (scope not in ("resource-census","resource-thread") and (resource_scope or m.get("resource_register_scope_acknowledged",False) is not False))):
                raise RuntimeError("Separate resource register census scope and exact staged mode required")
            if ((scope=="resource-thread") is not resource_thread_scope or
                m.get("resource_thread_scope_acknowledged",False) is not resource_thread_scope):
                raise RuntimeError("Separate resource thread preparation scope and its staged acknowledgment required")
            if ((self.folder / "rollback-complete.json").exists() or self.dropin.exists() or self.dropin.is_symlink() or
                    self.pending.exists() or self.pending.is_symlink()):
                raise RuntimeError("Existing/restored activation cannot be replaced")
            original = self.state();manager = self.reject_environment(original)
            snapshots = self.native_environment_files(original, {n: v["sha256"] for n, v in m["original_environment_files"].items()})
            if original != m["original_service"] or manager != m["original_manager_environment"] or snapshots != m["original_environment_files"]:
                raise RuntimeError("Native environment changed after staging")
            if json.loads(read_regular(self.folder / "staged.json", private=True)) != {"staged": True, "activated": False}:
                raise RuntimeError("Staging incomplete")
            private_parents(self.dropin.parent)
            try:
                # A short/full-disk write leaves only a non-.conf pending file.
                # link() publishes complete bytes exclusively without replacing
                # a foreign override, then the temporary hard link is removed.
                exclusive(self.pending, bytes.fromhex(m["activation"]["bytes_hex"]))
                os.link(self.pending, self.dropin, follow_symlinks=False)
                self.pending.unlink();directory_sync(self.dropin.parent)
                self.event("activation_created", scope_acknowledged=True, restart_readiness=True)
                self.command("daemon-reload");self.command("restart", UNIT);self.command("is-active", UNIT)
                self.active_environment(m)
                self.event("activated", actual_capture_verified=False, runtime_loaded_verified=False)
            except BaseException:
                # Disable exact owned bytes only. Never schedule/retry a restart
                # after failure/interruption: operator coordinates recovery.
                self.remove_override(m);self.recovery_event("activation_interrupted", native_restart_still_required=True)
                raise

    def active_environment(self, m):
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
                m["original_manager_environment"], manager, self.runtime)["manager_matches_except_session_paths"]:
            raise RuntimeError("Manager environment changed during activation")
        return current

    def start(self, *args, runtime_scope=False, connect_scope=False, resource_scope=False, resource_thread_scope=False, restart_ready=False, **kwargs):
        if type(resource_thread_scope) is not bool or (resource_thread_scope and resource_scope is not True):
            raise RuntimeError("Resource thread preparation requires explicit census scope")
        if type(resource_scope) is not bool or (resource_scope and
            (runtime_scope is not True or connect_scope is not True or restart_ready is not True)):
            raise RuntimeError("Resource census requires actual scope and restart readiness acknowledgments")
        if not runtime_scope or not restart_ready: raise RuntimeError("Explicit scope and restart readiness required")
        self.stage(*args, runtime_scope=True, connect_scope=connect_scope, resource_scope=resource_scope, resource_thread_scope=resource_thread_scope, **kwargs)
        self.activate(runtime_scope=True, connect_scope=connect_scope, resource_scope=resource_scope, resource_thread_scope=resource_thread_scope, restart_ready=True)

    def remove_override(self, m):
        owned = bytes.fromhex(m["activation"]["bytes_hex"])
        if self.pending.exists() or self.pending.is_symlink():
            # A crash between link and unlink can leave two *owned* links.
            # Never accept any other hard-link set or a partial pending write.
            path = safe_path(self.pending)
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
            with os.fdopen(fd, "rb") as stream:
                before = os.fstat(stream.fileno());current = path.lstat()
                fields = lambda x: (x.st_dev, x.st_ino, x.st_size, x.st_mtime_ns, x.st_mode, x.st_uid, x.st_nlink)
                pair = self.dropin.lstat() if self.dropin.exists() else None
                two_owned = (before.st_nlink == 2 and pair is not None and
                    (pair.st_dev, pair.st_ino) == (before.st_dev, before.st_ino) and not stat.S_ISLNK(pair.st_mode))
                if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid() or stat.S_IMODE(before.st_mode) != 0o600 or
                    (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) !=
                    (current.st_dev, current.st_ino, current.st_size, current.st_mtime_ns) or
                    (before.st_nlink != 1 and not two_owned) or before.st_size != len(owned) or stream.read(len(owned)+1) != owned or
                    fields(os.fstat(stream.fileno())) != fields(before) or fields(path.lstat()) != fields(current)):
                    raise RuntimeError("Partial/foreign pending activation preserved; manual recovery required")
            self.pending.unlink()
        if self.dropin.exists() or self.dropin.is_symlink():
            if read_regular(self.dropin, private=True) != owned:
                raise RuntimeError("Foreign/tampered override preserved; manual recovery required")
            self.dropin.unlink() # First recovery mutation; logging may fail.
            directory_sync(self.dropin.parent)

    def rollback(self, *, restart_ready=False):
        if not self.folder.exists():
            if self.dropin.exists() or self.dropin.is_symlink() or self.pending.exists() or self.pending.is_symlink():
                raise RuntimeError("Foreign override without owned session")
            return
        with self.lock():
            m = self.manifest();complete = self.folder / "rollback-complete.json"
            if complete.exists():
                proof = json.loads(read_regular(complete, private=True))
                if (any(proof.get(key) is not True for key in ("restored", "runtime_activation_absent",
                        "original_service_matches", "native_environment_files_match", "manager_matches_except_session_paths")) or
                        proof.get("native_mapping_unload_verified") is not False or self.dropin.exists() or self.dropin.is_symlink() or
                        self.pending.exists() or self.pending.is_symlink()):
                    raise RuntimeError("Recorded restoration changed")
                self.restoration_state(m)
                return
            self.remove_override(m)
            # Keep an interrupted archive's source ledger stable on retry.
            if not (self.folder / "archive").exists():
                self.recovery_event("activation_removed", restart_readiness=restart_ready)
            if not restart_ready:
                self.recovery_event("restart_pending", loaded_processes_may_remain=True);return
            self.command("daemon-reload");self.command("restart", UNIT);self.command("is-active", UNIT)
            comparison = self.restoration_state(m)
            archive = self.archive(m)
            receipt = {"restored": True, "runtime_activation_absent": True, "original_service_matches": True,
                "native_environment_files_match": True, "native_mapping_unload_verified": False,
                "archive": archive, "time": time.time(), **comparison}
            exclusive(complete, json_bytes(receipt));directory_sync(self.folder)
            self.recovery_event("rollback_complete", native_mapping_unload_verified=False)

    def restoration_state(self, m):
        restored = self.state();self._owned_caller = self.activation_values()
        try: manager = self.reject_environment(restored)
        finally: self._owned_caller = None
        snapshots = self.native_environment_files(restored, {n: v["sha256"] for n, v in m["original_environment_files"].items()})
        comparison = base.compare_manager(m["original_manager_environment"], manager, self.runtime)
        if (restored != m["original_service"] or snapshots != m["original_environment_files"] or
            not comparison["manager_matches_except_session_paths"]):
            raise RuntimeError("Original service/manager/native-file snapshot not restored")
        return comparison

    def archive(self, m):
        destination = self.folder / "archive";safe_path(destination)
        if not destination.exists(): destination.mkdir(mode=0o700)
        info = destination.lstat()
        if info.st_uid != os.getuid() or (info.st_mode & 0o777) != 0o700 or not destination.is_dir():
            raise RuntimeError("Archive directory ownership changed")
        names = {"owned.json", "deployment-plan.json", "session.jsonl", "client-startup.jsonl"}
        if any(p.name not in names | {"expected.json"} for p in destination.iterdir()):
            raise RuntimeError("Foreign archive file preserved")
        payloads, items = {}, {}
        for name in ("owned.json", "deployment-plan.json", "session.jsonl", "client-startup.jsonl"):
            source = self.folder / name
            if not source.exists(): continue
            raw = read_regular(source, CAPTURE_LIMIT if name == "client-startup.jsonl" else base.LIMIT, private=True)
            payloads[name] = raw;items[name] = {"bytes": len(raw), "sha256": digest(raw)}
        expected = destination / "expected.json"
        if expected.exists():
            if json.loads(read_regular(expected, private=True)) != items:
                raise RuntimeError("Archive source changed; preserved for manual review")
        elif any(destination.iterdir()):
            raise RuntimeError("Archive lacks its owned snapshot manifest")
        else: exclusive(expected, json_bytes(items))
        for name, raw in payloads.items():
            target = destination / name
            if target.exists():
                if read_regular(target, CAPTURE_LIMIT if name == "client-startup.jsonl" else base.LIMIT, private=True) != raw:
                    raise RuntimeError("Partial/tampered archive preserved for manual review")
            else: exclusive(target, raw)
        directory_sync(destination);return items

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("stage", "activate", "start", "rollback"))
    p.add_argument("--reviewed-plan", type=Path);p.add_argument("--reviewed-plan-sha256")
    for role in NATIVE: p.add_argument("--native-" + role, type=Path)
    p.add_argument("--observer", type=Path);p.add_argument("--audit", type=Path);p.add_argument("--envelope", type=Path)
    p.add_argument("--reviewed-session-helper-sha256");p.add_argument("--reviewed-receiver-helper-sha256")
    p.add_argument("--reviewed-environment-file", action="append", default=[], metavar="PATH=SHA256")
    p.add_argument("--ack-startup-diagnostic-scope", action="store_true")
    p.add_argument("--ack-connect-register-diagnostic-scope", action="store_true")
    p.add_argument("--ack-resource-register-census-scope", action="store_true")
    p.add_argument("--ack-resource-thread-preparation-scope", action="store_true")
    p.add_argument("--ack-steam-client-restart-readiness", action="store_true")
    a = p.parse_args(argv)
    try:
        if platform.system() != "Linux" or platform.machine().lower() not in ("aarch64", "arm64"):
            raise RuntimeError("Native Linux/AArch64 user session required")
        runtime = Path(os.environ.get("XDG_RUNTIME_DIR", ""))
        if runtime != Path(f"/run/user/{os.getuid()}"): raise RuntimeError("Native user runtime directory required")
        session = Session(Path.home(), runtime)
        if a.mode in ("stage", "start"):
            native = {role: getattr(a, "native_" + role) for role in NATIVE}
            if any(v is None for v in (*native.values(), a.reviewed_plan, a.reviewed_plan_sha256, a.observer, a.audit, a.envelope)):
                raise RuntimeError("Reviewed plan and every fresh native/own artifact path required")
            reviewed = dict(entry.rsplit("=", 1) for entry in a.reviewed_environment_file)
            if len(reviewed) != len(a.reviewed_environment_file): raise RuntimeError("Duplicate EnvironmentFile arguments")
            kwargs = {"runtime_scope": a.ack_startup_diagnostic_scope,
                "connect_scope":a.ack_connect_register_diagnostic_scope,
                "resource_scope":a.ack_resource_register_census_scope,
                "resource_thread_scope":a.ack_resource_thread_preparation_scope,
                "reviewed_helpers": {HELPERS[0]: a.reviewed_session_helper_sha256, HELPERS[1]: a.reviewed_receiver_helper_sha256},
                "reviewed_environment_files": reviewed}
            if a.mode == "start": kwargs["restart_ready"] = a.ack_steam_client_restart_readiness
            getattr(session, a.mode)(a.reviewed_plan, a.reviewed_plan_sha256, native,
                {MAIN: a.observer, AUDIT: a.audit, ENVELOPE: a.envelope}, **kwargs)
        elif a.mode == "activate": session.activate(runtime_scope=a.ack_startup_diagnostic_scope,
                                                    connect_scope=a.ack_connect_register_diagnostic_scope,
                                                    resource_scope=a.ack_resource_register_census_scope,
                                                    resource_thread_scope=a.ack_resource_thread_preparation_scope,
                                                    restart_ready=a.ack_steam_client_restart_readiness)
        else: session.rollback(restart_ready=a.ack_steam_client_restart_readiness)
    except (OSError, RuntimeError, ValueError, TypeError, KeyError, struct.error, subprocess.SubprocessError) as error:
        p.exit(1, f"Client startup session refused: {type(error).__name__}: {error}\n")
    print("Manual operation completed; native mapping/unload, capture and passthrough are not inferred.")
    return 0

if __name__ == "__main__": raise SystemExit(main())
