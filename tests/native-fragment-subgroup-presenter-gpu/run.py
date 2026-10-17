#!/usr/bin/env python3
"""Private subgroup guard equivalence and host cost; no native activation."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import native_fragment_subgroup_presenter as subgroup


def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);sys.modules[name]=result;spec.loader.exec_module(result);return result


old=load(ROOT/'tests/native-fragment-guarded-presenter-gpu/run.py','subgroup_borrowed_runner')
invoke,demand=old.invoke,old.demand


def selector_variant(binary):
    """Owned oracle-only eye selector alternates adjacent raster columns.

    All three compared modules receive the exact same test mutation. Production
    keeps the pinned original uniform selector. No native body is embedded here.
    """
    words=list(struct.unpack('<%dI'%(len(binary)//4),binary));at=5;instructions=[]
    while at<len(words):
        count,op=words[at]>>16,words[at]&65535;instructions.append(subgroup.ssa.Instruction(op,tuple(words[at+1:at+count])));at+=count
    require=subgroup.require
    matches=[i for i,x in enumerate(instructions) if x==subgroup.ssa.Instruction(61,(6,693,692))]
    require(len(matches)==1,'one exact original metadata selector load')
    bound=words[3];component,integer,one=bound,bound+1,bound+2;words[3]+=3
    fn=next(i for i,x in enumerate(instructions) if x.opcode==54)
    instructions.insert(fn,subgroup.ssa.Instruction(43,(6,one,1)))
    where=matches[0]+1
    instructions[where:where+1]=[subgroup.ssa.Instruction(81,(8,component,495,3)),
        subgroup.ssa.Instruction(109,(6,integer,component)),subgroup.ssa.Instruction(199,(6,693,integer,one))]
    return subgroup.guard.pack_module(words[:5],instructions)


def routing_variant(binary,record):
    """Owned diagnostic output only; never a production artifact."""
    header,instructions=subgroup.parse_module(binary);bound=header[3]
    constants={x.operands[2]:x.operands[1] for x in instructions if x.opcode==43 and x.operands[0]==8}
    zero,one=constants[0],constants[0x3f800000];output=[]
    for item in instructions:
        if item.opcode==62 and item.operands[0]==504:
            components=[]
            for name in ['may_share','helper','valid']:
                components.append(bound);output.append(subgroup.ssa.Instruction(169,(8,bound,record['subgroup_ssa'][name],one,zero)));bound+=1
            output.append(subgroup.ssa.Instruction(80,(9,bound,*components,one)))
            output.append(subgroup.ssa.Instruction(62,(504,bound)));bound+=1
        else:output.append(item)
    header[3]=bound;return subgroup.guard.pack_module(header,output)


def vertex(vary=False,edge=False,mixed=False):
    return '''#version 450
layout(push_constant) uniform Input {vec4 u;} fixture;
layout(location=0) noperspective out vec4 a;
layout(location=1) noperspective out vec4 originalInput;
layout(location=2) noperspective out vec4 b;
layout(location=3) flat out uint c;
void main(){vec2 corner=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
gl_Position=vec4(corner*'''+('1.43' if edge else '2.')+'''-1.,0.,1.);
originalInput='''+('vec4(fixture.u.xy+corner*fixture.u.zw,.625,.875)' if vary else 'fixture.u')+''';
'''+('originalInput.w=.875+corner.x*17.;' if mixed else '')+'''
a=vec4(.125,.375,.625,.875);b=vec4(.75,.5,.25,1.);c=0u;}
'''


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original',type=Path,required=True);p.add_argument('--record',type=Path,required=True)
    p.add_argument('--part',choices=('both','correctness','timing','witnesses'),default='both')
    args=p.parse_args(argv)
    report={'schema':'native-subgroup-presenter-gpu-v1','status':'FAILED','steps':[],
        'native_activation':False,'frame_timing_proven':False,'body_authenticated':False,'devices':[]}
    private=None;code=1
    try:
        demand(not args.record.exists(),'exclusive evidence path')
        original=args.original.read_bytes();profile=subgroup.guard.load_profile()
        baseline,base_record=subgroup.guard.transform(original,profile)
        candidate,record=subgroup.transform(original,profile)
        off,_=subgroup.transform(original,profile,False);demand(off==original,'OFF byte exact')
        cases,facts=old.cases();report.update(transformation=record,old_guard=base_record,owned_cases=facts)
        sources=[Path(__file__),Path(__file__).with_name('fixture.cpp'),Path(subgroup.__file__),Path(old.__file__),
                 ROOT/'tests/native-fragment-guarded-presenter-gpu/fixture.cpp',ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp']
        report['source_pins']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        with tempfile.TemporaryDirectory(prefix='native-subgroup-gpu-') as name:
            private=Path(name)
            inherited=(ROOT/'tests/native-fragment-guarded-presenter-gpu/fixture.cpp').read_text()
            inherited=inherited.replace('int main(int argc,char** argv){','int inherited_guard_main(int argc,char** argv){')
            inherited=inherited.replace('#include "../native-fragment-ssa-gpu/fixture.cpp"',
                '#include "'+str(ROOT/'tests/native-fragment-ssa-gpu/fixture.cpp')+'"')
            (private/'borrowed_guard_fixture.h').write_text(inherited)
            fixture=private/'fixture'
            build=invoke(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I',str(private),str(Path(__file__).with_name('fixture.cpp')),'-lvulkan','-o',str(fixture)])
            report['steps'].append(build);demand(build['returncode']==0,'fixture strict compile')
            paths={}
            for kind,binary in [('original',original),('baseline',baseline),('subgroup',candidate)]:
                for mixed in (False,True):
                    label=kind+('_mixed' if mixed else '')
                    path=private/(label+'.spv');path.write_bytes(selector_variant(binary) if mixed else binary);paths[label]=path
                    val=invoke(['spirv-val','--target-env','vulkan1.1',str(path)]);report['steps'].append(val)
                    demand(val['returncode']==0,'valid '+label)
            for kind,src in [('constant',vertex()),('edge',vertex(edge=True)),('mixed',vertex(edge=True,mixed=True)),('varying',vertex(vary=True))]:
                source=private/(kind+'.vert');source.write_text(src);path=private/(kind+'.spv');paths[kind]=path
                compile=invoke(['glslc','--target-env=vulkan1.1',str(source),'-o',str(path)]);report['steps'].append(compile)
                demand(compile['returncode']==0,'owned vertex '+kind)
            if args.part=='witnesses':
                for name,binary in [('routing',routing_variant(candidate,record)),('routing_mixed',selector_variant(routing_variant(candidate,record)))]:
                    path=private/(name+'.spv');path.write_bytes(binary);paths[name]=path
                    val=invoke(['spirv-val','--target-env','vulkan1.1',str(path)]);report['steps'].append(val);demand(val['returncode']==0,'valid '+name)
                helper_source=private/'helper.frag';helper_source.write_text('''#version 450
#extension GL_KHR_shader_subgroup_quad : require
layout(location=0) out vec4 witness;
void main(){float h=gl_HelperInvocation?1.:0.;
 float sum=subgroupQuadBroadcast(h,0)+subgroupQuadBroadcast(h,1)+subgroupQuadBroadcast(h,2)+subgroupQuadBroadcast(h,3);
 witness=vec4(sum,0.,0.,1.);}
''')
                paths['helper']=private/'helper.spv'
                compiled=invoke(['glslc','--target-env=vulkan1.1',str(helper_source),'-o',str(paths['helper'])]);report['steps'].append(compiled);demand(compiled['returncode']==0,'helper witness build')
                val=invoke(['spirv-val','--target-env','vulkan1.1',str(paths['helper'])]);report['steps'].append(val);demand(val['returncode']==0,'helper witness valid')
            case_path=private/'cases.bin';case_path.write_bytes(cases)
            empty=private/'empty';empty.mkdir()
            # A real unsupported driver must reject before creating the device;
            # it is not counted as a correctness pass.
            env=dict(os.environ,VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json',VK_LAYER_PATH=str(empty))
            refusal=invoke([str(fixture),str(paths['constant']),str(paths['original']),str(paths['baseline']),str(paths['subgroup']),str(case_path),'correctness','1','1'],env)
            report['lavapipe_refusal']=refusal;demand(refusal['returncode']!=0 and 'UNSUPPORTED:' in refusal['stderr'],'unsupported Lavapipe must refuse')
            entry={'driver':'intel'};report['devices'].append(entry)
            if args.part in ('both','correctness','witnesses'):
                settings=private/'validation';settings.mkdir();log=settings/'validation.log'
                (settings/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = true\nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\nkhronos_validation.report_flags = error,warn,perf\nkhronos_validation.log_filename = '+str(log)+'\n')
                env=dict(os.environ,VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/intel_icd.json',VK_LAYER_SETTINGS_PATH=str(settings),VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
                entry['correctness']=[]
                runs=[('constant',1,1,False),('edge',17,19,False),('mixed',17,19,True)]
                if args.part=='witnesses':runs=[('edge',17,19,False),('mixed',17,19,True),('helper',17,19,False)]
                for name,width,height,mixed in runs:
                    suffix='_mixed' if mixed else ''
                    output=paths['subgroup'+suffix];mode='correctness';vertex_path=paths[name]
                    if args.part=='witnesses':
                        mode='helper' if name=='helper' else 'routing';output=paths['helper'] if mode=='helper' else paths['routing'+suffix]
                        if name=='helper':vertex_path=paths['edge']
                    command=[str(fixture),str(vertex_path),str(paths['original'+suffix]),str(paths['baseline'+suffix]),str(output),str(case_path),mode,str(width),str(height)]+(['mixed'] if mixed else [])
                    result=invoke(command,env,timeout=120);result['name']=name;entry['correctness'].append(result)
                    demand(result['returncode']==0,'actual subgroup '+name+' correctness')
                    result['result']=json.loads(result['stdout'])
                messages=log.read_text(errors='replace') if log.exists() else ''
                entry['validation_log']=messages;demand('Validation Error' not in messages and 'SYNC-HAZARD' not in messages,'Vulkan/sync validation')
            if args.part in ('both','timing'):
                env=dict(os.environ,VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/intel_icd.json',VK_LAYER_PATH=str(empty))
                env.pop('VK_LAYER_ENABLES',None);env.pop('VK_LAYER_SETTINGS_PATH',None)
                entry['timing']=[]
                for width,height in [(512,512),(1152,4608)]:
                    result=invoke([str(fixture),str(paths['varying']),str(paths['original']),str(paths['baseline']),str(paths['subgroup']),str(case_path),'benchmark',str(width),str(height)],env,timeout=120)
                    entry['timing'].append(result);demand(result['returncode']==0,'actual bounded timing')
                    result['result']=json.loads(result['stdout'])
                    if width==512:demand(max(max(x['cpu_ms']) for x in result['result']['runs'].values())<400,'safe preflight cost')
                entry['timing_summary']={k:{'gpu_median_ms':statistics.median(v['gpu_ms']),'cpu_median_ms':statistics.median(v['cpu_ms'])}
                    for k,v in entry['timing'][-1]['result']['runs'].items()}
            report['status'],code='PASS',0
    except Exception as e:report['error']=str(e)
    finally:
        path=Path('/home/claw/gloomhaven_vr/scripts/town_run_retention.py')
        retention=load(path,'subgroup_retention')
        report['cleanup']=retention.cleanup_completed_run('native-subgroup-presenter-gpu',code,args.record,ROOT,process_reaped=True)
        report['temporary_cache_removed']=private is None or not private.exists()
        args.record.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'status':report['status'],'error':report.get('error')}));return code


if __name__=='__main__':raise SystemExit(main())
