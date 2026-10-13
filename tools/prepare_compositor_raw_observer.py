#!/usr/bin/env python3
"""Prepare an uninstalled compositor raw diagnostic bundle, strictly offline.

Package only independently reviewed own ELF files and fixed hash/layout metadata.
No Frame access, upload, service restart, native producer/Steam-sender change,
shader activation, native memory acquisition or mask association occurs here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
import prepare_client_startup_observer as common
import prepare_import_main_modules as modules
from run_import_native_forwarding import recorded_run

ROOT = Path(__file__).resolve().parents[1]
MAIN = 'libVkLayer_frame_mask_trace.so'
AUDIT = 'libframe_compositor_raw_audit.so'
SELECTOR = 'libframe_compositor_launch_selector.so'
SELECTOR_SOURCE = 'launch-configuration.c'
SELECTOR_SCOPE = 'frame_compositor_launch_scope'
FREEZE_IMPORTS = ('frame_compositor_launch_freeze_environment_v1',
                  'frame_compositor_launch_frozen_status_v1', 'frame_compositor_launch_provider_mask')
SELECTOR_EXPORTS = {'execve', 'execv', 'execvp', 'execvpe', 'posix_spawn', 'posix_spawnp',
                    'getenv', 'secure_getenv', *FREEZE_IMPORTS}
ENVELOPE = 'compositor-raw-envelope.bin'
MANIFEST = 'VkLayer_FRAME_compositor_raw.json'
PLAN = 'deployment-plan.json'
SCOPE = 'frame_compositor_raw_scope'
READY = 'frame_compositor_raw_ready'
BRIDGE = 'frame_compositor_raw_bridge'
LAYER = 'VK_LAYER_FRAME_MASK_trace'
ENVELOPE_BYTES = 8272
MARKER = b'FCMRAW01' + struct.pack('<II', 1, ENVELOPE_BYTES)
LIMIT_WORDS = (672, 21, 16, 8, 32, 65536, 120, 4)
CAPTURE_LIMITS = {'records': 32, 'bytes': 65536, 'seconds': 120}
RAW_LIMITS = {'transactions': 1, 'writes': 8, 'scalar_bytes': 672, 'wire_bytes': 21,
              'parent_bytes': 16, 'private_bytes': 709, 'bootstrap_poll_bytes': 0}
JOURNAL_LIMITS = {'bytes': 1048576, 'call_records': 512, 'binding_records': 32}
ROLES = (7, 3, 4, 5)
NATIVE_PROFILES = tuple(bytes.fromhex(value) for value in (
    'a6c63d91e7da71328088cf912241b737e7529a2adf383d5227bb43f5bf0a8c6e',
    '48edb257be746f755a00ff564c6db4284526b15ffc7e6bad5b8d6723b1891512',
    '51af1fa58396e7a1ab48760ce90d90e919db3e42653504ad550eb54e129a7405'))
LIBC_NAME = '/usr/lib/libc.so.6'
ENTRIES = tuple('frame_import_' + name + '_entry' for name in
                ('init', 'destroy', 'lock', 'unlock', 'write', 'close', 'read', 'connect', 'image', 'bind', 'view'))
VULKAN_FUNCTIONS = {
    'vkGetInstanceProcAddr': 'frame_compositor_raw_vkGetInstanceProcAddr',
    'vkGetDeviceProcAddr': 'frame_compositor_raw_vkGetDeviceProcAddr',
    'vkNegotiateLoaderLayerInterfaceVersion': 'frame_compositor_raw_vkNegotiateLoaderLayerInterfaceVersion'}
VULKAN_EXPORTS = tuple(VULKAN_FUNCTIONS.values())
PUBLIC = {BRIDGE, *VULKAN_EXPORTS}
ALIAS_IMPORTS = common.ALIASES
BOOTSTRAP_IMPORTS = ('dl_iterate_phdr@GLIBC_2.17', 'dladdr@GLIBC_2.34', 'dlvsym@GLIBC_2.34')
NATIVE_RVAS = {'lock': 0x87370, 'unlock': 0x88f74, 'write': 0xdfee0}
Elf = common.Elf
frozen_read = common.frozen_read
digest = common.digest


def remote_observer(value, home='/home/steamos'):
    """A single exact own DSO pathname, safe in loader/systemd lists."""
    path = PurePosixPath(value)
    common.user_directory(str(path.parent), home)
    if str(path) != value or path.name != MAIN or not re.fullmatch(r'[A-Za-z0-9_./-]+', value):
        raise ValueError('Observer path must be normalized inside the user home with the exact combined DSO filename')
    common.loader_name(value)
    return path


def hidden_object(elf, name, size):
    symbols = [s for s in elf.symbols if s['name'] == name and s['defined']]
    identities = {(s['rva'], s['size']) for s in symbols}
    if len(identities) != 1 or any(s['type'] != 1 or (s['binding'] != 0 and s['visibility'] != 2) for s in symbols):
        raise ValueError('Missing/ambiguous hidden own object ' + name)
    rva, actual_size = next(iter(identities))
    if actual_size != size or any(s['name'] == name for s in elf.exported()):
        raise ValueError('Own object has the wrong exact size/visibility ' + name)
    return rva


def scope_marker(elf):
    if any(s['defined'] and s['name'].endswith('_scope') and s['name'].startswith(('frame_client_', 'frame_compositor_'))
           and s['name'] != SCOPE for s in elf.symbols):
        raise ValueError('Artifact includes a conflicting retained diagnostic scope')
    rva = hidden_object(elf, SCOPE, 16)
    spans = [p for p in elf.phdrs if p[0] == 1 and p[1] == 4 and p[3] <= rva and rva + 16 <= p[3] + p[5]]
    if len(spans) != 1:
        raise ValueError('Own scope marker requires one exact read-only file-backed range')
    offset = spans[0][2] + rva - spans[0][3]
    if elf.data[offset:offset + 16] != MARKER:
        raise ValueError('Own scope marker magic/schema/size changed')
    return {'symbol': SCOPE, 'rva': rva, 'bytes': 16, 'sha256': digest(MARKER).hex()}


def ready_object(elf):
    rva = hidden_object(elf, READY, 4)
    spans = [p for p in elf.phdrs if p[0] == 1 and p[1] == 6 and p[3] <= rva and rva + 4 <= p[3] + p[6]]
    if rva % 4 or len(spans) != 1:
        raise ValueError('Own readiness atomic requires aligned writable nonexecuting storage')
    span = spans[0]
    file_count = max(0, min(4, span[3] + span[5] - rva))
    offset = span[2] + rva - span[3]
    if file_count and elf.data[offset:offset + file_count] != bytes(file_count):
        raise ValueError('Own readiness atomic is not initialized to zero')
    return {'symbol': READY, 'rva': rva, 'bytes': 4, 'initial_value': 0,
            'data_rva_begin': span[3], 'data_rva_end': span[3] + span[6]}


def validate_observer(data, reviewed_sha, provider_exports):
    if not isinstance(reviewed_sha, str) or not re.fullmatch('[0-9a-f]{64}', reviewed_sha) or digest(data).hex() != reviewed_sha:
        raise ValueError('Combined DSO differs from its independently reviewed SHA-256')
    elf = Elf(data)
    exports = {s['name'] for s in elf.exported()}
    if any(name.startswith('vk') for name in exports):
        raise ValueError('A preloaded raw observer must not export a standard Vulkan API')
    if exports != PUBLIC:
        raise ValueError('Combined observer/layer exports an unexpected public API: ' + ','.join(sorted(exports ^ PUBLIC)))
    for name in PUBLIC:
        symbol = elf.function(name, True)
        if symbol['binding'] != 1 or symbol['visibility'] != 0 or symbol['version']:
            raise ValueError('Public own function ABI changed ' + name)
    if any('openvr' in name or 'vrclient' in name or 'vulkan' in name for name in elf.needed):
        raise ValueError('Combined observer forces a native API or Vulkan-loader dependency')
    entries = {}
    for name in ENTRIES:
        symbol = elf.function(name)
        rows = [s for s in elf.symbols if s['name'] == name and s['defined']]
        if any(s['binding'] != 0 and s['visibility'] != 2 for s in rows) or name in exports or symbol['size'] != 156:
            raise ValueError('Reviewed native entry ABI/hidden visibility changed ' + name)
        entries[name] = symbol['rva']
    actual_entries = {s['name'] for s in elf.symbols if s['defined'] and s['name'].endswith('_entry')
                      and s['name'].startswith(('frame_import_', 'frame_compositor_', 'frame_client_'))}
    if actual_entries != set(ENTRIES) or len(set(entries.values())) != len(ENTRIES):
        raise ValueError('Combined DSO requires exactly eleven distinct reviewed hidden veneers')
    imports = elf.imports()
    if not set(FREEZE_IMPORTS) <= imports or any(not any(s['dynamic'] and not s['defined'] and
            s['name'] == name and s['binding'] == 2 and s['visibility'] == 0 and s['type'] in (0, 2) and
            s['rva'] == 0 and s['size'] == 0 and not s['version']
            for s in elf.symbols) for name in FREEZE_IMPORTS):
        raise ValueError('Combined observer lacks the exact weak cold-freeze handshake imports')
    required = (*ALIAS_IMPORTS, *BOOTSTRAP_IMPORTS)
    if not set(required) <= imports:
        raise ValueError('Combined DSO lacks reviewed aliases/bootstrap public providers')
    for item in required:
        name, version = item.split('@')
        if not any(s['dynamic'] and not s['defined'] and s['name'] == name and s['version'] == version and
                   s['type'] == 2 and s['binding'] in (1, 2) and s['visibility'] == 0 for s in elf.symbols):
            raise ValueError('Reviewed provider function import ABI changed ' + name)
    versioned = {item for item in imports if '@GLIBC_' in item}
    if not versioned <= provider_exports:
        raise ValueError('Pinned native libc/loader lacks a versioned combined-DSO import')
    if '__tls_get_addr' in {item.split('@')[0] for item in imports}:
        raise ValueError('Combined observer requires dynamic TLS resolution')
    tls = [p[6] for p in elf.phdrs if p[0] == 7]
    if len(tls) > 1 or any(size > 128 for size in tls):
        raise ValueError('Combined observer static TLS exceeds the small reviewed marker scope')
    marker, ready = scope_marker(elf), ready_object(elf)
    rx = elf.rx()
    if ready['data_rva_begin'] < rx[1] and rx[0] < ready['data_rva_end']:
        raise ValueError('Readiness storage overlaps executable bytes')
    return elf, {'sha256': reviewed_sha, 'bytes': len(data), 'bridge_rva': elf.function(BRIDGE, True)['rva'],
                 'ready_rva': ready['rva'], 'rx_rva_begin': rx[0], 'rx_rva_end': rx[1],
                 'data_rva_begin': ready['data_rva_begin'], 'data_rva_end': ready['data_rva_end'],
                 'hidden_entries': entries, 'scope_marker': marker, 'ready_object': ready,
                 'public_exports': sorted(exports), 'vulkan_functions': dict(VULKAN_FUNCTIONS),
                 'standard_vulkan_exports': [], 'required_aliases': list(ALIAS_IMPORTS),
                 'bootstrap_imports': list(BOOTSTRAP_IMPORTS), 'versioned_glibc_imports': sorted(versioned),
                 'static_tls_bytes': sum(tls), 'cold_environment_freeze_abi': 1,
                 'launch_provider_mask': 63, 'raw_only': True, 'native_namespace_associated': False,
                 'source_frame_associated': False, 'gpu_consumed': False}


def validate_profiles(blobs, reviewed):
    if len(blobs) != 4 or len(reviewed) != 32 or reviewed == bytes(32):
        raise ValueError('Four exact profiles and an independent observer profile identity required')
    for i, (blob, role) in enumerate(zip(blobs, ROLES)):
        if len(blob) != modules.PROFILE_BYTES or struct.unpack_from('<4Q', blob) != (1, role, 183, 3) or digest(blob[:-32]) != blob[-32:]:
            raise ValueError('Malformed compositor module descriptor')
        expected = NATIVE_PROFILES[i] if i < 3 else reviewed
        if blob[-32:] != expected:
            raise ValueError('Module descriptor differs from separately pinned identity')


def encode_envelope(blobs, reviewed):
    validate_profiles(blobs, reviewed)
    body = MARKER + reviewed + struct.pack('<8Q', *LIMIT_WORDS) + b''.join(blobs)
    result = body + digest(body)
    if len(result) != ENVELOPE_BYTES:
        raise ValueError('Compositor envelope POD layout changed')
    return result


def decode_envelope(data, expected_profile=None):
    if len(data) != ENVELOPE_BYTES or data[:16] != MARKER or digest(data[:-32]) != data[-32:]:
        raise ValueError('Compositor envelope size/scope/checksum changed')
    reviewed = data[16:48]
    if expected_profile is not None and reviewed != expected_profile:
        raise ValueError('Compositor envelope observer differs from independent review')
    if struct.unpack_from('<8Q', data, 48) != LIMIT_WORDS:
        raise ValueError('Compositor envelope acquisition/capture limits changed')
    blobs = tuple(data[112 + 2032 * i:112 + 2032 * (i + 1)] for i in range(4))
    validate_profiles(blobs, reviewed)
    return {'schema': 1, 'bytes': ENVELOPE_BYTES, 'reviewed_observer_profile_identity': reviewed.hex(),
            'module_roles': list(ROLES), 'module_count': 4, 'raw_limits': dict(RAW_LIMITS),
            'capture_limits': dict(CAPTURE_LIMITS), 'profiles': blobs, 'sha256': digest(data).hex(),
            'independent_observer_checked': expected_profile is not None}


def validate_audit(data):
    elf = Elf(data)
    expected = {'la_version', 'la_objopen', 'la_preinit', 'la_symbind64', 'la_objclose'}
    if len(data) > common.MAX_AUDIT or elf.imports() or elf.needed or {s['name'] for s in elf.exported()} != expected:
        raise ValueError('Selective audit acquired an import/dependency/public API')
    if any(p[0] == 7 for p in elf.phdrs) or any(s['type'] == 6 for s in elf.symbols):
        raise ValueError('Selective audit must have no TLS storage or provider')
    for name in expected:
        symbol = elf.function(name, True)
        if symbol['binding'] != 1 or symbol['visibility'] != 0 or symbol['version']:
            raise ValueError('Selective audit public callback ABI changed')
    return {'bytes': len(data), 'sha256': digest(data).hex(), 'imports': [], 'tls_bytes': 0,
            'exports': sorted(expected), 'native_rvas': {key: hex(value) for key, value in NATIVE_RVAS.items()},
            'early_forward_only': True, 'capture_requires_actual_preinit': True}


def _quoted(value):
    return '"' + ''.join(chr(byte) if 32 <= byte <= 126 and chr(byte) not in '"\\' else '\\%03o' % byte
                          for byte in value.encode('utf-8')) + '"'


def audit_source(compositor_rx, libc_rx, observer_report, observer_path, home='/home/steamos'):
    remote_observer(observer_path, home)
    def module(name, rx, data=(0, 0)):
        if not (0 <= rx[0] < rx[1] < 2**64 and 0 <= data[0] <= data[1] < 2**64):
            raise ValueError('Immutable audit module geometry invalid')
        return '{' + _quoted(name) + ',' + ','.join(map(str, (*rx, *data))) + '}'
    own_rx = (observer_report['rx_rva_begin'], observer_report['rx_rva_end'])
    own_data = (observer_report['data_rva_begin'], observer_report['data_rva_end'])
    bridge, ready = observer_report['bridge_rva'], observer_report['ready_rva']
    if (not own_rx[0] <= bridge < own_rx[1] or ready % 4 or not own_data[0] <= ready <= own_data[1] - 4 or
            own_data[0] < own_rx[1] and own_rx[0] < own_data[1]):
        raise ValueError('Audit bridge/readiness metadata outside reviewed own ranges')
    return ('#include "import/compositor_startup_audit.h"\nnamespace frame_compositor_audit {\n'
            'const Preparation* preparation() noexcept {\nstatic const Preparation p = {' +
            ','.join((module('', compositor_rx), module(observer_path, own_rx, own_data), module(LIBC_NAME, libc_rx))) +
            f',{bridge},0x87370,0x88f74,0xdfee0,true,{ready}' + '};\nreturn &p;\n}\n}\n')


def manifest_for(observer_path, home='/home/steamos'):
    remote_observer(observer_path, home)
    return {'file_format_version': '1.2.0', 'layer': {
        'name': LAYER, 'type': 'GLOBAL', 'library_path': observer_path, 'api_version': '1.3.0',
        'implementation_version': '1', 'description': 'Opt-in bounded passive compositor raw import metadata',
        # Loader-interface v2 obtains GPA pointers from the uniquely named
        # negotiation entry. Alternate GPA names are retained as exact metadata
        # for older loaders; modern loaders may ignore those two mapping keys.
        'functions': dict(VULKAN_FUNCTIONS),
        'enable_environment': {'FRAME_MASK_TRACE': '1'}, 'disable_environment': {'FRAME_MASK_TRACE_DISABLE': '1'}}}


def validate_native_providers(libc):
    layouts = (('pthread_mutex_init', 0x86a00, 556), ('pthread_mutex_destroy', 0x86980, 56),
               ('pthread_mutex_lock', 0x87370, 760), ('pthread_mutex_unlock', 0x88f74, 20),
               ('write', 0xdfee0, 204), ('close', 0xda610, 180), ('read', 0xdf330, 204), ('connect', 0xebcb0, 208))
    for name, rva, size in layouts:
        actual, alias = libc.function(name, True), libc.function('__' + name, True)
        shape = (rva, size, 'GLIBC_2.17')
        if (actual['rva'], actual['size'], actual['version']) != shape or (alias['rva'], alias['size'], alias['version']) != shape:
            raise ValueError('Pinned native public/alias provider shape changed ' + name)


def launch_config(frame, target='/opt/steamvr/bin/linuxarm64/vrcompositor'):
    path = PurePosixPath(target)
    if str(path) != target or not path.is_absolute() or '..' in path.parts or path.name != 'vrcompositor' or not re.fullmatch(r'[A-Za-z0-9_./-]+', target):
        raise ValueError('Exact normalized compositor launch pathname required')
    return {'target': target, 'selector': str(frame / SELECTOR), 'payload': str(frame / MAIN),
            'audit': str(frame / AUDIT), 'envelope': str(frame / ENVELOPE),
            'output': str(frame / 'compositor-raw.jsonl'), 'journal': str(frame / 'compositor-journal.jsonl')}


def launch_environment(config):
    return {'LD_PRELOAD': config['selector'] + ':' + config['payload'], 'LD_AUDIT': config['audit'],
            'GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE': config['envelope'],
            'GHVR_FRAME_COMPOSITOR_RAW_OUTPUT': config['output'], 'FRAME_MASK_TRACE': '1',
            'FRAME_MASK_TRACE_BINDINGS': '1', 'FRAME_MASK_TRACE_OUTPUT': config['journal']}


def launch_marker(config):
    canonical = json.dumps(config, sort_keys=True, separators=(',', ':')).encode()
    return b'FCMLAU01' + struct.pack('<II', 1, 63) + digest(canonical)


def launch_source(config):
    values = ','.join(json.dumps(config[key]) for key in ('target', 'selector', 'payload', 'audit', 'envelope', 'output', 'journal'))
    marker = ','.join(str(v) for v in launch_marker(config))
    return ('#include "receiver/compositor_launch_selector.h"\n'
            'static const struct frame_launch_config config = {1,' + values + '};\n'
            'const struct frame_launch_config *frame_compositor_launch_config_v1(void) { return &config; }\n'
            '__attribute__((visibility("hidden"),used)) const unsigned char ' + SELECTOR_SCOPE + '[48] = {' + marker + '};\n')


def validate_selector(data, config, provider_exports):
    elf = Elf(data)
    if {s['name'] for s in elf.exported()} != SELECTOR_EXPORTS:
        raise ValueError('Launch selector public ABI differs from reviewed freeze/getenv/exec surface')
    if any(p[0] == 7 for p in elf.phdrs) or any(name not in ('libc.so.6', 'ld-linux-aarch64.so.1') for name in elf.needed):
        raise ValueError('Launch selector must have no TLS or C++/foreign dependency')
    imports = elf.imports()
    if any(item.split('@')[0] in ('malloc', 'calloc', 'realloc', '__tls_get_addr', 'pthread_create') for item in imports):
        raise ValueError('Launch selector may not allocate, resolve TLS or start threads')
    if not {item for item in imports if '@GLIBC_' in item} <= provider_exports:
        raise ValueError('Pinned native providers cannot supply the selector ABI')
    rva = hidden_object(elf, SELECTOR_SCOPE, 48)
    segments = [p for p in elf.phdrs if p[0] == 1 and p[1] == 4 and p[3] <= rva and rva + 48 <= p[3] + p[5]]
    if len(segments) != 1: raise ValueError('Selector configuration marker requires exact read-only storage')
    segment = segments[0]; offset = segment[2] + rva - segment[3]
    if data[offset:offset + 48] != launch_marker(config): raise ValueError('Compiled selector configuration differs from reviewed launch paths')
    return {'sha256': digest(data).hex(), 'bytes': len(data), 'public_exports': sorted(SELECTOR_EXPORTS),
            'configuration': config, 'configuration_marker_sha256': digest(launch_marker(config)).hex(),
            'cold_environment_freeze_abi': 1, 'launch_provider_mask': 63, 'module_tls_bytes': 0,
            'standard_vulkan_exports': [], 'runtime_loaded_verified': False}


def prepare(native_directory, observer, reviewed_sha256, output, observer_remote_path,
            compiler='aarch64-linux-gnu-g++', home='/home/steamos', *, selector_compiler='aarch64-linux-gnu-gcc',
            compositor_remote_path='/opt/steamvr/bin/linuxarm64/vrcompositor'):
    frame_observer = remote_observer(observer_remote_path, home)
    frame = frame_observer.parent
    native_directory, observer, output = Path(native_directory), Path(observer), Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('Offline output must be new; never overwrite a reviewed bundle')
    if native_directory.resolve() == output.resolve() or native_directory.resolve() in output.resolve().parents or output.resolve() == observer.resolve():
        raise ValueError('Do not publish into borrowed native references or the reviewed input')
    native = {7: frozen_read(native_directory / modules.COMPOSITOR[0]),
              3: frozen_read(native_directory / modules.NATIVE[3][0]),
              4: frozen_read(native_directory / modules.NATIVE[4][0])}
    own = frozen_read(observer)
    profiles, reports = [], []
    for role in ROLES:
        blob, report = modules.profile_from_bytes(own if role == 5 else native[role], role, reviewed_sha256 if role == 5 else None)
        profiles.append(blob); reports.append(report)
    native_elf = {role: Elf(value) for role, value in native.items()}
    provider_exports = common.export_versions(native_elf[3]) | common.export_versions(native_elf[4])
    _, observer_report = validate_observer(own, reviewed_sha256, provider_exports)
    validate_native_providers(native_elf[3])
    reviewed = profiles[3][-32:]
    envelope = encode_envelope(profiles, reviewed)
    source = audit_source(native_elf[7].rx(0x153fc0), native_elf[3].rx(), observer_report, str(frame_observer), home)
    manifest = (json.dumps(manifest_for(str(frame_observer), home), indent=2) + '\n').encode()
    compiler_args = shlex.split(compiler)
    if not compiler_args:
        raise ValueError('Explicit cross compiler required')
    logs = {}
    audit_source_files = (ROOT / 'import/compositor_startup_audit.cpp', ROOT / 'import/compositor_startup_audit.h')
    audited_source_hashes = {str(path.relative_to(ROOT)): digest(frozen_read(path)).hex() for path in audit_source_files}
    configuration = launch_config(frame, compositor_remote_path)
    selector_source = launch_source(configuration)
    selector_source_files = tuple(ROOT / name for name in ('receiver/compositor_launch_selector.c',
        'receiver/compositor_launch_selector.h', 'receiver/compositor_launch_selector.map'))
    selector_source_hashes = {str(path.relative_to(ROOT)): digest(frozen_read(path)).hex() for path in selector_source_files}
    with tempfile.TemporaryDirectory(prefix='frame-compositor-raw-packaging-') as temporary:
        folder = Path(temporary); folder.chmod(0o700)
        source_path, target = folder / 'audit-preparation.cpp', folder / AUDIT
        source_path.write_text(source)
        command = compiler_args + ['-std=c++17', '-O2', '-shared', '-fPIC', '-nostdlib', '-fvisibility=hidden',
                                   '-fno-exceptions', '-fno-rtti', '-fno-builtin', '-mno-outline-atomics', '-Wl,--build-id',
                                   '-I', str(ROOT), str(ROOT / 'import/compositor_startup_audit.cpp'), str(source_path), '-o', str(target)]
        environment = {key: value for key, value in os.environ.items() if not key.startswith(('LD_', 'GHVR_FRAME_', 'FRAME_MASK_TRACE'))}
        built = recorded_run(command, folder, logs, 'audit_compile', 60, environment)
        if built.returncode:
            raise ValueError('Own audit compilation failed: ' + json.dumps(logs['audit_compile']))
        audit_data = frozen_read(target, common.MAX_AUDIT)
        audit_report = validate_audit(audit_data)
        if any(digest(frozen_read(path)).hex() != audited_source_hashes[str(path.relative_to(ROOT))] for path in audit_source_files):
            raise ValueError('Own selective audit source changed while building the reviewed bundle')
        audit_report['source_sha256'] = audited_source_hashes
        config_source = folder / SELECTOR_SOURCE; config_source.write_text(selector_source)
        selector_target = folder / SELECTOR
        selector_command = shlex.split(selector_compiler) + ['-std=c11', '-O2', '-shared', '-fPIC', '-fvisibility=hidden',
            '-Wl,-z,now', '-Wl,-z,separate-code', '-Wl,--build-id', '-Wl,--no-undefined', '-Wl,--version-script=' + str(ROOT / 'receiver/compositor_launch_selector.map'),
            '-I', str(ROOT), str(ROOT / 'receiver/compositor_launch_selector.c'), str(config_source), '-ldl', '-o', str(selector_target)]
        if not shlex.split(selector_compiler): raise ValueError('Explicit selector C compiler required')
        built_selector = recorded_run(selector_command, folder, logs, 'selector_compile', 60, environment)
        if built_selector.returncode: raise ValueError('Own selector compilation failed: ' + json.dumps(logs['selector_compile']))
        selector_data = frozen_read(selector_target, common.MAX_AUDIT)
        selector_report = validate_selector(selector_data, configuration, provider_exports)
        if any(digest(frozen_read(path)).hex() != selector_source_hashes[str(path.relative_to(ROOT))] for path in selector_source_files):
            raise ValueError('Own launch selector source changed during packaging')
        selector_report['source_sha256'] = selector_source_hashes
    artifacts = {MAIN: own, AUDIT: audit_data, SELECTOR: selector_data, ENVELOPE: envelope, MANIFEST: manifest,
                 'audit-preparation.cpp': source.encode(), SELECTOR_SOURCE: selector_source.encode()}
    inventory = [{'name': name, 'bytes': len(value), 'sha256': digest(value).hex(),
                  'frame_path': None if name in ('audit-preparation.cpp', SELECTOR_SOURCE) else str(frame / name),
                  'owned_source': True, 'native_bytes': False, 'mode': '0600'} for name, value in artifacts.items()]
    plan = {'schema': 'frame-compositor-raw-offline-bundle-v2', 'prepared': True, 'installed': False, 'activated': False,
            'runtime_authorized': False, 'frame_access': False, 'network_access': False, 'automatic_restart': False,
            'native_binary_or_script_changes': False, 'shader_or_mask_activation': False, 'source_frame_associated': False,
            'gpu_coherent': False, 'continuous_connection_owned': False, 'native_namespace_associated': False,
            'frame_directory': str(frame), 'frame_user_home': home, 'directory_mode': '0700',
            'observer': observer_report, 'audit': audit_report, 'selector': selector_report,
            'launch_isolation': {'schema': 'frame-compositor-startup-isolation-v1', 'target_exe': configuration['target'],
                'selector_path': configuration['selector'], 'payload_path': configuration['payload'],
                'audit_path': configuration['audit'], 'target_exec_environment': launch_environment(configuration)},
            'envelope': {'bytes': len(envelope), 'sha256': digest(envelope).hex(), 'schema': 1,
                         'reviewed_observer_profile_identity': reviewed.hex(), 'module_count': 4, 'module_roles': list(ROLES)},
            'manifest': {'name': MANIFEST, 'bytes': len(manifest), 'sha256': digest(manifest).hex(), 'layer_name': LAYER,
                         'functions': dict(VULKAN_FUNCTIONS),
                         'library_path': str(frame_observer), 'staged_path': str(frame / MANIFEST),
                         'frame_path': str(PurePosixPath(home) / '.local/share/vulkan/implicit_layer.d/frame_compositor_raw.json')},
            'native_profiles': reports[:3], 'observer_profile': reports[3],
            'native_slots': {'compositor': {'exact_loader_name': '', 'rx_rva_begin': native_elf[7].rx()[0], 'rx_rva_end': native_elf[7].rx()[1]},
                             'libc': {'exact_loader_name': LIBC_NAME, 'rx_rva_begin': native_elf[3].rx()[0], 'rx_rva_end': native_elf[3].rx()[1]}},
            'raw_limits': dict(RAW_LIMITS), 'capture_limits': dict(CAPTURE_LIMITS), 'journal_limits': dict(JOURNAL_LIMITS),
            'environment': {'LD_PRELOAD': configuration['selector']},
            'inventory': inventory, 'audit_source_sha256': digest(source.encode()).hex(), 'audit_compile_command': command, 'logs': logs,
            'local_receipt': {'name': PLAN, 'mode': '0600', 'frame_path': None},
            'scope': ['one current-thread native compositor import, at most 709 precisely listed scalar/reply metadata bytes',
                      'original public libc forwarding and public Vulkan image/bind/view results; no GPU pixel readback',
                      'same combined DSO for preload, three hidden Vulkan entries and the implicit Vulkan layer',
                      'tiny C launch selector in service descendants; full observer/audit only in the exact compositor target',
                      'cold single-threaded diagnostic environment freeze; internal system/popen children inherit clean actual environ',
                      'own cold-ready forwarding before actual la_preinit; native private reads remain unarmed until actual admission',
                      'local raw facts, not an immutable socket lease, native cross-endpoint namespace, video frame, eye, UV or GPU proof'],
            'before_activation': ['Independent final source/artifact/plan review', 'Fresh exact native Frame hashes and public loader names',
                                  'Exact user-owned staging files and no foreign service/preload/audit/layer state',
                                  'Separate runtime permission and coordinated manual SteamVR restart readiness'],
            'rollback': ['Remove only the manager-owned user-service override and matching implicit-layer manifest',
                         'Restart original SteamVR only after coordinated readiness; verify no diagnostic mappings remain',
                         'Archive bounded results and remove exact session/staging inventory; do not edit native/system/PC-sender files'],
            'evidence_limit': 'Offline preparation only; not a Frame run or working native-mask Steam Link passthrough.'}
    output.mkdir(mode=0o700, parents=True)
    for name, value in artifacts.items():
        common.private_write(output / name, value)
    common.private_write(output / PLAN, (json.dumps(plan, indent=2) + '\n').encode())
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer', type=Path, required=True)
    parser.add_argument('--observer-sha256', required=True)
    parser.add_argument('--observer-remote-path', required=True)
    parser.add_argument('--native-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='aarch64-linux-gnu-g++')
    parser.add_argument('--selector-compiler', default='aarch64-linux-gnu-gcc')
    parser.add_argument('--compositor-remote-path', default='/opt/steamvr/bin/linuxarm64/vrcompositor')
    parser.add_argument('--frame-user-home', default='/home/steamos')
    args = parser.parse_args()
    try:
        plan = prepare(args.native_directory, args.observer, args.observer_sha256, args.output,
                       args.observer_remote_path, args.compiler, args.frame_user_home,
                       selector_compiler=args.selector_compiler, compositor_remote_path=args.compositor_remote_path)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, 'Offline compositor bundle refused: ' + str(error) + '\n')
    print(json.dumps({'prepared': True, 'installed': False, 'runtime_authorized': False,
                      'observer_sha256': plan['observer']['sha256'], 'audit_sha256': plan['audit']['sha256'],
                      'envelope_sha256': plan['envelope']['sha256']}))


if __name__ == '__main__':
    main()
