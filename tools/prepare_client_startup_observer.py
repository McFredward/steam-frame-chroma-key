#!/usr/bin/env python3
"""Prepare a user-directory-only client startup diagnostic bundle, OFFLINE.

Only own observer/audit ELF files and bounded metadata are packaged. Native
Valve bytes remain private. Nothing is installed, executed on Frame, restarted,
armed, uploaded or enabled; the result is a reviewable deployment proposal.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
import prepare_import_main_modules as modules
import prepare_import_module_identity as client
from run_import_native_forwarding import recorded_run

ROOT = Path(__file__).resolve().parents[1]
ENVELOPE_BYTES = 11800
MAX_FILE = modules.MAX_FILE
MAX_AUDIT = 256 * 1024
BRIDGE = 'frame_client_startup_bridge'
RESOURCE_SCOPE = 'frame_client_resource_census_scope'
RESOURCE_MARKER = b'FCCENS01' + struct.pack('<II', 3, ENVELOPE_BYTES)
RESOURCE_BOUNDARIES = {'begin': '0x3623c0', 'pending': '0x3624c4', 'reference': '0x362d58'}
CAPTURE_LIMITS = {'records': 32, 'bytes': 65536, 'seconds': 120}
ENTRIES = ('frame_import_vr_init_entry', 'frame_import_vr_shutdown_entry')
MUTEX_ENTRIES = ('frame_import_init_entry', 'frame_import_destroy_entry')
CONNECTION_ENTRIES = ('frame_import_lock_entry', 'frame_import_unlock_entry', 'frame_import_connect_entry')
CONNECTION_RVAS = {'lock':'0x87370','unlock':'0x88f74','connect':'0xebcb0'}
ALIASES = tuple('__' + name + '@GLIBC_2.17' for name in
                ('pthread_mutex_init','pthread_mutex_destroy','pthread_mutex_lock','pthread_mutex_unlock',
                 'write','close','read','connect'))
MAIN = 'libframe_client_startup.so'
AUDIT = 'libframe_client_startup_audit.so'
ENVELOPE = 'client-startup-envelope.bin'
NATIVE_PROFILES = tuple(bytes.fromhex(value) for value in (
    '2f5ccf52a5ebbb4b7d36a9508451247bd370b03c0c1b3a4d4b18dcb9322b655c',
    '9ef296865d2583cd8136303727db150f6e4a349ffe7b175e3e0f651adfb81dcf',
    '48edb257be746f755a00ff564c6db4284526b15ffc7e6bad5b8d6723b1891512',
    '51af1fa58396e7a1ab48760ce90d90e919db3e42653504ad550eb54e129a7405'))
CLIENT_PROFILE = bytes.fromhex('1da2390f9d65a5271f0750d9d5558e90716c35629f2f39583ba5aeef2d451b71')


def digest(data): return hashlib.sha256(data).digest()


def frozen_read(path, limit=MAX_FILE):
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 64 <= before.st_size <= limit:
            raise ValueError('Input must be a bounded regular ELF file')
        data = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    identity = lambda s: (s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns,s.st_mode,s.st_uid)
    if len(data) != before.st_size or identity(before) != identity(after):
        raise ValueError('Input changed during offline preparation')
    return data


class Elf:
    """Bounded ELF metadata only; never loads an input or emits native bytes."""
    def __init__(self, data):
        if not 64 <= len(data) <= MAX_FILE: raise ValueError('ELF size limit')
        h = struct.unpack_from('<16sHHIQQQIHHHHHH',data)
        if h[0][:7] != b'\x7fELF\x02\x01\x01' or h[1:4] != (3,183,1) or h[8:10] != (64,56) or h[11] != 64:
            raise ValueError('Expected ELF64 AArch64 shared/PIE input')
        self.data=data; self.header=h
        self.phdrs=[];self.sections=[]
        for base,number,size,maximum,target in ((h[5],h[10],56,16,self.phdrs),(h[6],h[12],64,4096,self.sections)):
            if not 0 < number <= maximum or base > len(data) or number*size > len(data)-base:
                raise ValueError('ELF tables outside bounds')
            for i in range(number):
                row=struct.unpack_from('<IIQQQQQQ' if size==56 else '<IIQQQQIIQQ',data,base+i*size)
                off,length=(row[2],row[5]) if size==56 else (row[4],row[5])
                if size==56 or row[1]!=8: # NOBITS has no file payload.
                    if off>len(data) or length>len(data)-off:raise ValueError('ELF payload outside bounds')
                target.append(row)
        self.symbols=self._symbols()
        self.needed=self._needed()

    def string(self, section, offset):
        if section[1]!=3 or offset>=section[5]:raise ValueError('Invalid ELF string table')
        data=self.data[section[4]:section[4]+section[5]]
        end=data.find(b'\0',offset,min(len(data),offset+512))
        if end<0:raise ValueError('Unterminated/oversize ELF string')
        try:return data[offset:end].decode('ascii')
        except UnicodeDecodeError as error:raise ValueError('Non-ASCII ELF symbol metadata') from error

    def linked(self, section):
        if section[6]>=len(self.sections):raise ValueError('Invalid ELF section link')
        return self.sections[section[6]]

    def _versions(self):
        versions={0:'',1:''}
        for section in self.sections:
            if section[1] not in (0x6ffffffe,0x6ffffffd):continue
            strings=self.linked(section);at=section[4];end=at+section[5];seen=0
            while at<end:
                seen+=1
                if seen>128:raise ValueError('ELF version count limit')
                if section[1]==0x6ffffffe:
                    if end-at<16:raise ValueError('Truncated ELF version need')
                    version,count,_,aux,next_=struct.unpack_from('<HHIII',self.data,at)
                    if version!=1 or not 0<count<=128:raise ValueError('ELF version need shape')
                    cursor=at+aux
                    for _ in range(count):
                        if cursor<at or cursor+16>end:raise ValueError('ELF version auxiliary bounds')
                        _,_,number,name,step=struct.unpack_from('<IHHII',self.data,cursor)
                        versions[number&0x7fff]=self.string(strings,name)
                        if not step:break
                        cursor+=step
                else:
                    if end-at<20:raise ValueError('Truncated ELF version definition')
                    version,_,number,count,_,aux,next_=struct.unpack_from('<HHHHIII',self.data,at)
                    if version!=1 or not count or at+aux+8>end:raise ValueError('ELF version definition shape')
                    name,_=struct.unpack_from('<II',self.data,at+aux)
                    versions[number&0x7fff]=self.string(strings,name)
                if not next_:break
                if next_<16 or at+next_<=at or at+next_>=end:raise ValueError('ELF version chain bounds')
                at+=next_
        return versions

    def _symbols(self):
        versions=self._versions();result=[]
        for section_index,section in enumerate(self.sections):
            if section[1] not in (2,11):continue
            if section[9]!=24 or section[5]%24 or section[5]//24>65536:raise ValueError('ELF symbol bounds')
            strings=self.linked(section)
            version_section=next((s for s in self.sections if s[1]==0x6fffffff and s[6]==section_index),None)
            if version_section and version_section[5]!=2*(section[5]//24):raise ValueError('ELF version table bounds')
            for i in range(section[5]//24):
                name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',self.data,section[4]+24*i)
                text=self.string(strings,name)
                version=''
                if version_section:
                    key=struct.unpack_from('<H',self.data,version_section[4]+2*i)[0]&0x7fff
                    if key not in versions:raise ValueError('Unknown ELF symbol version')
                    version=versions[key]
                result.append({'name':text,'version':version,'binding':info>>4,'type':info&15,'visibility':other&3,
                               'defined':index!=0,'rva':value,'size':size,'dynamic':section[1]==11})
        if not result:raise ValueError('ELF has no reviewed symbol metadata')
        return result

    def _needed(self):
        result=[]
        for section in self.sections:
            if section[1]!=6:continue
            if section[9]!=16 or section[5]%16:raise ValueError('ELF dynamic table shape')
            strings=self.linked(section)
            for at in range(section[4],section[4]+section[5],16):
                kind,value=struct.unpack_from('<qQ',self.data,at)
                if kind==1:result.append(self.string(strings,value))
        return result

    def rx(self, rva=None, size=1):
        spans=[(ph[3],ph[3]+ph[5]) for ph in self.phdrs if ph[0]==1 and ph[1]&1 and not ph[1]&2]
        if rva is not None:spans=[s for s in spans if s[0]<=rva and size>0 and rva+size<=s[1]]
        if len(spans)!=1:raise ValueError('Expected one exact executable-file interval')
        return spans[0]

    def exported(self):
        return [s for s in self.symbols if s['dynamic'] and s['defined'] and s['binding'] in (1,2) and s['visibility'] in (0,3)]

    def imports(self):
        return {s['name']+('@'+s['version'] if s['version'] else '') for s in self.symbols if s['dynamic'] and not s['defined'] and s['name']}

    def function(self,name,dynamic=None):
        matches=[s for s in self.symbols if s['name']==name and s['defined'] and s['type']==2 and (dynamic is None or s['dynamic']==dynamic)]
        identities={(s['rva'],s['size']) for s in matches}
        if len(identities)!=1:raise ValueError('Missing/ambiguous reviewed function '+name)
        value=matches[0];self.rx(value['rva'],value['size'])
        return value


def resource_marker(elf):
    """Authenticate the retained own opt-in object, never native private bytes."""
    symbols=[s for s in elf.symbols if s['name']==RESOURCE_SCOPE and s['defined']]
    identities={(s['rva'],s['size']) for s in symbols}
    if len(identities)!=1 or any(s['type']!=1 or s['visibility'] not in (0,2) or
                               (s['binding']!=0 and s['visibility']!=2) for s in symbols):
        raise ValueError('Missing/ambiguous hidden resource census opt-in object')
    rva,size=next(iter(identities))
    spans=[ph for ph in elf.phdrs if ph[0]==1 and ph[1]==4 and size==len(RESOURCE_MARKER) and
           ph[3]<=rva and rva+size<=ph[3]+ph[5]]
    if len(spans)!=1:raise ValueError('Resource census opt-in is not exact read-only file-backed metadata')
    offset=spans[0][2]+rva-spans[0][3]
    if elf.data[offset:offset+size]!=RESOURCE_MARKER:
        raise ValueError('Resource census opt-in magic/schema/size mismatch')
    return {'symbol':RESOURCE_SCOPE,'rva':rva,'bytes':size,'sha256':digest(RESOURCE_MARKER).hex()}


def validate_observer(data,reviewed_sha,provider_exports,*,connection_observation=False,resource_census=False):
    if type(connection_observation) is not bool or type(resource_census) is not bool or (resource_census and not connection_observation):
        raise ValueError('Resource census requires explicit connection observation')
    if not re.fullmatch('[0-9a-f]{64}',reviewed_sha or '') or digest(data).hex()!=reviewed_sha:
        raise ValueError('Main observer differs from separately reviewed SHA-256')
    elf=Elf(data)
    forbidden={'dlopen','dlmopen','dlsym','execve','execveat'}
    if any(s['name'].startswith('VR_') or s['name'] in forbidden for s in elf.exported()):
        raise ValueError('Observer invents or interposes an unsupported public API')
    # The connection bundle is inherited by the complete Steam service. Its
    # only public entry is our bridge; actual source-client libc calls enter
    # hidden veneers through the separately admitted audit binding.
    public={BRIDGE} if connection_observation else {BRIDGE,'pthread_mutex_init','pthread_mutex_destroy'}
    if {s['name'] for s in elf.exported()}!=public:
        raise ValueError('Observer exports an unexpected public entry')
    if any('openvr' in name or 'vrclient' in name for name in elf.needed):
        raise ValueError('Observer forces an API/client dependency')
    bridge=elf.function(BRIDGE,True)
    if bridge['binding']!=1 or bridge['visibility']!=0 or bridge['version']:
        raise ValueError('Own bridge is not an exported unversioned default-visible function')
    entries={};mutex_entries={};connection_entries={}
    for name in ENTRIES+MUTEX_ENTRIES+(CONNECTION_ENTRIES if connection_observation else ()):
        symbol=elf.function(name)
        if any(s['name']==name for s in elf.exported()) or not any(s['name']==name and (s['binding']==0 or s['visibility']==2) for s in elf.symbols):
            raise ValueError('Native startup entry is not hidden '+name)
        (entries if name in ENTRIES else mutex_entries if name in MUTEX_ENTRIES else connection_entries)[name]=symbol['rva']
    mutex_aliases={}
    pairs=[] if connection_observation else [('pthread_mutex_init','frame_import_init_entry'),('pthread_mutex_destroy','frame_import_destroy_entry')]
    for public,hidden in pairs:
        alias=elf.function(public,True);target=elf.function(hidden)
        if alias['binding']!=1 or alias['visibility']!=0 or alias['version'] or alias['rva']!=target['rva'] or alias['size']!=target['size']:
            raise ValueError('Public mutex entry is not an exact native-entry alias')
        mutex_aliases[public]=alias['rva']
    imports=elf.imports()
    if not set(ALIASES)<=imports:raise ValueError('Observer lacks an exact reviewed native alias import')
    for alias in ALIASES:
        name,version=alias.split('@')
        if not any(s['dynamic'] and not s['defined'] and s['name']==name and s['version']==version and
                   s['type']==2 and s['binding'] in (1,2) and s['visibility']==0 for s in elf.symbols):
            raise ValueError('Native alias lacks its public function import ABI')
    versioned={name for name in imports if '@GLIBC_' in name}
    if not versioned<=provider_exports:raise ValueError('Actual native provider lacks an observer GLIBC import')
    if '__tls_get_addr' in {name.split('@')[0] for name in imports}:raise ValueError('Dynamic TLS in startup observer')
    marker=resource_marker(elf) if resource_census else None
    if not resource_census and any(s['name']==RESOURCE_SCOPE for s in elf.symbols):
        raise ValueError('Resource census ELF requires its explicit offline scope')
    return elf,{'sha256':reviewed_sha,'bytes':len(data),'bridge_rva':bridge['rva'],'hidden_entries':entries,
                'hidden_mutex_entries':mutex_entries,
                'hidden_connection_entries':connection_entries,'connection_register_observation':connection_observation,
                'public_mutex_aliases':mutex_aliases,'audit_only_libc_routing':connection_observation,
                'resource_register_census':resource_census,'resource_scope_marker':marker,
                'resource_private_bytes_read':0,'resource_transaction_accepted':False,'resource_namespace_associated':False,
                'required_aliases':list(ALIASES),'versioned_glibc_imports':sorted(versioned)}


def export_versions(elf):
    return {s['name']+('@'+s['version'] if s['version'] else '') for s in elf.exported()}


def loader_name(value):
    if not isinstance(value,str) or not value or '\0' in value or any(ord(c)<32 for c in value):raise ValueError('Invalid exact client/API loader name')
    encoded=value.encode('utf-8')
    if len(encoded)>=512:raise ValueError('Exact loader name exceeds copied ClientSlot')
    return encoded


def user_directory(value,home):
    path=PurePosixPath(value);root=PurePosixPath(home)
    if not path.is_absolute() or str(path)!=value or '..' in path.parts or '..' in root.parts or str(root)!=home or not root.is_absolute() or root==PurePosixPath('/') or path==root or root not in path.parents:
        raise ValueError('Frame artifact directory must be normalized and strictly inside its user home')
    if any(ord(c)<32 or c in '\0"\\' for c in value):raise ValueError('Unsafe future artifact path')
    return path


def validate_profiles(blobs,client_blob,reviewed):
    if len(blobs)!=5 or len(reviewed)!=32:raise ValueError('Envelope requires all five module profiles and reviewed identity')
    for i,blob in enumerate(blobs):
        if len(blob)!=2032 or struct.unpack_from('<4Q',blob)!=(1,i+1,183,3) or digest(blob[:-32])!=blob[-32:]:
            raise ValueError('Malformed module profile')
        expected=NATIVE_PROFILES[i] if i<4 else reviewed
        if blob[-32:]!=expected:raise ValueError('Module profile differs from independently pinned descriptor')
    if len(client_blob)!=1064 or client_blob[-32:]!=CLIENT_PROFILE or digest(client_blob[:-32])!=CLIENT_PROFILE:
        raise ValueError('Client profile is not the canonical source descriptor')


def _assemble(blobs,client_blob,reviewed,name,rx,*,connection_observation=False,resource_census=False):
    if resource_census and not connection_observation:raise ValueError('Resource census requires explicit connection observation')
    raw=loader_name(name)
    if not 0<=rx[0]<rx[1]<2**64:raise ValueError('Client executable interval')
    magic,schema=(b'FCCENS01',3) if resource_census else (b'FCCONN01',2) if connection_observation else (b'FCSTART1',1)
    result=magic+struct.pack('<II',schema,ENVELOPE_BYTES)+reviewed+raw+bytes(512-len(raw))+struct.pack('<QQ',*rx)+b''.join(blobs)+client_blob
    if len(result)!=ENVELOPE_BYTES:raise ValueError('Envelope schema size changed')
    return result


def encode_envelope(blobs,client_blob,reviewed,name,rx,*,connection_observation=False,resource_census=False):
    validate_profiles(blobs,client_blob,reviewed)
    # Bind the copied ClientSlot geometry to the canonical descriptor itself,
    # not merely to a caller-supplied pair of plausible offsets.
    count=struct.unpack_from('<Q',client_blob,24)[0]
    spans=[]
    for index in range(count):
        ph=struct.unpack_from('<8Q',client_blob,264+64*index)
        if ph[0]==1 and ph[1]&1 and ph[3]<=client.CALLER_RVA<ph[3]+ph[5]:
            spans.append((ph[3],ph[3]+ph[5]))
    if spans!=[tuple(rx)]:raise ValueError('ClientSlot differs from the canonical caller-containing interval')
    return _assemble(blobs,client_blob,reviewed,name,rx,connection_observation=connection_observation,resource_census=resource_census)


def audit_source(receiver_rx,api_rx,observer_rx,client_rx,bridge,api_name,client_name,observer_path,libc_rx,libc_name,*,connection_observation=False):
    loader_name(api_name);loader_name(client_name);loader_name(libc_name)
    # Octal UTF-8 bytes preserve the exact public loader name, including names
    # JSON would represent as surrogate escapes invalid in a C++ literal.
    quoted=lambda name:'"'+''.join(c if 32<=ord(c)<=126 and c not in '"\\' else '\\%03o'%ord(c) for c in name.encode('utf-8').decode('latin1'))+'"'
    module=lambda name,rx:'{'+quoted(name)+f',{rx[0]},{rx[1]}'+'}'
    return ('#include "import/import_startup_audit.h"\nnamespace frame_import_startup_audit {\n'
            'const Preparation* preparation() noexcept {\nstatic const Preparation p = {'+
            ','.join((module('',receiver_rx),module(api_name,api_rx),module(observer_path,observer_rx)))+
            f',{bridge},0x5768,0x5330,true,'+module(client_name,client_rx)+','+module(libc_name,libc_rx)+
            ',0x86a00,0x86980'+(',0x87370,0x88f74,0xebcb0' if connection_observation else '')+'};\nreturn &p;\n}\n}\n')


def private_write(path,data):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'wb') as stream:stream.write(data)


def prepare(native_dir,observer,reviewed_sha,output,frame_directory,api_name,client_name,*,libc_name=None,home='/home/steamos',compiler='aarch64-linux-gnu-g++',api_file=None,connection_observation=False,resource_census=False):
    if resource_census and not connection_observation:raise ValueError('Resource census requires explicit connection observation')
    frame=user_directory(frame_directory,home);loader_name(api_name);loader_name(client_name);loader_name(libc_name)
    loader_name(str(frame/MAIN))
    if len({api_name,client_name,libc_name,str(frame/MAIN)})!=4:raise ValueError('Module loader-name collision')
    if output.exists() or output.is_symlink():raise ValueError('Offline output must be new; never overwrite a reviewed bundle')
    if native_dir.resolve() in output.resolve().parents or output.resolve()==native_dir.resolve():
        raise ValueError('Do not publish into borrowed native references')
    if api_file is not None and api_file.resolve().parent in output.resolve().parents:
        raise ValueError('Do not publish into borrowed API references')
    inputs={role:frozen_read(api_file if role==2 and api_file is not None else native_dir/name)
            for role,(name,_) in modules.NATIVE.items()}
    client_data=frozen_read(native_dir/'vrclient.so');main_data=frozen_read(observer)
    profiles=[];reports=[]
    for role in range(1,6):
        data=inputs[role] if role<5 else main_data
        blob,report=modules.profile_from_bytes(data,role,reviewed_sha if role==5 else None)
        profiles.append(blob);reports.append(report)
    client_blob,client_report=client.profile_from_bytes(client_data)
    # Authenticate borrowed native inputs before interpreting extra metadata.
    native_elfs={role:Elf(data) for role,data in inputs.items()}
    providers=export_versions(native_elfs[3])|export_versions(native_elfs[4])
    main_elf,main_report=validate_observer(main_data,reviewed_sha,providers,connection_observation=connection_observation,resource_census=resource_census)
    rx=Elf(client_data).rx(client.CALLER_RVA)
    reviewed=profiles[4][-32:]
    envelope=encode_envelope(profiles,client_blob,reviewed,client_name,rx,connection_observation=connection_observation,resource_census=resource_census)
    for name,rva,size in (('VR_InitInternal2',0x5768,236),('VR_ShutdownInternal',0x5330,192)):
        actual=native_elfs[2].function(name,True)
        if actual['rva']!=rva or actual['size']!=size:raise ValueError('Native public API schema changed')
    native_functions=[('pthread_mutex_init',0x86a00,556),('pthread_mutex_destroy',0x86980,56)]
    if connection_observation:native_functions += [('pthread_mutex_lock',0x87370,760),('pthread_mutex_unlock',0x88f74,20),('connect',0xebcb0,208)]
    for name,rva,size in native_functions:
        actual=native_elfs[3].function(name,True);alias=native_elfs[3].function('__'+name,True)
        if (actual['rva'],actual['size'],actual['version'])!=(rva,size,'GLIBC_2.17') or \
           (alias['rva'],alias['size'],alias['version'])!=(rva,size,'GLIBC_2.17'):
            raise ValueError('Native public mutex alias schema changed')
    if 'libopenvr_api.so' not in native_elfs[1].needed or 'vrclient.so' in native_elfs[1].needed:
        raise ValueError('Receiver startup dependency changed')
    source=audit_source(native_elfs[1].rx(),native_elfs[2].rx(),main_elf.rx(),rx,main_report['bridge_rva'],api_name,client_name,
                        str(frame/MAIN),native_elfs[3].rx(),libc_name,connection_observation=connection_observation)
    logs={};compiler_args=shlex.split(compiler)
    if not compiler_args:raise ValueError('Explicit cross compiler required')
    with tempfile.TemporaryDirectory(prefix='frame-client-startup-packaging-') as temporary:
        folder=Path(temporary);folder.chmod(0o700)
        source_path=folder/'audit-preparation.cpp';source_path.write_text(source)
        target=folder/AUDIT
        command=compiler_args+['-std=c++17','-O2','-shared','-fPIC','-nostdlib','-fvisibility=hidden','-fno-exceptions','-fno-rtti','-fno-builtin',
                              '-mno-outline-atomics','-Wl,--build-id','-I',str(ROOT),str(ROOT/'import/import_startup_audit.cpp'),str(source_path),'-o',str(target)]
        compile_environment={key:value for key,value in os.environ.items() if key not in
                             ('LD_PRELOAD','LD_AUDIT','GHVR_FRAME_CLIENT_STARTUP_ENVELOPE','GHVR_FRAME_CLIENT_STARTUP_OUTPUT')}
        built=recorded_run(command,folder,logs,'audit_compile',60,compile_environment)
        if built.returncode:
            # Preserve bounded diagnostic context even though a refused bundle
            # creates no output directory or final artifacts.
            raise ValueError('Own audit compilation failed: '+json.dumps(logs['audit_compile']))
        audit_data=frozen_read(target,MAX_AUDIT);audit_elf=Elf(audit_data)
        if audit_elf.imports() or audit_elf.needed:raise ValueError('Audit acquired an external provider')
        exports={s['name'] for s in audit_elf.exported()}
        if exports!={'la_version','la_objopen','la_preinit','la_symbind64','la_objclose'}:
            raise ValueError('Audit exports an unexpected API')
        # Publish only after all validation/compilation has succeeded. Partial
        # output, if a later disk write fails, remains a non-activating proposal.
        output.mkdir(mode=0o700,parents=True)
        artifacts={MAIN:main_data,AUDIT:audit_data,ENVELOPE:envelope,'audit-preparation.cpp':source.encode()}
        for name,data in artifacts.items():private_write(output/name,data)
        inventory=[{'name':name,'bytes':len(data),'sha256':digest(data).hex(),'frame_path':str(frame/name) if name!='audit-preparation.cpp' else None,
                    'owned_source':True,'native_bytes':False,'mode':'0600'} for name,data in artifacts.items()]
        plan={'schema':'frame-client-startup-offline-bundle-v1','prepared':True,'installed':False,'activated':False,'runtime_authorized':False,
              'frame_access':False,'network_access':False,'native_binary_or_script_changes':False,'source_frame_associated':False,'gpu_coherent':False,
              'frame_directory':str(frame),'frame_user_home':home,'directory_mode':'0700','observer':main_report,'audit':{'bytes':len(audit_data),'sha256':digest(audit_data).hex(),'imports':[],
              'native_api_rvas':{'init':'0x5768','shutdown':'0x5330'},
              'native_connection_rvas':CONNECTION_RVAS if connection_observation else {},
              'native_mutex_rvas':{'init':'0x86a00','destroy':'0x86980'},'default_inactive_without_prepared_tu':True},
              'connection_register_observation':connection_observation,
              'connection_private_bytes_read':0,'continuous_connection_owned':False,
              'resource_register_census':resource_census,'resource_private_bytes_read':0,
              'resource_transaction_accepted':False,'resource_namespace_associated':False,
              'resource_source_boundaries':RESOURCE_BOUNDARIES if resource_census else {},
              'capture_limits':dict(CAPTURE_LIMITS),
              'envelope':{'bytes':len(envelope),'sha256':digest(envelope).hex(),'schema':3 if resource_census else 2 if connection_observation else 1,'reviewed_observer_profile_identity':reviewed.hex(),'module_count':5},
              'client_slot':{'exact_loader_name':client_name,'rx_rva_begin':rx[0],'rx_rva_end':rx[1],'canonical_profile_sha256':client_blob[-32:].hex()},
              'libc_slot':{'exact_loader_name':libc_name,'rx_rva_begin':native_elfs[3].rx()[0],'rx_rva_end':native_elfs[3].rx()[1]},
              'loader_name_provenance':'Explicit expected raw names supplied for offline preparation; not observed link-map lifetimes or deployment authorization',
              'native_profiles':reports[:4],'observer_profile':reports[4],'client_profile':client_report,
              'environment':{'LD_PRELOAD':str(frame/MAIN),'LD_AUDIT':str(frame/AUDIT),'GHVR_FRAME_CLIENT_STARTUP_ENVELOPE':str(frame/ENVELOPE),
                             'GHVR_FRAME_CLIENT_STARTUP_OUTPUT':str(frame/'client-startup.jsonl')},
              'inventory':inventory,'local_receipt':{'name':'deployment-plan.json','mode':'0600','frame_path':None,
                                                    'scope':'This metadata receipt is not a deployed artifact'},
              'audit_source_sha256':digest(source.encode()).hex(),'audit_compile_command':command,'logs':logs,
              'scope':['public init/shutdown forwarding','local mapped module identity','source-verified generic native pipe birth',
                       *(['actual supported connect-entry register snapshot and original result under observed native lock; no extra private memory reads'] if connection_observation else []),
                       *(['actual source begin/pending/reference boundary registers and original results only; no resource transaction or namespace accepted'] if resource_census else []),
                       'copied lifecycle/refusal facts; no pixels, shader replacement, endpoint pairing, mask frame, UV or GPU proof'],
              'before_activation':['Fresh actual runtime provider/receiver hashes and loader names','Separately reviewed final observer source/ELF and fingerprints',
                                   'Exact owned user-directory files; no foreign preload/audit/service override','Explicit permission and coordinated service restart readiness'],
              'rollback':['Remove only the future session manager-owned user-service environment override matching its recorded bytes',
                          'Restart the original Steam user service only with maintainer readiness; verify no observer/audit mappings remain',
                          'Archive compact capture/status and restore original environment; remove only inventory-owned staging files',
                          'Never modify native scripts, system images, global Vulkan settings or another session files'],
              'evidence_limit':'Offline preparation only. Not a Frame run or completed native-mask/Steam Link transport.'}
        private_write(output/'deployment-plan.json',(json.dumps(plan,indent=2)+'\n').encode())
        return plan


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir',type=Path,required=True)
    parser.add_argument('--api',type=Path,help='Explicit private Steam-runtime API file; the role-2 hard pin remains required')
    parser.add_argument('--observer',type=Path,required=True)
    parser.add_argument('--observer-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--frame-directory',required=True)
    parser.add_argument('--frame-user-home',default='/home/steamos')
    parser.add_argument('--api-loader-name',required=True)
    parser.add_argument('--client-loader-name',required=True)
    parser.add_argument('--libc-loader-name',required=True,help='Explicit expected raw libc loader name; kernel-map pathname alone is not a link-map proof')
    parser.add_argument('--compiler',default='aarch64-linux-gnu-g++')
    parser.add_argument('--connection-observation',action='store_true',help='Separate offline scope: register-only supported connect calls; never continuous FD ownership')
    parser.add_argument('--resource-census',action='store_true',help='Additional explicit offline scope: source resource-boundary registers/results only; requires --connection-observation')
    args=parser.parse_args()
    try:
        plan=prepare(args.native_dir,args.observer,args.observer_sha256,args.output,args.frame_directory,args.api_loader_name,args.client_loader_name,
                     libc_name=args.libc_loader_name,home=args.frame_user_home,compiler=args.compiler,api_file=args.api,
                     connection_observation=args.connection_observation,resource_census=args.resource_census)
    except (OSError,ValueError,struct.error) as error:parser.exit(1,'Offline bundle refused: '+str(error)+'\n')
    print(json.dumps({'prepared':True,'installed':False,'runtime_authorized':False,'envelope_sha256':plan['envelope']['sha256'],
                      'observer_sha256':plan['observer']['sha256'],'audit_sha256':plan['audit']['sha256']}))


if __name__=='__main__':main()
