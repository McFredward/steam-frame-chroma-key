#!/usr/bin/env python3
"""Execute the exact final ARM diagnostic inertly with private pinned providers.

No Frame access, deployment, graphics calls or native capture admission. Owned
compiler/QEMU processes are bounded and reaped before temporary files are removed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile

sys.dont_write_bytecode = True
import prepare_client_startup_observer as elf_tools
import run_import_native_forwarding as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('observer', 'qemu', 'providers', 'record'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--compiler', default='aarch64-linux-gnu-g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    receipt = {'status': 'failed', 'frame_access': False, 'production_admission': False,
               'headset': False, 'logs': {}, 'cases': {}}
    try:
        observer = args.observer.resolve(strict=True)
        data = elf_tools.frozen_read(observer)
        elf = elf_tools.Elf(data)
        names = ('init', 'destroy', 'lock', 'unlock', 'write', 'close', 'read', 'connect')
        entries = []
        for name in names:
            found = [s for s in elf.symbols if s['name'] == 'frame_import_' + name + '_entry']
            if len(found) != 1: raise ValueError('Missing or ambiguous hidden entry')
            entries.append(found[0])
        receipt['observer_sha256'] = hashlib.sha256(data).hexdigest()
        qemu = args.qemu.resolve(strict=True)
        prefix = args.providers.resolve(strict=True)
        for name, pin in common.PINNED.items():
            if common.digest(prefix / 'lib' / name) != pin:
                raise ValueError('Unreviewed native provider: ' + name)
        source = root / 'tests/test_compositor_raw_preload.cpp'
        receipt['source_sha256'] = common.digest(source)
        clean = {k: v for k, v in os.environ.items() if not k.startswith(('LD_', 'GHVR_FRAME_', 'FRAME_MASK_', 'QEMU_'))}
        with tempfile.TemporaryDirectory(prefix='frame-compositor-final-abi-') as folder:
            folder = Path(folder)
            folder.chmod(0o700)
            target = folder / 'fixture'
            result = common.recorded_run(shlex.split(args.compiler) + [
                '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                str(source), '-pthread', '-ldl', '-o', str(target)],
                folder, receipt['logs'], 'compile', 60, clean)
            if result.returncode:
                raise ValueError('ARM fixture compilation failed')
            bad = folder / 'invalid-envelope.bin'
            bad.write_bytes(bytes(8272)); bad.chmod(0o600)
            fifo = folder / 'fifo'; os.mkfifo(fifo, 0o600)
            cases = {'no_profile': None, 'invalid_profile': bad, 'fifo_profile': fifo}
            for name, envelope in cases.items():
                output = folder / (name + '.jsonl')
                env = {'LD_PRELOAD': str(observer),
                       'LD_LIBRARY_PATH': str(prefix / 'lib') + ':/usr/aarch64-linux-gnu/lib',
                       'GHVR_FRAME_COMPOSITOR_RAW_OUTPUT': str(output)}
                if envelope is not None:
                    env['GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE'] = str(envelope)
                command = [str(qemu), '-L', str(prefix)]
                for key, value in env.items(): command += ['-E', key + '=' + value]
                command += [str(target), str(observer)] + [format(e['rva'], 'x') for e in entries]
                result = common.recorded_run(command, folder, receipt['logs'], name, 20, clean)
                parsed = [json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith('RESULT=')]
                if result.returncode or len(parsed) != 1 or output.exists() or not parsed[0].get('read_forced_unwind'):
                    raise ValueError('Final ELF inert/ABI fixture failed: ' + name)
                receipt['cases'][name] = parsed[0]
            receipt['status'] = 'passed'
    finally:
        receipt['cleanup'] = 'Owned groups reaped; temporary compiler/QEMU fixture and inputs removed'
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(receipt, indent=2) + '\n'); args.record.chmod(0o600)
    print(json.dumps({'status': receipt['status'], 'cases': receipt['cases'], 'headset': False}))


if __name__ == '__main__':
    main()
