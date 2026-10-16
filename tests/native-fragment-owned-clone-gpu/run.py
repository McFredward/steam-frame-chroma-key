#!/usr/bin/env python3
"""Actual production cold factory with original native VS/PS and retired source objects."""
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
    parser.add_argument('--vertex',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--driver',choices=('software','intel','both'),default='both')
    args=parser.parse_args(argv)
    report={'schema':'owned-real-cold-clone-proof-v1','status':'FAILED','steps':[],'devices':[],
        'scope':'Actual original native VS/PS creation then production capture+owned factory recreation after original-object retirement and source-array overwrite. No command recording/draw/queue wait.',
        'limitations':['Owned host resources and explicitly enabled features only; no native receiver admission.',
            'Original generations are explicit owned-fixture provenance, not native observer generations.',
            'No pixel/render equivalence, compatibility with a live receiver, or runtime lifetime is proven.'],
        'native_admission_proven':False,'native_frame_association_proven':False,
        'physical_eye_proven':False,'application_ray_mapping_proven':False,'passthrough_authorized':False}
    code,private,source_hashes=1,None,{}
    try:
        demand(not args.record.exists() and not args.record.is_symlink(),'exclusive receipt required')
        original=tool.recovery.read_owned(args.original,tool.ssa.MAX_BYTES)
        demand(len(original)==tool.ssa.ORIGINAL_BYTES and tool.probe.digest(original)==tool.ssa.ORIGINAL_SHA256,'pinned original PS required')
        vertex=tool.recovery.read_owned(args.vertex,tool.ssa.MAX_BYTES)
        demand(len(vertex)==9460 and tool.probe.digest(vertex)=='94abc0c24124e8b4a301e7f1df5a4c8c0e8b0eb50e4c6ea78f3521370a6897c5','pinned original VS required')
        sources=[Path(__file__),Path(__file__).with_name('fixture.cpp'),ROOT/'layer/pixel_ssbo_owned_clone.cpp',
            ROOT/'layer/pixel_ssbo_owned_clone.h',ROOT/'layer/pixel_ssbo_clone_contract.h',ROOT/'receiver/sha256.h',
            args.vertex,args.original,ROOT/'tools/native_fragment_ssbo_probe.py',
            ROOT/'tools/native_fragment_ssa_probe.py',ROOT/'tests/native-fragment-ssbo-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssbo-gpu/run.py',ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp',
            ROOT/'tests/native-fragment-ssa-gpu/fixture.vert',ROOT/'tests/native-fragment-atlas-gpu/run.py']
        source_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        report['source_sha256']=source_hashes;report['original_sha256']=tool.ssa.ORIGINAL_SHA256
        with tempfile.TemporaryDirectory(prefix='owned-cold-clone-') as directory:
            private=Path(directory);fixture=private/'fixture';probe=private/'probe.spv'
            command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2',str(Path(__file__).with_name('fixture.cpp')),
                str(ROOT/'layer/pixel_ssbo_owned_clone.cpp'),'-lvulkan','-o',str(fixture)]
            step=base.base.invoke(command);report['steps'].append(step);demand(step['returncode']==0,'actual production factory fixture build')
            binary,record=tool.transform(original,base.admission());probe.write_bytes(binary);report['transformation']=record
            step=base.base.invoke(['spirv-val','--target-env','vulkan1.1',str(probe)]);report['steps'].append(step)
            demand(step['returncode']==0,'original-SSA SSBO probe validates')
            report['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest()
            report['vertex_sha256']=tool.probe.digest(vertex)
            for driver in (('software','intel') if args.driver=='both' else (args.driver,)):
                settings=private/driver;settings.mkdir();messages=settings/'validation.log'
                (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\n'
                    'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
                    'khronos_validation.report_flags = error,warn,perf\n'
                    'khronos_validation.log_filename = '+str(messages)+'\n')
                icd='/usr/share/vulkan/icd.d/'+('lvp_icd.json' if driver=='software' else 'intel_icd.json')
                env=dict(os.environ,VK_ICD_FILENAMES=icd,VK_LAYER_SETTINGS_PATH=str(settings),
                    VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                result=base.base.invoke([str(fixture),str(args.vertex),str(args.original),str(probe),record['instrumented_sha256']],env)
                result['validation_messages']=messages.read_text(errors='replace') if messages.exists() else ''
                report['devices'].append({'driver':driver,'run':result})
                demand('Validation Error' not in result['stderr']+result['validation_messages']
                    and 'SYNC-HAZARD' not in result['stderr']+result['validation_messages'],'actual Vulkan validation failure')
                demand(result['returncode']==0,'actual cold factory failed '+driver)
                value=json.loads(result['stdout']);result['result']=value
                demand(value['cases']==1 and value['original_pipeline_created'] is True
                    and value['recipe_captured_from_actual_successful_objects'] is True
                    and value['original_objects_destroyed_and_inputs_overwritten_before_prepare'] is True,'actual original ownership/capture required')
                demand(value['native_vertex_bytes']==9460 and value['native_fragment_bytes']==6620
                    and value['shader_output_layer_enabled'] is True and value['shader_output_viewport_index_enabled'] is True
                    and value['shader_viewport_index_layer_extension_enabled'] is True
                    and value['fragment_stores_enabled'] is True and value['geometry_enabled'] is True,'actual native shader features explicitly enabled')
                demand(value['factory_prepare_succeeded'] is True and value['record_zero_bytes']==128
                    and value['never_referenced_release'] is True and value['queue_submissions']==0
                    and value['queue_waits']==0 and value['command_buffers']==0,'complete actual owned factory without invented GPU lifetime proof')
                demand(value['validation_enabled'] is True and value['validation_errors']==0,'active actual validation required')
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
