"""Offline preparation controls; own synthetic ELFs are never runtime authority.

The optional private-input checks authenticate borrowed native metadata, not a
Frame execution. Compilation fixtures contain only this repository's source.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_client_startup_observer as tool

PRIVATE_NATIVE = None
PRIVATE_API = None
FINAL_OBSERVER = None
FINAL_SHA = None
CONNECTION_OBSERVATION = False
RESOURCE_CENSUS = False
API_NAME = '/home/steamos/.local/share/Steam/steamrtarm64/libopenvr_api.so'
CLIENT_NAME = '/opt/steamvr/bin/linuxarm64/vrclient.so'
LIBC_NAME = '/usr/lib/libc.so.6'
FRAME_DIRECTORY = '/home/steamos/.local/share/GloomhavenVR/frame-client-startup'


def observer_fixture(*, extra_export=None, missing_alias=None, alias_rva=False,
                     bridge_visibility=0, entry_visibility=2, mutex_visibility=2, dynamic_tls=False,
                     connection=False, missing_entry=None, resource=False):
    """Construct bounded own metadata; no executable native code is included."""
    raw = bytearray(0x4000)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIQQQIHHHHHH', raw, 0, ident, 3, 183, 1, 0, 64,
                     0x3800, 0, 64, 56, 3 if resource else 2, 64, 8, 0)
    struct.pack_into('<IIQQQQQQ', raw, 64, 1, 5, 0, 0, 0, len(raw), len(raw), 4096)
    struct.pack_into('<IIQQQQQQ', raw, 120, 4, 4, 0x180, 0x180, 0x180, 36, 36, 4)
    struct.pack_into('<III', raw, 0x180, 4, 20, 3)
    raw[0x18c:0x190] = b'GNU\0'
    raw[0x190:0x1a4] = bytes(range(20))
    if resource:
        struct.pack_into('<IIQQQQQQ',raw,176,1,4,0x3400,0x3400,0x3400,16,16,8)
        raw[0x3400:0x3410]=tool.RESOURCE_MARKER
    dynamic = [(tool.BRIDGE, 0x220, 1, bridge_visibility)]
    if not connection:
        dynamic += [('pthread_mutex_init', 0x230 + (4 if alias_rva else 0), 1, 0),
                    ('pthread_mutex_destroy', 0x240, 1, 0)]
    if extra_export:
        dynamic.append((extra_export, 0x250, 1, 0))
    imports = [name.split('@')[0] for name in tool.ALIASES if name != missing_alias]
    if dynamic_tls:
        imports.append('__tls_get_addr')
    dynstr = bytearray(b'\0')
    offsets = {}
    for name in [row[0] for row in dynamic] + imports + ['GLIBC_2.17', 'libc.so.6']:
        offsets[name] = len(dynstr)
        dynstr += name.encode() + b'\0'
    dynsym = bytearray(24)
    versions = [0]
    for name, rva, binding, visibility in dynamic:
        dynsym += struct.pack('<IBBHQQ', offsets[name], binding << 4 | 2, visibility, 1, rva, 4)
        versions.append(1)
    for name in imports:
        dynsym += struct.pack('<IBBHQQ', offsets[name], 0x12, 0, 0, 0, 0)
        versions.append(2)
    statstr = bytearray(b'\0')
    symtab = bytearray(24)
    for index, name in enumerate(tool.ENTRIES + tool.MUTEX_ENTRIES + (tool.CONNECTION_ENTRIES if connection else ())):
        if name == missing_entry:
            continue
        off = len(statstr)
        statstr += name.encode() + b'\0'
        rva = (0x200, 0x210, 0x230, 0x240,0x250,0x260,0x270)[index]
        symtab += struct.pack('<IBBHQQ', off, 0x12, entry_visibility if index<2 else mutex_visibility, 1, rva, 4)
    if resource:
        off=len(statstr);statstr+=tool.RESOURCE_SCOPE.encode()+b'\0'
        symtab+=struct.pack('<IBBHQQ',off,1,2,1,0x3400,16)
    verneed = struct.pack('<HHIII', 1, 1, offsets['libc.so.6'], 16, 0)
    verneed += struct.pack('<IHHII', 0, 0, 2, offsets['GLIBC_2.17'], 0)
    versym = struct.pack('<' + 'H' * len(versions), *versions)
    dynamic_table = struct.pack('<qQqQ', 1, offsets['libc.so.6'], 0, 0)
    sections = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    cursor = 0x400
    for kind, payload, link, entsize in [(3, dynstr, 0, 0), (11, dynsym, 1, 24),
                                        (3, statstr, 0, 0), (2, symtab, 3, 24),
                                        (0x6ffffffe, verneed, 1, 0), (0x6fffffff, versym, 2, 2),
                                        (6, dynamic_table, 1, 16)]:
        raw[cursor:cursor + len(payload)] = payload
        sections.append((0, kind, 0, 0, cursor, len(payload), link, 0, 8, entsize))
        cursor += (len(payload) + 7) & ~7
    for index, section in enumerate(sections):
        struct.pack_into('<IIQQQQIIQQ', raw, 0x3800 + 64 * index, *section)
    return bytes(raw)


class PreparationTests(unittest.TestCase):
    def test_resource_census_requires_explicit_scope_and_retained_exact_elf_marker(self):
        raw=observer_fixture(connection=True,resource=True)
        _,report=tool.validate_observer(raw,tool.digest(raw).hex(),set(tool.ALIASES),
                                       connection_observation=True,resource_census=True)
        self.assertTrue(report['resource_register_census'])
        self.assertEqual(report['resource_scope_marker'],{'symbol':tool.RESOURCE_SCOPE,'rva':0x3400,
                         'bytes':16,'sha256':tool.digest(tool.RESOURCE_MARKER).hex()})
        self.assertEqual(report['resource_private_bytes_read'],0)
        self.assertFalse(report['resource_transaction_accepted'])
        self.assertFalse(report['resource_namespace_associated'])
        self.assertEqual(len(report['hidden_entries'])+len(report['hidden_mutex_entries'])+len(report['hidden_connection_entries']),7)
        for connection,census in ((False,True),(True,False),(False,False)):
            with self.subTest(connection=connection,census=census),self.assertRaises(ValueError):
                tool.validate_observer(raw,tool.digest(raw).hex(),set(tool.ALIASES),
                                       connection_observation=connection,resource_census=census)
        old=observer_fixture(connection=True)
        with self.assertRaisesRegex(ValueError,'opt-in object'):
            tool.validate_observer(old,tool.digest(old).hex(),set(tool.ALIASES),
                                   connection_observation=True,resource_census=True)
        for offset,value in ((0x3400,ord('X')),(0x3408,2),(0x340c,0),(180,6)):
            changed=bytearray(raw);changed[offset]=value
            with self.subTest(corrupt_offset=offset),self.assertRaises(ValueError):
                tool.validate_observer(bytes(changed),tool.digest(changed).hex(),set(tool.ALIASES),
                                       connection_observation=True,resource_census=True)

    def test_resource_envelope_keeps_native_profiles_and_requires_connection_scope(self):
        args=([bytes(2032)]*5,bytes(1064),bytes(32),CLIENT_NAME,(0,0x60699a))
        connection=tool._assemble(*args,connection_observation=True)
        census=tool._assemble(*args,connection_observation=True,resource_census=True)
        self.assertEqual(census[:16],b'FCCENS01'+struct.pack('<II',3,11800))
        self.assertEqual(census[16:],connection[16:])
        with self.assertRaisesRegex(ValueError,'explicit connection'):
            tool._assemble(*args,resource_census=True)
        with self.assertRaisesRegex(ValueError,'explicit connection'):
            tool.prepare(Path('/unused'),Path('/unused'),'',Path('/unused'),FRAME_DIRECTORY,API_NAME,CLIENT_NAME,
                         libc_name=LIBC_NAME,resource_census=True)

    def test_connection_scope_requires_explicit_matching_elf(self):
        legacy=observer_fixture();connection=observer_fixture(connection=True)
        with self.assertRaises(ValueError):
            tool.validate_observer(connection,tool.digest(connection).hex(),set(tool.ALIASES))
        with self.assertRaises(ValueError):
            tool.validate_observer(legacy,tool.digest(legacy).hex(),set(tool.ALIASES),connection_observation=True)
        _,report=tool.validate_observer(connection,tool.digest(connection).hex(),set(tool.ALIASES),connection_observation=True)
        self.assertTrue(report['connection_register_observation'])
        self.assertEqual(report['hidden_connection_entries'],dict(zip(tool.CONNECTION_ENTRIES,(0x250,0x260,0x270))))
        self.assertEqual(report['public_mutex_aliases'],{})
        self.assertTrue(report['audit_only_libc_routing'])
        self.assertEqual({s['name'] for s in tool.Elf(connection).exported()},{tool.BRIDGE})
        for name in ('pthread_mutex_init','pthread_mutex_destroy','pthread_mutex_lock','pthread_mutex_unlock','connect'):
            with self.subTest(global_interposition=name), self.assertRaisesRegex(ValueError,'unexpected public entry'):
                raw=observer_fixture(connection=True,extra_export=name)
                tool.validate_observer(raw,tool.digest(raw).hex(),set(tool.ALIASES),connection_observation=True)
        for name in tool.MUTEX_ENTRIES+tool.CONNECTION_ENTRIES:
            with self.subTest(missing_hidden_entry=name), self.assertRaisesRegex(ValueError,'Missing/ambiguous'):
                raw=observer_fixture(connection=True,missing_entry=name)
                tool.validate_observer(raw,tool.digest(raw).hex(),set(tool.ALIASES),connection_observation=True)
        with self.assertRaisesRegex(ValueError,'not hidden'):
            raw=observer_fixture(connection=True,mutex_visibility=0)
            tool.validate_observer(raw,tool.digest(raw).hex(),set(tool.ALIASES),connection_observation=True)

    def test_connection_envelope_and_audit_are_distinct_non_activating_scope(self):
        legacy=tool._assemble([bytes(2032)]*5,bytes(1064),bytes(32),CLIENT_NAME,(0,0x60699a))
        conn=tool._assemble([bytes(2032)]*5,bytes(1064),bytes(32),CLIENT_NAME,(0,0x60699a),connection_observation=True)
        self.assertEqual(len(conn),11800)
        self.assertEqual(conn[:16],b'FCCONN01'+struct.pack('<II',2,11800))
        self.assertEqual(conn[16:],legacy[16:])
        args=((0,2),(0,3),(0,4),(0,5),1,API_NAME,CLIENT_NAME,FRAME_DIRECTORY+'/'+tool.MAIN,(0,6),LIBC_NAME)
        self.assertNotIn('0x87370',tool.audit_source(*args))
        source=tool.audit_source(*args,connection_observation=True)
        self.assertIn('0x87370,0x88f74,0xebcb0',source)
        self.assertNotIn('0xda610',source)

    def test_own_observer_metadata_has_exact_reviewed_exports_and_imports(self):
        raw = observer_fixture()
        elf, report = tool.validate_observer(raw, tool.digest(raw).hex(), set(tool.ALIASES))
        self.assertEqual(report['bridge_rva'], 0x220)
        self.assertEqual(report['hidden_entries'], dict(zip(tool.ENTRIES, (0x200, 0x210))))
        self.assertEqual(report['public_mutex_aliases'], {'pthread_mutex_init': 0x230, 'pthread_mutex_destroy': 0x240})
        self.assertEqual(report['hidden_mutex_entries'], dict(zip(tool.MUTEX_ENTRIES, (0x230, 0x240))))
        self.assertEqual(elf.imports(), set(tool.ALIASES))
        self.assertEqual(elf.needed, ['libc.so.6'])
        profile, _ = tool.modules.profile_from_bytes(raw, 5, tool.digest(raw).hex())
        self.assertEqual(len(profile), 2032)
        self.assertEqual(profile[-32:], tool.digest(profile[:-32]))

    def test_observer_refusals_use_actual_elf_symbols(self):
        controls = [('VR_InitInternal2', {'extra_export': 'VR_InitInternal2'}),
                    ('VR_ShutdownInternal', {'extra_export': 'VR_ShutdownInternal'}),
                    ('dlopen', {'extra_export': 'dlopen'}), ('execve', {'extra_export': 'execve'}),
                    ('unexpected ordinary export', {'extra_export': 'unreviewed_callback'}),
                    ('alias address', {'alias_rva': True}), ('bridge visibility', {'bridge_visibility': 2}),
                    ('public startup entry', {'entry_visibility': 0}), ('public mutex entry', {'mutex_visibility': 0}),
                    ('dynamic TLS', {'dynamic_tls': True})]
        controls += [(name, {'missing_alias': name}) for name in tool.ALIASES]
        for name, arguments in controls:
            with self.subTest(name=name):
                raw = observer_fixture(**arguments)
                with self.assertRaises(ValueError):
                    tool.validate_observer(raw, tool.digest(raw).hex(), set(tool.ALIASES) | {'__tls_get_addr@GLIBC_2.17'})

    def test_observer_independent_sha_and_native_provider_versions_required(self):
        raw = observer_fixture()
        for sha in ('0' * 64, tool.digest(raw).hex().upper(), '', None):
            with self.subTest(sha=sha), self.assertRaises(ValueError):
                tool.validate_observer(raw, sha, set(tool.ALIASES))
        for name in tool.ALIASES:
            with self.subTest(provider=name), self.assertRaises(ValueError):
                tool.validate_observer(raw, tool.digest(raw).hex(), set(tool.ALIASES) - {name})

    def test_alias_import_function_abi_and_forced_native_dependency_refused(self):
        raw = observer_fixture()
        elf = tool.Elf(raw)
        dynsym = next(section for section in elf.sections if section[1] == 11)
        # Null + bridge + two public aliases precede the eight imports.
        changed = bytearray(raw)
        changed[dynsym[4] + 24 * 4 + 4] = 0x11  # STT_OBJECT instead of STT_FUNC.
        with self.assertRaisesRegex(ValueError, 'function import ABI'):
            tool.validate_observer(bytes(changed), tool.digest(changed).hex(), set(tool.ALIASES))
        dynamic = next(section for section in elf.sections if section[1] == 6)
        strings = elf.linked(dynamic)
        changed = bytearray(raw)
        # An actual DT_NEEDED entry now requests a native client load. Use an
        # existing string slot without altering any symbol table references.
        name = b'libc.so.6\0'
        offset = changed[strings[4]:strings[4] + strings[5]].index(name)
        changed[strings[4] + offset:strings[4] + offset + len(name)] = b'vrclient\0\0'
        with self.assertRaisesRegex(ValueError, 'forces an API/client'):
            tool.validate_observer(bytes(changed), tool.digest(changed).hex(), set(tool.ALIASES))

    def test_elf_parser_rejects_truncated_and_wrong_table_geometry(self):
        raw = observer_fixture()
        mutations = [(18, '<H', 62), (16, '<H', 2), (58, '<H', 63), (54, '<H', 55),
                     (56, '<H', 17), (60, '<H', 4097), (40, '<Q', len(raw)),
                     (0x3800 + 64 * 2 + 40, '<I', 999),
                     (0x3800 + 64 * 6 + 32, '<Q', 2)]
        for offset, fmt, value in mutations:
            changed = bytearray(raw)
            struct.pack_into(fmt, changed, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises((ValueError, struct.error)):
                tool.Elf(bytes(changed))
        for length in (0, 63, 127, len(raw) - 1):
            with self.subTest(length=length), self.assertRaises((ValueError, struct.error)):
                tool.Elf(raw[:length])
        with self.assertRaises(ValueError):
            tool.Elf(bytes(tool.MAX_FILE + 1))

    def test_native_self_consistent_synthetic_profiles_are_not_authority(self):
        raw = observer_fixture()
        for role in range(1, 5):
            with self.subTest(role=role), self.assertRaisesRegex(ValueError, 'native identity'):
                tool.modules.profile_from_bytes(raw, role)
        with self.assertRaisesRegex(ValueError, 'Unsupported client'):
            tool.client.profile_from_bytes(raw)
        synthetic = []
        for role in range(1, 6):
            value = bytearray(2032)
            struct.pack_into('<4Q', value, 0, 1, role, 183, 3)
            value[-32:] = tool.digest(value[:-32])
            synthetic.append(bytes(value))
        with self.assertRaisesRegex(ValueError, 'independently pinned'):
            tool.validate_profiles(synthetic, bytes(1064), synthetic[4][-32:])

    def test_schema_offsets_and_exact_non_normalized_utf8_client_name(self):
        profiles = [bytes([role]) * 2032 for role in range(1, 6)]
        reviewed = b'x' * 32
        client = b'c' * 1064
        name = '/tmp/exact/../raw client-\N{SNOWMAN}.so'
        result = tool._assemble(profiles, client, reviewed, name, (0, 6318490))
        self.assertEqual(len(result), 11800)
        self.assertEqual(result[:16], b'FCSTART1' + struct.pack('<II', 1, 11800))
        self.assertEqual(result[16:48], reviewed)
        self.assertEqual(result[48:560], name.encode() + bytes(512 - len(name.encode())))
        self.assertEqual(struct.unpack_from('<QQ', result, 560), (0, 6318490))
        self.assertEqual(result[576:10736], b''.join(profiles))
        self.assertEqual(result[10736:], client)
        # Raw layout construction is deliberately not the authenticated encoder.
        with self.assertRaises(ValueError):
            tool.encode_envelope(profiles, client, reviewed, name, (0, 6318490))

    def test_invalid_name_geometry_paths_and_collisions(self):
        for value in ('', 'x\0y', 'a\ny', 'x' * 512, '\N{SNOWMAN}' * 171):
            with self.subTest(name=value), self.assertRaises(ValueError):
                tool.loader_name(value)
        for rx in ((1, 1), (2, 1), (-1, 3), (0, 2**64)):
            with self.subTest(rx=rx), self.assertRaises(ValueError):
                tool._assemble([bytes(2032)] * 5, bytes(1064), bytes(32), 'client', rx)
        for value in ('relative', '/tmp/frame', '/home/steamos', '/home/steamos/../other',
                      '/home/steamos//repeat', '/home/steamos/a\\b', '/home/steamos/a"b', '/home/steamos/a\nb'):
            with self.subTest(path=value), self.assertRaises(ValueError):
                tool.user_directory(value, '/home/steamos')
        self.assertEqual(str(tool.user_directory(FRAME_DIRECTORY, '/home/steamos')), FRAME_DIRECTORY)
        with self.assertRaises(ValueError):
            tool.user_directory('/unrestricted', '/')
        with tempfile.TemporaryDirectory(prefix='client-startup-paths-') as temporary:
            folder = Path(temporary)
            for api, client_name in (('same', 'same'), (FRAME_DIRECTORY + '/' + tool.MAIN, 'client')):
                with self.subTest(api=api), self.assertRaisesRegex(ValueError, 'collision'):
                    tool.prepare(folder, folder / 'none', '0' * 64, folder / 'new', FRAME_DIRECTORY, api, client_name,
                                 libc_name=LIBC_NAME)
            (folder / 'exists').mkdir()
            with self.assertRaisesRegex(ValueError, 'new'):
                tool.prepare(folder, folder / 'none', '0' * 64, folder / 'exists', FRAME_DIRECTORY, API_NAME, CLIENT_NAME,
                             libc_name=LIBC_NAME)
            with self.assertRaisesRegex(ValueError, 'borrowed'):
                tool.prepare(folder, folder / 'none', '0' * 64, folder / 'borrowed-output', FRAME_DIRECTORY, API_NAME, CLIENT_NAME,
                             libc_name=LIBC_NAME)
            for libc in (None, API_NAME, CLIENT_NAME, FRAME_DIRECTORY + '/' + tool.MAIN):
                with self.subTest(libc=libc), self.assertRaises(ValueError):
                    tool.prepare(folder, folder / 'none', '0' * 64, folder / 'new', FRAME_DIRECTORY, API_NAME, CLIENT_NAME,
                                 libc_name=libc)

    def test_private_files_are_bounded_nofollow_and_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='client-startup-files-') as temporary:
            folder = Path(temporary)
            file = folder / 'own'
            tool.private_write(file, observer_fixture())
            self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
            self.assertEqual(tool.frozen_read(file), observer_fixture())
            with self.assertRaises(FileExistsError):
                tool.private_write(file, b'replaced')
            link = folder / 'link'
            link.symlink_to(file)
            with self.assertRaises(OSError):
                tool.frozen_read(link)
            fifo = folder / 'fifo'
            os.mkfifo(fifo)
            with self.assertRaisesRegex(ValueError, 'regular'):
                tool.frozen_read(fifo)
            with self.assertRaises(ValueError):
                tool.frozen_read(file, 64)
            (folder / 'tiny').write_bytes(b'x')
            with self.assertRaises(ValueError):
                tool.frozen_read(folder / 'tiny')

    @unittest.skipUnless(shutil.which('aarch64-linux-gnu-g++') and shutil.which('g++'), 'explicit local compilers unavailable')
    def test_actual_own_audit_compile_has_no_imports_and_default_is_inactive(self):
        logs = {}
        with tempfile.TemporaryDirectory(prefix='client-startup-audit-control-') as temporary:
            folder = Path(temporary)
            source = tool.audit_source((0, 500), (0, 600), (0, 700), (0, 800), 0x200,
                                       'api "\\\N{SNOWMAN}.so', 'client-\N{GRINNING FACE}.so', FRAME_DIRECTORY + '/' + tool.MAIN,
                                       (0, 900), 'libc-\N{SNOWMAN}.so')
            (folder / 'preparation.cpp').write_text(source)
            command = ['aarch64-linux-gnu-g++', '-std=c++17', '-shared', '-O2', '-fPIC', '-nostdlib',
                       '-fno-exceptions', '-fno-rtti', '-fno-builtin', '-mno-outline-atomics', '-fvisibility=hidden',
                       '-I', str(ROOT), str(ROOT / 'import/import_startup_audit.cpp'), str(folder / 'preparation.cpp'),
                       '-Wl,--build-id', '-o', str(folder / 'audit.so')]
            compiled = tool.recorded_run(command, folder, logs, 'compile_prepared_own_audit', 45)
            self.assertEqual(compiled.returncode, 0, compiled.stderr[-4096:])
            elf = tool.Elf(tool.frozen_read(folder / 'audit.so', tool.MAX_AUDIT))
            self.assertEqual(elf.imports(), set())
            self.assertEqual(elf.needed, [])
            self.assertEqual({symbol['name'] for symbol in elf.exported()},
                             {'la_version', 'la_objopen', 'la_preinit', 'la_symbind64', 'la_objclose'})
            (folder / 'prepared.cpp').write_text(
                '#include "import/import_startup_audit.h"\n'
                'int main(){const auto* p=frame_import_startup_audit::preparation();\n'
                'return !p || !p->owned_prepared_files || p->bridge_rva!=0x200 || '
                'p->init_rva!=0x5768 || p->shutdown_rva!=0x5330 || '
                'p->client.rx_rva_end!=800 || p->libc.rx_rva_begin!=0 || p->libc.rx_rva_end!=900 || '
                'p->native_mutex_init_rva!=0x86a00 || p->native_mutex_destroy_rva!=0x86980;}\n')
            compiled = tool.recorded_run(['g++', '-std=c++17', '-I', str(ROOT),
                                         str(ROOT / 'import/import_startup_audit.cpp'), str(folder / 'preparation.cpp'),
                                         str(folder / 'prepared.cpp'), '-o', str(folder / 'prepared')],
                                        folder, logs, 'compile_prepared_host_pod', 45)
            self.assertEqual(compiled.returncode, 0, compiled.stderr[-4096:])
            result = tool.recorded_run([str(folder / 'prepared')], folder, logs, 'execute_prepared_host_pod', 5)
            self.assertEqual(result.returncode, 0)
            # Execute a source-only host fixture against the real default weak
            # implementation. No native refs, loader activation or Frame access.
            (folder / 'inactive.cpp').write_text('#include "import/import_startup_audit.h"\n'
                                               'int main(){return frame_import_startup_audit::preparation()!=nullptr;}\n')
            compiled = tool.recorded_run(['g++', '-std=c++17', '-I', str(ROOT),
                                         str(ROOT / 'import/import_startup_audit.cpp'), str(folder / 'inactive.cpp'),
                                         '-o', str(folder / 'inactive')], folder, logs, 'compile_inactive', 45)
            self.assertEqual(compiled.returncode, 0, compiled.stderr[-4096:])
            result = tool.recorded_run([str(folder / 'inactive')], folder, logs, 'execute_inactive', 5)
            self.assertEqual(result.returncode, 0)
        self.assertFalse(folder.exists())

    def test_private_canonical_profiles_and_mutated_descriptors(self):
        if PRIVATE_NATIVE is None:
            self.skipTest('private actual native references not requested')
        profiles = []
        for role, (name, _) in tool.modules.NATIVE.items():
            path = PRIVATE_API if role == 2 and PRIVATE_API is not None else PRIVATE_NATIVE / name
            value, _ = tool.modules.profile_from_bytes(tool.frozen_read(path), role)
            profiles.append(value)
            self.assertEqual(value[-32:], tool.NATIVE_PROFILES[role - 1])
            if role == 2:
                api = tool.Elf(tool.frozen_read(path))
                self.assertEqual((api.function('VR_InitInternal2', True)['rva'],
                                  api.function('VR_InitInternal2', True)['size']), (0x5768, 236))
                self.assertEqual((api.function('VR_ShutdownInternal', True)['rva'],
                                  api.function('VR_ShutdownInternal', True)['size']), (0x5330, 192))
                self.assertEqual(value[-32:].hex(), '9ef296865d2583cd8136303727db150f6e4a349ffe7b175e3e0f651adfb81dcf')
        # The explicitly selected new API never turns the older opt-runtime
        # copy into an alternative accepted provider.
        if PRIVATE_API is not None:
            with self.assertRaisesRegex(ValueError, 'native identity'):
                tool.modules.profile_from_bytes(tool.frozen_read(PRIVATE_NATIVE / 'libopenvr_api.so'), 2)
        own = observer_fixture()
        fifth, _ = tool.modules.profile_from_bytes(own, 5, tool.digest(own).hex())
        profiles.append(fifth)
        client, _ = tool.client.profile_from_bytes(tool.frozen_read(PRIVATE_NATIVE / 'vrclient.so'))
        rx = tool.Elf(tool.frozen_read(PRIVATE_NATIVE / 'vrclient.so')).rx(tool.client.CALLER_RVA)
        envelope = tool.encode_envelope(profiles, client, fifth[-32:], CLIENT_NAME, rx)
        self.assertEqual(envelope[576:10736], b''.join(profiles))
        for i in range(5):
            for mode in ('length', 'field', 'self_hash', 'role'):
                changed = list(profiles)
                candidate = bytearray(changed[i])
                if mode == 'length':
                    candidate = candidate[:-1]
                elif mode == 'role':
                    struct.pack_into('<Q', candidate, 8, 6)
                    candidate[-32:] = tool.digest(candidate[:-32])
                else:
                    candidate[128] ^= 1
                    if mode == 'self_hash':
                        candidate[-32:] = tool.digest(candidate[:-32])
                changed[i] = bytes(candidate)
                with self.subTest(role=i, mode=mode), self.assertRaises(ValueError):
                    tool.encode_envelope(changed, client, fifth[-32:], CLIENT_NAME, rx)
        for mode in ('length', 'field', 'self_hash'):
            candidate = bytearray(client)
            if mode == 'length':
                candidate = candidate[:-1]
            else:
                candidate[128] ^= 1
                if mode == 'self_hash':
                    candidate[-32:] = tool.digest(candidate[:-32])
            with self.subTest(client=mode), self.assertRaises(ValueError):
                tool.encode_envelope(profiles, bytes(candidate), fifth[-32:], CLIENT_NAME, rx)
        with self.assertRaisesRegex(ValueError, 'canonical caller-containing'):
            tool.encode_envelope(profiles, client, fifth[-32:], CLIENT_NAME, (rx[0], rx[1] - 1))

    def test_full_private_bundle_is_owned_bounded_and_never_activated(self):
        if FINAL_OBSERVER is None or PRIVATE_NATIVE is None:
            self.skipTest('final own observer not requested')
        with tempfile.TemporaryDirectory(prefix='client-startup-final-bundle-control-') as temporary:
            output = Path(temporary) / 'reviewable'
            plan = tool.prepare(PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA, output, FRAME_DIRECTORY, API_NAME, CLIENT_NAME,
                                libc_name=LIBC_NAME,api_file=PRIVATE_API,
                                connection_observation=CONNECTION_OBSERVATION,resource_census=RESOURCE_CENSUS)
            self.assertEqual(len(list(output.iterdir())), 5)
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
            self.assertEqual((output / tool.MAIN).read_bytes(), tool.frozen_read(FINAL_OBSERVER))
            self.assertEqual((output / tool.ENVELOPE).stat().st_size, 11800)
            self.assertIn('not observed link-map lifetimes', plan['loader_name_provenance'])
            self.assertEqual(plan['audit']['native_mutex_rvas'], {'init':'0x86a00','destroy':'0x86980'})
            self.assertEqual(plan['libc_slot'], {'exact_loader_name':LIBC_NAME,'rx_rva_begin':0,'rx_rva_end':1637944})
            for entry in plan['inventory']:
                file = output / entry['name']
                self.assertEqual(entry['sha256'], tool.digest(file.read_bytes()).hex())
                self.assertEqual(entry['bytes'], file.stat().st_size)
                self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
                self.assertFalse(entry['native_bytes'])
            for field in ('installed', 'activated', 'runtime_authorized', 'frame_access', 'network_access',
                          'native_binary_or_script_changes', 'source_frame_associated', 'gpu_coherent'):
                self.assertFalse(plan[field], field)
            self.assertEqual(json.loads((output / 'deployment-plan.json').read_text()), plan)
            before = {file.name: tool.digest(file.read_bytes()) for file in output.iterdir()}
            with self.assertRaises(ValueError):
                tool.prepare(PRIVATE_NATIVE, FINAL_OBSERVER, FINAL_SHA, output, FRAME_DIRECTORY, API_NAME, CLIENT_NAME,
                             libc_name=LIBC_NAME,api_file=PRIVATE_API,
                             connection_observation=CONNECTION_OBSERVATION,resource_census=RESOURCE_CENSUS)
            self.assertEqual(before, {file.name: tool.digest(file.read_bytes()) for file in output.iterdir()})
        self.assertFalse(output.exists())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', type=Path)
    parser.add_argument('--api', type=Path)
    parser.add_argument('--observer', type=Path)
    parser.add_argument('--observer-sha256')
    parser.add_argument('--api-loader-name', default=API_NAME)
    parser.add_argument('--client-loader-name', default=CLIENT_NAME)
    parser.add_argument('--libc-loader-name', default=LIBC_NAME)
    parser.add_argument('--connection-observation',action='store_true')
    parser.add_argument('--resource-census',action='store_true')
    args, remaining = parser.parse_known_args()
    PRIVATE_NATIVE, PRIVATE_API, FINAL_OBSERVER, FINAL_SHA = args.native_dir, args.api, args.observer, args.observer_sha256
    CONNECTION_OBSERVATION,RESOURCE_CENSUS=args.connection_observation,args.resource_census
    API_NAME, CLIENT_NAME, LIBC_NAME = args.api_loader_name, args.client_loader_name, args.libc_loader_name
    if FINAL_OBSERVER and not FINAL_SHA:
        parser.error('--observer requires its separately reviewed --observer-sha256')
    unittest.main(argv=[sys.argv[0]] + remaining)
