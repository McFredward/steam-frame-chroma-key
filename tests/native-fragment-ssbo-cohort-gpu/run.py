#!/usr/bin/env python3
"""Owned Vulkan proof of per-execution SSBO reset on a reused recording.

No native device admission, decoded epoch, physical eye or live alpha authority.
All temporary derivatives of the explicit private original are removed on exit.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import native_fragment_ssbo_probe as tool
spec = importlib.util.spec_from_file_location('owned_ssbo_runner',ROOT/'tests/native-fragment-ssbo-gpu/run.py')
base = importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
demand = base.demand


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--driver',choices=('software','intel','both'),default='both')
    args = parser.parse_args(argv)
    report = {'schema':'owned-ssbo-replay-cohort-proof-v1','status':'FAILED','steps':[],'devices':[],
        'scope':'One owned queue, serial fence-completed submissions, unchanged target recording and descriptors. No native receiver is loaded.',
        'unsupported':['simultaneous submission','different target command buffers','different queues','native descriptor/source mutation'],
        'native_admission_proven':False,'native_frame_association_proven':False,'physical_eye_proven':False,
        'application_ray_mapping_proven':False,'linear_game_alpha_proven':False,'passthrough_authorized':False}
    code,private,source_hashes = 1,None,{}
    try:
        demand(not args.record.exists() and not args.record.is_symlink(),'exclusive receipt required')
        original = tool.recovery.read_owned(args.original,tool.ssa.MAX_BYTES)
        demand(len(original)==tool.ssa.ORIGINAL_BYTES and tool.probe.digest(original)==tool.ssa.ORIGINAL_SHA256,'exact private original module required')
        sources = [Path(__file__),Path(__file__).with_name('fixture.cpp'),ROOT/'tools/native_fragment_ssbo_probe.py',
            ROOT/'tools/native_fragment_ssa_probe.py',ROOT/'tests/native-fragment-ssbo-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssbo-gpu/run.py',ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssa-gpu/fixture.vert',ROOT/'tests/native-fragment-atlas-gpu/run.py']
        source_hashes = {str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        report['source_sha256']=source_hashes;report['original_sha256']=tool.ssa.ORIGINAL_SHA256
        with tempfile.TemporaryDirectory(prefix='owned-ssbo-cohort-') as directory:
            private=Path(directory);fixture=private/'fixture';vertex=private/'vertex.spv';probe=private/'probe.spv'
            for command in (['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2',str(Path(__file__).with_name('fixture.cpp')),'-lvulkan','-o',str(fixture)],
                    ['glslc','--target-env=vulkan1.1',str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'),'-o',str(vertex)]):
                result=base.base.invoke(command);report['steps'].append(result);demand(result['returncode']==0,'owned cohort fixture compile')
            binary,metadata=tool.transform(original,base.admission());probe.write_bytes(binary)
            report['transformation']=metadata
            result=base.base.invoke(['spirv-val','--target-env','vulkan1.1',str(probe)]);report['steps'].append(result)
            demand(result['returncode']==0,'generated original-SSA probe must validate')
            report['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest()
            report['vertex_sha256']=hashlib.sha256(vertex.read_bytes()).hexdigest()
            for driver in (('software','intel') if args.driver=='both' else (args.driver,)):
                entry={'driver':driver,'runs':{}};report['devices'].append(entry)
                icd='/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver=='software' else 'intel_icd.json')
                for mode in ('reset','no-reset'):
                    settings=private/(driver+'-'+mode);settings.mkdir();messages=settings/'validation.log'
                    (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n'
                        'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                        'khronos_validation.report_flags = error,warn,perf\n'
                        'khronos_validation.log_filename = '+str(messages)+'\n')
                    env=dict(os.environ,VK_ICD_FILENAMES=icd,VK_LAYER_SETTINGS_PATH=str(settings),
                        VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                    result=base.base.invoke([str(fixture),str(vertex),str(args.original),str(probe),mode],env)
                    result['validation_messages']=messages.read_text(errors='replace') if messages.exists() else ''
                    entry['runs'][mode]=result
                    demand('Validation Error' not in result['stderr']+result['validation_messages']
                        and 'SYNC-HAZARD' not in result['stderr']+result['validation_messages'],'real Vulkan validation error')
                    if mode=='reset':
                        demand(result['returncode']==0,'per-execution reset proof failed '+driver)
                        value=json.loads(result['stdout']);result['result']=value
                        demand(value['validation_enabled'] is True and value['validation_errors']==0,'active validation required')
                        demand(value['actual_feature_enabled'] is True and value['target_recordings']==1 and value['target_resets']==0
                            and value['target_begin_flags']==0 and value['target_submissions']==3 and value['cohorts']==3,'real same-CB replay proof required')
                        demand(value['source_fingerprint_first']!=value['source_fingerprint_last'],'captured GPU source must change')
                        demand(value['reset_bytes']==128 and value['active_color_attachments']==1,'bounded record/output fixture')
                    else:
                        demand(result['returncode']==1 and 'cohort fixture failed: cohort mismatch: FIRST SSBO record with LAST sampled image' in result['stderr'],
                            'specific stale-first/latest-image causal failure required')
                        controls=[json.loads(line) for line in result['stderr'].splitlines() if line.startswith('{')]
                        value=next((item for item in controls if item.get('causal_no_reset') is True),None)
                        demand(value is not None and value['target_recordings']==1 and value['target_submissions']==2
                            and value['validation_errors']==0 and value['source_fingerprint_first']!=value['source_fingerprint_current']
                            and value['first_record']==value['current_record'],'actual legal mixed-cohort counterexample required')
                        result['causal_result']=value
            report['status']='PASS';code=0
    except (OSError,ValueError,KeyError,tool.probe.ProbeError,base.base.subprocess.SubprocessError) as error:
        report['error']=str(error)
    finally:
        report['temporary_native_outputs_removed']=private is None or not private.exists()
        report['production_changed_during_run']=[name for name,digest in source_hashes.items() if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=digest]
        if report['production_changed_during_run']:code=1;report['status']='FAILED'
        try:tool.recovery.write_owned(args.record,(json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+'\n').encode())
        except (OSError,tool.probe.ProbeError) as error:print(str(error),file=sys.stderr);code=1
    print(json.dumps({'status':report['status'],'devices':len(report['devices']),'passthrough_authorized':False}))
    return code


if __name__=='__main__':raise SystemExit(main())
