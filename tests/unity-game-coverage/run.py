#!/usr/bin/env python3
"""Inert owned Unity production-shader pixels; never a complete game mask."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
FAMILIES = {'Environments/EnvParticleAlpha.shader': ('Blend SrcAlpha OneMinusSrcAlpha','Blend SrcAlpha OneMinusSrcAlpha, One OneMinusSrcAlpha'),
            'Environments/EnvParticleAdd.shader': ('Blend SrcAlpha One','Blend SrcAlpha One, Zero One'),
            'HealthBars/ActorBarSprite.shader': ('Blend SrcAlpha OneMinusSrcAlpha','Blend SrcAlpha OneMinusSrcAlpha, One OneMinusSrcAlpha'),
            'Head/HeadUnlit.shader': ('return tex2D(_MainTex, i.uv) * _Color;',
                                     'fixed4 owned = tex2D(_MainTex, i.uv) * _Color; return fixed4(owned.rgb, 1);')}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--production-root',type=Path,required=True)
    parser.add_argument('--output-root',type=Path,required=True)
    parser.add_argument('--unity',type=Path,default=Path('/home/claw/unity-2021.3.5/Editor/Unity'))
    parser.add_argument('--backend',choices=('vulkan','glcore'),default='vulkan')
    parser.add_argument('--color-space',choices=('gamma','linear','both'),default='both')
    args=parser.parse_args()
    source=args.production_root.resolve()
    assert (source/'scripts/town_run_retention.py').is_file() and args.unity.is_file()
    sys.path.insert(0,str(source/'scripts'))
    cleanup=importlib.import_module('town_run_retention').cleanup_completed_run
    args.output_root.mkdir(parents=True,exist_ok=True)
    run=Path(tempfile.mkdtemp(prefix='run-',dir=args.output_root.resolve()))
    (run/'.keep').touch() # Preserve evidence; generated project is removed explicitly below.
    log=run/'completion.log';log.write_text('evidence: '+str(run)+'\n')
    report={'scope':'INERT_OWNED_SHADER_FAMILIES_ONLY','production_shader_mutation':False,
            'complete_game_coverage_proven':False,'live_alpha_authorized':False,'profiles':{}}
    code=1
    removed_projects=[]
    copied=run/'sources';copied.mkdir()
    bundle=source/'unity/GloomhavenVR.Assets/Assets/Bundle'
    hashes={}
    for family in FAMILIES:
        for path in (bundle/Path(family).parent).glob('*.cginc'):
            dest=copied/path.relative_to(bundle);dest.parent.mkdir(parents=True,exist_ok=True)
            data=path.read_bytes();dest.write_bytes(data);hashes[str(path.relative_to(bundle))]=sha(data)
        path=bundle/family;data=path.read_bytes()
        dest=copied/family;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data);hashes[family]=sha(data)
    (run/'source-pins.json').write_text(json.dumps({'production':hashes,'fixture':sha((HERE/'Fixture.cs').read_bytes()),
                                                 'runner':sha(Path(__file__).read_bytes())},indent=2)+'\n')
    try:
        for color in (('gamma','linear') if args.color_space=='both' else (args.color_space,)):
            project=run/('unity-'+color)
            assets=project/'Assets';(assets/'Editor').mkdir(parents=True)
            shutil.copyfile(HERE/'Fixture.cs',assets/'Editor/CoverageFixture.cs')
            shutil.copytree(copied,assets/'Shaders')
            for family,(old,new) in FAMILIES.items():
                text=(copied/family).read_text()
                assert text.count(old)==1,'Known single original alpha state required: '+family
                family_name=Path(family).stem
                original_name='Shader "GloomhavenVR/'+family_name+'"'
                assert text.count(original_name)==1
                changed=text.replace(old,new).replace(original_name,'Shader "CoverageFixture/'+family_name+'"')
                dest=assets/'Shaders'/Path(family).parent/(family_name+'Coverage.shader')
                dest.write_text(changed)
                (run/(family_name+'Coverage.shader')).write_text(changed)
            (project/'Packages').mkdir();(project/'Packages/manifest.json').write_text('{"dependencies":{}}\n')
            settings=project/'ProjectSettings';settings.mkdir()
            (settings/'ProjectVersion.txt').write_text('m_EditorVersion: 2021.3.5f1\n')
            (settings/'ProjectSettings.asset').write_text('%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!129 &1\nPlayerSettings:\n  m_ObjectHideFlags: 0\n  serializedVersion: 26\n  m_ActiveColorSpace: '+('1' if color=='linear' else '0')+'\n')
            output=run/(color+'.json')
            command=['xvfb-run','-a',str(args.unity),'-batchmode','-force-'+args.backend,'-projectPath',str(project),
                     '-executeMethod','CoverageFixture.Start','-coverageResult',str(output),'-logFile',str(run/(color+'.unity.log'))]
            environment=os.environ.copy()
            if args.backend=='vulkan' and Path('/usr/share/vulkan/icd.d/intel_icd.json').is_file():
                environment['VK_DRIVER_FILES']='/usr/share/vulkan/icd.d/intel_icd.json'
            with (run/(color+'.launcher.log')).open('w') as stream:
                process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True,env=environment)
                try: status=process.wait(timeout=240)
                except BaseException:
                    if process.poll() is None:os.killpg(process.pid,signal.SIGKILL)
                    process.wait();raise
            assert status==0 and output.is_file(),'Actual Unity fixture failed: '+str(run)
            result=json.loads(output.read_text());report['profiles'][color]=result
            assert result['passed'] and result['color_space'].lower()==color
            cleanup('steamlink-game-coverage',0,log,source,process_reaped=True)
            shutil.rmtree(project)
            removed_projects.append(str(project))
        assert all(sha((bundle/name).read_bytes())==digest for name,digest in hashes.items()),'Production inputs changed'
        code=0
    finally:
        report['exit_code']=code
        (run/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
        receipt=cleanup('steamlink-game-coverage',code,log,source,process_reaped=True)
        for project in run.glob('unity-*'):
            if project.is_dir() and not project.is_symlink():shutil.rmtree(project);removed_projects.append(str(project))
        (run/'cleanup.json').write_text(json.dumps({'retention_api':receipt,'owned_projects_removed':removed_projects},indent=2)+'\n')
        print('evidence: '+str(run))
    return code


if __name__=='__main__':
    raise SystemExit(main())
