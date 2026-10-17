#!/usr/bin/env python3
"""INERT current-original-R1 presenter on explicit synthetic Vulkan sources.

No native frame, physical eye, source transfer or live alpha permission is derived.
All original/derivative modules and compiled fixtures stay private and temporary.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import pixel_uniform_mapping as ref
import pixel_payload_recovery as recovery

F = ref.f32
PIN = ref.STAGE_SHA256[1]
RECORD = struct.Struct('<II41fI1024f')
MAPPING = {'schema': 'native-fragment-r1-presenter-mapping-v1',
    'scope': 'INERT_OFFLINE_CURRENT_R1_SYNTHETIC_SOURCE_ONLY',
    'chart': 'ORIGINAL_CURRENT_R1_NORMALIZED_CANVAS',
    'source_encoding': 'OWNED_LINEAR_RGBA32F_COMPONENT_G_COVERAGE',
    'rgb_convention': 'straight',
    'color': [.4, 0., .05, 0., .8, .1], 'coverage': [.4, 0., .55, 0., .8, .1]}


def demand(value, message):
    if not value:
        raise ValueError(message)


def invoke(command, env=None):
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90, check=False)
    return {'command': [str(v) for v in command], 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}


def load_tool(path):
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location('owned_r1_presenter', path)
    tool = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = tool
    spec.loader.exec_module(tool)
    return tool


def affine(a, point):
    a = [F(v) for v in a]
    return [ref.add(ref.add(ref.mul(a[row], point[0]), ref.mul(a[row+1], point[1])), a[row+2])
            for row in (0, 3)]


def encoded(rgb, flags):
    # Independently invert the two documented linear original decoder branches.
    r, g, b = rgb
    if (flags >> 16) & 4:
        y = (r+b+2*g)/4
        return [F((r-b)/4+.5), F(y), F((g-y)/2+.5)]
    y = (g+.187299997*b/1.8556+.468100011*r/1.57480001)/(1+.187299997/1.8556+.468100011/1.57480001)
    return [F((r-y)/1.57480001+.501960814), F(y), F((b-y)/1.8556+.501960814)]


def pixel(uv):
    return tuple(max(0, min(15, math.floor(F(v)*16))) for v in uv)


def sample(texels, uv):
    x, y = pixel(uv)
    return texels[(y*16+x)*4:(y*16+x)*4+3]


def panel(r0, r1, patch, flags, q, texels, tint):
    layout = ref.lookup_reference(r0, r1, patch[:2], flags, q)
    first, second = (sample(texels, layout[key]) for key in ('first_packed_lookup', 'second_packed_lookup'))
    full = ref.lookup_reference(r0, r1, patch[:2], flags, q, first, second, tint)
    w = full['second_sample_weight']
    raw_g = ref.add(ref.mul(second[1], w), ref.mul(first[1], ref.sub(1., w)))
    return full, raw_g


def cases():
    records, facts = [], []
    rectangles = ([0., .25, 1., .5], [0., 0., 1., .25], [0., .75, 1., 1.], [.13, .29, .81, .73])
    skipped = []
    for chart, rectangle in enumerate(rectangles):
        r1 = [F(v) for v in rectangle]
        span = [ref.sub(r1[i+2], r1[i]) for i in range(2)]
        raster = lambda q: [ref.add(r1[i], ref.mul(span[i], q[i])) for i in range(2)]+[.625, .875]
        # R0 is not the panel crop: the original separately reconstructs each
        # retargeted read against this same current metadata rectangle.
        r0 = raster([F(.1), F(.15)])[:2]+raster([F(.6), F(.85)])[:2]
        for eye in (0, 1):
            patch = [F(.12+.03*eye), F(.66-.11*chart+.025*eye), 0., 0.]
            tint = [F(.73), F(.59), F(.91), 1.]
            for branch in range(4):
                flags = 0x02018018 | ((branch & 1) << 17) | ((branch & 2) << 17)
                for point in ((.029, .447), (.99, .447)):
                    u = raster([F(v) for v in point])
                    q = [ref.div(ref.sub(u[i], r1[i]), span[i]) for i in range(2)]
                    qc, qa = (affine(MAPPING[key], q) for key in ('color', 'coverage'))
                    layouts = [ref.lookup_reference(r0, r1, patch[:2], flags, qp) for qp in (qc, qa)]
                    footprints = [[pixel(layout[key]) for key in ('first_packed_lookup', 'second_packed_lookup')]
                                  for layout in layouts]
                    if len(set(footprints[0]+footprints[1])) != 4:
                        skipped.append({'chart': chart, 'eye': eye, 'flags': flags, 'q': q, 'footprints': footprints})
                        continue
                    for kind in range(3):
                        # Equal A=0/1 endpoints plus DIFFERENT partial first/
                        # second coverage make shared color/mask weights causal.
                        aa, ab = ((0., 0.) if kind == 0 else (.2, .8) if kind == 1 else (1., 1.))
                        texels = [F((7+(i%13))/31) for i in range(1024)]
                        color_rgb = ([.35, .65, .25], [.7, .2, .4])
                        if kind == 2 and chart % 2 == 0:
                            color_rgb = ([0., 0., 0.], [0., 0., 0.])  # Opaque black is not a key.
                        for xy, rgb in zip(footprints[0], color_rgb):
                            x, y = xy;texels[(y*16+x)*4:(y*16+x)*4+4] = encoded(rgb, flags)+[.5]
                        for xy, coverage in zip(footprints[1], (aa, ab)):
                            x, y = xy;texels[(y*16+x)*4:(y*16+x)*4+4] = [F(.31), F(coverage), F(.68), .75]
                        color, _ = panel(r0, r1, patch, flags, qc, texels, tint[:3])
                        mask, raw_g = panel(r0, r1, patch, flags, qa, texels, [1., 1., 1.])
                        aperture = ref.lookup_reference(r0, r1, patch[:2], flags, q)['outer_attenuation']
                        expected = [ref.mul(ref.mul(ref.mul(value, aperture), tint[i]), raw_g)
                                    for i, value in enumerate(color['blended_rgb'])]+[ref.mul(raw_g, aperture)]
                        if (flags >> 16) & 4:
                            inverse_g = [.25, .5, .25]
                        else:
                            red, blue = F(.468100011)/F(1.57480001), F(.187299997)/F(1.8556)
                            green = 1/(1+red+blue);inverse_g = [red*green, green, blue*green]
                        values = u+r0+r1+patch+tint+expected+raster(qc)+raster(qa)+[raw_g,
                            color['outer_attenuation'], mask['outer_attenuation'], aperture,
                            color['second_sample_weight'], mask['second_sample_weight']]+[F(v) for v in inverse_g]
                        demand(len(values) == 41 and RECORD.size == 4272, 'fixed independent oracle record')
                        records.append(RECORD.pack(eye, flags, *values, kind, *texels))
                        facts.append({'chart': chart, 'r1': r1, 'eye': eye, 'flags': flags, 'kind': kind,
                            'u': u[:2], 'q': q, 'qc': qc, 'qa': qa, 'coverage': raw_g,
                            'expected': expected, 'color_weight': color['second_sample_weight'],
                            'mask_weight': mask['second_sample_weight'], 'original_aperture': aperture,
                            'color_footprints': footprints[0], 'mask_footprints': footprints[1]})
    demand(32 <= len(records) <= 256, 'enough bounded disjoint-footprint cases')
    demand({f['chart'] for f in facts} == set(range(4)), 'all R1 charts retained')
    demand(any(abs(f['color_weight']-f['mask_weight'])>.1 and f['kind'] == 1 for f in facts), 'causal unequal partial coverage weights')
    return b'R1PRES1\0'+struct.pack('<I', len(records))+b''.join(records), facts, skipped


def mutate(binary, record, mode):
    # Bounded framing only: OpModuleProcessed is intentionally not added to the
    # existing strict original-shader parser. Preserve the marker unchanged.
    words = list(struct.unpack('<%dI' % (len(binary)//4), binary))
    demand(20 <= len(binary) <= 65536 and len(binary)%4 == 0, 'bounded generated module')
    ids = record['new_ssa_ids'];instructions = [];cursor = 5
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 65535
        demand(count and count <= len(words)-cursor, 'complete generated instruction')
        instructions.append((opcode, words[cursor+1:cursor+count]));cursor += count
    one = next(args[1] for op, args in instructions if op == 43 and args[0] == 8 and args[2] == 0x3f800000)
    changed = [];replaced = 0;after_color = False
    for op, args in instructions:
        args = list(args)
        if op == 133 and args[1] == ids['combined_color_rgb']:
            after_color = True
        if mode == 'missing_rgb_coverage' and op == 142 and args[1] == ids['premultiplied_rgb']:
            args[3] = one;replaced += 1
        elif mode == 'double_aperture' and op == 80 and args[1] == ids['output']:
            fresh = words[3];words[3] += 1
            changed.append((133, [8, fresh, ids['output_alpha'], ids['original_aperture']]))
            args[3] = fresh;replaced += 1
        elif mode == 'reused_mask_weight' and after_color and op in (131, 133):
            for i in range(2, len(args)):
                if args[i] == ids['mask_weight']:
                    args[i] = ids['color_weight'];replaced += 1
        elif mode == 'wrong_current_R1_chart' and op == 81 and args[1] >= 1010 and args[2] == 883:
            args[2] = 538;replaced += 1
        changed.append((op, args))
    expected = {'missing_rgb_coverage': 1, 'double_aperture': 1, 'reused_mask_weight': 2,
                'wrong_current_R1_chart': 2}[mode]
    demand(replaced == expected, 'exact causal added-use mutation: '+mode+' '+str(replaced))
    result = words[:5]+[word for op, args in changed for word in ([(len(args)+1)<<16 | op]+args)]
    return struct.pack('<%dI' % len(result), *result)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'probe-tool', 'record'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    parser.add_argument('--mode', action='append', choices=('enabled_count1', 'enabled_array3', 'disabled',
        'missing_rgb_coverage', 'double_aperture', 'reused_mask_weight', 'wrong_current_R1_chart'))
    args = parser.parse_args(argv)
    report = {'schema': 'frame-private-native-r1-presenter-gpu-proof-v1', 'status': 'FAILED',
        'steps': [], 'devices': [], 'mapping': MAPPING, 'rgb_convention': 'straight',
        'scope': 'INERT_OFFLINE_CURRENT_R1_SYNTHETIC_SOURCE_ONLY', 'native_activation': False,
        'native_frame_association_proven': False, 'physical_eye_proven': False,
        'native_sampler_equivalence_proven': False, 'linear_game_alpha_proven': False,
        'passthrough_authorized': False}
    private = None;code = 1;source_hashes = {}
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive result path required')
        original = recovery.read_owned(args.original, 65536)
        demand(len(original) == 6620 and hashlib.sha256(original).hexdigest() == PIN, 'pinned original PS required')
        tool = load_tool(args.probe_tool)
        data, facts, skipped = cases()
        report.update(original_sha256=PIN, owned_oracle_facts=facts, skipped_aliased_footprints=skipped)
        sources = [Path(__file__), Path(__file__).with_name('fixture.cpp'), args.probe_tool,
            ROOT/'tools/native_fragment_atlas_probe.py', ROOT/'tools/native_fragment_packed_output.py',
            ROOT/'tools/pixel_uniform_mapping.py', ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssa-gpu/fixture.vert']
        source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        report['source_sha256'] = source_hashes
        binary, proof = tool.transform(original, MAPPING, descriptor_count=1)
        array3, proof3 = tool.transform(original, MAPPING, descriptor_count=3)
        disabled, proof_off = tool.transform(original, MAPPING, enabled=False)
        demand(disabled == original, 'OFF exact original binary')
        report.update(transformation=proof, array3_transformation=proof3, disabled_transformation=proof_off)
        modes = {'enabled_count1': binary, 'enabled_array3': array3, 'disabled': disabled}
        for mode in ('missing_rgb_coverage', 'double_aperture', 'reused_mask_weight', 'wrong_current_R1_chart'):
            modes[mode] = mutate(binary, proof, mode)
        selected = args.mode or list(modes)
        with tempfile.TemporaryDirectory(prefix='frame-native-r1-presenter-gpu-') as directory:
            private = Path(directory);fixture, vertex = private/'fixture', private/'vertex.spv'
            for command in (['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                    str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)],
                    ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)]):
                step = invoke(command);report['steps'].append(step);demand(step['returncode'] == 0, 'owned fixture build')
            case_path = private/'cases.bin';case_path.write_bytes(data)
            paths = {}
            for mode in selected:
                path = private/(mode+'.spv');path.write_bytes(modes[mode]);paths[mode] = path
                step = invoke(['spirv-val', '--target-env', 'vulkan1.1', str(path)])
                report['steps'].append(step);demand(step['returncode'] == 0, 'valid generated positive/causal module: '+mode)
                report[mode+'_sha256'] = hashlib.sha256(modes[mode]).hexdigest()
            for driver in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                entry = {'driver': driver, 'runs': {}};report['devices'].append(entry)
                for mode, path in paths.items():
                    settings = private/(driver+'-'+mode);settings.mkdir();log = settings/'validation.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n'
                        'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                        'khronos_validation.report_flags = error,warn,perf\n'
                        'khronos_validation.log_filename = '+str(log)+'\n')
                    env = dict(os.environ, VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/'+
                        ('lvp_icd.json' if driver == 'software' else 'intel_icd.json'),
                        VK_LAYER_SETTINGS_PATH=str(settings),
                        VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result = invoke([str(fixture), str(vertex), str(args.original), str(path), str(case_path),
                                     'disabled' if mode == 'disabled' else 'enabled',
                                     '3' if mode in ('disabled', 'enabled_array3') else '1'], env)
                    result['validation_messages'] = log.read_text(errors='replace') if log.exists() else ''
                    entry['runs'][mode] = result
                    combined = result['stderr']+result['validation_messages']
                    demand('Validation Error' not in combined and 'SYNC-HAZARD' not in combined, 'actual validation failure: '+driver+'/'+mode)
                    if mode not in ('enabled_count1', 'enabled_array3', 'disabled'):
                        failure = 'current-R1 explicit alpha differs' if mode == 'double_aperture' else 'current-R1 premultiplied color differs'
                        demand(result['returncode'] == 1 and 'presenter fixture failed: '+failure in result['stderr'], 'specific GPU output rejection: '+mode)
                    else:
                        demand(result['returncode'] == 0, 'actual presenter output failed: '+driver+'/'+mode)
                        result['result'] = json.loads(result['stdout'])
                        demand(result['result']['validation_enabled'] and result['result']['validation_errors'] == 0,
                               'active Khronos validation required')
            report['status'], code = 'PASS', 0
    except (OSError, ValueError, TypeError, KeyError, IndexError, struct.error,
            recovery.probe.ProbeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        retention_path = Path('/home/claw/gloomhaven_vr/scripts/town_run_retention.py')
        if retention_path.is_file():
            spec = importlib.util.spec_from_file_location('owned_completion_retention', retention_path)
            retention = importlib.util.module_from_spec(spec);spec.loader.exec_module(retention)
            report['completion_cleanup'] = retention.cleanup_completed_run(
                'native-r1-presenter-gpu', code, args.record, ROOT, process_reaped=True)
        report['temporary_generated_outputs_removed'] = private is None or not private.exists()
        report['source_changed_during_run'] = [path for path, digest in source_hashes.items()
                                               if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest]
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
