#!/usr/bin/env python3
"""Real, controlled ELF audit/preload fixtures; never execute native OpenVR init.

An optional explicit QEMU/private-provider pair repeats these own-source ELF
fixtures under AArch64 and the already supplied Frame libc/loader. No network,
Frame access, system package changes or production observer activation occurs.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'import/import_startup_audit.h'
CORE = ROOT / 'import/import_startup_audit.cpp'

API = r'''
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
extern "C" void fixture_event(unsigned, std::uintptr_t, std::uintptr_t, std::uintptr_t);
static void* client;
extern "C" std::uint32_t VR_InitInternal2(std::uint32_t* error, std::uint32_t type, const char* startup) {
    fixture_event(1, reinterpret_cast<std::uintptr_t>(error), type, reinterpret_cast<std::uintptr_t>(startup));
    if (startup[0] == 't') throw 31;
    client = dlopen(CLIENT_FILE, RTLD_NOW);
    if (!client) std::abort();
    *error = 17; errno = 83; return 0x11223344;
}
extern "C" void VR_ShutdownInternal() {
    fixture_event(3, 0, 0, 0);
    if (client) { dlclose(client); client = nullptr; }
    errno = 89;
}
'''
CLIENT = r'''
#include <cstdint>
extern "C" void fixture_event(unsigned, std::uintptr_t, std::uintptr_t, std::uintptr_t);
__attribute__((constructor)) static void initialize() { fixture_event(2, 0, 0, 0); }
__attribute__((destructor)) static void finalize() { fixture_event(4, 0, 0, 0); }
'''
OBSERVER = r'''
#include "import/import_startup_audit.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
using namespace frame_import_startup_audit;
using Init = std::uint32_t(*)(std::uint32_t*, std::uint32_t, const char*);
using Shutdown = void(*)();
static Init original_init;
static Shutdown original_shutdown;
static bool owner;
static unsigned prepares, binds, befores, afters, retires;
static std::uintptr_t caller;
static BoundProvider attached;
static __thread unsigned active;
__attribute__((constructor)) static void initialize() {
    const int saved = errno;
    char path[1024]; const auto length = readlink("/proc/self/exe", path, sizeof(path)-1);
    if (length > 0) { path[length] = 0; owner = !std::strcmp(path, RECEIVER_FILE); }
    // Real constructor-time presence checks, not a forced API/client load.
    original_init = reinterpret_cast<Init>(dlsym(RTLD_NEXT, "VR_InitInternal2"));
    original_shutdown = reinterpret_cast<Shutdown>(dlsym(RTLD_NEXT, "VR_ShutdownInternal"));
    errno = saved;
}
struct Scope { Scope() { ++active; } ~Scope() { --active; } };
extern "C" __attribute__((visibility("hidden")))
std::uint32_t fixture_owned_init(std::uint32_t* error, std::uint32_t type, const char* startup) {
    caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    ++befores; Scope scope;
    const auto value = original_init(error, type, startup); ++afters; return value;
}
extern "C" __attribute__((visibility("hidden"))) void fixture_owned_shutdown() {
    ++befores; original_shutdown(); ++afters;
}
extern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_bridge(BridgeRequest request) noexcept {
    const int saved = errno;
    BridgeResponse result{};
    if (request.stage == Stage::Retire) { ++retires; }
    else if (request.stage == Stage::Prepare) {
        ++prepares;
        if (owner && original_init && original_shutdown && !request.missed_before_preinit) {
            attached = request.provider; result.ready = true;
        }
    } else {
        ++binds;
        const auto native = request.provider.function == Function::Init ? reinterpret_cast<std::uintptr_t>(original_init)
                                                                      : reinterpret_cast<std::uintptr_t>(original_shutdown);
        result.ready = owner && request.provider.original == native &&
            request.provider.receiver.bias == attached.receiver.bias && request.provider.api.bias == attached.api.bias &&
            request.provider.observer.bias == attached.observer.bias && request.provider.api.observed_open_generation == attached.api.observed_open_generation;
        result.hidden_entry = request.provider.function == Function::Init ? reinterpret_cast<std::uintptr_t>(&fixture_owned_init)
                                                                         : reinterpret_cast<std::uintptr_t>(&fixture_owned_shutdown);
        if (std::getenv("FIXTURE_BAD_ENTRY")) result.hidden_entry = native;
    }
    errno = saved; return result;
}
extern "C" unsigned fixture_probe(unsigned which) {
    switch (which) { case 0: return active; case 1: return prepares; case 2: return binds;
        case 3: return befores; case 4: return afters; case 5: return retires;
        case 6: return owner; case 7: return original_init && original_shutdown; default: return 0; }
}
extern "C" std::uintptr_t fixture_address(unsigned which) {
    return which == 0 ? reinterpret_cast<std::uintptr_t>(original_init) : which == 1 ? reinterpret_cast<std::uintptr_t>(original_shutdown) : caller;
}
'''
MAIN = r'''
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
extern "C" std::uint32_t VR_InitInternal2(std::uint32_t*, std::uint32_t, const char*);
extern "C" void VR_ShutdownInternal();
extern "C" std::uint32_t fixture_call_init(std::uint32_t*, std::uint32_t, const char*);
extern "C" void fixture_expected_return();
#if defined(__aarch64__)
__asm__(R"(
.text
.global fixture_call_init
.hidden fixture_call_init
.type fixture_call_init,%function
fixture_call_init:
.cfi_startproc
stp x29,x30,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 29,-16
.cfi_offset 30,-8
mov x29,sp
bl VR_InitInternal2
.global fixture_expected_return
.hidden fixture_expected_return
fixture_expected_return:
ldp x29,x30,[sp],#16
.cfi_def_cfa_offset 0
.cfi_restore 29
.cfi_restore 30
ret
.cfi_endproc
)");
#else
__asm__(R"(
.text
.global fixture_call_init
.hidden fixture_call_init
.type fixture_call_init,@function
fixture_call_init:
.cfi_startproc
push %rbp
.cfi_def_cfa_offset 16
.cfi_offset %rbp,-16
mov %rsp,%rbp
call VR_InitInternal2@PLT
.global fixture_expected_return
.hidden fixture_expected_return
fixture_expected_return:
pop %rbp
.cfi_def_cfa_offset 8
.cfi_restore %rbp
ret
.cfi_endproc
)");
#endif
using Probe = unsigned(*)(unsigned);
using Address = std::uintptr_t(*)(unsigned);
unsigned checks, native_calls, constructor_calls, finalizer_calls, retired_at_finalizer;
bool expected_redirect;
std::uint32_t* expected_error;
const char* expected_startup;
void require(bool yes, const char* text) { ++checks; if (!yes) { std::fprintf(stderr,"CHECK %u: %s\n",checks,text); std::abort(); } }
Probe probe() { return reinterpret_cast<Probe>(dlsym(RTLD_DEFAULT,"fixture_probe")); }
extern "C" void fixture_event(unsigned kind, std::uintptr_t error, std::uintptr_t type, std::uintptr_t startup) {
    const int saved = errno;
    const auto query = probe();
    if (kind == 1) {
        ++native_calls;
        require(error == reinterpret_cast<std::uintptr_t>(expected_error) && type == 1 && startup == reinterpret_cast<std::uintptr_t>(expected_startup),"native argument identity changed");
        require(saved == 23,"native incoming errno changed");
    }
    if (kind == 2) ++constructor_calls;
    if (kind == 4) { ++finalizer_calls; retired_at_finalizer = query ? query(5) : 99; }
    if (kind == 1 || kind == 2) {
        if (!query || bool(query(0)) != expected_redirect)
            std::fprintf(stderr,"AUDIT_STATE expected=%d active=%u prepare=%u bind=%u owner=%u providers=%u\n",expected_redirect,
                query?query(0):99,query?query(1):99,query?query(2):99,query?query(6):99,query?query(7):99);
        require(query && bool(query(0)) == expected_redirect,"client constructor/provider used wrong namespace gate");
    }
    errno = saved;
}
#ifdef CONSTRUCTOR_BIND
__attribute__((constructor)) static void early() {
    std::uint32_t error; expected_error=&error; expected_startup="fixture-start"; errno=23;
    require(VR_InitInternal2(&error,1,expected_startup)==0x11223344 && error==17,"early native call changed");
    VR_ShutdownInternal();
}
#endif
int main(int argc,char** argv) {
    expected_redirect = argc>1 && !std::strcmp(argv[1],"redirect");
    auto query=probe(); auto address=reinterpret_cast<Address>(dlsym(RTLD_DEFAULT,"fixture_address"));
    require(query && address,"own-source fixture observer missing");
    const auto before=query(3), after=query(4), prior_native=native_calls, prior_ctor=constructor_calls, prior_finalizer=finalizer_calls;
    const auto init=dlsym(RTLD_DEFAULT,"VR_InitInternal2"), shutdown=dlsym(RTLD_DEFAULT,"VR_ShutdownInternal");
    require(init && shutdown && reinterpret_cast<std::uintptr_t>(init)==address(0) && reinterpret_cast<std::uintptr_t>(shutdown)==address(1),"DLSYM API presence/pointer changed");
    std::uint32_t error; expected_error=&error; expected_startup="fixture-start"; errno=23;
    require(fixture_call_init(&error,1,expected_startup)==0x11223344 && error==17 && errno==83,"native token/error/errno changed");
    require(native_calls==prior_native+1 && constructor_calls==prior_ctor+1,"natural client initializer missing");
    if (expected_redirect) require(address(2)==reinterpret_cast<std::uintptr_t>(&fixture_expected_return),"audit PLT trampoline changed original incoming caller LR");
    expected_startup="throw"; errno=23; bool caught=false;
    try { (void)fixture_call_init(&error,1,expected_startup); } catch (int value) { caught=value==31; }
    require(caught && query(0)==0,"native exception/gate cleanup changed");
    require(query(3)==before+(expected_redirect?2:0) && query(4)==after+(expected_redirect?1:0),"Before/After forwarding changed");
    errno=29; VR_ShutdownInternal(); require(errno==89,"shutdown errno changed");
    require(finalizer_calls==prior_finalizer+1,"client finalizer did not complete");
    if (expected_redirect) require(query(5)>retired_at_finalizer,"client retirement was not copied after finalizer/before unmap");
    if (argc>2) {
        const auto child=fork(); require(child>=0,"fork failed");
        if (!child) { execl(argv[2],argv[2],nullptr); _exit(127); }
        int status; require(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"exec child inherited invented API presence");
    }
    std::printf("RESULT={\"checks\":%u,\"redirected\":%s,\"bindings\":%u,\"retirements\":%u,\"exact_caller_lr\":%s}\n",checks,expected_redirect?"true":"false",query(2),query(5),expected_redirect?"true":"false");
}
'''
MISSING = r'''
#include <dlfcn.h>
#include <cstdio>
int main() {
    if (dlsym(RTLD_DEFAULT,"VR_InitInternal2") || dlsym(RTLD_DEFAULT,"VR_ShutdownInternal")) return 9;
    std::puts("NO_API=unchanged"); return 0;
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path)
    parser.add_argument('--providers',type=Path)
    parser.add_argument('--record',type=Path)
    parser.add_argument('--case',action='append',choices=('normal','wrong_process','missing_api','eager_now',
        'environment_now','constructor_binding','bad_owned_entry','bad_native_rva','inactive_default','weak_default_inactive'),
        help='focused case selection; the receipt records its limited scope')
    args=parser.parse_args()
    if bool(args.qemu)!=bool(args.providers): parser.error('explicit QEMU and providers must be supplied together')
    helper_path=ROOT/'tools/run_import_native_forwarding.py'
    spec=importlib.util.spec_from_file_location('owned_runner',helper_path)
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    report={'status':'failed','frame_access':False,'native_openvr_init_executed':False,'source_sha256':{'core':sha(CORE),'header':sha(HEADER),'fixture':sha(Path(__file__).resolve())},'architectures':{}}
    report['selected_cases']=args.case or 'complete own-source audit fixture'
    clean=os.environ.copy()
    for name in ('LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH','QEMU_LD_PREFIX'): clean.pop(name,None)
    try:
        architectures=[('host','g++',None)]
        if args.qemu:
            prefix=args.providers.resolve(strict=True)
            for name,pin in helper.PINNED.items():
                if sha(prefix/'lib'/name)!=pin: raise SystemExit('Private provider mismatch')
            architectures.append(('aarch64','aarch64-linux-gnu-g++',args.qemu.resolve(strict=True)))
            report['providers']={name:sha(prefix/'lib'/name) for name in helper.PINNED}
            report['qemu_sha256']=sha(args.qemu.resolve(strict=True))
        for arch,compiler,qemu in architectures:
            logs={};cases={};report['architectures'][arch]={'logs':logs,'cases':cases}
            with tempfile.TemporaryDirectory(prefix='frame-startup-audit-'+arch+'-') as name:
                folder=Path(name);folder.chmod(0o700)
                files={key:folder/(key+'.cpp') for key in ('api','client','observer','main','missing')}
                for key,source in zip(files,(API,CLIENT,OBSERVER,MAIN,MISSING)):files[key].write_text(source)
                outputs={key:folder/(('lib'+key+'.so') if key in ('api','client','observer') else key) for key in files}
                common=[compiler,'-std=c++17','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-I',str(ROOT)]
                def compile_one(label,command):
                    value=helper.recorded_run(command,folder,logs,label+'_compile',45,clean)
                    logs[label+'_compile']['argv']=command
                    if value.returncode: raise SystemExit(value.stderr)
                compile_one('client',common+['-shared','-fPIC',str(files['client']),'-o',str(outputs['client'])])
                compile_one('api',common+['-shared','-fPIC','-DCLIENT_FILE='+json.dumps(str(outputs['client'])),str(files['api']),'-ldl','-o',str(outputs['api'])])
                compile_one('observer',common+['-shared','-fPIC','-ftls-model=initial-exec','-DRECEIVER_FILE='+json.dumps(str(outputs['main'])),str(files['observer']),'-ldl','-o',str(outputs['observer'])])
                # Match the pinned receiver's lazy PLT, overriding the host
                # distro compiler's default NOW hardening. NOW is a separate
                # explicit refusal control, never silently changed on native.
                executable_args=['-rdynamic','-Wl,-z,lazy',str(files['main']),'-L',str(folder),'-Wl,-rpath,'+str(folder),'-lapi','-ldl']
                compile_one('main',common+executable_args+['-o',str(outputs['main'])])
                compile_one('wrong',common+executable_args+['-o',str(folder/'wrong')])
                compile_one('now',common+executable_args+['-Wl,-z,now','-o',str(folder/'now')])
                compile_one('constructor',common+['-DCONSTRUCTOR_BIND']+executable_args+['-o',str(folder/'constructor')])
                compile_one('missing',common+[str(files['missing']),'-ldl','-o',str(outputs['missing'])])
                def geometry(path):
                    output=subprocess.check_output(['readelf','-lW',str(path)],text=True)
                    ranges=[]
                    for line in output.splitlines():
                        columns=line.split()
                        if columns and columns[0]=='LOAD' and 'E' in columns[6:-1]: ranges.append((int(columns[2],16),int(columns[2],16)+int(columns[5],16)))
                    if len(ranges)!=1: raise SystemExit('Expected one own-source RX segment')
                    return ranges[0]
                def symbol(path,wanted):
                    for line in subprocess.check_output(['nm','--defined-only',str(path)],text=True).splitlines():
                        columns=line.split()
                        if len(columns)>=3 and columns[-1]==wanted:return int(columns[0],16)
                    raise SystemExit('Missing own-source symbol '+wanted)
                rx={key:geometry(outputs[key]) for key in ('main','api','observer','client')}
                bridge=symbol(outputs['observer'],'fixture_bridge');init=symbol(outputs['api'],'VR_InitInternal2');shutdown=symbol(outputs['api'],'VR_ShutdownInternal')
                config=folder/'configuration.cpp'
                def configure(init_offset=init,enabled=True):
                    config.write_text('#include "import/import_startup_audit.h"\nnamespace frame_import_startup_audit {\nconst Preparation* preparation() noexcept {\nstatic const Preparation p = {'+
                        '{"",'+str(rx['main'][0])+','+str(rx['main'][1])+'},'+
                        '{'+json.dumps(str(outputs['api']))+','+str(rx['api'][0])+','+str(rx['api'][1])+'},'+
                        '{'+json.dumps(str(outputs['observer']))+','+str(rx['observer'][0])+','+str(rx['observer'][1])+'},'+
                        f'{bridge},{init_offset},{shutdown},'+('true' if enabled else 'false')+','+
                        '{'+json.dumps(str(outputs['client']))+','+str(rx['client'][0])+','+str(rx['client'][1])+'}};\nreturn &p;}\n}\n')
                audit=folder/'libaudit.so';configure()
                audit_args=common+['-shared','-fPIC','-nostdlib','-fvisibility=hidden','-fno-exceptions','-fno-rtti','-fno-builtin']
                if qemu:audit_args+=['-mno-outline-atomics']
                def compile_audit(label,target):compile_one(label,audit_args+[str(CORE),str(config),'-o',str(target)])
                compile_audit('audit',audit)
                report['architectures'][arch]['main_api_relocations']=[line for line in subprocess.check_output(['readelf','-rW',str(outputs['main'])],text=True).splitlines() if 'VR_' in line]
                report['architectures'][arch]['main_dynamic_flags']=[line for line in subprocess.check_output(['readelf','-dW',str(outputs['main'])],text=True).splitlines() if 'FLAGS' in line]
                undefined=subprocess.check_output(['nm','-u',str(audit)],text=True)
                if undefined.strip():raise SystemExit('Audit DSO unexpectedly imports a provider: '+undefined)
                exported=subprocess.check_output(['nm','-D','--defined-only',str(audit)],text=True)
                observer_exports=subprocess.check_output(['nm','-D','--defined-only',str(outputs['observer'])],text=True)
                if 'VR_InitInternal2' in exported+observer_exports or 'VR_ShutdownInternal' in exported+observer_exports:raise SystemExit('Invented API export')
                def run_case(label,program,mode=None,extra=(),overlay=None,selected_audit=audit,expected_failure=False):
                    if args.case and label not in args.case:return
                    env_values={'LD_AUDIT':str(selected_audit),'LD_PRELOAD':str(outputs['observer']),'LD_LIBRARY_PATH':str(folder)}
                    env_values.update(overlay or {})
                    if os.environ.get('FRAME_AUDIT_FIXTURE_DEBUG'):env_values['LD_DEBUG']='bindings'
                    if qemu:
                        env_values['LD_LIBRARY_PATH']=str(prefix/'lib')+':'+str(folder)+':/usr/aarch64-linux-gnu/lib'
                        command=[str(qemu),'-L',str(prefix)]
                        for key,value in env_values.items():command+=['-E',key+'='+value]
                        command+=[str(program)]
                        # QEMU does not binfmt-register ARM64 child execs. The
                        # own executable still verifies missing APIs separately.
                        if extra:extra=()
                        environment=clean
                    else:
                        command=[str(program)];environment={**clean,**env_values}
                    if mode:command.append(mode)
                    command+=list(map(str,extra))
                    value=helper.recorded_run(command,folder,logs,label,15,environment)
                    logs[label]['argv']=command
                    logs[label]['target_loader_environment']=env_values
                    if bool(value.returncode)!=expected_failure:
                        details=value.stderr
                        if os.environ.get('FRAME_AUDIT_FIXTURE_DEBUG'):details='\n'.join(line for line in details.splitlines() if 'VR_' in line or 'AUDIT_STATE' in line or 'CHECK ' in line)
                        raise SystemExit(label+' unexpected status: '+value.stdout+details)
                    if expected_failure and 'client constructor/provider used wrong namespace gate' not in value.stderr:
                        raise SystemExit(label+' failed before the intended semantic negative control')
                    cases[label]={'exit_status':value.returncode,'expected_failure':expected_failure,'stdout':value.stdout[-2048:]}
                run_case('normal',outputs['main'],'redirect',(outputs['missing'],))
                run_case('wrong_process',folder/'wrong','native')
                run_case('missing_api',outputs['missing'])
                run_case('eager_now',folder/'now','native')
                run_case('environment_now',outputs['main'],'native',overlay={'LD_BIND_NOW':'1'})
                run_case('constructor_binding',folder/'constructor','native')
                run_case('bad_owned_entry',outputs['main'],'redirect',overlay={'FIXTURE_BAD_ENTRY':'1'},expected_failure=True)
                configure(init+1);bad_audit=folder/'libaudit-bad-rva.so';compile_audit('bad_rva',bad_audit)
                run_case('bad_native_rva',outputs['main'],'redirect',selected_audit=bad_audit,expected_failure=True)
                configure(enabled=False);inactive=folder/'libaudit-inactive.so';compile_audit('inactive',inactive)
                run_case('inactive_default',outputs['main'],'native',selected_audit=inactive)
                default_audit=folder/'libaudit-default.so'
                compile_one('weak_default',audit_args+[str(CORE),'-o',str(default_audit)])
                run_case('weak_default_inactive',outputs['main'],'native',selected_audit=default_audit)
                report['architectures'][arch]['audit_dynamic_imports']=[]
                report['architectures'][arch]['public_openvr_exports']=[]
                report['architectures'][arch]['child_exec_tested']=not bool(qemu)
                report['architectures'][arch]['fixture_elf_sha256']={key:sha(path) for key,path in outputs.items()}
        report['status']='passed'
        print(json.dumps({'status':report['status'],'cases':{arch:len(data['cases']) for arch,data in report['architectures'].items()},'native_openvr_init_executed':False}))
    except BaseException as error:
        report['failure']=str(error);raise
    finally:
        cache=ROOT/'tools/__pycache__'
        if cache.exists():
            for path in cache.glob('run_import_native_forwarding.*.pyc'):path.unlink()
            if not any(cache.iterdir()):cache.rmdir()
        if args.record:
            args.record.parent.mkdir(parents=True,exist_ok=True)
            args.record.write_text(json.dumps(report,indent=2)+'\n');args.record.chmod(0o600)


if __name__=='__main__':main()
