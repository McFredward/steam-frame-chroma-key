#!/usr/bin/env python3
"""Private original-fragment dual-read oracle on owned host Vulkan resources.

All affines are explicit synthetic inputs. There is no inferred receiver W,
physical-eye or native-frame association and no live alpha activation. Original
modules and generated derivatives stay private and are deleted after each run.
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
sys.path.insert(0, str(ROOT / 'tools'))
import pixel_uniform_mapping as reference
import pixel_payload_recovery as recovery

PIN = 'f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2'
RECORD = struct.Struct('<II45f')
F = reference.f32
MAPPING = {'schema': 'native-fragment-atlas-mapping-v1', 'scope': 'EXPLICIT_DIAGNOSTIC_AFFINES_ONLY',
    'W': [.9, -.22, .14, .18, .83, -.03],
    'color': [320 / 688, 0, 16 / 688, 0, 192 / 548, 244 / 548],
    'coverage': [320 / 688, 0, 352 / 688, 0, 192 / 548, 244 / 548]}


def demand(value, text):
    if not value:
        raise ValueError(text)


def compose(a, b):
    # Original affine algebra; no native mapping or instruction reproduction.
    return [a[0]*b[0]+a[1]*b[3], a[0]*b[1]+a[1]*b[4], a[0]*b[2]+a[1]*b[5]+a[2],
            a[3]*b[0]+a[4]*b[3], a[3]*b[1]+a[4]*b[4], a[3]*b[2]+a[4]*b[5]+a[5]]


def inverse(a):
    det = a[0]*a[4]-a[1]*a[3]
    demand(abs(det) > .0001, 'explicit owned affine is invertible')
    b = [a[4]/det, -a[1]/det, 0, -a[3]/det, a[0]/det, 0]
    b[2], b[5] = -(b[0]*a[2]+b[1]*a[5]), -(b[3]*a[2]+b[4]*a[5])
    return b


def apply(a, q):
    return [reference.add(reference.add(reference.mul(a[row], q[0]), reference.mul(a[row+1], q[1])), a[row+2])
            for row in (0, 3)]


def texel(uv):
    # The approved shared fixture's exact owned16x16 RGBA32F component field.
    x, y = [max(0, min(15, math.floor(F(v)*16))) for v in uv]
    return [F((17+x*7+y*3)/255), F((31+x*2+y*8)/255), F((19+x*5+y*4)/255)]


def sampled(r0, r1, patch, flags, q, tint):
    layout = reference.lookup_reference(r0, r1, patch[:2], flags, q)
    a, b = texel(layout['first_packed_lookup']), texel(layout['second_packed_lookup'])
    full = reference.lookup_reference(r0, r1, patch[:2], flags, q, a, b, tint)
    w = layout['second_sample_weight']
    raw_g = reference.add(reference.mul(b[1], w), reference.mul(a[1], reference.sub(1., w)))
    return full, raw_g


def cases(enabled):
    # Match the declared coefficient contract without calling the transformer:
    # normalize caller coefficients to32-bit first, compose in64-bit, then round.
    normalized = {key: [F(v) for v in MAPPING[key]] for key in ('W', 'color', 'coverage')}
    w = normalized['W']
    mc, ma = ([F(v) for v in compose(compose(w, normalized[key]), inverse(w))] for key in ('color', 'coverage'))
    records, facts = [], []
    for eye in (0, 1):
        for branch in range(4):
            flags = 0x02018018 | ((branch & 1) << 17) | ((branch & 2) << 17)
            for source_q in ((.371, .413), (.513, .537), (.697, .653), (.99, .447), (.029, .731), (.423, .99)):
                r0 = [F(v) for v in ([.18, .21, .63, .72] if eye == 0 else [.29, .19, .79, .84])]
                r1 = [F(v) for v in ([.04, .08, .94, .92] if eye == 0 else [.11, .05, .91, .96])]
                patch, tint = [F(.07+.06*eye), F(.13+.06*eye), 0., 0.], [F(.73), F(.59), F(.91), 1.]
                u = [reference.add(r1[i], reference.mul(reference.sub(r1[i+2], r1[i]), F(source_q[i]))) for i in range(2)] + [.625, .875]
                # This re-observes the same normalized-q algebra from rounded own
                # raster inputs, rather than treating source_q as native input.
                q = [reference.div(reference.sub(u[i], r1[i]), reference.sub(r1[i+2], r1[i])) for i in range(2)]
                qc, qa = apply(mc, q), apply(ma, q)
                color, _ = sampled(r0, r1, patch, flags, qc, tint[:3])
                mask, raw_g = sampled(r0, r1, patch, flags, qa, [1., 1., 1.])
                original = reference.lookup_reference(r0, r1, patch[:2], flags, q)
                aperture = original['outer_attenuation']
                rgb = [reference.mul(reference.mul(v, aperture), tint[i]) for i, v in enumerate(color['blended_rgb'])]
                expected = rgb + [raw_g] + qc + qa + [color['second_sample_weight'], mask['second_sample_weight'], aperture, float(enabled)]
                def raster(qpanel):
                    return [reference.add(r1[i], reference.mul(reference.sub(r1[i+2], r1[i]), qpanel[i])) for i in range(2)] + [.625, .875]
                # Inverse coefficients cancel chroma in an independent original
                # GPU draw with unit tint. No source component-G/alpha identity
                # for a real video conversion is inferred from this fixture.
                if (flags >> 16) & 4:
                    inverse_g = [.25, .5, .25]
                else:
                    r, b = F(.468100011)/F(1.57480001), F(.187299997)/F(1.8556)
                    g = 1/(1+r+b); inverse_g = [r*g, g, b*g]
                oracle = [color['outer_attenuation'], mask['outer_attenuation']] + [F(v) for v in inverse_g]
                values = u+r0+r1+patch+tint+expected+raster(qc)+raster(qa)+oracle
                demand(len(values) == 45, 'fixed owned fixture shape')
                records.append(RECORD.pack(eye, flags, *values))
                facts.append({'eye': eye, 'flags': flags, 'q': q, 'qc': qc, 'qa': qa,
                    'color_weight': color['second_sample_weight'], 'mask_weight': mask['second_sample_weight'],
                    'original_aperture': aperture, 'raw_g': raw_g, 'decoded_mask_green': mask['blended_rgb'][1]})
    return b'ATLQRY1\0' + struct.pack('<I', len(records)) + b''.join(records), facts


def invoke(command, env=None):
    p = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60, check=False)
    return {'command': [str(v) for v in command], 'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}


def load_tool(path):
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location('private_atlas_transformer', path)
    module = importlib.util.module_from_spec(spec); sys.modules[spec.name] = module; spec.loader.exec_module(module)
    return module


def mutate_owned_result(binary, ids, mode):
    """Corrupt only added result uses; original native instructions stay intact."""
    demand(type(binary) is bytes and 20<=len(binary)<=65536 and len(binary)%4==0, 'bounded generated module')
    words=list(struct.unpack('<%dI' % (len(binary)//4),binary))
    cursor=5; after_color=False; replacements=0; color_type=None; output_at=None
    while cursor<len(words):
        count,opcode=words[cursor]>>16,words[cursor]&65535
        demand(count>0 and count<=len(words)-cursor,'valid generated instruction framing')
        if opcode==133 and count>=5 and words[cursor+2]==ids['combined_color_rgb']:
            after_color=True;color_type=words[cursor+1]
        if opcode==80 and count>=5 and words[cursor+2]==ids['output1']:
            output_at=cursor
        if mode=='reused_mask_weight' and after_color and opcode in (131,133):
            for index in range(cursor+3,cursor+count):
                if words[index]==ids['mask_weight']:
                    words[index]=ids['color_weight'];replacements+=1
        cursor+=count
    if mode=='reused_mask_weight':
        demand(replacements==2,'exact two added mask-weight uses required')
    elif mode=='double_aperture':
        demand(output_at is not None and color_type is not None and words[output_at+3]==ids['combined_color_rgb'],
               'exact added color/output relation required')
        fresh=words[3];words[3]+=1;words[output_at+3]=fresh
        words[output_at:output_at]=[(5<<16)|142,color_type,fresh,ids['combined_color_rgb'],ids['original_aperture']]
    else:raise ValueError('unknown causal mutation')
    return struct.pack('<%dI' % len(words),*words)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--probe-tool', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--driver', choices=('software', 'intel', 'both'), default='software')
    args = parser.parse_args(argv)
    report = {'schema': 'frame-private-atlas-gpu-proof-v1', 'status': 'FAILED', 'steps': [], 'devices': [],
        'mapping': MAPPING, 'mapping_scope': 'Explicit synthetic diagnostic affines; no inferred native W.',
        'raw_g_scope': 'Separate native sampled-component G reconstruction only; not a generic linear-alpha or video-conversion proof.',
        'native_spatial_mapping_proven': False, 'native_frame_association_proven': False, 'passthrough_authorized': False}
    private = None; exit_code = 1
    try:
        demand(not args.record.exists() and not args.record.is_symlink(), 'exclusive result path required')
        original = recovery.read_owned(args.original, 65536)
        demand(len(original) == 6620 and hashlib.sha256(original).hexdigest() == PIN, 'pinned private original PS required')
        report['original_sha256'] = PIN
        report['source_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (Path(__file__), Path(__file__).with_name('fixture.cpp'), ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
                      ROOT/'tests/native-fragment-ssa-gpu/fixture.vert', ROOT/'tools/pixel_uniform_mapping.py')}
        report['probe_tool_sha256'] = hashlib.sha256(recovery.read_owned(args.probe_tool, 65536)).hexdigest()
        tool = load_tool(args.probe_tool)
        enabled_cases, facts = cases(True); disabled_cases, _ = cases(False)
        report['owned_oracle_facts'] = facts
        demand(any(abs(f['raw_g']-f['decoded_mask_green'])>.05 for f in facts), 'asymmetric chroma must distinguish rawG from decoded green')
        with tempfile.TemporaryDirectory(prefix='frame-atlas-gpu-') as temporary:
            private = Path(temporary); fixture = private/'fixture'; vertex = private/'vertex.spv'
            for command in (['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', str(Path(__file__).with_name('fixture.cpp')), '-lvulkan', '-o', str(fixture)],
                    ['glslc', '--target-env=vulkan1.1', str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'), '-o', str(vertex)]):
                step=invoke(command);report['steps'].append(step);demand(step['returncode']==0,'owned fixture build failed')
            modes={}
            correct_binary=correct_record=None
            for name, enabled, mapping, data in (('enabled',True,MAPPING,enabled_cases),('disabled',False,MAPPING,disabled_cases),
                    ('swapped_panels',True,{**MAPPING,'color':MAPPING['coverage'],'coverage':MAPPING['color']},enabled_cases)):
                binary, transform = tool.transform(original,mapping,enabled=enabled)
                path=private/(name+'.spv');path.write_bytes(binary);input_path=private/(name+'.bin');input_path.write_bytes(data)
                step=invoke(['spirv-val','--target-env','vulkan1.1',str(path)]);report['steps'].append(step);demand(step['returncode']==0,'atlas module validity')
                modes[name]=(path,input_path)
                report[name+'_transform']=transform
                if name=='enabled':correct_binary,correct_record=binary,transform
            for name in ('reused_mask_weight','double_aperture'):
                binary=mutate_owned_result(correct_binary,correct_record['new_ssa_ids'],name)
                path=private/(name+'.spv');path.write_bytes(binary);input_path=private/(name+'.bin');input_path.write_bytes(enabled_cases)
                step=invoke(['spirv-val','--target-env','vulkan1.1',str(path)]);report['steps'].append(step);demand(step['returncode']==0,'valid causal mutant required')
                modes[name]=(path,input_path);report[name+'_sha256']=hashlib.sha256(binary).hexdigest()
            for label in (('software','intel') if args.driver=='both' else (args.driver,)):
                icd='/usr/share/vulkan/icd.d/'+('lvp_icd.json' if label=='software' else 'intel_icd.json')
                entry={'driver':label,'runs':{}};report['devices'].append(entry)
                for mode,(path,input_path) in modes.items():
                    settings=private/(label+'-'+mode);settings.mkdir();messages=settings/'messages.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(messages)+'\n')
                    env=dict(os.environ,VK_ICD_FILENAMES=icd,VK_LAYER_SETTINGS_PATH=str(settings),VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result=invoke([str(fixture),str(vertex),str(args.original),str(path),str(input_path)],env)
                    result['validation_messages']=messages.read_text(errors='replace') if messages.exists() else ''
                    entry['runs'][mode]=result
                    demand('Validation Error' not in result['stderr']+result['validation_messages'],'Vulkan validation error')
                    demand('SYNC-HAZARD' not in result['validation_messages'],'synchronization validation error')
                    if mode not in ('enabled','disabled'):
                        expected_error='atlas raw component-G differs' if mode=='reused_mask_weight' else 'atlas retargeted color differs'
                        demand(result['returncode']==1 and 'atlas fixture failed: '+expected_error in result['stderr'],'causal '+mode+' was not specifically detected')
                    else:
                        demand(result['returncode']==0,'atlas GPU positive failed: '+label+'/'+mode)
                        result['result']=json.loads(result['stdout'])
                        demand(result['result']['validation_enabled'] is True and result['result']['validation_errors']==0,'active validation required')
            report['status']='PASS';exit_code=0
    except (OSError,ValueError,recovery.probe.ProbeError,subprocess.SubprocessError) as e:
        report['error']=str(e)
    finally:
        report['temporary_native_outputs_removed']=private is None or not private.exists()
        try: recovery.write_owned(args.record,(json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+'\n').encode())
        except (OSError,recovery.probe.ProbeError) as e:
            print(str(e),file=sys.stderr);exit_code=1
    print(json.dumps({'status':report['status'],'devices':len(report['devices']),'passthrough_authorized':False}))
    return exit_code


if __name__=='__main__':raise SystemExit(main())
