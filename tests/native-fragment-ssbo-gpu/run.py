#!/usr/bin/env python3
"""Private actual-host GPU proof for inert same-invocation SSBO instrumentation."""
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
sys.path.insert(0, str(ROOT/'tools'))
import native_fragment_ssbo_probe as tool
spec = importlib.util.spec_from_file_location('owned_atlas_runner', ROOT/'tests/native-fragment-atlas-gpu/run.py')
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
demand = base.demand


def admission():
    # This is an explicit OWNED test contract. The fixture independently creates
    # a real feature-enabled device and checks its current limits BEFORE pipeline
    # creation. These fields do not attest any receiver feature or layout.
    return {'schema': tool.ADMISSION_SCHEMA, 'scope': tool.ADMISSION_SCOPE,
        'fragment_stores_and_atomics_enabled': True, 'own_layout_compatible': True,
        'descriptor_set': 1, 'max_bound_descriptor_sets': 2, 'max_per_stage_storage_buffers': 1,
        'max_storage_buffer_range': 256, 'descriptor_range': 128}


def mutate(binary, record, mode):
    words = list(struct.unpack('<%dI' % (len(binary)//4), binary)); cursor = 5; stores, pointers = {}, {}
    while cursor < len(words):
        count, op = words[cursor] >> 16, words[cursor] & 65535
        demand(0 < count <= len(words)-cursor, 'bounded own generated instruction')
        if op == 62: stores[words[cursor+1]] = cursor+2
        if op == 65: pointers[words[cursor+2]] = cursor
        cursor += count
    exports = record['new_ssa_ids']['word_pointers']
    if mode == 'wrong_r1':
        for index in range(4): words[stores[exports[5+index]]] = words[stores[exports[9+index]]]
    elif mode == 'wrong_flags':
        words[stores[exports[15]]] = words[stores[exports[1]]]  # Version1, uint typed.
    elif mode == 'overflow':
        access = pointers[exports[-1]]
        demand(words[access] >> 16 == 6, 'exact owned runtime-array access chain')
        # Header word3 stores the uint constant32. The adversarial fixture binds
        #256B so this sole logical overflow is still a legal descriptor access.
        words[access+5] = words[stores[exports[2]]]
    else: raise ValueError('known causal mutation required')
    return struct.pack('<%dI' % len(words), *words)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    args = parser.parse_args(argv)
    report = {'schema': 'private-native-ssbo-gpu-proof-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'scope': 'Inert owned-host pipeline only. Native enabled feature/layout/output lifetime remain unproven.',
        'native_feature_enabled_proven': False, 'native_layout_compatible_proven': False,
        'native_output_lifetime_proven': False, 'native_frame_association_proven': False,
        'physical_eye_proven': False, 'application_ray_mapping_proven': False, 'passthrough_authorized': False}
    code, private = 1, None
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive receipt required')
        original = tool.recovery.read_owned(args.original, tool.ssa.MAX_BYTES)
        demand(len(original) == tool.ssa.ORIGINAL_BYTES and tool.probe.digest(original) == tool.ssa.ORIGINAL_SHA256, 'pinned private native PS')
        report['original_sha256'] = tool.ssa.ORIGINAL_SHA256
        sources = [Path(__file__), Path(__file__).with_name('fixture.cpp'), ROOT/'tools/native_fragment_ssbo_probe.py',
                   ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp', ROOT/'tests/native-fragment-ssa-gpu/fixture.vert']
        report['source_sha256'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        for enabled, definition in ((False, admission()), (True, {**admission(), 'fragment_stores_and_atomics_enabled': False})):
            try: tool.transform(original, definition, enabled=enabled)
            except tool.probe.ProbeError: pass
            else: raise ValueError('disabled/no-feature compile contract was admitted')
        with tempfile.TemporaryDirectory(prefix='native-ssbo-gpu-') as temporary:
            private = Path(temporary); fixture, vertex = private/'fixture', private/'vertex.spv'
            for command in (['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)],
                    ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)]):
                step = base.invoke(command); report['steps'].append(step); demand(step['returncode'] == 0, 'owned tiny fixture build')
            active, transformation = tool.transform(original, admission())
            absent, absent_record = tool.transform(original, admission(), (7, 3))
            modules = {'normal': (active, transformation, 'normal'), 'no-pixel': (absent, absent_record, 'no-pixel'),
                       'no-feature': (active, transformation, 'no-feature')}
            for mode in ('wrong_r1', 'wrong_flags', 'overflow'):
                modules[mode] = mutate(active, transformation, mode), {}, 'overflow' if mode == 'overflow' else 'normal'
            modes = {}
            for name, (binary, metadata, mode) in modules.items():
                path = private/(name+'.spv'); path.write_bytes(binary)
                step = base.invoke(['spirv-val', '--target-env', 'vulkan1.1', str(path)]); report['steps'].append(step)
                demand(step['returncode'] == 0, 'valid positive/causal instrumented module')
                modes[name] = path, mode; report[name+'_transform'] = metadata
                report[name+'_sha256'] = hashlib.sha256(binary).hexdigest()
            modes['missing_module'] = private/'deliberately-absent-third-module.spv', 'normal'
            for driver in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                icd = '/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver == 'software' else 'intel_icd.json')
                entry = {'driver': driver, 'runs': {}}; report['devices'].append(entry)
                for name, (path, mode) in modes.items():
                    settings = private/(driver+'-'+name); settings.mkdir(); messages = settings/'messages.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(messages)+'\n')
                    env = dict(os.environ, VK_ICD_FILENAMES=icd, VK_LAYER_SETTINGS_PATH=str(settings), VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result = base.invoke([str(fixture), str(vertex), str(args.original), str(path), mode], env)
                    result['validation_messages'] = messages.read_text(errors='replace') if messages.exists() else ''
                    entry['runs'][name] = result
                    demand('Validation Error' not in result['stderr']+result['validation_messages'] and 'SYNC-HAZARD' not in result['validation_messages'], 'Vulkan validation error')
                    if name in ('wrong_r1', 'wrong_flags', 'overflow', 'missing_module'):
                        failure = {'wrong_r1': 'SSBO R1 raw bits differ', 'wrong_flags': 'SSBO consumed flag bits differ',
                                   'overflow': 'SSBO logical record canary overwritten', 'missing_module': 'explicit shader input unavailable'}[name]
                        demand(result['returncode'] == 1 and 'ssbo fixture failed: '+failure in result['stderr'], 'specific causal failure '+name)
                    else:
                        demand(result['returncode'] == 0, 'actual SSBO positive failed: '+driver+'/'+name)
                        result['result'] = json.loads(result['stdout'])
                        demand(result['result']['validation_enabled'] is True and result['result']['validation_errors'] == 0, 'Khronos validation active')
                        if name == 'no-feature': demand(result['result']['actual_feature_enabled'] is False and result['result']['pipeline_created'] is False, 'actual disabled device must refuse before pipeline')
            report['status'] = 'PASS'; code = 0
    except (OSError, ValueError, tool.probe.ProbeError, base.subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        report['temporary_native_outputs_removed'] = private is None or not private.exists()
        try: tool.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+'\n').encode())
        except (OSError, tool.probe.ProbeError) as error: print(str(error), file=sys.stderr); code = 1
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__': raise SystemExit(main())
