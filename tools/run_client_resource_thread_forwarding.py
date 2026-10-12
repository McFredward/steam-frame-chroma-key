#!/usr/bin/env python3
"""Check the actual final hidden ARM epoll entry with private pinned libc/QEMU.

The fixture dlopens the supplied own ELF in an unrelated, unprepared process.
It proves native epoll ABI/event/errno/forced-unwind behavior, not production
cold thread registration, resource acceptance, Frame operation or passthrough.
No download, installation, network, restart or native OpenVR initialization.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile

sys.dont_write_bytecode=True
import prepare_client_startup_observer as metadata
import run_import_native_forwarding as common

ENTRY='frame_client_startup_epoll_wait_entry'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer',required=True,type=Path)
    parser.add_argument('--qemu',required=True,type=Path)
    parser.add_argument('--providers',required=True,type=Path)
    parser.add_argument('--compiler',default='aarch64-linux-gnu-g++')
    parser.add_argument('--support-libraries',default=Path('/usr/aarch64-linux-gnu/lib'),type=Path)
    parser.add_argument('--record',required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    receipt={'status':'failed','frame_access':False,'network_access':False,'native_openvr':False,
             'headset':False,'production_ready':False,'cold_registration':False,'logs':{}}
    try:
        observer=args.observer.resolve(strict=True);qemu=args.qemu.resolve(strict=True)
        prefix=args.providers.resolve(strict=True);support=args.support_libraries.resolve(strict=True)
        data=metadata.frozen_read(observer);elf=metadata.Elf(data)
        public={s['name'] for s in elf.symbols if s['dynamic'] and s['defined'] and
                s['binding'] in (1,2) and s['visibility']==0}
        if public!={metadata.BRIDGE}:raise SystemExit('Actual observer must retain sole own public bridge')
        entries=[s for s in elf.symbols if s['name']==ENTRY and s['defined']]
        if (len(entries)!=1 or entries[0]['type']!=2 or
                (entries[0]['binding']!=0 and entries[0]['visibility']!=2) or entries[0]['size']!=156):
            raise SystemExit('Actual final ELF lacks exact private standard ARM epoll entry')
        entry=entries[0]
        spans=[p for p in elf.phdrs if p[0]==1 and p[1]&1 and
               p[3]<=entry['rva'] and entry['rva']+entry['size']<=p[3]+p[5]]
        if len(spans)!=1:raise SystemExit('Private epoll entry not in unique file-backed native RX segment')
        receipt['observer_sha256']=common.digest(observer);receipt['observer_bytes']=len(data)
        receipt['hidden_entry']={'name':ENTRY,'rva':entry['rva'],'bytes':entry['size']}
        receipt['qemu_sha256']=common.digest(qemu);receipt['providers']={}
        for name,pin in common.PINNED.items():
            actual=common.digest(prefix/'lib'/name)
            if actual!=pin:raise SystemExit('Refusing unreviewed native provider '+name)
            receipt['providers'][name]=actual
        clean=os.environ.copy()
        for name in list(clean):
            if name.startswith('GHVR_FRAME_') or name in ('LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH','QEMU_LD_PREFIX'):
                clean.pop(name,None)
        source=root/'tests/test_client_resource_thread_forwarding.cpp'
        receipt['source_sha256']={str(p.relative_to(root)):common.digest(p) for p in
            (source,root/'import/import_startup_audit.h',root/'tools/prepare_client_startup_observer.py',
             root/'tools/run_import_native_forwarding.py',Path(__file__).resolve())}
        with tempfile.TemporaryDirectory(prefix='frame-resource-thread-entry-') as folder:
            folder=Path(folder);folder.chmod(0o700)
            executable=folder/'actual-epoll-entry'
            command=shlex.split(args.compiler)+['-std=c++17','-O2','-g','-fno-omit-frame-pointer',
                '-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root),str(source),'-pthread','-ldl','-o',str(executable)]
            built=common.recorded_run(command,folder,receipt['logs'],'compile',60,clean)
            if built.returncode:raise SystemExit('ARM epoll fixture build failed: '+built.stderr[-3000:])
            receipt['fixture_elf_sha256']=common.digest(executable)
            output=folder/'unrelated-output.jsonl'
            command=[str(qemu),'-L',str(prefix),'-E','LD_LIBRARY_PATH='+str(prefix/'lib')+':'+str(support),
                     '-E','GHVR_FRAME_CLIENT_STARTUP_OUTPUT='+str(output),
                     str(executable),str(observer),str(prefix/'lib/libc.so.6'),hex(entry['rva'])]
            result=common.recorded_run(command,folder,receipt['logs'],'actual_final_epoll_entry',20,clean)
            if result.returncode:raise SystemExit('Actual ARM epoll entry failed: '+result.stdout[-3000:]+result.stderr[-3000:])
            values=[json.loads(line[7:]) for line in result.stdout.splitlines() if line.startswith('RESULT=')]
            if len(values)!=1 or output.exists() or 'CANCELLATION_BEGIN=epoll_wait' not in result.stdout.splitlines():
                raise SystemExit('Missing real cancellation/inert output evidence')
            proof=values[0]
            required_true=('actual_final_observer','actual_native_libc','real_epoll_event_delivery','epoll_forced_unwind')
            required_false=('public_libc_interposition','cold_registration','production_ready','native_openvr','frame_kernel','headset')
            if (not isinstance(proof.get('checks'),int) or proof['checks']<30 or
                    any(proof.get(name) is not True for name in required_true) or
                    any(proof.get(name) is not False for name in required_false)):
                raise SystemExit('Actual fixture omitted required ABI facts or exceeded its proof scope')
            receipt['result']=proof;receipt['status']='passed'
            print(json.dumps({'status':'passed','result':proof,'scope':'Actual final ARM entry ABI only; no cold receiver registration'}))
    except (SystemExit,OSError,ValueError) as error:
        receipt['failure']=str(error);raise
    finally:
        receipt['cleanup']='Owned compiler/QEMU groups reaped; own temporary executable removed; private inputs and final observer preserved'
        args.record.parent.mkdir(parents=True,exist_ok=True)
        args.record.write_text(json.dumps(receipt,indent=2)+'\n');args.record.chmod(0o600)


if __name__=='__main__':main()
