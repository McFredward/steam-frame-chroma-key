#!/usr/bin/env python3
"""Composed private-native output on owned Vulkan resources; no live authority.

Reuse existing packed affine oracles and GPU allocation code. The new integration
has a single color attachment, actual GPU-produced overlapping transparency,
linear source coverage and immutable-original/off recovery. Native B, sampling
transfer and exact native frame association remain independent prerequisites.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import native_fragment_packed_output as output
spec = importlib.util.spec_from_file_location('existing_packed_oracle', ROOT/'tests/native-fragment-packed-atlas-gpu/run.py')
packed = importlib.util.module_from_spec(spec); spec.loader.exec_module(packed)
base, reference = packed.base, packed.reference
F, demand = base.F, base.demand
RECORD = struct.Struct('<II31fI1024f')


def mapping(convention="straight"):
    return {'schema': output.SCHEMA, 'scope': output.SCOPE, 'source_encoding': output.ENCODING,
        'rgb_convention': convention, **{k: packed.MAPPING[k] for k in ('B', 'color', 'coverage')}}


def encode(rgb, flags):
    r, g, b = rgb
    if (flags >> 16) & 4:
        y = (r+b+2*g)/4
        return [F((r-b)/4+.5), F(y), F((g-y)/2+.5)]
    y = (g+.187299997*b/1.8556+.468100011*r/1.57480001)/(1+.187299997/1.8556+.468100011/1.57480001)
    return [F((r-y)/1.57480001+.501960814), F(y), F((b-y)/1.8556+.501960814)]


def footprint(layout):
    return {tuple(max(0, min(15, math.floor(F(v)*16))) for v in layout[k])
            for k in ('first_packed_lookup', 'second_packed_lookup')}


def cases(convention="straight"):
    existing, facts = packed.cases(True)
    records, evidence = [], []
    for index, fact in enumerate(facts):
        if index % 6 not in (0, 3): continue
        eye, flags, *v = base.RECORD.unpack_from(existing, 12+index*base.RECORD.size)
        u, r0, r1, patch, tint = (v[o:o+4] for o in (0, 4, 8, 12, 16))
        color_layout = reference.lookup_reference(r0, r1, patch[:2], flags, fact['qc'])
        coverage_layout = reference.lookup_reference(r0, r1, patch[:2], flags, fact['qa'])
        color_pixels, coverage_pixels = footprint(color_layout), footprint(coverage_layout)
        demand(not color_pixels & coverage_pixels, 'owned color/coverage sample footprints must be distinct')
        for kind, coverage in enumerate((0., .375, 1., .625) if convention == 'straight' else
                                        (0., .375, 1., .625, 0., .625, .5)):
            # The opaque-black case and identical RGB at zero/partial alpha
            # prevent a key-color interpretation from satisfying the oracle.
            rgb = [0., 0., 0.] if kind == 2 and index % 12 == 0 else [.35, .65, .25]
            if kind == 3: rgb = [.2/.625, .375/.625, .1625/.625]
            if convention == 'premultiplied':
                if kind < 4: rgb = [component*coverage for component in rgb]
                elif kind == 4: rgb = [.12, .18, .08]  # Actual additive emission over transparency.
                elif kind == 5: rgb = [.32, .555, .2425]  # Additive over two-layer glass.
                else: rgb = [.16, .44, .19]  # Glass attenuates previously drawn emission.
            expected = [F(rgb[i]*(coverage if convention == 'straight' else 1.)*
                          fact['original_aperture']*tint[i]) for i in range(3)]
            expected += [F(coverage*fact['original_aperture'])]
            source = [F((7+(i%13))/31) for i in range(1024)]
            encoded = encode(rgb, flags)
            for x, y in color_pixels: source[(y*16+x)*4:(y*16+x)*4+4] = encoded+[.5 if kind >= 3 else 1.]
            for x, y in coverage_pixels: source[(y*16+x)*4:(y*16+x)*4+4] = [F(.31), F(coverage), F(.68), .75 if kind >= 3 else 1.]
            values = u+r0+r1+patch+tint+expected+fact['uc']+u[2:]+[coverage, color_layout['outer_attenuation'], fact['original_aperture']]
            demand(len(values) == 31 and len(source) == 1024, 'fixed single-output record')
            records.append(RECORD.pack(eye, flags, *values, kind, *source))
            evidence.append({'eye': eye, 'flags': flags, 'kind': kind, 'coverage': coverage, 'source_rgb': rgb, 'rgb_convention': convention,
                'u': u[:2], 'uc': fact['uc'], 'ua': fact['ua'], 'expected': expected,
                'original_aperture': fact['original_aperture'], 'color_weight': fact['color_weight'],
                'coverage_weight': fact['mask_weight'], 'r1': r1,
                'producer_source': {3: 'actual GPU over blend A=.5+.25*(1-.5)=.625',
                    4: 'actual GPU additive emission over transparent alpha0',
                    5: 'actual GPU additive emission over overlapping glass',
                    6: 'actual GPU glass over additive emission'}[kind] if kind >= 3 else 'explicit owned source'})
    demand(len(records) == (64 if convention == 'straight' else 112) and RECORD.size == 4232, 'bounded stereo source matrix')
    return b'PKOUT1\0\0'+struct.pack('<I', len(records))+b''.join(records), evidence


def mutate(binary, record, mode):
    header, instructions = output.ssa.parse_module(binary)
    header = list(header)
    ids = record['new_ssa_ids']; changed = []
    replacements = 0
    for item in instructions:
        if mode in ('missing_rgb_coverage', 'double_premultiply') and item.opcode == 142 and item.operands[1] == ids['premultiplied_rgb']:
            # A causal mutant either removes required coverage or incorrectly adds it.
            one = next(i.operands[1] for i in instructions if i.opcode == 43 and i.operands[0] == 8 and i.operands[2] == 0x3f800000)
            item = output.ssa.Instruction(142, (*item.operands[:3], ids['clamped_coverage'] if mode == 'double_premultiply' else one)); replacements += 1
        elif mode == 'double_aperture' and item.opcode == 80 and item.operands[1] == ids['output']:
            fresh = header[3]; header[3] += 1
            changed.append(output.ssa.Instruction(133, (8, fresh, ids['output_alpha'], ids['original_aperture'])))
            item = output.ssa.Instruction(80, (*item.operands[:3], fresh)); replacements += 1
        changed.append(item)
    demand(replacements == 1, 'exact composed-output mutation')
    words = header+[word for item in changed for word in item.words()]
    return struct.pack('<%dI' % len(words), *words)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'record'): parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='both')
    parser.add_argument('--rgb-convention', choices=('straight', 'premultiplied'), default='straight')
    parser.add_argument('--mode', action='append', choices=('enabled', 'disabled', 'missing_rgb_coverage', 'double_premultiply', 'double_aperture', 'count1_validation_refusal', 'specialized_count1'))
    args = parser.parse_args(argv)
    default_modes = ['enabled', 'disabled',
        'missing_rgb_coverage' if args.rgb_convention == 'straight' else 'double_premultiply',
        'double_aperture', 'count1_validation_refusal', 'specialized_count1']
    report = {'schema': 'frame-private-packed-output-proof-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'scope': 'OFFLINE_EXPLICIT_OWNED_PACKED_SOURCE_ONLY', 'native_spatial_mapping_proven': False,
        'native_frame_association_proven': False, 'native_sampler_equivalence_proven': False,
        'physical_eye_proven': False, 'linear_game_alpha_proven': False, 'passthrough_authorized': False,
        'rgb_convention': args.rgb_convention, 'requested_modes': args.mode or default_modes}
    code, private = 1, None
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive result path required')
        original = output.recovery.read_owned(args.original, output.ssa.MAX_BYTES)
        binary, transform = output.transform(original, mapping(args.rgb_convention))
        disabled, off_record = output.transform(original, mapping(args.rgb_convention), False)
        specialized, specialized_record = output.transform(original, mapping(args.rgb_convention), descriptor_count=1)
        demand(original == disabled, 'disabled exact source recovery')
        report.update(original_sha256=output.ssa.ORIGINAL_SHA256, transformation=transform, disabled=off_record,
            owned_count1_specialization=specialized_record, source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (
                Path(__file__), ROOT/'tools/native_fragment_packed_output.py', ROOT/'tools/native_fragment_atlas_probe.py',
                ROOT/'tests/native-fragment-packed-output-gpu/fixture.cpp', ROOT/'tests/native-fragment-packed-output-gpu/source.frag',
                ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp', ROOT/'tests/native-fragment-packed-atlas-gpu/run.py')})
        data, report['owned_cases'] = cases(args.rgb_convention)
        with tempfile.TemporaryDirectory(prefix='frame-packed-output-') as directory:
            private = Path(directory); fixture, vertex, producer = (private/name for name in ('fixture', 'vertex.spv', 'source.spv'))
            commands = [
                ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(ROOT/'tests/native-fragment-packed-output-gpu/fixture.cpp'), '-lvulkan', '-o', str(fixture)],
                ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)],
                ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-packed-output-gpu/source.frag'), '-o', str(producer)]]
            for command in commands:
                result = base.invoke(command); report['steps'].append(result); demand(result['returncode'] == 0, 'owned fixture compilation')
            case_path = private/'cases.bin'; case_path.write_bytes(data)
            # Put a genuine alpha0 emission case first for the double-premultiply
            # mutant: its failure specifically proves that emission would vanish.
            emission_path = private/'emission-first-cases.bin'
            if args.rgb_convention == 'premultiplied':
                records = [data[offset:offset+RECORD.size] for offset in range(12, len(data), RECORD.size)]
                emission_first = sorted(zip(report['owned_cases'], records), key=lambda pair: pair[0]['kind'] != 4)
                emission_path.write_bytes(data[:12]+b''.join(record for _, record in emission_first))
                report['double_premultiply_first_case_kind'] = emission_first[0][0]['kind']
            modes = {'enabled': binary, 'disabled': disabled,
                'missing_rgb_coverage': mutate(binary, transform, 'missing_rgb_coverage'),
                'double_aperture': mutate(binary, transform, 'double_aperture'),
                'double_premultiply': mutate(binary, transform, 'double_premultiply'), 'count1_validation_refusal': binary, 'specialized_count1': specialized}
            selected = args.mode or default_modes
            demand(not ('missing_rgb_coverage' in selected and args.rgb_convention != 'straight')
                   and not ('double_premultiply' in selected and args.rgb_convention != 'premultiplied'),
                   'causal RGB mutant must match explicit source convention')
            modes = {name: modes[name] for name in dict.fromkeys(selected)}
            paths = {}
            for name, module in modes.items():
                path = private/(name+'.spv'); path.write_bytes(module); paths[name] = path
                result = base.invoke(['spirv-val', '--target-env', 'vulkan1.1', str(path)])
                report['steps'].append(result); demand(result['returncode'] == 0, 'valid positive/causal module')
            for driver in (('software', 'intel') if args.driver == 'both' else (args.driver,)):
                entry = {'driver': driver, 'runs': {}}; report['devices'].append(entry)
                for name, path in paths.items():
                    settings = private/(driver+'-'+name); settings.mkdir(); log = settings/'validation.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(log)+'\n')
                    env = dict(os.environ, VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver == 'software' else 'intel_icd.json'),
                        VK_LAYER_SETTINGS_PATH=str(settings), VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result = base.invoke([str(fixture), str(vertex), str(args.original), str(path), str(emission_path if name == 'double_premultiply' else case_path), str(producer), 'disabled' if name == 'disabled' else 'enabled', '1' if name == 'count1_validation_refusal' else 'specialized1' if name == 'specialized_count1' else '3', args.rgb_convention], env)
                    result['validation_messages'] = log.read_text(errors='replace') if log.exists() else ''
                    entry['runs'][name] = result
                    if name == 'count1_validation_refusal':
                        demand(result['returncode'] == 1 and 'pipeline creation Vulkan validation errors' in result['stderr']
                               and result['stderr'].count('Validation: Validation Error: [ VUID-VkGraphicsPipelineCreateInfo-layout-07991 ]') == 2
                               and 'requires at least 3' in result['stderr'], 'exact original and composed array3/count1 refusal')
                        continue
                    demand('Validation Error' not in result['stderr']+result['validation_messages'] and 'SYNC-HAZARD' not in result['validation_messages'], 'Vulkan validation error')
                    if name in ('missing_rgb_coverage', 'double_premultiply', 'double_aperture'):
                        failure = 'composed premultiplied color differs' if name in ('missing_rgb_coverage', 'double_premultiply') else 'composed explicit alpha differs'
                        demand(result['returncode'] == 1 and failure in result['stderr'], 'specific causal output failure: '+name)
                    else:
                        demand(result['returncode'] == 0, 'composed GPU output: '+driver+'/'+name)
                        result['result'] = json.loads(result['stdout'])
                        demand(result['result']['validation_enabled'] is True and result['result']['validation_errors'] == 0, 'active validation required')
            report['status'] = 'PASS'; code = 0
    except (OSError, ValueError, TypeError, KeyError, output.atlas.probe.ProbeError, base.subprocess.SubprocessError) as error:
        report['error'] = str(error)
    finally:
        report['temporary_generated_outputs_removed'] = private is None or not private.exists()
        try: output.recovery.write_owned(args.record, (json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+'\n').encode())
        except (OSError, output.atlas.probe.ProbeError) as error: print(str(error), file=sys.stderr); code = 1
    print(json.dumps({'status': report['status'], 'devices': len(report['devices']), 'passthrough_authorized': False}))
    return code


if __name__ == '__main__': raise SystemExit(main())
