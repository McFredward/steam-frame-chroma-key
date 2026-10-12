#!/usr/bin/env python3
"""Execute the actual final ARM64 observer ELF with pinned libc under QEMU.

The controlled executable is deliberately unrelated to vrlink. It checks inert
loading and exact libc forwarding, not production startup, a Frame kernel or GPU.
No network, installation, restart, native OpenVR initialization or activation.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile

sys.dont_write_bytecode=True
import run_import_native_forwarding as common


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer',required=True,type=Path)
    parser.add_argument('--qemu',required=True,type=Path)
    parser.add_argument('--providers',required=True,type=Path)
    parser.add_argument('--compiler',default='aarch64-linux-gnu-g++')
    parser.add_argument('--record',required=True,type=Path)
    parser.add_argument('--connection-observation',action='store_true',help='Verify all five entries in the actual final optional ARM ELF')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    receipt={'status':'failed','frame_access':False,'native_openvr':False,'headset':False,
             'production_ready':False,'logs':{},'cases':{}}
    try:
        observer=args.observer.resolve(strict=True);qemu=args.qemu.resolve(strict=True)
        prefix=args.providers.resolve(strict=True)
        data=observer.read_bytes()
        if len(data)>8*1024*1024 or data[:7]!=b'\x7fELF\x02\x01\x01' or data[16:20]!=b'\x03\x00\xb7\x00':
            raise SystemExit('Expected actual bounded ARM64 observer ELF')
        receipt['observer_sha256']=common.digest(observer);receipt['qemu_sha256']=common.digest(qemu)
        receipt['providers']={}
        for name,pin in common.PINNED.items():
            actual=common.digest(prefix/'lib'/name)
            if actual!=pin:raise SystemExit('Refusing unreviewed native provider '+name)
            receipt['providers'][name]=actual
        clean=os.environ.copy()
        for name in list(clean):
            if name.startswith('GHVR_FRAME_CLIENT_STARTUP_') or name in ('LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH','QEMU_LD_PREFIX'):
                clean.pop(name,None)
        source=root/'tests/test_client_startup_preload.cpp'
        receipt['source_sha256']={str(p.relative_to(root)):common.digest(p) for p in
            (source,root/'import/import_startup_audit.h',Path(__file__).resolve())}
        with tempfile.TemporaryDirectory(prefix='frame-client-preload-') as folder:
            folder=Path(folder);folder.chmod(0o700)
            executable=folder/'inert-preload'
            build=common.recorded_run(shlex.split(args.compiler)+['-std=c++17','-O2','-Wall','-Wextra','-Werror',
                '-I',str(root),str(source),'-pthread','-ldl','-o',str(executable)],folder,receipt['logs'],'compile',60,clean)
            if build.returncode:raise SystemExit('ARM preload fixture build failed: '+build.stderr[-3000:])
            bad=folder/'bad-envelope.bin';bad.write_bytes(bytes(11800));bad.chmod(0o600)
            fifo=folder/'fifo-envelope';os.mkfifo(fifo,0o600)
            cases={'no_profile':{},'invalid_profile':{'GHVR_FRAME_CLIENT_STARTUP_ENVELOPE':str(bad)},
                   'fifo_profile':{'GHVR_FRAME_CLIENT_STARTUP_ENVELOPE':str(fifo)}}
            for name,extra in cases.items():
                output=folder/(name+'.jsonl')
                values={'LD_PRELOAD':str(observer),'LD_LIBRARY_PATH':str(prefix/'lib')+':/usr/aarch64-linux-gnu/lib',
                        'GHVR_FRAME_CLIENT_STARTUP_OUTPUT':str(output),**extra}
                command=[str(qemu),'-L',str(prefix)]
                for key,value in values.items():command+=['-E',key+'='+value]
                command += [str(executable),str(observer),str(prefix/'lib/libc.so.6')]
                if args.connection_observation:command += ['--connection']
                result=common.recorded_run(command,folder,receipt['logs'],name,15,clean)
                if result.returncode:raise SystemExit(name+' failed: '+result.stdout[-3000:]+result.stderr[-3000:])
                results=[json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith('RESULT=')]
                expected_checks=29 if args.connection_observation else 21
                if len(results)!=1 or results[0].get('checks')!=expected_checks or output.exists():
                    raise SystemExit('Incomplete inert/preload or unexpected output evidence: '+name)
                receipt['cases'][name]=results[0]
            receipt['status']='passed'
            print(json.dumps({'status':'passed','cases':receipt['cases'],'production_ready':False}))
    finally:
        receipt['cleanup']='Owned compiler/QEMU groups reaped; temporary executable and inputs removed; bounded receipt retained'
        args.record.parent.mkdir(parents=True,exist_ok=True)
        args.record.write_text(json.dumps(receipt,indent=2)+'\n');args.record.chmod(0o600)


if __name__=='__main__':main()
