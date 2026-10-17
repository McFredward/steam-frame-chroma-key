#!/usr/bin/env python3
"""Private source-content guard proof and bounded host GPU cost measurement.

This is synthetic format recognition, not body authentication, source liveness,
physical-eye mapping, native activation or a passthrough permission.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import pixel_payload_recovery as recovery
import pixel_uniform_mapping as ref

F = ref.f32
RECORD = struct.Struct('<II32fIIiIII')
CHARTS = [[0., .25, 1., .5], [0., 0., 1., 1.], [0., .75, 1., 1.], [.13, .29, .81, .73]]
BACKGROUND = [.11, .17, .23, 1.]
SIGNATURE = '6f999b336f525cdb66e379bd3038c50fc9d1237f534a66323ab0215b503ed586'


def demand(value, message):
    if not value:
        raise ValueError(message)


def invoke(command, env=None, timeout=90):
    p = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout, check=False)
    return {'command': [str(v) for v in command], 'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}


def load_tool(path):
    sys.path.insert(0, str(path.parent))
    s = importlib.util.spec_from_file_location('private_guarded_transformer', path)
    tool = importlib.util.module_from_spec(s);sys.modules[s.name] = tool;s.loader.exec_module(tool)
    return tool


def cases():
    records, facts = [], []
    def add(name, mode=0, cell=0, delta=0, chart=0, valid=True, slot=0, r1=None, u=None,
            q=(.5, .5), eye=0, decoder=0, body=(.35, .65, .25, .375)):
        r1 = [F(v) if math.isfinite(v) else v for v in (CHARTS[chart] if r1 is None else r1)]
        if u is None:
            u = [ref.add(r1[i], ref.mul(ref.sub(r1[i+2], r1[i]), F(q[i]))) for i in range(2)]+[.625, .875]
        else:
            u = [F(v) if math.isfinite(v) else v for v in u]
        flags = 0x02038018 | (decoder << 18)  # Different decoder; inner weight explicitly zero.
        r0, patch, tint = [.91, .92, .97, .98], [.07, .13, 0., 0.], [.73, .59, .91, 1.]
        if valid:
            normalized = [ref.div(ref.sub(u[i], r1[i]), ref.sub(r1[i+2], r1[i])) for i in range(2)]
            aperture = ref.lookup_reference(r0, r1, patch[:2], flags, normalized)['outer_attenuation']
            expected = [F(F(F(body[i])*F(tint[i]))*F(body[3])*aperture) for i in range(3)]+[F(F(body[3])*aperture)]
        else:
            aperture = None;expected = [0., 0., 0., 0.]
        values = u+r0+r1+patch+tint+expected+list(body)+BACKGROUND
        demand(len(values) == 32 and RECORD.size == 160, 'fixed owned guard case')
        records.append(RECORD.pack(eye, flags, *values, mode, cell, delta, chart, int(valid), slot))
        safe = lambda v: [x if math.isfinite(x) else 'NaN' if math.isnan(x) else 'Infinity' for x in v]
        facts.append({'name': name, 'mode': mode, 'cell': cell, 'delta': delta, 'source_chart': chart,
            'r1': safe(r1), 'u': safe(u), 'expected_valid': valid, 'image_slot': slot,
            'flags': flags, 'eye': eye, 'body': body, 'expected': expected, 'original_aperture': aperture})
    add('on_initial');add('off_same_recorded_command', mode=5, valid=False);add('on_same_recorded_command_recovery')
    for cell in range(12):
        for delta in (-2, 2, -3, 3):
            add('single_cell_'+str(cell)+'_delta_'+str(delta), mode=8, cell=cell, delta=delta, valid=abs(delta)<=2)
    for mode, name in ((1, 'wrong_strip'), (2, 'shifted_strip'), (3, 'mirrored_strip'), (4, 'scaled_strip'),
                       (5, 'erased_strip'), (6, 'old_style_header_without_guard'), (7, 'plain_image')):
        add(name, mode=mode, valid=False)
    add('copied_valid_strip_changed_body_is_recognized', mode=9, body=(.8, .3, .1, .625))
    for name, r1, u in (
            ('r1_zero_span', [0., .25, 0., .5], [.5, .375, .625, .875]),
            ('r1_reversed', [.8, .25, .2, .5], [.5, .375, .625, .875]),
            ('r1_nan', [float('nan'), .25, 1., .5], [.5, .375, .625, .875]),
            ('r1_infinite', [0., .25, float('inf'), .5], [.5, .375, .625, .875]),
            ('r1_below_unit', [-.01, .25, 1., .5], [.5, .375, .625, .875]),
            ('r1_above_unit', [0., .25, 1.01, .5], [.5, .375, .625, .875]),
            ('u_outside_r1', CHARTS[0], [-.1, .375, .625, .875]),
            ('u_nan', CHARTS[0], [float('nan'), .375, .625, .875])):
        add(name, r1=r1, u=u, valid=False)
    # The same chart/metadata must never reuse validity from another VkImage.
    add('image_a_valid', slot=0);add('image_b_invalid_same_chart', mode=7, slot=1, valid=False)
    add('image_a_still_valid', slot=0);add('image_b_now_valid_same_chart', slot=1)
    add('image_a_now_invalid_same_chart', mode=5, slot=0, valid=False)
    for chart in range(4):
        for eye in (0, 1):
            for decoder in (0, 1):
                add('valid_chart_'+str(chart)+'_eye_'+str(eye)+'_decoder_'+str(decoder), chart=chart, eye=eye, decoder=decoder)
    add('valid_partial_original_aperture', q=(.99, .447))
    add('valid_zero_coverage', body=(.35, .65, .25, 0.))
    add('valid_full_coverage_opaque_black', body=(0., 0., 0., 1.))
    demand(len(records) <= 256, 'bounded guard case count')
    return b'GVRGUAR2'+struct.pack('<I', len(records))+b''.join(records), facts


VARYING_VERTEX = '''#version 450
// Owned varying raster coordinates for host cost only, not native vertex code.
layout(push_constant) uniform Input { vec4 u; } fixture;
layout(location=0) noperspective out vec4 unused0;
layout(location=1) noperspective out vec4 originalInput;
layout(location=2) noperspective out vec4 unused2;
layout(location=3) flat out uint unused3;
void main(){vec2 corner=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
 gl_Position=vec4(corner*2.-1.,0,1);
 originalInput=vec4(fixture.u.xy+corner*fixture.u.zw,.625,.875);
 unused0=vec4(.125,.375,.625,.875);unused2=vec4(.75,.5,.25,1);unused3=0u;}
'''


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'probe-tool', 'record'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    p.add_argument('--part', choices=('both', 'correctness', 'timing'), default='both')
    args = p.parse_args(argv)
    report = {'schema': 'private-native-guarded-presenter-gpu-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'scope': 'INERT_OFFLINE_SYNTHETIC_FORMAT_RECOGNITION_ONLY', 'native_activation': False,
        'body_authenticated': False, 'source_liveness_proven': False, 'native_frame_association_proven': False,
        'physical_eye_proven': False, 'passthrough_authorized': False}
    code, private, source_hashes = 1, None, {}
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive evidence path')
        profile_path = ROOT/'docs/protocols/synthetic-presenter-v2.json'
        profile = json.loads(profile_path.read_text())
        guard = profile['guard']
        demand(hashlib.sha256(guard['signature_preimage'].encode()).hexdigest() == SIGNATURE == guard['signature_sha256'], 'frozen guard signature')
        demand(guard['codes'] == list(bytes.fromhex(SIGNATURE)[:12]), 'frozen twelve code bytes')
        original = recovery.read_owned(args.original, 65536)
        demand(len(original) == 6620 and hashlib.sha256(original).hexdigest() == ref.STAGE_SHA256[1], 'exact private original PS')
        tool = load_tool(args.probe_tool)
        candidate, proof = tool.transform(original, profile, descriptor_count=1)
        disabled, disabled_proof = tool.transform(original, profile, enabled=False)
        demand(disabled == original, 'OFF transformer returns original byte-for-byte')
        data, facts = cases()
        report.update(original_sha256=ref.STAGE_SHA256[1], transformation=proof, disabled=disabled_proof,
                      owned_cases=facts, candidate_sha256=hashlib.sha256(candidate).hexdigest())
        paths = [Path(__file__), Path(__file__).with_name('fixture.cpp'), args.probe_tool, profile_path,
                 ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp', ROOT/'tests/native-fragment-ssa-gpu/fixture.vert',
                 ROOT/'tools/native_fragment_r1_presenter.py', ROOT/'tools/native_fragment_atlas_probe.py',
                 ROOT/'tools/pixel_uniform_mapping.py']
        paths.extend(Path(module.__file__) for module in (
            tool.presenter, tool.presenter.atlas, tool.presenter.packed_output, tool.ssa, recovery, ref))
        source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        report['source_sha256'] = source_hashes
        with tempfile.TemporaryDirectory(prefix='native-guard-gpu-') as directory:
            private = Path(directory);fixture, vertex, varying = (private/name for name in ('fixture', 'vertex.spv', 'varying.spv'))
            varying_source = private/'varying.vert';varying_source.write_text(VARYING_VERTEX)
            modules = {'original': original, 'guarded': candidate, 'off': disabled}
            shader_paths = {}
            for name, binary in modules.items():
                path = private/(name+'.spv');path.write_bytes(binary);shader_paths[name] = path
                step = invoke(['spirv-val', '--target-env', 'vulkan1.1', str(path)]);report['steps'].append(step)
                demand(step['returncode'] == 0, 'valid generated module: '+name)
            for command in (['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)],
                    ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)],
                    ['glslc', '--target-env=vulkan1.1', str(varying_source), '-o', str(varying)]):
                step = invoke(command);report['steps'].append(step);demand(step['returncode'] == 0, 'owned fixture build')
            case_path = private/'cases.bin';case_path.write_bytes(data)
            empty_layers = private/'empty-layers';empty_layers.mkdir()
            for driver in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                icd = '/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver == 'software' else 'intel_icd.json')
                entry = {'driver': driver};report['devices'].append(entry)
                if args.part in ('both', 'correctness'):
                    settings = private/(driver+'-correctness');settings.mkdir();log = settings/'validation.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(log)+'\n')
                    env = dict(os.environ, VK_ICD_FILENAMES=icd, VK_LAYER_SETTINGS_PATH=str(settings),
                               VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result = invoke([str(fixture), str(vertex), str(shader_paths['original']), str(shader_paths['guarded']), str(case_path), 'correctness'], env)
                    result['validation_messages'] = log.read_text(errors='replace') if log.exists() else ''
                    entry['correctness'] = result
                    demand('Validation Error' not in result['stderr']+result['validation_messages'] and 'SYNC-HAZARD' not in result['validation_messages'], 'Vulkan/sync correctness validation: '+driver)
                    demand(result['returncode'] == 0, 'actual GPU guard correctness: '+driver)
                    result['result'] = json.loads(result['stdout'])
                    demand(result['result']['validation_enabled'] and result['result']['validation_errors'] == 0, 'actual validation required')
                if args.part in ('both', 'timing'):
                    # Explicitly hide the private layer package for timing. The
                    # fixture refuses to report timings if validation is active.
                    env = dict(os.environ, VK_ICD_FILENAMES=icd, VK_LAYER_PATH=str(empty_layers))
                    env.pop('VK_LAYER_ENABLES', None);env.pop('VK_LAYER_SETTINGS_PATH', None)
                    entry['timing'] = []
                    for width, height in ((512, 512), (1152, 4608)):
                        result = invoke([str(fixture), str(varying), str(shader_paths['original']), str(shader_paths['guarded']), str(case_path), 'benchmark', str(width), str(height)], env, timeout=120)
                        entry['timing'].append(result);demand(result['returncode'] == 0, 'bounded actual GPU timing: '+driver)
                        result['result'] = json.loads(result['stdout'])
                        demand(not result['result']['validation_enabled'] and result['result']['pixels'] == width*height, 'unvalidated bounded timing extent')
                        if width == 512:
                            maximum = max(value for run in result['result']['runs'].values() for value in run['cpu_ms'])
                            demand(maximum < 400, 'preflight too expensive for safe full-size GPU timing')
                    full = entry['timing'][-1]['result']
                    entry['timing_summary_ms'] = {name: {'gpu_median': statistics.median(v['gpu_ms']),
                                                       'cpu_fence_median': statistics.median(v['cpu_ms'])}
                        for name, v in full['runs'].items()}
            report['status'], code = 'PASS', 0
    except (OSError, ValueError, TypeError, KeyError, IndexError, struct.error,
            recovery.probe.ProbeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        retention_path = Path('/home/claw/gloomhaven_vr/scripts/town_run_retention.py')
        if retention_path.is_file():
            spec = importlib.util.spec_from_file_location('guard_completion_retention', retention_path)
            retention = importlib.util.module_from_spec(spec);spec.loader.exec_module(retention)
            report['completion_cleanup'] = retention.cleanup_completed_run('native-guarded-presenter-gpu', code, args.record, ROOT, process_reaped=True)
        report['temporary_generated_outputs_removed'] = private is None or not private.exists()
        report['source_changed_during_run'] = [path for path, digest in source_hashes.items() if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest]
        if report['source_changed_during_run']:
            report['status'], code = 'FAILED', 1
        try:
            recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+'\n').encode())
        except (OSError, recovery.probe.ProbeError) as error:
            print(str(error), file=sys.stderr);code = 1
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
