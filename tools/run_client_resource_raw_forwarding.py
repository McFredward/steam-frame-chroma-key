#!/usr/bin/env python3
"""Execute actual final private ARM read/write entries with pinned libc/QEMU.

The supplied own observer loads inert without a profile. Controlled real pipes
verify native bytes, ABI, errno and cancellation through the actual final DSO;
no rebuilt toy entry, native transaction reads, game/OpenVR startup, network,
Frame deployment, resource lease, frame/GPU association or passthrough proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sys
import tempfile

sys.dont_write_bytecode = True
import prepare_client_startup_observer as metadata
import run_import_native_forwarding as common

ENTRIES = ('frame_import_read_entry', 'frame_import_write_entry')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer', required=True, type=Path)
    parser.add_argument('--expected-observer-sha256', required=True,
                        help='explicit reviewed own artifact pin; never silently test another build')
    parser.add_argument('--qemu', required=True, type=Path)
    parser.add_argument('--providers', required=True, type=Path)
    parser.add_argument('--compiler', default='aarch64-linux-gnu-g++')
    parser.add_argument('--support-libraries', default=Path('/usr/aarch64-linux-gnu/lib'), type=Path)
    parser.add_argument('--record', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    receipt = {'status': 'failed', 'frame_access': False, 'network_access': False,
               'native_openvr': False, 'headset': False, 'production_ready': False,
               'cold_registration': False, 'native_transaction_acquisition': False,
               'logs': {}}
    try:
        if not re.fullmatch('[0-9a-f]{64}', args.expected_observer_sha256):
            raise SystemExit('Explicit reviewed observer SHA-256 must be 64 lowercase hex digits')
        observer = args.observer.resolve(strict=True)
        qemu = args.qemu.resolve(strict=True)
        prefix = args.providers.resolve(strict=True)
        support = args.support_libraries.resolve(strict=True)
        data = metadata.frozen_read(observer)
        pin = hashlib.sha256(data).hexdigest()
        if pin != args.expected_observer_sha256:
            raise SystemExit('Refusing a different actual observer artifact')
        elf = metadata.Elf(data)
        public = {s['name'] for s in elf.symbols if s['dynamic'] and s['defined'] and
                  s['binding'] in (1, 2) and s['visibility'] == 0}
        if public != {metadata.BRIDGE}:
            raise SystemExit('Actual final observer must retain its sole own public bridge')
        offsets = []
        receipt['hidden_entries'] = []
        for name in ENTRIES:
            matches = [s for s in elf.symbols if s['name'] == name and s['defined']]
            if (len(matches) != 1 or matches[0]['type'] != 2 or
                    (matches[0]['binding'] != 0 and matches[0]['visibility'] != 2) or
                    matches[0]['size'] != 156):
                raise SystemExit('Actual artifact lacks its exact private standard ARM entry: ' + name)
            entry = matches[0]
            spans = [p for p in elf.phdrs if p[0] == 1 and p[1] & 1 and
                     p[3] <= entry['rva'] and entry['rva'] + entry['size'] <= p[3] + p[5]]
            if len(spans) != 1:
                raise SystemExit('Private entry lacks unique file-backed native RX segment')
            offsets.append(entry['rva'])
            receipt['hidden_entries'].append({'name': name, 'rva': entry['rva'], 'bytes': entry['size']})
        if len(set(offsets)) != 2:
            raise SystemExit('Read/write entry offsets must be distinct')
        receipt['observer_sha256'] = pin
        receipt['observer_bytes'] = len(data)
        receipt['qemu_sha256'] = common.digest(qemu)
        receipt['providers'] = {}
        for name, expected in common.PINNED.items():
            actual = common.digest(prefix / 'lib' / name)
            if actual != expected:
                raise SystemExit('Refusing unreviewed native provider ' + name)
            receipt['providers'][name] = actual
        clean = os.environ.copy()
        for name in list(clean):
            if name.startswith('GHVR_FRAME_') or name in ('LD_PRELOAD', 'LD_AUDIT', 'LD_LIBRARY_PATH', 'QEMU_LD_PREFIX'):
                clean.pop(name, None)
        source = root / 'tests/test_client_resource_raw_forwarding.cpp'
        receipt['source_sha256'] = {str(p.relative_to(root)): common.digest(p) for p in
            (source, root / 'import/import_startup_audit.h', root / 'tools/prepare_client_startup_observer.py',
             root / 'tools/run_import_native_forwarding.py', Path(__file__).resolve())}
        with tempfile.TemporaryDirectory(prefix='frame-resource-raw-entries-') as folder:
            folder = Path(folder)
            folder.chmod(0o700)
            executable = folder / 'actual-read-write-entries'
            command = shlex.split(args.compiler) + ['-std=c++17', '-O2', '-g', '-fno-omit-frame-pointer',
                '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', str(root), str(source),
                '-pthread', '-ldl', '-o', str(executable)]
            built = common.recorded_run(command, folder, receipt['logs'], 'compile', 60, clean)
            if built.returncode:
                raise SystemExit('ARM actual-entry fixture build failed: ' + built.stderr[-3000:])
            receipt['fixture_elf_sha256'] = common.digest(executable)
            output = folder / 'unrelated-output.jsonl'
            command = [str(qemu), '-L', str(prefix), '-E',
                       'LD_LIBRARY_PATH=' + str(prefix / 'lib') + ':' + str(support),
                       '-E', 'GHVR_FRAME_CLIENT_STARTUP_OUTPUT=' + str(output),
                       str(executable), str(observer), str(prefix / 'lib/libc.so.6'),
                       *(hex(offset) for offset in offsets)]
            result = common.recorded_run(command, folder, receipt['logs'], 'actual_final_read_write_entries', 25, clean)
            if result.returncode:
                raise SystemExit('Actual ARM final entries failed: ' + result.stdout[-3000:] + result.stderr[-3000:])
            values = [json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith('RESULT=')]
            if (len(values) != 1 or output.exists() or
                    any(marker not in result.stdout.splitlines() for marker in
                        ('CANCELLATION_BEGIN=read', 'CANCELLATION_BEGIN=write'))):
                raise SystemExit('Missing real cancellation or inert output evidence')
            proof = values[0]
            required_true = ('actual_final_observer', 'actual_native_libc', 'real_pipe_bytes',
                             'read_forced_unwind', 'write_forced_unwind')
            required_false = ('public_libc_interposition', 'native_transaction_acquisition',
                              'cold_registration', 'production_ready', 'native_openvr', 'frame_kernel', 'headset')
            if (type(proof.get('checks')) is not int or proof['checks'] < 45 or
                    any(proof.get(name) is not True for name in required_true) or
                    any(proof.get(name) is not False for name in required_false)):
                raise SystemExit('Fixture omitted native ABI facts or exceeded its proof scope')
            if metadata.frozen_read(observer) != data:
                raise SystemExit('Actual final observer changed during fixture execution')
            receipt['result'] = proof
            receipt['status'] = 'passed'
            print(json.dumps({'status': 'passed', 'observer_sha256': pin, 'result': proof,
                              'scope': 'Actual final ARM read/write ABI only; no native transaction acquisition'}))
    except (SystemExit, OSError, ValueError) as error:
        receipt['failure'] = str(error)
        raise
    finally:
        receipt['cleanup'] = ('Owned compiler/QEMU groups reaped and temporary fixture removed; '
                              'supplied actual artifact and borrowed private providers preserved')
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(receipt, indent=2) + '\n')
        args.record.chmod(0o600)


if __name__ == '__main__':
    main()
