#!/usr/bin/env python3
"""Private owned-Vulkan proof of Set0 prefix compatibility and restoration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import native_fragment_ssbo_probe as tool
spec=importlib.util.spec_from_file_location('owned_ssbo_runner',ROOT/'tests/native-fragment-ssbo-gpu/run.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
demand=base.demand


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--driver',choices=('software','intel','both'),default='both')
    args=parser.parse_args(argv)
    report={'schema':'owned-native-prefix-restore-proof-v1','status':'FAILED','steps':[],'devices':[],
        'scope':'Owned single-command-buffer Set0-only pipeline vs compatible Set0+ownSet1 clone; no receiver code is loaded.',
        'limitations':['Set1 stays bound and unused after original-pipeline restore; it is not unbound.',
            'Only complete identical fixture push ranges and distinct identically-defined Set0 layouts without immutable samplers/extensions are admitted.',
            'Two pure pre-issue guard refusals are not invalid-layout Vulkan negative tests.',
            'No native vertex module, native admission, physical eye or frame association is proven.'],
        'native_admission_proven':False,'native_frame_association_proven':False,
        'physical_eye_proven':False,'application_ray_mapping_proven':False,'passthrough_authorized':False}
    code,private,source_hashes=1,None,{}
    try:
        demand(not args.record.exists() and not args.record.is_symlink(),'exclusive receipt required')
        original=tool.recovery.read_owned(args.original,tool.ssa.MAX_BYTES)
        demand(len(original)==tool.ssa.ORIGINAL_BYTES and tool.probe.digest(original)==tool.ssa.ORIGINAL_SHA256,'pinned original PS required')
        sources=[Path(__file__),Path(__file__).with_name('fixture.cpp'),ROOT/'tools/native_fragment_ssbo_probe.py',
            ROOT/'tools/native_fragment_ssa_probe.py',ROOT/'tests/native-fragment-ssbo-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssbo-gpu/run.py',ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssa-gpu/fixture.vert',ROOT/'tests/native-fragment-atlas-gpu/run.py']
        source_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        report['source_sha256']=source_hashes;report['original_sha256']=tool.ssa.ORIGINAL_SHA256
        with tempfile.TemporaryDirectory(prefix='owned-prefix-restore-') as directory:
            private=Path(directory);fixture=private/'fixture';vertex=private/'vertex.spv';probe=private/'probe.spv'
            for command in (['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2',str(Path(__file__).with_name('fixture.cpp')),'-lvulkan','-o',str(fixture)],
                    ['glslc','--target-env=vulkan1.1',str(ROOT/'tests/native-fragment-ssa-gpu/fixture.vert'),'-o',str(vertex)]):
                step=base.base.invoke(command);report['steps'].append(step);demand(step['returncode']==0,'new owned prefix fixture build')
            binary,record=tool.transform(original,base.admission());probe.write_bytes(binary);report['transformation']=record
            step=base.base.invoke(['spirv-val','--target-env','vulkan1.1',str(probe)]);report['steps'].append(step)
            demand(step['returncode']==0,'original-SSA SSBO probe validates')
            report['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest()
            report['vertex_sha256']=hashlib.sha256(vertex.read_bytes()).hexdigest()
            for driver in (('software','intel') if args.driver=='both' else (args.driver,)):
                settings=private/driver;settings.mkdir();messages=settings/'validation.log'
                (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n'
                    'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                    'khronos_validation.report_flags = error,warn,perf\n'
                    'khronos_validation.log_filename = '+str(messages)+'\n')
                icd='/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver=='software' else 'intel_icd.json')
                env=dict(os.environ,VK_ICD_FILENAMES=icd,VK_LAYER_SETTINGS_PATH=str(settings),
                    VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                result=base.base.invoke([str(fixture),str(vertex),str(args.original),str(probe)],env)
                result['validation_messages']=messages.read_text(errors='replace') if messages.exists() else ''
                report['devices'].append({'driver':driver,'run':result})
                demand('Validation Error' not in result['stderr']+result['validation_messages']
                    and 'SYNC-HAZARD' not in result['stderr']+result['validation_messages'],'actual Vulkan validation failure')
                demand(result['returncode']==0,'actual prefix restoration failed '+driver)
                value=json.loads(result['stdout']);result['result']=value
                demand(value['cases']==2 and value['original_layout_set_count']==1 and value['clone_layout_set_count']==2
                    and value['distinct_set0_handles'] is True and value['identical_set0_definitions'] is True
                    and value['complete_push_ranges_equal'] is True,'real distinct compatible layouts required')
                demand(value['original_restored_bit_identical'] is True and value['initial_set0_binds']==2
                    and value['original_state_rebinds_around_probe']==0 and value['owned_set1_binds']==2
                    and value['immediate_original_pipeline_restorations']==2,'original state must survive without rebinding')
                demand(value['dynamic_viewport_scissor'] is True and value['set1_remains_bound_unused'] is True
                    and value['guard_preissue_refusals']==2,'precise dynamic/higher-set guard contract required')
                demand(value['actual_feature_enabled'] is True and value['validation_enabled'] is True
                    and value['validation_errors']==0,'enabled feature and active validation required')
            report['status']='PASS';code=0
    except (OSError,ValueError,KeyError,tool.probe.ProbeError,base.base.subprocess.SubprocessError) as error:
        report['error']=str(error)
    finally:
        report['temporary_native_outputs_removed']=private is None or not private.exists()
        report['production_changed_during_run']=[p for p,d in source_hashes.items() if hashlib.sha256(Path(p).read_bytes()).hexdigest()!=d]
        if report['production_changed_during_run']:code=1;report['status']='FAILED'
        try:tool.recovery.write_owned(args.record,(json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+'\n').encode())
        except (OSError,tool.probe.ProbeError) as error:print(str(error),file=sys.stderr);code=1
    print(json.dumps({'status':report['status'],'devices':len(report['devices']),'passthrough_authorized':False}));return code


if __name__=='__main__':raise SystemExit(main())
