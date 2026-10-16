#!/usr/bin/env python3
"""Inert direct-packed-u GPU controls; no inferred B/W or native frame proof.

Reuse the approved Vulkan fixture and unchanged native comparison instances.
All input metadata and affine charts are explicit asymmetric owned test values.
"""
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
spec = importlib.util.spec_from_file_location('owned_atlas_controls', ROOT/'tests/native-fragment-atlas-gpu/run.py')
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
reference, recovery = base.reference, base.recovery
F, demand = base.F, base.demand
MAPPING = {'schema': 'native-fragment-packed-atlas-mapping-v1', 'scope': 'EXPLICIT_DIAGNOSTIC_PACKED_AFFINES_ONLY',
    'B': [.9, -.22, .14, .18, .83, -.03], 'color': base.MAPPING['color'], 'coverage': base.MAPPING['coverage']}


def matrices():
    # Independent host conjugation, with the same declared32-bit input contract.
    b = [F(v) for v in MAPPING['B']]
    return tuple([F(v) for v in base.compose(base.compose(b, [F(v) for v in MAPPING[key]]), base.inverse(b))]
                 for key in ('color', 'coverage'))


def cases(enabled):
    pc, pa = matrices(); old, _ = base.cases(enabled)
    records, facts = [], []
    for at in range(12, len(old), base.RECORD.size):
        eye, flags, *v = base.RECORD.unpack_from(old, at)
        u, r0, r1, patch, tint = (v[offset:offset+4] for offset in (0, 4, 8, 12, 16))
        span = [reference.sub(r1[i+2], r1[i]) for i in range(2)]
        normalized = lambda point: [reference.div(reference.sub(point[i], r1[i]), span[i]) for i in range(2)]
        q, uc, ua = normalized(u), base.apply(pc, u), base.apply(pa, u)
        qc, qa = normalized(uc), normalized(ua)
        color, _ = base.sampled(r0, r1, patch, flags, qc, tint[:3])
        mask, raw_g = base.sampled(r0, r1, patch, flags, qa, [1., 1., 1.])
        aperture = reference.lookup_reference(r0, r1, patch[:2], flags, q)['outer_attenuation']
        rgb = [reference.mul(reference.mul(c, aperture), tint[i]) for i, c in enumerate(color['blended_rgb'])]
        expected = rgb+[raw_g]+qc+qa+[color['second_sample_weight'], mask['second_sample_weight'], aperture, float(enabled)]
        # Existing immutable-original GPU oracles receive mapped packed-u
        # directly. There is no roundtrip through a copied R1 on the GPU path.
        oracle = [color['outer_attenuation'], mask['outer_attenuation']]+v[42:45]
        values = u+r0+r1+patch+tint+expected+uc+u[2:]+ua+u[2:]+oracle
        demand(len(values) == 45, 'fixed reused fixture record')
        records.append(base.RECORD.pack(eye, flags, *values))
        facts.append({'eye': eye, 'flags': flags, 'u': u[:2], 'q': q, 'uc': uc, 'ua': ua, 'qc': qc, 'qa': qa,
            'r1': r1, 'color_weight': color['second_sample_weight'], 'mask_weight': mask['second_sample_weight'],
            'original_aperture': aperture, 'raw_g': raw_g, 'decoded_mask_green': mask['blended_rgb'][1]})
    return b'ATLQRY1\0'+struct.pack('<I', len(records))+b''.join(records), facts


