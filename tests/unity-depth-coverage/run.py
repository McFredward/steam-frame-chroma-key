#!/usr/bin/env python3
"""Actual camera/depth/MSAA alpha experiment; never modifies game or live source."""
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


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--production-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--unity', type=Path, default=Path('/home/claw/unity-2021.3.5/Editor/Unity'))
    parser.add_argument('--backend', choices=('vulkan', 'glcore'), default='vulkan')
    parser.add_argument('--color-space', choices=('gamma', 'linear', 'both'), default='both')
    args = parser.parse_args()
    root = args.production_root.resolve()
    sys.path.insert(0, str(root / 'scripts'))
    cleanup = importlib.import_module('town_run_retention').cleanup_completed_run
    args.output_root.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run-', dir=args.output_root.resolve()))
    (run / '.keep').touch()
    log = run / 'completion.log'; log.write_text('evidence: ' + str(run) + '\n')
    copied = run / 'sources'; copied.mkdir()
    bundle = root / 'unity/GloomhavenVR.Assets/Assets/Bundle'
    families = ('Head/HeadUnlit.shader', 'Environments/EnvParticleAlpha.shader', 'Environments/EnvParticleAdd.shader')
    pins = {}
    for family in families:
        source = bundle / family
        data = source.read_bytes(); pins[family] = digest(data)
        dest = copied / family; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(data)
        for include in source.parent.glob('*.cginc'):
            path = copied / include.relative_to(bundle); path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(include.read_bytes()); pins[str(include.relative_to(bundle))] = digest(include.read_bytes())
    for path in HERE.iterdir():
        if path.suffix in ('.cs', '.shader', '.py'):
            shutil.copyfile(path, run / path.name)
    (run / 'source-pins.json').write_text(json.dumps(pins, indent=2) + '\n')
    report = {'scope': 'OFFLINE_SAME_TARGET_DEPTH_STAMP', 'production_mutation': False,
              'xr_executed': False, 'game_wide_coverage_proven': False, 'profiles': {}}
    code = 1
    removed = []
    try:
        for color in (('gamma', 'linear') if args.color_space == 'both' else (args.color_space,)):
            project = run / ('unity-' + color)
            assets = project / 'Assets'; (assets / 'Editor').mkdir(parents=True)
            shutil.copyfile(HERE / 'Fixture.cs', assets / 'Editor/DepthCoverageFixture.cs')
            shutil.copytree(copied, assets / 'Shaders')
            shutil.copyfile(HERE / 'DepthCoverage.shader', assets / 'Shaders/DepthCoverage.shader')
            opaque = (copied / families[0]).read_text()
            assert opaque.count('Shader "GloomhavenVR/HeadUnlit"') == 1
            for name, state, statement in (('Depthless', 'ZWrite Off', ''),
                                            ('Cutout', '', 'clip(owned.a - .5);')):
                changed = opaque.replace('Shader "GloomhavenVR/HeadUnlit"', 'Shader "DepthCoverageFixture/' + name + '"')
                changed = changed.replace('Cull [_Cull]', 'Cull [_Cull] ' + state)
                changed = changed.replace('return tex2D(_MainTex, i.uv) * _Color;',
                                          'fixed4 owned = tex2D(_MainTex, i.uv) * _Color; ' + statement + ' return owned;')
                (assets / 'Shaders/Head' / (name + '.shader')).write_text(changed)
                (run / (name + '.shader')).write_text(changed)
            for family, old, new in ((families[1], 'Blend SrcAlpha OneMinusSrcAlpha', 'Blend SrcAlpha OneMinusSrcAlpha, One OneMinusSrcAlpha'),
                                     (families[2], 'Blend SrcAlpha One', 'Blend SrcAlpha One, Zero One')):
                source = (copied / family).read_text(); assert source.count(old) == 1
                name = Path(family).stem
                changed = source.replace(old, new).replace('Shader "GloomhavenVR/' + name + '"', 'Shader "DepthCoverageFixture/' + name + '"')
                (assets / 'Shaders/Environments' / (name + 'Coverage.shader')).write_text(changed)
                (run / (name + 'Coverage.shader')).write_text(changed)
            (project / 'Packages').mkdir(); (project / 'Packages/manifest.json').write_text('{"dependencies":{}}\n')
            settings = project / 'ProjectSettings'; settings.mkdir()
            (settings / 'ProjectVersion.txt').write_text('m_EditorVersion: 2021.3.5f1\n')
            (settings / 'ProjectSettings.asset').write_text('%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!129 &1\nPlayerSettings:\n  m_ObjectHideFlags: 0\n  serializedVersion: 26\n  m_ActiveColorSpace: ' + ('1' if color == 'linear' else '0') + '\n')
            output = run / (color + '.json')
            command = ['xvfb-run', '-a', str(args.unity), '-batchmode', '-force-' + args.backend,
                       '-projectPath', str(project), '-executeMethod', 'DepthCoverageFixture.Start',
                       '-depthCoverageResult', str(output), '-logFile', str(run / (color + '.unity.log'))]
            env = os.environ.copy()
            if args.backend == 'vulkan' and Path('/usr/share/vulkan/icd.d/intel_icd.json').is_file():
                env['VK_DRIVER_FILES'] = '/usr/share/vulkan/icd.d/intel_icd.json'
            with (run / (color + '.launcher.log')).open('w') as stream:
                process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env=env, start_new_session=True)
                try: status = process.wait(timeout=240)
                except BaseException:
                    if process.poll() is None: os.killpg(process.pid, signal.SIGKILL)
                    process.wait(); raise
            if output.exists(): report['profiles'][color] = json.loads(output.read_text())
            assert status == 0 and output.is_file(), 'Actual Unity fixture failed: ' + str(run)
            assert report['profiles'][color]['passed']
            cleanup('steamlink-depth-coverage', 0, log, root, process_reaped=True)
            shutil.rmtree(project); removed.append(str(project))
        assert all(digest((bundle / name).read_bytes()) == value for name, value in pins.items()), 'Production input mutation'
        code = 0
    finally:
        report['exit_code'] = code
        (run / 'results.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
        receipt = cleanup('steamlink-depth-coverage', code, log, root, process_reaped=True)
        for project in run.glob('unity-*'):
            if project.is_dir() and not project.is_symlink(): shutil.rmtree(project); removed.append(str(project))
        (run / 'cleanup.json').write_text(json.dumps({'retention_api': receipt, 'owned_projects_removed': removed}, indent=2) + '\n')
        print('evidence: ' + str(run))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
