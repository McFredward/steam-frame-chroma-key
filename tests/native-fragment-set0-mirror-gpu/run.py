#!/usr/bin/env python3
"""Private, owned real-GPU oracle for a compatible one-set mirror and restoration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import native_fragment_ssbo_profile as profile
import native_fragment_packed_output as packed
spec = importlib.util.spec_from_file_location('set0_gpu_base', ROOT / 'tests/native-fragment-atlas-gpu/run.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def consumer_entry(binary):
    """Rename only the owned GLSL consumer's entry to match the borrowed builder."""
    words = list(struct.unpack('<%dI' % (len(binary) // 4), binary))
    out, at, count = words[:5], 5, 0
    while at < len(words):
        size, opcode = words[at] >> 16, words[at] & 65535
        base.demand(0 < size <= len(words) - at, 'owned consumer bounded instruction')
        item = words[at:at + size]
        if opcode == 15:
            # Original null-terminated entry name occupies two words (main\0).
            base.demand(item[1] == 4 and struct.pack('<2I', *item[3:5]) == b'main\0\0\0\0', 'owned consumer fragment main entry')
            name = struct.unpack('<3I', b'PS_DISTORT\0\0')
            item = [((size + 1) << 16) | opcode, *item[1:3], *name, *item[5:]]
            count += 1
        out.extend(item)
        at += size
    base.demand(count == 1, 'one owned consumer entry renamed')
    return struct.pack('<%dI' % len(out), *out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--vertex', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    args = parser.parse_args(argv)
    report = {'schema': 'owned-compatible-set0-mirror-gpu-proof-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'scope': 'Owned raster vertex; exact pinned native PS with independently proven count1 control narrowing; one compatible Set0 mirror, full dynamic offsets, higher-set consumers and later native-style suffix draws.',
        'limitations': ['Production Clone and Transaction are not called by this call-pattern GPU oracle.',
            'The finite owned eight-binding shape is not the complete actual native29 layout census.',
            'The original native vertex is checked for the mirror profile but not replayed by this raster test.',
            'Original array3 vs count1 control equivalence remains separately inherited evidence; this run uses the narrowed control.'],
        'native_admission_proven': False, 'native_frame_association_proven': False,
        'physical_eye_proven': False, 'passthrough_authorized': False}
    code, private, hashes = 1, None, {}
    try:
        base.demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive private result')
        original = profile.ssbo.recovery.read_owned(args.original, profile.ssa.MAX_BYTES)
        vertex = profile.ssbo.recovery.read_owned(args.vertex, profile.ssa.MAX_BYTES)
        files = [Path(__file__), Path(__file__).with_name('fixture.cpp'), Path(__file__).with_name('consumer.frag'),
            ROOT / 'tests/native-fragment-ssbo-gpu/fixture.cpp', ROOT / 'tests/native-fragment-ssa-gpu/fixture.cpp',
            ROOT / 'tests/native-fragment-ssa-gpu/fixture.vert', ROOT / 'tools/native_fragment_ssbo_profile.py',
            ROOT / 'tools/native_fragment_packed_output.py', args.original, args.vertex]
        hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        report['source_sha256'] = hashes
        mirror, metadata = profile.transform_mirror(vertex, original)
        control = packed.specialize_owned_binding9_count1(original, original)
        report['mirror_transform'] = metadata
        report['control_narrowing'] = {'helper': 'specialize_owned_binding9_count1(original, original)',
            'sha256': hashlib.sha256(control).hexdigest(), 'bytes': len(control),
            'executable_original_instructions_changed': False}
        with tempfile.TemporaryDirectory(prefix='owned-set0-mirror-') as temporary:
            private = Path(temporary)
            fixture, raster, consumer = private / 'fixture', private / 'vertex.spv', private / 'consumer.spv'
            control_file, mirror_file = private / 'control.spv', private / 'mirror.spv'
            control_file.write_bytes(control)
            mirror_file.write_bytes(mirror)
            commands = [
                ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)],
                ['glslc', '--target-env=vulkan1.1', str(ROOT / 'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(raster)],
                ['glslc', '--target-env=vulkan1.1', str(Path(__file__).with_name('consumer.frag')), '-o', str(consumer)]]
            for command in commands:
                step = base.invoke(command)
                report['steps'].append(step)
                base.demand(step['returncode'] == 0, 'owned fixture build')
            consumer.write_bytes(consumer_entry(consumer.read_bytes()))
            for module in (raster, consumer, control_file, mirror_file):
                step = base.invoke(['spirv-val', '--target-env', 'vulkan1.1', str(module)])
                report['steps'].append(step)
                base.demand(step['returncode'] == 0, 'owned oracle SPIR-V validation')
            for driver in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                settings = private / driver
                settings.mkdir()
                messages = settings / 'validation.log'
                (settings / 'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n'
                    'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                    'khronos_validation.report_flags = error,warn,perf\n'
                    'khronos_validation.log_filename = ' + str(messages) + '\n')
                icd = '/usr/share/vulkan/icd.d/' + ('lvp_icd.json' if driver == 'software' else 'intel_icd.json')
                env = dict(os.environ, VK_ICD_FILENAMES=icd, VK_LAYER_SETTINGS_PATH=str(settings),
                    VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                result = base.invoke([str(fixture), str(raster), str(control_file), str(mirror_file), str(consumer)], env)
                result['validation_messages'] = messages.read_text(errors='replace') if messages.exists() else ''
                report['devices'].append({'driver': driver, 'run': result})
                base.demand(result['returncode'] == 0 and 'Validation Error' not in result['stderr'] + result['validation_messages']
                    and 'SYNC-HAZARD' not in result['stderr'] + result['validation_messages'], 'real mirror GPU oracle or validation failed')
                value = json.loads(result['stdout'])
                result['result'] = value
                base.demand(value['cases'] >= 2 and value['causal_mutations'] == 2 and value['descriptor_copies_once'] == 7
                    and value['higher_sets_preserved'] and value['full_dynamic_offsets_restored'] and value['native_storage_unchanged']
                    and value['validation_errors'] == 0, 'complete real owned descriptor-state oracle')
            report['status'] = 'PASS'
            code = 0
    except (OSError, ValueError, KeyError, TypeError, profile.ssbo.probe.ProbeError, base.subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        report['temporary_outputs_removed'] = private is None or not private.exists()
        changed = [p for p, digest in hashes.items() if hashlib.sha256(Path(p).read_bytes()).hexdigest() != digest]
        report['changed_sources'] = changed
        if changed:
            report['status'], code = 'FAILED', 1
        profile.ssbo.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True) + '\n').encode())
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
