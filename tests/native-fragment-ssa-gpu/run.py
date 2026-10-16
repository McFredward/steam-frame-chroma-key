#!/usr/bin/env python3
"""Run private original-fragment/MRT controls on explicitly selected host Vulkan.

No native code is embedded, installed or tracked. The original module is a
bounded, version-pinned private input. Temporary transformed modules are removed
on every exit. A real host GPU result is not a headset or native W/alpha proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import pixel_payload_recovery as recovery

ORIGINAL_SHA256 = 'f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2'


def invoke(command, env=None, timeout=60):
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout, check=False)
    return {'command': [str(item) for item in command], 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}


def demand(condition, message):
    if not condition:
        raise ValueError(message)


def mutate_export(probe):
    """Causal defect: replace only output-location2's vec4 store with location3.

    Both pointers/values are float vec4. This preserves original output0 while
    proving the GPU oracle detects a wrong exported R0 independently of colour.
    No native instructions or transformed binary are retained in source files.
    """
    demand(type(probe) is bytes and 20 <= len(probe) <= 65536 and len(probe) % 4 == 0,
           'bounded whole-word probe required')
    words = list(struct.unpack('<%dI' % (len(probe) // 4), probe))
    locations, stores = {}, {}
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 65535
        demand(size > 0 and size <= len(words) - cursor, 'malformed generated probe framing')
        args = words[cursor + 1:cursor + size]
        if opcode == 71 and len(args) == 3 and args[1] == 30:
            locations[args[2]] = args[0]
        if opcode == 62 and len(args) == 2:
            stores.setdefault(args[0], []).append((cursor + 2, args[1]))
        cursor += size
    demand(2 in locations and 3 in locations, 'generated probe output locations absent')
    a, b = stores.get(locations[2], []), stores.get(locations[3], [])
    demand(len(a) == len(b) == 1 and a[0][1] != b[0][1], 'expected unique different export stores')
    words[a[0][0]] = b[0][1]
    return struct.pack('<%dI' % len(words), *words)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--probe-tool', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='Existing compiled owned fixture; otherwise compile only this fixture.')
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='software')
    args = parser.parse_args(argv)
    report = {'schema': 'frame-private-fragment-ssa-gpu-validation-v1', 'steps': [], 'devices': [],
              'status': 'FAILED', 'native_frame_association_proven': False,
              'native_spatial_mapping_proven': False, 'passthrough_authorized': False}
    code = 1
    private_folder = None
    try:
        # Refuse overwrite before creating any private generated artifact.
        directory, name = recovery.parent_fd(args.record)
        try:
            try:
                os.stat(name, dir_fd=directory, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise ValueError('record already exists')
        finally:
            os.close(directory)
        original = recovery.read_owned(args.original, 65536)
        demand(len(original) == 6620 and hashlib.sha256(original).hexdigest() == ORIGINAL_SHA256,
               'private original fragment version differs')
        report['original_sha256'] = ORIGINAL_SHA256
        report['source_sha256'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (Path(__file__), Path(__file__).with_name('fixture.cpp'), Path(__file__).with_name('fixture.vert'))}
        report['probe_tool_sha256'] = hashlib.sha256(recovery.read_owned(args.probe_tool, 65536)).hexdigest()
        with tempfile.TemporaryDirectory(prefix='frame-private-ssa-gpu-') as temporary:
            folder = Path(temporary)
            private_folder = folder
            vertex, probe, mutant = (folder / name for name in ('vertex.spv', 'probe.spv', 'mutant.spv'))
            fixture = args.fixture or folder / 'fixture'
            commands = []
            if args.fixture is None:
                commands.append(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                    str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)])
            commands += [['glslc', '--target-env=vulkan1.1', str(Path(__file__).with_name('fixture.vert')), '-o', str(vertex)],
                [sys.executable, '-B', str(args.probe_tool), '--original', str(args.original), '--output', str(probe),
                    '--record', str(folder / 'transform.json')]]
            for command in commands:
                step = invoke(command); report['steps'].append(step)
                demand(step['returncode'] == 0, 'bounded fixture compile or probe transform failed')
            probe_bytes = recovery.read_owned(probe, 65536)
            mutant.write_bytes(mutate_export(probe_bytes))
            step = invoke(['spirv-val', '--target-env', 'vulkan1.1', str(mutant)])
            report['steps'].append(step); demand(step['returncode'] == 0, 'causal mutant must remain valid SPIR-V')
            report['instrumented_sha256'] = hashlib.sha256(probe_bytes).hexdigest()
            report['mutant_sha256'] = hashlib.sha256(mutant.read_bytes()).hexdigest()
            for label in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                icd = Path('/usr/share/vulkan/icd.d') / ('lvp_icd.json' if label == 'software' else 'intel_icd.json')
                if not icd.exists():
                    icd = icd.with_name(icd.stem + '.x86_64.json')
                demand(icd.is_file(), 'requested actual ICD unavailable: ' + label)
                env = dict(os.environ, VK_ICD_FILENAMES=str(icd))
                positive = invoke([str(fixture), str(vertex), str(args.original), str(probe)], env, 30)
                negative = invoke([str(fixture), str(vertex), str(args.original), str(mutant)], env, 30)
                entry = {'driver': label, 'icd': str(icd), 'positive': positive, 'mutated_export': negative}
                report['devices'].append(entry)
                demand(positive['returncode'] == 0, 'actual GPU positive control failed: ' + label)
                entry['positive_result'] = json.loads(positive['stdout'])
                demand(entry['positive_result']['output0_bit_identical'] is True, 'original output equivalence absent')
                demand(negative['returncode'] == 1 and 'fixture failed: export loc2 R0 differs' in negative['stderr'],
                       'valid mutated R0 export was not specifically rejected')
                entry['mutated_export_rejected_after_output0_equivalence'] = True
            report['status'] = 'PASS'
            code = 0
    except (OSError, ValueError, recovery.probe.ProbeError, struct.error, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        report['temporary_native_outputs_removed'] = private_folder is None or not private_folder.exists()
        report['validation_scope'] = 'Actual private original fragment on selected host Vulkan devices with owned inputs; no receiver, live frame, original vertex-stage or native W/alpha proof.'
        try:
            recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + '\n').encode())
        except (OSError, recovery.probe.ProbeError) as error:
            print('Could not preserve exclusive result: ' + str(error), file=sys.stderr)
            code = 1
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
