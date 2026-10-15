"""Offline package controls; synthetic own metadata never authorizes Frame use."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_compositor_raw_observer as tool

PRIVATE_NATIVE = None
FINAL_OBSERVER = None
FINAL_SHA = None
REMOTE = '/home/steamos/.local/share/GloomhavenVR/frame-compositor-raw/' + tool.MAIN


def fixture(*, missing_entry=None, missing_import=None, extra_export=None, extra_entry=None,
            marker=tool.MARKER, marker_flags=4, marker_visibility=2, ready_rva=0x7400,
            ready_flags=6, ready_size=4, ready_value=0, entry_size=156, entry_visibility=2,
            dynamic_tls=False, tls_bytes=0, extra_scope=None, dependency='libc.so.6', standard_exports=False,
            missing_export=None, sampling=tool.SAMPLING_MARKER, sampling_rva=0x6040,
            sampling_visibility=2, sampling_size=len(tool.SAMPLING_MARKER)):
    """Only ELF metadata and zero-filled own synthetic instruction slots."""
    raw = bytearray(0x8000)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    phnum = 4 + bool(tls_bytes)
    struct.pack_into('<16sHHIQQQIHHHHHH', raw, 0, ident, 3, 183, 1, 0, 64, 0x7800, 0, 64, 56, phnum, 64, 8, 0)
    struct.pack_into('<IIQQQQQQ', raw, 64, 1, 5, 0, 0, 0, 0x6000, 0x6000, 4096)
    struct.pack_into('<IIQQQQQQ', raw, 120, 1, marker_flags, 0x6000, 0x6000, 0x6000, 0x1000, 0x1000, 4096)
    struct.pack_into('<IIQQQQQQ', raw, 176, 1, ready_flags, 0x7000, 0x7000, 0x7000, 0x300, 0x1000, 4096)
    struct.pack_into('<IIQQQQQQ', raw, 232, 4, 4, 0x180, 0x180, 0x180, 36, 36, 4)
    if tls_bytes:
        struct.pack_into('<IIQQQQQQ', raw, 288, 7, 4, 0x7100, 0x7100, 0x7100, 0, tls_bytes, 8)
    struct.pack_into('<III', raw, 0x180, 4, 20, 3)
    raw[0x18c:0x190] = b'GNU\0'; raw[0x190:0x1a4] = bytes(range(20))
    raw[0x6000:0x6010] = marker
    if sampling is not None:
        raw[sampling_rva:sampling_rva + len(sampling)] = sampling
    if 0x7000 <= ready_rva < 0x7300:
        struct.pack_into('<I', raw, ready_rva, ready_value)
    public = {tool.BRIDGE, *tool.VULKAN_FUNCTIONS} if standard_exports else tool.PUBLIC
    dynamic = [(name, 0x2200 + 16 * i) for i, name in enumerate(sorted(public)) if name != missing_export]
    if extra_export:
        dynamic.append((extra_export, 0x2250))
    imports = list(tool.ALIAS_IMPORTS) + list(tool.BOOTSTRAP_IMPORTS) + list(tool.FREEZE_IMPORTS)
    if dynamic_tls:
        imports.append('__tls_get_addr@GLIBC_2.17')
    if missing_import:
        imports.remove(missing_import)
    dynstr = bytearray(b'\0'); offsets = {}
    def text(value):
        if value not in offsets:
            offsets[value] = len(dynstr); dynstr.extend(value.encode() + b'\0')
        return offsets[value]
    for name, _ in dynamic:
        text(name)
    for item in imports:
        text(item.split('@')[0])
    for name in ('GLIBC_2.17', 'GLIBC_2.34', dependency):
        text(name)
    dynsym = bytearray(24); versions = [0]
    for name, rva in dynamic:
        dynsym.extend(struct.pack('<IBBHQQ', offsets[name], 0x12, 0, 1, rva, 4)); versions.append(1)
    for item in imports:
        if '@' in item:
            name, version = item.split('@')
            dynsym.extend(struct.pack('<IBBHQQ', offsets[name], 0x12, 0, 0, 0, 0)); versions.append(2 if version == 'GLIBC_2.17' else 3)
        else:
            # AArch64 GCC's address-tested unresolved weak C functions are
            # STT_NOTYPE; the defined selector providers are STT_FUNC.
            dynsym.extend(struct.pack('<IBBHQQ', offsets[item], 0x20, 0, 0, 0, 0)); versions.append(1)
    statstr = bytearray(b'\0'); symtab = bytearray(24)
    def hidden(name, rva, size, kind=2, visibility=2):
        at = len(statstr); statstr.extend(name.encode() + b'\0')
        symtab.extend(struct.pack('<IBBHQQ', at, 0x10 | kind, visibility, 1, rva, size))
    for i, name in enumerate(tool.ENTRIES):
        if name != missing_entry:
            hidden(name, 0x1000 + i * 160, entry_size, visibility=entry_visibility)
    if extra_entry:
        hidden(extra_entry, 0x1800, 156)
    hidden(tool.SCOPE, 0x6000, 16, kind=1, visibility=marker_visibility)
    hidden(tool.READY, ready_rva, ready_size, kind=1)
    if sampling is not None:
        hidden(tool.SAMPLING_SCOPE, sampling_rva, sampling_size, kind=1, visibility=sampling_visibility)
    if extra_scope:
        hidden(extra_scope, 0x6020, 16, kind=1)
    verneed = struct.pack('<HHIII', 1, 2, offsets[dependency], 16, 0)
    verneed += struct.pack('<IHHII', 0, 0, 2, offsets['GLIBC_2.17'], 16)
    verneed += struct.pack('<IHHII', 0, 0, 3, offsets['GLIBC_2.34'], 0)
    versym = struct.pack('<' + 'H' * len(versions), *versions)
    dynamic_table = struct.pack('<qQqQ', 1, offsets[dependency], 0, 0)
    sections = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]; cursor = 0x3000
    for kind, payload, link, entsize in ((3, dynstr, 0, 0), (11, dynsym, 1, 24), (3, statstr, 0, 0),
                                        (2, symtab, 3, 24), (0x6ffffffe, verneed, 1, 0),
                                        (0x6fffffff, versym, 2, 2), (6, dynamic_table, 1, 16)):
        raw[cursor:cursor + len(payload)] = payload
        sections.append((0, kind, 0, 0, cursor, len(payload), link, 0, 8, entsize)); cursor += (len(payload) + 7) & ~7
    for i, section in enumerate(sections):
        struct.pack_into('<IIQQQQIIQQ', raw, 0x7800 + i * 64, *section)
    return bytes(raw)


def synthetic_profiles():
    blobs = []
    for role in tool.ROLES:
        body = struct.pack('<4Q', 1, role, 183, 3) + bytes(2032 - 64)
        blobs.append(body + tool.digest(body))
    return blobs


class PackageControls(unittest.TestCase):
    def validate(self, data):
        providers = set(tool.ALIAS_IMPORTS) | set(tool.BOOTSTRAP_IMPORTS)
        return tool.validate_observer(data, tool.digest(data).hex(), providers)

    def test_exact_combined_own_metadata(self):
        elf, report = self.validate(fixture())
        self.assertEqual(set(report['hidden_entries']), set(tool.ENTRIES))
        self.assertEqual(report['public_exports'], sorted(tool.PUBLIC))
        self.assertEqual(report['vulkan_functions'], tool.VULKAN_FUNCTIONS)
        self.assertEqual(report['standard_vulkan_exports'], [])
        self.assertEqual(report['scope_marker']['bytes'], 16)
        self.assertEqual(report['ready_object']['bytes'], 4)
        self.assertEqual(report['ready_object']['initial_value'], 0)
        self.assertEqual(report['ready_rva'], 0x7400)
        self.assertEqual((report['data_rva_begin'], report['data_rva_end']), (0x7000, 0x8000))
        self.assertFalse(report['native_namespace_associated']); self.assertFalse(report['source_frame_associated'])
        self.assertEqual(set(elf.imports()), set(tool.ALIAS_IMPORTS) | set(tool.BOOTSTRAP_IMPORTS) | set(tool.FREEZE_IMPORTS))
        self.assertEqual(report['cold_environment_freeze_abi'], 1)
        self.assertEqual(report['launch_provider_mask'], 63)

    def test_old_observer_without_cold_freeze_cannot_be_packaged(self):
        for name in tool.FREEZE_IMPORTS:
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'cold-freeze'):
                self.validate(fixture(missing_import=name))

    def test_old_creation_route_scope_cannot_use_the_new_acquisition_contract(self):
        for version in (1, 2, 3):
            old = ('FCMRAW0' + str(version)).encode() + struct.pack('<II', version, tool.ENVELOPE_BYTES)
            with self.subTest(version=version), self.assertRaises(ValueError): self.validate(fixture(marker=old))

    def test_public_image_selection_requires_the_new_actual_acquisition_marker(self):
        _, report = self.validate(fixture())
        self.assertEqual(report['candidate_selection'], tool.CANDIDATE_SELECTION)
        self.assertEqual(report['resource_join_validation'], tool.RESOURCE_JOIN_VALIDATION)
        self.assertEqual(report['scope_marker']['sha256'], tool.digest(tool.MARKER).hex())
        self.assertFalse(report['candidate_selection']['stream_origin_proven'])
        self.assertFalse(report['candidate_selection']['begin_early_private_replay'])
        self.assertEqual(tool.LIMIT_WORDS, (720, 21, 16, 8, 32, 65536, 120, 4))

    def test_operator_arm_descriptor_reserves_the_existing_budget_without_new_environment(self):
        frame = Path('/home/steamos/.local/share/GloomhavenVR/frame-compositor-raw')
        descriptor = tool.operator_arm_descriptor(frame)
        self.assertEqual(descriptor['schema'], 'frame-compositor-operator-arm-v2')
        self.assertEqual(descriptor['allowed_stream_selection_phases'], ['visible', 'start-intent'])
        self.assertIs(descriptor['required'], True)
        self.assertEqual(descriptor['marker_path'], str(frame / 'capture-arm.marker'))
        self.assertEqual(descriptor['marker_bytes'], len(b'FRAME_COMPOSITOR_RAW/1\n'))
        self.assertEqual(descriptor['marker_sha256'], tool.digest(b'FRAME_COMPOSITOR_RAW/1\n').hex())
        self.assertEqual(descriptor['deadline_from_capture_start_seconds'], 120)
        self.assertIs(descriptor['deadline_reset_allowed'], False)
        self.assertIs(descriptor['restart_required'], False)
        self.assertIs(descriptor['source_frame_associated'], False)
        self.assertEqual(len(tool.launch_environment(tool.launch_config(frame))), 7)

    def test_sampling_requires_actual_exact_readonly_hidden_artifact_marker(self):
        _, report = self.validate(fixture())
        self.assertEqual(report['raw_trace_sampling'], tool.RAW_TRACE_SAMPLING)
        self.assertEqual(report['trace_sampling_marker']['sha256'], tool.digest(tool.SAMPLING_MARKER).hex())
        for kwargs in ({'sampling': None}, {'sampling': b'FCMSMP00' + tool.SAMPLING_MARKER[8:]},
                       {'sampling': b'FCMSMP01' + struct.pack('<6I', 1, 32, 16, 16, 32, 1)},
                       {'sampling': b'FCMSMP01' + struct.pack('<6I', 1, 33, 16, 16, 32, 3)},
                       {'sampling': b'FCMSMP02' + struct.pack('<8I', 2, 40, 32, 16, 16, 32, 4096, 7)},
                       {'sampling': b'FCMSMP03' + struct.pack('<12I', 3, 56, 32, 16, 16, 32, 4096, 63, 60000000, 2, 16, 1)},
                       {'sampling_visibility': 0}, {'sampling_size': 31},
                       {'sampling_rva': 0x2400}, {'sampling_rva': 0x7080}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError): self.validate(fixture(**kwargs))
        self.assertEqual(len(tool.SAMPLING_MARKER), 60)
        self.assertEqual(struct.unpack_from('<13I', tool.SAMPLING_MARKER, 8),
                         (3, 60, 32, 16, 16, 32, 4096, 63, 60000000, 2, 16, 1, 32))
        for word in range(13):
            changed = bytearray(tool.SAMPLING_MARKER)
            at = 8 + 4 * word
            struct.pack_into('<I', changed, at, struct.unpack_from('<I', changed, at)[0] + 1)
            with self.subTest(word=word), self.assertRaises(ValueError):
                self.validate(fixture(sampling=bytes(changed)))

    def test_launch_configuration_is_exact_and_child_service_only_preloads_selector(self):
        config = tool.launch_config(Path('/home/steamos/.local/share/GloomhavenVR/frame-compositor-raw'))
        env = tool.launch_environment(config)
        self.assertEqual(env['LD_PRELOAD'], config['selector'] + ':' + config['payload'])
        self.assertEqual(env['LD_AUDIT'], config['audit'])
        self.assertEqual(len(env), 7)
        marker = tool.launch_marker(config)
        self.assertEqual(len(marker), 48)
        changed = dict(config, target='/opt/other/vrcompositor')
        self.assertNotEqual(marker, tool.launch_marker(changed))
        for target in ('vrcompositor', '/opt/../vrcompositor', '/opt/a:bad/vrcompositor'):
            with self.subTest(target=target), self.assertRaises(ValueError): tool.launch_config(Path('/own'), target)

    def test_standard_vulkan_exports_are_unsafe_even_with_exact_raw_scope(self):
        # The old combined export surface had the valid raw marker/envelope and
        # all eleven veneers, but preloading its ordinary GPA names could steal
        # application/loader lookups. Independently reviewing its SHA is not a
        # reason to admit that export ABI.
        with self.assertRaisesRegex(ValueError, 'standard Vulkan API'):
            self.validate(fixture(standard_exports=True))
        for name in (*tool.VULKAN_FUNCTIONS, 'vkCreateQueryPool', 'vkCreateDevice', 'vkDestroyInstance'):
            with self.subTest(standard=name), self.assertRaisesRegex(ValueError, 'standard Vulkan API'):
                self.validate(fixture(extra_export=name))
        for name in tool.VULKAN_EXPORTS:
            with self.subTest(private=name), self.assertRaises(ValueError):
                self.validate(fixture(missing_export=name))

    def test_missing_entry_provider_export_scope_never_relabels(self):
        for name in tool.ENTRIES:
            with self.subTest(missing=name), self.assertRaises(ValueError):
                self.validate(fixture(missing_entry=name))
        for item in (*tool.ALIAS_IMPORTS, *tool.BOOTSTRAP_IMPORTS):
            with self.subTest(missing=item), self.assertRaises(ValueError):
                self.validate(fixture(missing_import=item))
        for name in ('pthread_mutex_lock', 'write', 'VR_InitInternal2', 'vkCreateImage'):
            with self.subTest(public=name), self.assertRaises(ValueError):
                self.validate(fixture(extra_export=name))
        for name in ('frame_import_other_entry', 'frame_client_startup_epoll_wait_entry'):
            with self.subTest(entry=name), self.assertRaises(ValueError):
                self.validate(fixture(extra_entry=name))
        for name in ('frame_client_resource_raw_scope', 'frame_compositor_other_scope'):
            with self.subTest(scope=name), self.assertRaises(ValueError):
                self.validate(fixture(extra_scope=name))
        for option in ({'entry_size': 152}, {'entry_visibility': 0}, {'dynamic_tls': True}, {'tls_bytes': 129},
                       {'dependency': 'libopenvr_api.so'}, {'dependency': 'libvulkan.so.1'}):
            with self.subTest(option=option), self.assertRaises(ValueError):
                self.validate(fixture(**option))

    def test_marker_and_ready_object_exact_permissions_identity(self):
        for i in range(16):
            bad = bytearray(tool.MARKER); bad[i] ^= 1
            with self.subTest(marker_byte=i), self.assertRaises(ValueError):
                self.validate(fixture(marker=bad))
        for option in ({'marker_flags': 5}, {'marker_flags': 6}, {'marker_visibility': 0},
                       {'ready_flags': 4}, {'ready_flags': 7}, {'ready_rva': 0x7401}, {'ready_rva': 0x8000},
                       {'ready_rva': 0x6000}, {'ready_size': 8}, {'ready_rva': 0x7100, 'ready_value': 1}):
            with self.subTest(option=option), self.assertRaises(ValueError):
                self.validate(fixture(**option))
        _, ready_in_file = self.validate(fixture(ready_rva=0x7100))
        self.assertEqual(ready_in_file['ready_object']['initial_value'], 0)

    def test_separate_review_hash_and_actual_provider_set(self):
        data = fixture(); providers = set(tool.ALIAS_IMPORTS) | set(tool.BOOTSTRAP_IMPORTS)
        for value in (None, '', 'f' * 64, hashlib.sha256(data).hexdigest().upper()):
            with self.subTest(review=value), self.assertRaises(ValueError):
                tool.validate_observer(data, value, providers)
        for provider in providers:
            with self.subTest(provider=provider), self.assertRaises(ValueError):
                tool.validate_observer(data, tool.digest(data).hex(), providers - {provider})

    def test_remote_single_dso_and_source_readiness_layout(self):
        _, report = self.validate(fixture())
        source = tool.audit_source((0, 6000000), (0, 1700000), report, REMOTE)
        self.assertIn('namespace frame_compositor_audit', source)
        self.assertIn('"",0,6000000,0,0', source)
        self.assertIn('"/usr/lib/libc.so.6",0,1700000,0,0', source)
        self.assertIn('0x87370,0x88f74,0xdfee0,true,29696', source)
        self.assertIn(REMOTE, source)
        for path in ('/usr/lib/' + tool.MAIN, REMOTE.replace('/frame-compositor-raw/', '/frame-compositor-raw/../'),
                     REMOTE.replace(tool.MAIN, 'other.so'), REMOTE.replace('GloomhavenVR', 'Gloomhaven%VR'),
                     REMOTE.replace('GloomhavenVR', 'Gloomhaven VR'), REMOTE.replace('GloomhavenVR', 'Gloomhaven:VR')):
            with self.subTest(path=path), self.assertRaises(ValueError):
                tool.remote_observer(path)
        manifest = tool.manifest_for(REMOTE)
        self.assertEqual(manifest['layer']['library_path'], REMOTE)
        self.assertEqual(manifest['layer']['name'], tool.LAYER)
        self.assertEqual(manifest['layer']['enable_environment'], {'FRAME_MASK_TRACE': '1'})
        self.assertEqual(manifest['layer']['functions'], tool.VULKAN_FUNCTIONS)

    def test_fixed_envelope_framing_and_each_limit(self):
        blobs = synthetic_profiles(); reviewed = blobs[3][-32:]
        # A controlled envelope layout test explicitly substitutes only synthetic
        # descriptor identities. Production's canonical native pins stay fixed.
        with mock.patch.object(tool, 'NATIVE_PROFILES', tuple(blob[-32:] for blob in blobs[:3])):
            envelope = tool.encode_envelope(blobs, reviewed)
            self.assertEqual(len(envelope), 8272)
            self.assertEqual(envelope[:16], tool.MARKER)
            self.assertEqual(envelope[16:48], reviewed)
            self.assertEqual(struct.unpack_from('<8Q', envelope, 48), tool.LIMIT_WORDS)
            self.assertEqual(envelope[112:8240], b''.join(blobs))
            parsed = tool.decode_envelope(envelope, reviewed)
            self.assertEqual(parsed['schema'], 5)
            self.assertTrue(parsed['independent_observer_checked'])
            self.assertEqual(parsed['raw_limits']['private_bytes'], 757)
            self.assertEqual(parsed['raw_limits']['bootstrap_poll_bytes'], 0)
            for i in range(8272):
                bad = bytearray(envelope); bad[i] ^= 1
                with self.subTest(byte=i), self.assertRaises(ValueError):
                    tool.decode_envelope(bad, reviewed)
            for i in range(8):
                bad = bytearray(envelope); struct.pack_into('<Q', bad, 48 + i * 8, tool.LIMIT_WORDS[i] + 1)
                bad[-32:] = tool.digest(bad[:-32])
                with self.subTest(limit=i), self.assertRaises(ValueError):
                    tool.decode_envelope(bad, reviewed)
            with self.assertRaises(ValueError): tool.decode_envelope(envelope, bytes(32))
        with self.assertRaises(ValueError): tool.encode_envelope(blobs, reviewed)

    def test_actual_private_final_artifact_prepares_nonnative_bundle(self):
        if not (PRIVATE_NATIVE and FINAL_OBSERVER and FINAL_SHA):
            self.skipTest('Explicit private inputs not supplied')
        with tempfile.TemporaryDirectory(prefix='frame-compositor-preparer-controls-') as temporary:
            output = Path(temporary) / 'bundle'
            plan = tool.prepare(PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA, output, REMOTE)
            self.assertEqual(plan['schema'], 'frame-compositor-raw-offline-bundle-v7')
            self.assertEqual(plan['raw_limits'], tool.RAW_LIMITS)
            self.assertEqual(plan['capture_limits'], tool.CAPTURE_LIMITS)
            self.assertEqual(plan['operator_arm'], tool.operator_arm_descriptor(Path(REMOTE).parent))
            for field in ('installed', 'activated', 'runtime_authorized', 'frame_access', 'network_access', 'automatic_restart',
                          'native_binary_or_script_changes', 'shader_or_mask_activation', 'source_frame_associated', 'gpu_coherent'):
                self.assertIs(plan[field], False)
            self.assertEqual(plan['environment'], {'LD_PRELOAD': plan['selector']['configuration']['selector']})
            self.assertEqual(plan['manifest']['library_path'], plan['selector']['configuration']['payload'])
            self.assertEqual(plan['launch_isolation']['target_exec_environment'], tool.launch_environment(plan['selector']['configuration']))
            self.assertEqual(plan['manifest']['functions'], tool.VULKAN_FUNCTIONS)
            self.assertEqual(plan['manifest']['frame_path'], '/home/steamos/.local/share/vulkan/implicit_layer.d/frame_compositor_raw.json')
            self.assertEqual([p['role'] for p in plan['native_profiles']], [7, 3, 4])
            parsed = tool.decode_envelope((output / tool.ENVELOPE).read_bytes(), bytes.fromhex(plan['envelope']['reviewed_observer_profile_identity']))
            self.assertEqual(parsed['module_roles'], [7, 3, 4, 5])
            self.assertEqual(os.stat(output).st_mode & 0o777, 0o700)
            for entry in plan['inventory']:
                artifact = output / entry['name']
                self.assertEqual(os.stat(artifact).st_mode & 0o777, 0o600)
                self.assertEqual(artifact.stat().st_size, entry['bytes'])
                self.assertEqual(hashlib.sha256(artifact.read_bytes()).hexdigest(), entry['sha256'])
                self.assertFalse(entry['native_bytes'])
            with self.assertRaises(ValueError): tool.prepare(PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA, output, REMOTE)
            self.assertEqual({p.name for p in output.iterdir()}, {tool.MAIN, tool.AUDIT, tool.SELECTOR, tool.SELECTOR_SOURCE,
                tool.ENVELOPE, tool.MANIFEST, tool.PLAN, 'audit-preparation.cpp'})


def main():
    global PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-directory', type=Path)
    parser.add_argument('--observer', type=Path)
    parser.add_argument('--observer-sha256')
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    if any((args.native_directory, args.observer, args.observer_sha256)) and not all((args.native_directory, args.observer, args.observer_sha256)):
        parser.error('All three private input arguments must be supplied together')
    PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA = args.native_directory, args.observer, args.observer_sha256
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PackageControls)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if args.record:
        record = {'status': 'passed' if result.wasSuccessful() else 'failed', 'tests': result.testsRun,
                  'skipped': len(result.skipped), 'scope': 'offline synthetic metadata controls plus explicitly supplied private actual-artifact preparation',
                  'frame_access': False, 'native_execution': False, 'regenerable_fixture_directories_removed': True}
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(record, indent=2) + '\n'); args.record.chmod(0o600)
    raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == '__main__':
    main()