def q_equivalent(eye, data, facts):
    r1 = next(f['r1'] for f in facts if f['eye'] == eye)
    n = [F(r1[2]-r1[0]), 0., r1[0], 0., F(r1[3]-r1[1]), r1[1]]
    # q=N^-1 u, hence W=N^-1 B and N W T W^-1 N^-1=B T B^-1.
    w = base.compose(base.inverse(n), [F(v) for v in MAPPING['B']])
    mapping = {'schema': base.MAPPING['schema'], 'scope': base.MAPPING['scope'], 'W': w,
               'color': MAPPING['color'], 'coverage': MAPPING['coverage']}
    records = [data[at:at+base.RECORD.size] for at in range(12, len(data), base.RECORD.size)
               if struct.unpack_from('<I', data, at)[0] == eye]
    return mapping, b'ATLQRY1\0'+struct.pack('<I', len(records))+b''.join(records)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'probe-tool', 'record'): parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    args = parser.parse_args(argv)
    report = {'schema': 'frame-private-packed-atlas-gpu-proof-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'mapping': MAPPING, 'coordinate_scope': 'Explicit synthetic packed B; same-invocation original u/R1; diagnostic normalized q only.',
        'raw_g_scope': 'Reconstructed native sampled component-G, not linear game alpha.',
        'native_spatial_mapping_proven': False, 'native_frame_association_proven': False,
        'native_draw_coherence_proven': False, 'physical_eye_proven': False, 'passthrough_authorized': False}
    code, private = 1, None
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive result path required')
        original = recovery.read_owned(args.original, 65536)
        demand(len(original) == 6620 and hashlib.sha256(original).hexdigest() == base.PIN, 'pinned private original required')
        report['original_sha256'] = base.PIN
        sources = [Path(__file__), ROOT/'tests/native-fragment-atlas-gpu/fixture.cpp',
                   ROOT/'tests/native-fragment-atlas-gpu/run.py', ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
                   ROOT/'tests/native-fragment-ssa-gpu/fixture.vert', ROOT/'tools/pixel_uniform_mapping.py', args.probe_tool]
        report['source_sha256'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        tool = base.load_tool(args.probe_tool)
        active, facts = cases(True); disabled, _ = cases(False)
        report['owned_oracle_facts'] = facts
        demand(any(abs(f['raw_g']-f['decoded_mask_green']) > .05 for f in facts), 'rawG versus decoded green adversary')
        with tempfile.TemporaryDirectory(prefix='frame-packed-atlas-') as temporary:
            private = Path(temporary); fixture, vertex = private/'fixture', private/'vertex.spv'
            for command in (['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(ROOT/'tests/native-fragment-atlas-gpu/fixture.cpp'), '-lvulkan', '-o', str(fixture)],
                    ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)]):
                step = base.invoke(command); report['steps'].append(step); demand(step['returncode'] == 0, 'owned fixture build')
            binary, record = tool.transform_packed(original, MAPPING)
            inactive, inactive_record = tool.transform_packed(original, MAPPING, enabled=False)
            # Causal chart error: apply B as if it mapped normalized q. The same
            # private builder/cones stay valid, but this is the wrong coordinate chart.
            wrong = {**base.MAPPING, 'W': MAPPING['B'], 'color': MAPPING['color'], 'coverage': MAPPING['coverage']}
            wrong_binary, wrong_record = tool.transform(original, wrong)
            inputs = {'enabled': (binary, record, active), 'disabled': (inactive, inactive_record, disabled),
                      'wrong_coordinate_chart': (wrong_binary, wrong_record, active),
                      'reused_mask_weight': (base.mutate_owned_result(binary, record['new_ssa_ids'], 'reused_mask_weight'), {}, active)}
            for eye in (0, 1):
                definition, data = q_equivalent(eye, active, facts)
                equivalent, equivalent_record = tool.transform(original, definition)
                inputs['q_equivalent_eye'+str(eye)] = equivalent, equivalent_record, data
            modes = {}
            for name, (module, transformation, data) in inputs.items():
                path, case_path = private/(name+'.spv'), private/(name+'.bin')
                path.write_bytes(module); case_path.write_bytes(data)
                step = base.invoke(['spirv-val', '--target-env', 'vulkan1.1', str(path)])
                report['steps'].append(step); demand(step['returncode'] == 0, 'valid generated positive/mutant module')
                modes[name] = path, case_path
                report[name+'_transform'] = transformation
                report[name+'_sha256'] = hashlib.sha256(module).hexdigest()
            for label in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                icd = '/usr/share/vulkan/icd.d/'+('lvp_icd.json' if label == 'software' else 'intel_icd.json')
                entry = {'driver': label, 'runs': {}}; report['devices'].append(entry)
                for name, (path, case_path) in modes.items():
                    settings = private/(label+'-'+name); settings.mkdir(); messages = settings/'messages.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(messages)+'\n')
                    env = dict(os.environ, VK_ICD_FILENAMES=icd, VK_LAYER_SETTINGS_PATH=str(settings),
                               VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result = base.invoke([str(fixture), str(vertex), str(args.original), str(path), str(case_path)], env)
                    result['validation_messages'] = messages.read_text(errors='replace') if messages.exists() else ''
                    entry['runs'][name] = result
                    demand('Validation Error' not in result['stderr']+result['validation_messages'] and 'SYNC-HAZARD' not in result['validation_messages'], 'Vulkan validation error')
                    if name in ('wrong_coordinate_chart', 'reused_mask_weight'):
                        failure = 'atlas raw component-G differs' if name == 'reused_mask_weight' else 'atlas retargeted color differs'
                        demand(result['returncode'] == 1 and 'atlas fixture failed: '+failure in result['stderr'], 'specific causal failure: '+name)
                    else:
                        demand(result['returncode'] == 0, 'packed GPU positive failed: '+label+'/'+name)
                        result['result'] = json.loads(result['stdout'])
                        demand(result['result']['validation_enabled'] is True and result['result']['validation_errors'] == 0, 'active Khronos validation required')
            report['status'] = 'PASS'; code = 0
    except (OSError, ValueError, recovery.probe.ProbeError, base.subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        report['temporary_native_outputs_removed'] = private is None or not private.exists()
        try: recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+'\n').encode())
        except (OSError, recovery.probe.ProbeError) as error: print(str(error), file=sys.stderr); code = 1
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__': raise SystemExit(main())
