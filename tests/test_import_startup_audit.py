#!/usr/bin/env python3
"""Real, controlled ELF audit/preload fixtures; never execute native OpenVR init.

An optional explicit QEMU/private-provider pair repeats these own-source ELF
fixtures under AArch64 and the already supplied Frame libc/loader. No network,
Frame access, system package changes or production observer activation occurs.
An explicit private own-source override lets isolated workers test an unmerged
audit core. Its two source hashes are recorded; it grants no runtime authority.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'import/import_startup_audit.h'
CORE = ROOT / 'import/import_startup_audit.cpp'
AUDIT_ROOT = ROOT


def private_audit_source(path):
    """Explicit bounded borrowed OWN source for a worker's unmerged core check."""
    root=path.resolve(strict=True)
    if root!=path.absolute() or not root.is_dir() or root.stat().st_uid!=os.getuid() or stat.S_IMODE(root.stat().st_mode)!=0o700:
        raise SystemExit('Audit source directory must be normalized, private and user-owned')
    folder=root/'import'
    if (folder.is_symlink() or not folder.is_dir() or folder.stat().st_uid!=os.getuid() or
        stat.S_IMODE(folder.stat().st_mode)!=0o700 or
        set(p.name for p in folder.iterdir())!={'import_startup_audit.cpp','import_startup_audit.h'}):
        raise SystemExit('Exact two borrowed own audit sources required')
    for name in ('import_startup_audit.cpp','import_startup_audit.h'):
        file=folder/name;before=file.lstat()
        if not stat.S_ISREG(before.st_mode) or before.st_uid!=os.getuid() or stat.S_IMODE(before.st_mode)!=0o600 or not 64<=before.st_size<=65536:
            raise SystemExit('Borrowed audit source must be bounded private regular files')
    return root

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
    if (request.stage == Stage::Loss) { /* No new binding after sticky loss. */ }
    else if (request.stage == Stage::Retire) { ++retires; }
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


DEEP_CLIENT = r'''
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>
#include <cstdint>
extern "C" void fixture_deep_labels(std::uintptr_t,std::uintptr_t);
extern "C" void fixture_deep_event(unsigned,std::uintptr_t,std::uintptr_t,int,int);
extern "C" int fixture_call_mutex_init(pthread_mutex_t*,const pthread_mutexattr_t*);
extern "C" int fixture_call_mutex_destroy(pthread_mutex_t*);
extern "C" void fixture_mutex_init_return();
extern "C" void fixture_mutex_destroy_return();
#if defined(__aarch64__)
__asm__(R"(
.text
.global fixture_call_mutex_init
.hidden fixture_call_mutex_init
.type fixture_call_mutex_init,%function
fixture_call_mutex_init:
stp x29,x30,[sp,#-16]!
mov x29,sp
bl pthread_mutex_init
.global fixture_mutex_init_return
fixture_mutex_init_return:
ldp x29,x30,[sp],#16
ret
.global fixture_call_mutex_destroy
.hidden fixture_call_mutex_destroy
.type fixture_call_mutex_destroy,%function
fixture_call_mutex_destroy:
stp x29,x30,[sp,#-16]!
mov x29,sp
bl pthread_mutex_destroy
.global fixture_mutex_destroy_return
fixture_mutex_destroy_return:
ldp x29,x30,[sp],#16
ret
)");
#else
__asm__(R"(
.text
.global fixture_call_mutex_init
.hidden fixture_call_mutex_init
.type fixture_call_mutex_init,@function
fixture_call_mutex_init:
push %rbp
mov %rsp,%rbp
call pthread_mutex_init@PLT
.global fixture_mutex_init_return
fixture_mutex_init_return:
pop %rbp
ret
.global fixture_call_mutex_destroy
.hidden fixture_call_mutex_destroy
.type fixture_call_mutex_destroy,@function
fixture_call_mutex_destroy:
push %rbp
mov %rsp,%rbp
call pthread_mutex_destroy@PLT
.global fixture_mutex_destroy_return
fixture_mutex_destroy_return:
pop %rbp
ret
)");
#endif
static pthread_mutex_t mutex;
static pthread_mutexattr_t attr;
__attribute__((constructor)) static void initialize() {
    fixture_deep_labels(reinterpret_cast<std::uintptr_t>(&fixture_mutex_init_return),reinterpret_cast<std::uintptr_t>(&fixture_mutex_destroy_return));
    if(pthread_mutexattr_init(&attr)||pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE))std::abort();
    errno=23;const int result=fixture_call_mutex_init(&mutex,&attr);const int saved=errno;
    const auto queried=dlsym(RTLD_NEXT,"pthread_mutex_init");
    fixture_deep_event(1,reinterpret_cast<std::uintptr_t>(&mutex),reinterpret_cast<std::uintptr_t>(&attr),result,saved);
    fixture_deep_event(3,reinterpret_cast<std::uintptr_t>(queried),0,0,0);
    if(result||pthread_mutex_lock(&mutex)||pthread_mutex_lock(&mutex)||pthread_mutex_unlock(&mutex)||pthread_mutex_unlock(&mutex))std::abort();
}
__attribute__((destructor)) static void finalize() {
    errno=31;const int result=fixture_call_mutex_destroy(&mutex);const int saved=errno;
    fixture_deep_event(2,reinterpret_cast<std::uintptr_t>(&mutex),0,result,saved);pthread_mutexattr_destroy(&attr);
}
'''
DEEP_OBSERVER = r'''
#include "import/import_startup_audit.h"
#include "import/import_forwarding.h"
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
using namespace frame_import_startup_audit;
static std::uintptr_t expected_init,expected_destroy,native_init,native_destroy;
static unsigned init_before,init_after,destroy_before,destroy_after,binds,prepare_count;
static std::uintptr_t init_arg,init_attr,destroy_arg,libc_bias;
static int init_result,destroy_result,init_errno,destroy_errno;
static bool reject_hidden,retired;
static unsigned loss_events,retired_destroy_calls,retired_init_calls;
extern "C" void fixture_deep_labels(std::uintptr_t a,std::uintptr_t b){expected_init=a;expected_destroy=b;}
#if defined(__aarch64__)
extern "C" __attribute__((visibility("hidden"))) void frame_import_init_entry();
extern "C" __attribute__((visibility("hidden"))) void frame_import_destroy_entry();
static void notify(frame_import_forwarding::Notification value,void*) noexcept {
    if(retired){
        if(value.phase==frame_import_forwarding::Phase::Before&&value.entry.x[30]==expected_destroy&&value.operation==frame_import_forwarding::Operation::Destroy)++retired_destroy_calls;
        if(value.phase==frame_import_forwarding::Phase::Before&&value.entry.x[30]==expected_init&&value.operation==frame_import_forwarding::Operation::Init)++retired_init_calls;
        return;
    }
    const auto pc=value.entry.x[30];const bool before=value.phase==frame_import_forwarding::Phase::Before;
    if(pc==expected_init&&value.operation==frame_import_forwarding::Operation::Init) {
        if(before){++init_before;init_arg=value.entry.x[0];init_attr=value.entry.x[1];}
        else{++init_after;init_result=static_cast<int>(value.result_bits);init_errno=value.preserved_errno;}
    }
    if(pc==expected_destroy&&value.operation==frame_import_forwarding::Operation::Destroy) {
        if(before){++destroy_before;destroy_arg=value.entry.x[0];}
        else{++destroy_after;destroy_result=static_cast<int>(value.result_bits);destroy_errno=value.preserved_errno;}
    }
}
#define INIT_ENTRY frame_import_init_entry
#define DESTROY_ENTRY frame_import_destroy_entry
#else
extern "C" int host_real_mutex_init(pthread_mutex_t*,const pthread_mutexattr_t*);
extern "C" int host_real_mutex_destroy(pthread_mutex_t*);
__asm__(".symver host_real_mutex_init,__pthread_mutex_init@GLIBC_2.2.5");
__asm__(".symver host_real_mutex_destroy,__pthread_mutex_destroy@GLIBC_2.2.5");
extern "C" int pthread_mutex_init(pthread_mutex_t* m,const pthread_mutexattr_t* a) noexcept {
    const auto pc=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(pc==expected_init&&retired)++retired_init_calls;
    if(pc==expected_init&&!retired){++init_before;init_arg=reinterpret_cast<std::uintptr_t>(m);init_attr=reinterpret_cast<std::uintptr_t>(a);}
    const int result=host_real_mutex_init(m,a);
    if(pc==expected_init&&!retired){++init_after;init_result=result;init_errno=errno;}return result;
}
extern "C" int pthread_mutex_destroy(pthread_mutex_t* m) noexcept {
    const auto pc=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(pc==expected_destroy&&retired)++retired_destroy_calls;
    if(pc==expected_destroy&&!retired){++destroy_before;destroy_arg=reinterpret_cast<std::uintptr_t>(m);}
    const int result=host_real_mutex_destroy(m);
    if(pc==expected_destroy&&!retired){++destroy_after;destroy_result=result;destroy_errno=errno;}return result;
}
#define INIT_ENTRY pthread_mutex_init
#define DESTROY_ENTRY pthread_mutex_destroy
#endif
__attribute__((constructor)) static void initialize() {
    const auto saved=errno;
    native_init=reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,"pthread_mutex_init"));
    native_destroy=reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,"pthread_mutex_destroy"));
    Dl_info a{},b{};
    if(!native_init||!native_destroy||!dladdr(reinterpret_cast<void*>(native_init),&a)||!dladdr(reinterpret_cast<void*>(native_destroy),&b)||a.dli_fbase!=b.dli_fbase)std::abort();
    libc_bias=reinterpret_cast<std::uintptr_t>(a.dli_fbase);
    reject_hidden=std::getenv("FIXTURE_BAD_MUTEX_ENTRY")!=nullptr;
#if defined(__aarch64__)
    if(!frame_import_forwarding::register_callbacks_before_threads({notify,nullptr},frame_import_forwarding::InitializationGate::OwnedPreparedContext))std::abort();
#endif
    errno=saved;
}
extern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_deep_bridge(BridgeRequest r) noexcept {
    BridgeResponse result{};
    if(r.stage==Stage::Loss){retired=true;++loss_events;return result;}
    if(r.stage==Stage::Prepare) {
        ++prepare_count;
        result.ready=!r.missed_before_preinit&&r.provider.libc.bias==libc_bias&&r.provider.libc.observed_open_generation!=0;
    } else if(r.stage==Stage::Bind) {
        const auto selected=r.provider.function==Function::MutexInit?native_init:native_destroy;
        if(r.provider.function!=Function::MutexInit&&r.provider.function!=Function::MutexDestroy)return result;
        ++binds;
        result.ready=r.provider.original==selected&&r.provider.libc.bias==libc_bias&&
            r.provider.client.observed_open_generation&&r.provider.libc.observed_open_generation&&
            r.provider.client.observed_open_generation!=r.provider.libc.observed_open_generation&&
            r.provider.client.rx_begin<r.provider.client.rx_end;
        result.hidden_entry=reject_hidden?selected:r.provider.function==Function::MutexInit?
            reinterpret_cast<std::uintptr_t>(&INIT_ENTRY):reinterpret_cast<std::uintptr_t>(&DESTROY_ENTRY);
    }
    return result;
}
extern "C" std::uintptr_t fixture_deep_probe(unsigned n) {
    switch(n){case 0:return init_before;case 1:return init_after;case 2:return destroy_before;case 3:return destroy_after;
        case 4:return init_arg;case 5:return init_attr;case 6:return destroy_arg;case 7:return init_result;
        case 8:return destroy_result;case 9:return init_errno;case 10:return destroy_errno;case 11:return binds;
        case 12:return native_init;case 13:return expected_init;case 14:return expected_destroy;case 15:return prepare_count;case 16:return loss_events;case 17:return retired_destroy_calls;case 18:return retired_init_calls;default:return 0;}
}
'''
DEEP_MAIN = r'''
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
extern "C" std::uintptr_t fixture_deep_probe(unsigned);
static unsigned init_events,destroy_events;
static std::uintptr_t init_arg,init_attr,destroy_arg,dynamic_init;
static void* early;
extern "C" void fixture_deep_event(unsigned kind,std::uintptr_t a,std::uintptr_t b,int result,int saved) {
    if(kind==1){++init_events;init_arg=a;init_attr=b;if(result||saved!=23)std::abort();}
    if(kind==2){++destroy_events;destroy_arg=a;if(result||saved!=31)std::abort();}
    if(kind==3)dynamic_init=a;
}
#ifdef DEEP_EARLY
__attribute__((constructor)) static void initialize(){early=dlopen(CLIENT_FILE,RTLD_NOW|RTLD_DEEPBIND);if(!early)std::abort();}
#endif
int main(int argc,char** argv) {
    if(argc>1&&!std::strcmp(argv[1],"provider")) {
        Dl_info d{};if(!dladdr(reinterpret_cast<void*>(fixture_deep_probe(12)),&d))return 9;
        std::printf("%s\n",d.dli_fname);return 0;
    }
    void* handle=dlopen(std::getenv("FIXTURE_FOREIGN_CLIENT")?FOREIGN_FILE:CLIENT_FILE,RTLD_NOW|RTLD_DEEPBIND);
    if(!handle){std::fprintf(stderr,"DLOPEN: %s\n",dlerror());return 3;}
    if(early){dlclose(early);early=nullptr;}
    const bool expected=argc>1&&!std::strcmp(argv[1],"redirect");
    const bool partial=argc>1&&!std::strncmp(argv[1],"partial_",8);
    const bool expected_destroy=expected;
    if(init_events!=1||dynamic_init!=fixture_deep_probe(12)||
       reinterpret_cast<std::uintptr_t>(dlsym(handle,"fixture_mutex_init_return"))!=fixture_deep_probe(13)||
       reinterpret_cast<std::uintptr_t>(dlsym(handle,"fixture_mutex_destroy_return"))!=fixture_deep_probe(14))return 4;
    if(fixture_deep_probe(0)!=(expected?1u:0u)||fixture_deep_probe(1)!=(expected?1u:0u))return 5;
    if(expected&&(fixture_deep_probe(4)!=init_arg||fixture_deep_probe(5)!=init_attr||fixture_deep_probe(7)||fixture_deep_probe(9)!=23))return 6;
    dlclose(handle);
    if(destroy_events!=1||fixture_deep_probe(2)!=(expected_destroy?1u:0u)||fixture_deep_probe(3)!=(expected_destroy?1u:0u))return 7;
    if(expected_destroy&&(fixture_deep_probe(6)!=destroy_arg||fixture_deep_probe(8)||fixture_deep_probe(10)!=31))return 8;
    if(partial&&(fixture_deep_probe(16)!=1||fixture_deep_probe(!std::strcmp(argv[1],"partial_destroy")?17:18)!=1))return 10;
    std::printf("RESULT={\"mutex_redirected\":%s,\"prior_mutex_binding_retained\":%s,\"loss_events\":%llu,\"bindings\":%llu,\"initializers\":%u,\"finalizers\":%u,\"native_results_errno\":true,\"dlsym_native_unchanged\":true,\"caller_pc_exact\":%s}\n",
        expected?"true":"false",partial?"true":"false",static_cast<unsigned long long>(fixture_deep_probe(16)),static_cast<unsigned long long>(fixture_deep_probe(11)),init_events,destroy_events,expected?"true":"false");
    return 0;
}
'''
CONNECTION_CLIENT = r'''
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
extern "C" void fixture_connection_label(unsigned,std::uintptr_t);
extern "C" void fixture_connection_event(unsigned,std::uintptr_t,std::uintptr_t,std::uintptr_t,int,int);
#define DECLARE(N,ARGS) extern "C" int fixture_call_##N ARGS; extern "C" void fixture_return_##N();
DECLARE(init,(pthread_mutex_t*,const pthread_mutexattr_t*))
DECLARE(destroy,(pthread_mutex_t*))
DECLARE(lock,(pthread_mutex_t*))
DECLARE(unlock,(pthread_mutex_t*))
DECLARE(connect,(int,const sockaddr*,socklen_t))
#if defined(__aarch64__)
#define CALL_ASM(N,P) ".global fixture_call_" N "\n.hidden fixture_call_" N "\n.type fixture_call_" N ",%function\nfixture_call_" N ":\nstp x29,x30,[sp,#-16]!\nmov x29,sp\nbl " P "\n.global fixture_return_" N "\nfixture_return_" N ":\nldp x29,x30,[sp],#16\nret\n"
#else
#define CALL_ASM(N,P) ".global fixture_call_" N "\n.hidden fixture_call_" N "\n.type fixture_call_" N ",@function\nfixture_call_" N ":\npush %rbp\nmov %rsp,%rbp\ncall " P "@PLT\n.global fixture_return_" N "\nfixture_return_" N ":\npop %rbp\nret\n"
#endif
__asm__(".text\n" CALL_ASM("init","pthread_mutex_init") CALL_ASM("destroy","pthread_mutex_destroy")
    CALL_ASM("lock","pthread_mutex_lock") CALL_ASM("unlock","pthread_mutex_unlock") CALL_ASM("connect","connect"));
static pthread_mutex_t mutex;
static sockaddr_un address;
static void event(unsigned n,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,int result,int saved) {
    fixture_connection_event(n,a,b,c,result,saved);
}
__attribute__((constructor)) static void initialize() {
    fixture_connection_label(0,reinterpret_cast<std::uintptr_t>(&fixture_return_init));
    fixture_connection_label(1,reinterpret_cast<std::uintptr_t>(&fixture_return_destroy));
    fixture_connection_label(2,reinterpret_cast<std::uintptr_t>(&fixture_return_lock));
    fixture_connection_label(3,reinterpret_cast<std::uintptr_t>(&fixture_return_unlock));
    fixture_connection_label(4,reinterpret_cast<std::uintptr_t>(&fixture_return_connect));
    const auto m=reinterpret_cast<std::uintptr_t>(&mutex);
    errno=23;int result=fixture_call_init(&mutex,nullptr);event(0,m,0,0,result,errno);if(result)std::abort();
    errno=29;result=fixture_call_lock(&mutex);event(2,m,0,0,result,errno);if(result)std::abort();
    address.sun_family=AF_UNIX;
    errno=31;result=fixture_call_connect(-1,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
    event(4,static_cast<std::uintptr_t>(static_cast<std::uint32_t>(-1)),reinterpret_cast<std::uintptr_t>(&address),sizeof(address),result,errno);
    if(result!=-1||errno!=EBADF)std::abort();
    errno=37;result=fixture_call_unlock(&mutex);event(3,m,0,0,result,errno);if(result)std::abort();
    const char* names[]{"pthread_mutex_init","pthread_mutex_destroy","pthread_mutex_lock","pthread_mutex_unlock","connect"};
    for(unsigned i=0;i<5;++i)event(5+i,reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,names[i])),0,0,0,0);
}
__attribute__((destructor)) static void finalize() {
    errno=41;const int result=fixture_call_destroy(&mutex);event(1,reinterpret_cast<std::uintptr_t>(&mutex),0,0,result,errno);
    if(result)std::abort();
}
'''
CONNECTION_OBSERVER = r'''
#include "import/import_startup_audit.h"
#include "import/import_forwarding.h"
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
using namespace frame_import_startup_audit;
static std::uintptr_t native[5],labels[5],args[5][3],libc_bias;
static unsigned before[5],after[5],events[5],bind_mask,retired_mask,losses,prepares,unscoped_notifications,nonclient_checks;
static int results[5],errors[5];
static bool retired,bad_entry;
extern "C" void fixture_connection_label(unsigned n,std::uintptr_t pc){if(n>=5)std::abort();labels[n]=pc;}
static unsigned operation(frame_import_forwarding::Operation value) {
    using O=frame_import_forwarding::Operation;
    switch(value){case O::Init:return 0;case O::Destroy:return 1;case O::Lock:return 2;case O::Unlock:return 3;case O::Connect:return 4;default:return 5;}
}
static void notify(frame_import_forwarding::Notification value,void*) noexcept {
    const auto n=operation(value.operation);if(n>=5||value.entry.x[30]!=labels[n]){++unscoped_notifications;return;}
    if(retired){if(value.phase==frame_import_forwarding::Phase::Before)retired_mask|=1u<<n;return;}
    if(value.phase==frame_import_forwarding::Phase::Before){++before[n];for(unsigned i=0;i<3;++i)args[n][i]=value.entry.x[i];}
    else{++after[n];results[n]=static_cast<int>(static_cast<std::uint32_t>(value.result_bits));errors[n]=value.preserved_errno;}
}
#if defined(__aarch64__)
#define ENTRY(N) extern "C" __attribute__((visibility("hidden"))) void frame_import_##N##_entry();
ENTRY(init) ENTRY(destroy) ENTRY(lock) ENTRY(unlock) ENTRY(connect)
#else
// Own-source host controls use the same actual link-time provider dispatchers;
// only ARM uses the production register-saving assembly.
#define SNAPSHOT(N,A,B,C) frame_import_native::Entry e{};e.x[0]=(A);e.x[1]=(B);e.x[2]=(C);e.x[30]=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));return static_cast<int>(frame_import_##N##_dispatch(&e));
extern "C" __attribute__((visibility("hidden"))) int frame_import_init_entry(pthread_mutex_t* m,const pthread_mutexattr_t* a) noexcept {SNAPSHOT(init,reinterpret_cast<std::uintptr_t>(m),reinterpret_cast<std::uintptr_t>(a),0)}
extern "C" __attribute__((visibility("hidden"))) int frame_import_destroy_entry(pthread_mutex_t* m) noexcept {SNAPSHOT(destroy,reinterpret_cast<std::uintptr_t>(m),0,0)}
extern "C" __attribute__((visibility("hidden"))) int frame_import_lock_entry(pthread_mutex_t* m) noexcept {SNAPSHOT(lock,reinterpret_cast<std::uintptr_t>(m),0,0)}
extern "C" __attribute__((visibility("hidden"))) int frame_import_unlock_entry(pthread_mutex_t* m) noexcept {SNAPSHOT(unlock,reinterpret_cast<std::uintptr_t>(m),0,0)}
extern "C" __attribute__((visibility("hidden"))) int frame_import_connect_entry(int fd,const sockaddr* a,socklen_t size){SNAPSHOT(connect,static_cast<std::uint32_t>(fd),reinterpret_cast<std::uintptr_t>(a),size)}
#endif
static std::uintptr_t entries[5]{reinterpret_cast<std::uintptr_t>(&frame_import_init_entry),reinterpret_cast<std::uintptr_t>(&frame_import_destroy_entry),
    reinterpret_cast<std::uintptr_t>(&frame_import_lock_entry),reinterpret_cast<std::uintptr_t>(&frame_import_unlock_entry),reinterpret_cast<std::uintptr_t>(&frame_import_connect_entry)};
__attribute__((constructor)) static void initialize() {
    const auto saved=errno;
    const char* names[]{"pthread_mutex_init","pthread_mutex_destroy","pthread_mutex_lock","pthread_mutex_unlock","connect"};
    for(unsigned i=0;i<5;++i){native[i]=reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,names[i]));Dl_info d{};
        if(!native[i]||!dladdr(reinterpret_cast<void*>(native[i]),&d)||(libc_bias&&libc_bias!=reinterpret_cast<std::uintptr_t>(d.dli_fbase)))std::abort();
        libc_bias=reinterpret_cast<std::uintptr_t>(d.dli_fbase);}
    bad_entry=std::getenv("FIXTURE_BAD_CONNECTION_ENTRY")!=nullptr;
    if(!frame_import_forwarding::register_callbacks_before_threads({notify,nullptr},frame_import_forwarding::InitializationGate::OwnedPreparedContext))std::abort();
    errno=saved;
}
extern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_connection_bridge(BridgeRequest r) noexcept {
    BridgeResponse out{};
    if(r.stage==Stage::Loss){retired=true;++losses;return out;}
    if(r.stage==Stage::Retire){retired=true;return out;}
    if(r.stage==Stage::Prepare){++prepares;out.ready=!r.missed_before_preinit&&r.provider.libc.bias==libc_bias&&r.provider.libc.observed_open_generation;return out;}
    unsigned n=5;
    switch(r.provider.function){case Function::MutexInit:n=0;break;case Function::MutexDestroy:n=1;break;case Function::MutexLock:n=2;break;case Function::MutexUnlock:n=3;break;case Function::Connect:n=4;break;default:return out;}
    bind_mask|=1u<<n;
    out.ready=r.provider.original==native[n]&&r.provider.libc.bias==libc_bias&&r.provider.client.observed_open_generation&&
        r.provider.libc.observed_open_generation&&r.provider.client.observed_open_generation!=r.provider.libc.observed_open_generation;
    out.hidden_entry=bad_entry?native[n]:entries[n];return out;
}
extern "C" void fixture_connection_event(unsigned n,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,int result,int saved) {
    if(n>=5){if(n>=10||a!=native[n-5])std::abort();return;}
    ++events[n];const int expected_errno[]{23,41,29,37,EBADF};
    if(result!=(n==4?-1:0)||saved!=expected_errno[n])std::abort();
    const bool wrong_first=n==4?static_cast<std::uint32_t>(args[n][0])!=static_cast<std::uint32_t>(a):args[n][0]!=a;
    if(before[n]&&(before[n]!=1||after[n]!=1||wrong_first||
       (n==0||n==4?args[n][1]!=b:false)||(n==4&&args[n][2]!=c)||results[n]!=result||errors[n]!=saved))std::abort();
}
extern "C" std::uintptr_t fixture_connection_probe(unsigned n) {
    if(n<5)return before[n];
    if(n<10)return after[n-5];
    if(n<15)return events[n-10];
    switch(n){case 15:return native[0];case 16:return bind_mask;case 17:return retired_mask;case 18:return losses;case 19:return prepares;case 20:return unscoped_notifications;case 21:return nonclient_checks;default:return 0;}
}
extern "C" void fixture_connection_nonclient() {
    // These public calls originate from the unrelated observer DSO, before
    // and after client dlopen. Client-only audit redirection must not touch
    // their actual providers or send notifications to the sampling callback.
    const std::uintptr_t actual[]{reinterpret_cast<std::uintptr_t>(&pthread_mutex_init),
        reinterpret_cast<std::uintptr_t>(&pthread_mutex_destroy),reinterpret_cast<std::uintptr_t>(&pthread_mutex_lock),
        reinterpret_cast<std::uintptr_t>(&pthread_mutex_unlock),reinterpret_cast<std::uintptr_t>(&connect)};
    for(unsigned i=0;i<5;++i)if(actual[i]!=native[i])std::abort();
    pthread_mutex_t mutex;
    sockaddr address{};address.sa_family=AF_UNIX;
    errno=43;if(pthread_mutex_init(&mutex,nullptr)||errno!=43)std::abort();
    errno=47;if(pthread_mutex_lock(&mutex)||errno!=47)std::abort();
    if(connect(-1,&address,sizeof(address))!=-1||errno!=EBADF)std::abort();
    errno=53;if(pthread_mutex_unlock(&mutex)||errno!=53)std::abort();
    errno=59;if(pthread_mutex_destroy(&mutex)||errno!=59)std::abort();
    if(unscoped_notifications)std::abort();
    ++nonclient_checks;
}
'''
CONNECTION_MAIN = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
extern "C" std::uintptr_t fixture_connection_probe(unsigned);
extern "C" void fixture_connection_nonclient();
static void* early;
#ifdef DEEP_EARLY
__attribute__((constructor)) static void initialize(){early=dlopen(CLIENT_FILE,RTLD_NOW|RTLD_DEEPBIND);if(!early)std::abort();}
#endif
int main(int argc,char** argv) {
    if(argc>1&&!std::strcmp(argv[1],"provider")){Dl_info d{};if(!dladdr(reinterpret_cast<void*>(fixture_connection_probe(15)),&d))return 9;std::printf("%s\n",d.dli_fname);return 0;}
    fixture_connection_nonclient();
    void* handle=dlopen(std::getenv("FIXTURE_FOREIGN_CLIENT")?FOREIGN_FILE:CLIENT_FILE,RTLD_NOW|RTLD_DEEPBIND);
    if(!handle){std::fprintf(stderr,"DLOPEN: %s\n",dlerror());return 3;}
    if(early){dlclose(early);early=nullptr;}dlclose(handle);
    fixture_connection_nonclient();
    if(fixture_connection_probe(20)||fixture_connection_probe(21)!=2)return 6;
    const unsigned expected=argc>1?std::strtoul(argv[1],nullptr,10):0;
    unsigned observed=0;
    for(unsigned n=0;n<5;++n){if(fixture_connection_probe(n+10)!=1||fixture_connection_probe(n)!=fixture_connection_probe(n+5)||fixture_connection_probe(n)!=((expected>>n)&1u))return 4;
        if(fixture_connection_probe(n))observed|=1u<<n;}
    if(argc>2&&!std::strcmp(argv[2],"loss")&&fixture_connection_probe(18)!=1)return 5;
    std::printf("RESULT={\"observed_provider_mask\":%u,\"bindings_mask\":%llu,\"retired_forwarded_mask\":%llu,\"loss_events\":%llu,\"native_results_errno\":true,\"dlsym_native_unchanged\":true,\"nonclient_all_five_native\":true,\"unscoped_notifications\":0,\"caller_pc_exact\":%s}\n",observed,
        static_cast<unsigned long long>(fixture_connection_probe(16)),static_cast<unsigned long long>(fixture_connection_probe(17)),static_cast<unsigned long long>(fixture_connection_probe(18)),observed?"true":"false");
}
'''
CONNECTION_CASES = ('connection_redirect','connection_preload_only','connection_zero_defaults',
    'connection_partial_optin','connection_duplicate_rva','connection_outside_rx','connection_no_mutex_scope',
    'connection_wrong_client','connection_wrong_native_rva','connection_early_client','connection_bad_owned_entry')

RESOURCE_THREAD_OBSERVER = r'''
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
using Epoll = int(*)(int,epoll_event*,int,int);
static Epoll native_epoll;
static BoundProvider thread_attached;
static std::uintptr_t epoll_label,epoll_args[4];
static unsigned epoll_before,epoll_after,epoll_bindings,epoll_nonclient;
static int epoll_result,epoll_errno;
static bool receiver_owner,thread_missed;
extern "C" __attribute__((visibility("hidden"))) int fixture_owned_epoll(int fd,epoll_event* events,int maxevents,int timeout) {
    if(retired)return native_epoll(fd,events,maxevents,timeout);
    if(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))!=epoll_label)std::abort();
    ++epoll_before;epoll_args[0]=static_cast<std::uint32_t>(fd);epoll_args[1]=reinterpret_cast<std::uintptr_t>(events);
    epoll_args[2]=static_cast<std::uint32_t>(maxevents);epoll_args[3]=static_cast<std::uint32_t>(timeout);
    const int value=native_epoll(fd,events,maxevents,timeout);
    epoll_result=value;epoll_errno=errno;++epoll_after;return value;
}
__attribute__((constructor)) static void thread_initialize() {
    const int saved=errno;
    native_epoll=reinterpret_cast<Epoll>(dlsym(RTLD_NEXT,"epoll_wait"));
    Dl_info d{};if(!native_epoll||!dladdr(reinterpret_cast<void*>(native_epoll),&d)||
        reinterpret_cast<std::uintptr_t>(d.dli_fbase)!=libc_bias)std::abort();
    char path[1024];const auto length=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(length>0){path[length]=0;receiver_owner=!std::strcmp(path,RECEIVER_FILE)||
        !std::strcmp(path,RECEIVER_EARLY_FILE)||!std::strcmp(path,RECEIVER_NOW_FILE);}
    errno=saved;
}
static bool equal_view(ModuleView a,ModuleView b) {
    return a.bias==b.bias&&a.rx_begin==b.rx_begin&&a.rx_end==b.rx_end&&
           a.observed_open_generation==b.observed_open_generation&&a.observed_open_generation;
}
static bool fixture_resource_thread_bridge(BridgeRequest r,BridgeResponse& out) noexcept {
    if(r.stage==Stage::Prepare){thread_attached=r.provider;thread_missed=r.missed_before_preinit;if(!receiver_owner){++prepares;return true;}return false;}
    if(r.stage!=Stage::Bind||r.provider.function!=Function::EpollWait)return false;
    ++epoll_bindings;
    out.ready=receiver_owner&&!retired&&!std::getenv("FIXTURE_REJECT_THREAD_BIND")&&
        r.provider.original==reinterpret_cast<std::uintptr_t>(native_epoll)&&
        equal_view(r.provider.receiver,thread_attached.receiver)&&equal_view(r.provider.api,thread_attached.api)&&
        equal_view(r.provider.observer,thread_attached.observer)&&equal_view(r.provider.libc,thread_attached.libc);
    out.hidden_entry=std::getenv("FIXTURE_BAD_THREAD_ENTRY")?reinterpret_cast<std::uintptr_t>(native_epoll):
        reinterpret_cast<std::uintptr_t>(&fixture_owned_epoll);
    return true;
}
extern "C" void fixture_resource_thread_label(std::uintptr_t label){epoll_label=label;}
extern "C" void fixture_resource_thread_check(int fd,epoll_event* events,int count,int timeout,int result,int saved,bool redirected) {
    if(!redirected){if(epoll_before||epoll_after)std::abort();return;}
    if(epoll_before!=epoll_after||epoll_args[0]!=static_cast<std::uint32_t>(fd)||
       epoll_args[1]!=reinterpret_cast<std::uintptr_t>(events)||epoll_args[2]!=static_cast<std::uint32_t>(count)||
       epoll_args[3]!=static_cast<std::uint32_t>(timeout)||epoll_result!=result||epoll_errno!=saved)std::abort();
}
extern "C" void fixture_resource_thread_nonclient() {
    if(reinterpret_cast<std::uintptr_t>(&epoll_wait)!=reinterpret_cast<std::uintptr_t>(native_epoll))std::abort();
    const auto before_count=epoll_before;epoll_event event{};
    if(epoll_wait(-1,&event,1,0)!=-1||errno!=EBADF||epoll_before!=before_count)std::abort();
    ++epoll_nonclient;
}
extern "C" std::uintptr_t fixture_resource_thread_probe(unsigned n) {
    switch(n){case 0:return epoll_before;case 1:return epoll_after;case 2:return epoll_bindings;
        case 3:return epoll_nonclient;case 4:return thread_missed;default:return 0;}
}
'''
RESOURCE_THREAD_MAIN = r'''
#include <sys/epoll.h>
#include <cerrno>
#include <unistd.h>
extern "C" int fixture_call_epoll(int,epoll_event*,int,int);
extern "C" void fixture_return_epoll();
extern "C" void fixture_resource_thread_label(std::uintptr_t);
extern "C" void fixture_resource_thread_check(int,epoll_event*,int,int,int,int,bool);
extern "C" void fixture_resource_thread_nonclient();
extern "C" std::uintptr_t fixture_resource_thread_probe(unsigned);
#if defined(__aarch64__)
__asm__(".text\n.global fixture_call_epoll\n.hidden fixture_call_epoll\n.type fixture_call_epoll,%function\nfixture_call_epoll:\nstp x29,x30,[sp,#-16]!\nmov x29,sp\nbl epoll_wait\n.global fixture_return_epoll\nfixture_return_epoll:\nldp x29,x30,[sp],#16\nret\n");
#else
__asm__(".text\n.global fixture_call_epoll\n.hidden fixture_call_epoll\n.type fixture_call_epoll,@function\nfixture_call_epoll:\npush %rbp\nmov %rsp,%rbp\ncall epoll_wait@PLT\n.global fixture_return_epoll\nfixture_return_epoll:\npop %rbp\nret\n");
#endif
#ifdef THREAD_EARLY
__attribute__((constructor)) static void thread_early() {
    fixture_resource_thread_label(reinterpret_cast<std::uintptr_t>(&fixture_return_epoll));
    epoll_event event{};if(fixture_call_epoll(-1,&event,1,0)!=-1||errno!=EBADF)std::abort();
}
#endif
static void resource_thread_checks(int argc,char** argv) {
    const bool redirected=argc>2&&!std::strcmp(argv[2],"epoll");
    fixture_resource_thread_label(reinterpret_cast<std::uintptr_t>(&fixture_return_epoll));
    fixture_resource_thread_nonclient();
    if(std::getenv("FIXTURE_THREAD_DLSYM")&&!dlsym(RTLD_DEFAULT,"epoll_wait"))std::abort();
    const int fd=epoll_create1(0);if(fd<0)std::abort();epoll_event events[2]{};
    // Real native success, bad FD and invalid-capacity outcomes. Timeout zero
    // prevents this controlled receiver call from blocking on an event wait.
    errno=73;int result=fixture_call_epoll(fd,events,2,0);int saved=errno;
    if(result||saved!=73)std::abort();
    fixture_resource_thread_check(fd,events,2,0,result,saved,redirected);
    errno=79;result=fixture_call_epoll(-1,events,2,0);saved=errno;
    if(result!=-1||saved!=EBADF)std::abort();
    fixture_resource_thread_check(-1,events,2,0,result,saved,redirected);
    errno=83;result=fixture_call_epoll(fd,events,0,0);saved=errno;
    if(result!=-1||saved!=EINVAL)std::abort();
    fixture_resource_thread_check(fd,events,0,0,result,saved,redirected);
    close(fd);fixture_resource_thread_nonclient();
    if(fixture_resource_thread_probe(0)!=(redirected?3u:0u)||fixture_resource_thread_probe(1)!=(redirected?3u:0u)||
       fixture_resource_thread_probe(3)!=2)std::abort();
    std::printf("THREAD={\"epoll_redirected\":%s,\"calls\":%llu,\"bindings\":%llu,\"missed_before_preinit\":%s,\"unrelated_epoll_native\":true,\"four_arguments_results_errno\":true,\"exact_caller_lr\":%s,\"production_cold_ready_proven\":false}\n",
        redirected?"true":"false",static_cast<unsigned long long>(fixture_resource_thread_probe(0)),
        static_cast<unsigned long long>(fixture_resource_thread_probe(2)),fixture_resource_thread_probe(4)?"true":"false",redirected?"true":"false");
}
static void resource_thread_after_retire() {
    if(!std::getenv("FIXTURE_THREAD_POST_RETIRE"))return;
    const auto before_count=fixture_resource_thread_probe(0),after_count=fixture_resource_thread_probe(1);
    epoll_event event{};if(fixture_call_epoll(-1,&event,1,0)!=-1||errno!=EBADF||
        fixture_resource_thread_probe(0)!=before_count||fixture_resource_thread_probe(1)!=after_count)std::abort();
    std::puts("RETIRE={\"cached_entry_native_after_lifetime_end\":true,\"new_observer_events\":false}");
}
'''
RESOURCE_THREAD_ALT_AUDIT = r'''
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <link.h>
#include <cstdint>
extern "C" unsigned la_version(unsigned v){return v>=LAV_CURRENT?LAV_CURRENT:0;}
extern "C" unsigned la_objopen(link_map*,Lmid_t,std::uintptr_t*){return LA_FLG_BINDFROM|LA_FLG_BINDTO;}
extern "C" std::uintptr_t la_symbind64(Elf64_Sym* symbol,unsigned,std::uintptr_t*,std::uintptr_t*,unsigned* flags,const char* name){
    const char wanted[]="epoll_wait";unsigned i=0;for(;wanted[i]&&name[i]==wanted[i];++i){}
    if(!wanted[i]&&!name[i])*flags|=LA_SYMB_ALTVALUE;
    return symbol->st_value;
}
'''
RESOURCE_THREAD_CASES = ('resource_thread_redirect','resource_thread_preload_only','resource_thread_zero_default',
    'resource_thread_eager_now','resource_thread_environment_now','resource_thread_preinit_wait',
    'resource_thread_wrong_receiver','resource_thread_wrong_native_rva','resource_thread_wrong_libc_name',
    'resource_thread_missing_history','resource_thread_partial_scope','resource_thread_bad_owned_entry',
    'resource_thread_bridge_refusal','resource_thread_dlsym_first_miss','resource_thread_altvalue_first_miss','resource_thread_retired_binding')


def resource_thread_sources():
    observer=CONNECTION_OBSERVER.replace('extern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_connection_bridge',
        'static bool fixture_resource_thread_bridge(BridgeRequest,BridgeResponse&) noexcept;\nextern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_connection_bridge')
    observer=observer.replace('BridgeResponse out{};','BridgeResponse out{};if(fixture_resource_thread_bridge(r,out))return out;',1)
    observer+=RESOURCE_THREAD_OBSERVER
    main=CONNECTION_MAIN.replace('static void* early;',RESOURCE_THREAD_MAIN+'\nstatic void* early;')
    main=main.replace('fixture_connection_nonclient();\n    void* handle','resource_thread_checks(argc,argv);\n    fixture_connection_nonclient();\n    void* handle',1)
    main=main.replace('dlclose(handle);\n    fixture_connection_nonclient();','dlclose(handle);\n    resource_thread_after_retire();\n    fixture_connection_nonclient();',1)
    return observer,main

DEEP_CASES = ('deepbind_preload_only','deepbind_redirect','deepbind_wrong_client',
              'deepbind_wrong_native_rva','deepbind_partial_optin','deepbind_early_client',
              'deepbind_bad_owned_entry','deepbind_dlsym')

def run_deepbind(args,helper,report,clean):
    architectures=[] if args.native_only else [('host','g++',None)]
    if args.qemu:
        prefix=args.providers.resolve(strict=True)
        for name,pin in helper.PINNED.items():
            if sha(prefix/'lib'/name)!=pin:raise SystemExit('Private provider mismatch')
        architectures.append(('aarch64','aarch64-linux-gnu-g++',args.qemu.resolve(strict=True)))
        report['providers']={name:sha(prefix/'lib'/name) for name in helper.PINNED}
        report['qemu_sha256']=sha(args.qemu.resolve(strict=True))
    for arch,compiler,qemu in architectures:
        logs={};cases={};data={'logs':logs,'cases':cases};report['architectures'][arch]=data
        with tempfile.TemporaryDirectory(prefix='frame-deepbind-audit-'+arch+'-') as name:
            folder=Path(name);folder.chmod(0o700)
            common=[compiler,'-std=c++17','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-I',str(AUDIT_ROOT),'-I',str(ROOT)]
            def compile_one(label,command):
                value=helper.recorded_run(command,folder,logs,label+'_compile',45,clean)
                if value.returncode:raise SystemExit(value.stderr)
            client,foreign,observer,api,main,early=[folder/n for n in ('libclient.so','libforeign.so','libobserver.so','libapi.so','main','early')]
            client_cpp,observer_cpp,main_cpp,api_cpp=[folder/n for n in ('client.cpp','observer.cpp','main.cpp','api.cpp')]
            client_cpp.write_text(CONNECTION_CLIENT if args.connection else DEEP_CLIENT)
            observer_cpp.write_text(CONNECTION_OBSERVER if args.connection else DEEP_OBSERVER)
            main_cpp.write_text(CONNECTION_MAIN if args.connection else DEEP_MAIN)
            if args.resource_thread:
                thread_observer,thread_main=resource_thread_sources();observer_cpp.write_text(thread_observer);main_cpp.write_text(thread_main)
            api_cpp.write_text('#include <cstdint>\nextern "C" std::uint32_t VR_InitInternal2(void*,std::uint32_t,const char*){return 0;}\nextern "C" void VR_ShutdownInternal(){}\n')
            compile_one('client',common+['-shared','-fPIC',str(client_cpp),'-pthread','-ldl','-o',str(client)])
            compile_one('foreign',common+['-shared','-fPIC',str(client_cpp),'-pthread','-ldl','-o',str(foreign)])
            obs_args=common+['-shared','-fPIC','-ftls-model=initial-exec',str(observer_cpp)]
            if args.resource_thread:
                obs_args+=['-DRECEIVER_FILE='+json.dumps(str(main)),'-DRECEIVER_EARLY_FILE='+json.dumps(str(early)),
                           '-DRECEIVER_NOW_FILE='+json.dumps(str(folder/'now'))]
            if args.connection:
                obs_args += [str(ROOT/'import/import_forwarding.cpp')]
                if qemu:obs_args += [str(ROOT/'import/native_connection_entry.S')]
            elif qemu:obs_args += [str(ROOT/'import/native_mutex_entry.S'),str(ROOT/'import/import_forwarding.cpp')]
            compile_one('observer',obs_args+['-pthread','-ldl','-o',str(observer)])
            if args.connection:
                defined_names={line.split()[-1].split('@')[0] for line in
                    subprocess.check_output(['nm','-D','--defined-only',str(observer)],text=True).splitlines()
                    if line.split()}
                forbidden={'pthread_mutex_init','pthread_mutex_destroy','pthread_mutex_lock','pthread_mutex_unlock','connect'}
                if args.resource_thread:forbidden.add('epoll_wait')
                if defined_names & forbidden:raise SystemExit('Audit-only observer exports a public libc provider')
                data['public_libc_aliases_absent']=sorted(forbidden)
            compile_one('api',common+['-shared','-fPIC',str(api_cpp),'-o',str(api)])
            exe=common+['-rdynamic','-Wl,-z,lazy','-DCLIENT_FILE='+json.dumps(str(client)),
                        '-DFOREIGN_FILE='+json.dumps(str(foreign)),str(main_cpp),'-L',str(folder),
                        '-Wl,-rpath,'+str(folder),'-Wl,--no-as-needed','-lobserver','-lapi','-pthread','-ldl']
            compile_one('main',exe+['-o',str(main)])
            compile_one('early',exe+['-DTHREAD_EARLY' if args.resource_thread else '-DDEEP_EARLY','-o',str(early)])
            if args.resource_thread:
                compile_one('now',exe+['-Wl,-z,now','-o',str(folder/'now')])
                compile_one('wrong_receiver',exe+['-o',str(folder/'wrong')])
            def command_for(program,env_values,extra=()):
                if qemu:
                    command=[str(qemu),'-L',str(prefix)]
                    env_values={**env_values,'LD_LIBRARY_PATH':str(prefix/'lib')+':'+str(folder)+':/usr/aarch64-linux-gnu/lib'}
                    for key,value in env_values.items():command+=['-E',key+'='+value]
                    return command+[str(program),*extra],clean
                return [str(program),*extra],{**clean,**env_values}
            command,environment=command_for(main,{'LD_LIBRARY_PATH':str(folder)},['provider'])
            observed=helper.recorded_run(command,folder,logs,'actual_provider',15,environment)
            if observed.returncode:raise SystemExit(observed.stderr)
            libc_name=observed.stdout.strip();libc=Path(libc_name).resolve(strict=True)
            def geometry(path):
                ranges=[]
                for line in subprocess.check_output(['readelf','-lW',str(path)],text=True).splitlines():
                    columns=line.split()
                    if columns and columns[0]=='LOAD' and 'E' in columns[6:-1]:ranges.append((int(columns[2],16),int(columns[2],16)+int(columns[4],16)))
                if len(ranges)!=1:raise SystemExit('Expected one executable file-backed segment')
                return ranges[0]
            def symbol(path,wanted):
                args=['nm','-a','--defined-only',str(path)] if path!=libc else ['nm','-D','--defined-only',str(path)]
                for line in subprocess.check_output(args,text=True).splitlines():
                    columns=line.split()
                    if len(columns)>=3 and columns[-1].split('@')[0]==wanted:return int(columns[0],16)
                raise SystemExit('Missing fixture symbol '+wanted)
            rx={key:geometry(path) for key,path in [('main',main),('api',api),('observer',observer),('client',client),('libc',libc)]}
            bridge=symbol(observer,'fixture_connection_bridge' if args.connection else 'fixture_deep_bridge')
            init=symbol(api,'VR_InitInternal2');shutdown=symbol(api,'VR_ShutdownInternal')
            mutex_init=symbol(libc,'pthread_mutex_init');mutex_destroy=symbol(libc,'pthread_mutex_destroy')
            connection_rvas=[symbol(libc,n) for n in ('pthread_mutex_lock','pthread_mutex_unlock','connect')] if args.connection else []
            epoll_rva=symbol(libc,'epoll_wait') if args.resource_thread else 0
            config=folder/'configuration.cpp'
            def configured(label,rva_delta=0,partial=False,wrong_target='init',connection_mode='full',resource_mode='full'):
                def spec(name,extent):return '{'+json.dumps(name)+','+str(extent[0])+','+str(extent[1])+'}'
                extra=list(connection_rvas)
                if args.connection:
                    if connection_mode=='zero':extra=[0,0,0]
                    if connection_mode=='partial':extra[1]=0
                    if connection_mode=='duplicate':extra[0]=mutex_init
                    if connection_mode=='outside':extra[2]=rx['libc'][1]
                    if wrong_target in ('lock','unlock','connect'):extra[('lock','unlock','connect').index(wrong_target)]+=rva_delta
                native_init=mutex_init+(rva_delta if wrong_target=='init' else 0)
                native_destroy=0 if partial else mutex_destroy+(rva_delta if wrong_target=='destroy' else 0)
                libc_spec=spec(libc_name,rx['libc'])
                if resource_mode=='wrong_libc_name':libc_spec=spec(libc_name+'.wrong',rx['libc'])
                if connection_mode=='no_mutex':native_init=native_destroy=0;libc_spec='{nullptr,0,0}'
                api_spec=spec(str(api)+('.missing' if resource_mode=='missing_history' else ''),rx['api'])
                wait_rva=0 if resource_mode=='zero' else epoll_rva+(1 if resource_mode=='wrong_rva' else 0)
                config.write_text('#include "import/import_startup_audit.h"\nnamespace frame_import_startup_audit {const Preparation* preparation() noexcept {static const Preparation p={'+
                    spec('',rx['main'])+','+api_spec+','+spec(str(observer),rx['observer'])+','+
                    f'{bridge},{init},{shutdown},true,'+spec(str(client),rx['client'])+','+libc_spec+','+
                    f'{native_init},{native_destroy}'+(''.join(','+str(rva) for rva in extra))+
                    (','+str(wait_rva) if args.resource_thread else '')+'};return &p;}}\n')
                audit=folder/(label+'.so')
                audit_args=common+['-shared','-fPIC','-nostdlib','-fvisibility=hidden','-fno-exceptions','-fno-rtti','-fno-builtin']
                if qemu:audit_args+=['-mno-outline-atomics']
                compile_one(label,audit_args+[str(CORE),str(config),'-o',str(audit)])
                if subprocess.check_output(['nm','-u',str(audit)],text=True).strip():raise SystemExit('Audit imports foreign namespace providers')
                return audit
            relocations=subprocess.check_output(['readelf','-rW',str(client)],text=True)
            order=[]
            for line in relocations.splitlines():
                if 'JUMP_SLOT' not in line:continue
                for kind in ('init','destroy','lock','unlock','connect') if args.connection else ('init','destroy'):
                    name='connect' if kind=='connect' else 'pthread_mutex_'+kind
                    if name+'@' in line:order.append(kind)
            if len(order)!=(5 if args.connection else 2):raise SystemExit('Unknown owned eager relocation order')
            data['actual_client_mutex_relocation_order']=order
            audit=configured('audit');bad=configured('bad',1,wrong_target=order[-1]);partial=configured('partial',partial=True)
            alternatives={}
            if args.resource_thread:
                alternatives={key:configured('thread_'+key,resource_mode=key) for key in ('zero','wrong_rva','wrong_libc_name','missing_history')}
                alternatives['partial']=configured('thread_partial',connection_mode='partial')
                alt_cpp=folder/'alternative.cpp';alt_cpp.write_text(RESOURCE_THREAD_ALT_AUDIT);alt_audit=folder/'alternative.so'
                compile_one('alternative_audit',common+['-shared','-fPIC','-nostdlib',str(alt_cpp),'-o',str(alt_audit)])
            elif args.connection:
                alternatives={key:configured('connection_'+key,connection_mode=key)
                    for key in ('zero','partial','duplicate','outside','no_mutex')}
            selected=args.case or (RESOURCE_THREAD_CASES if args.resource_thread else CONNECTION_CASES if args.connection else DEEP_CASES)
            for label in selected:
                if label not in (RESOURCE_THREAD_CASES if args.resource_thread else CONNECTION_CASES if args.connection else DEEP_CASES):raise SystemExit('Not a focused deepbind case: '+label)
                env_values={'LD_PRELOAD':str(observer),'LD_LIBRARY_PATH':str(folder)}
                if args.resource_thread:
                    mode='31' if label in ('resource_thread_redirect','resource_thread_zero_default','resource_thread_retired_binding') else '0'
                    selected_audit=alternatives.get({'resource_thread_zero_default':'zero','resource_thread_wrong_native_rva':'wrong_rva',
                        'resource_thread_wrong_libc_name':'wrong_libc_name','resource_thread_missing_history':'missing_history',
                        'resource_thread_partial_scope':'partial'}.get(label,''),audit)
                    if label!='resource_thread_preload_only':env_values['LD_AUDIT']=str(selected_audit)
                    if label=='resource_thread_environment_now':env_values['LD_BIND_NOW']='1'
                    if label=='resource_thread_bad_owned_entry':env_values['FIXTURE_BAD_THREAD_ENTRY']='1'
                    if label=='resource_thread_bridge_refusal':env_values['FIXTURE_REJECT_THREAD_BIND']='1'
                    if label=='resource_thread_dlsym_first_miss':env_values['FIXTURE_THREAD_DLSYM']='1'
                    if label=='resource_thread_retired_binding':env_values['FIXTURE_THREAD_POST_RETIRE']='1'
                    if label=='resource_thread_altvalue_first_miss':env_values['LD_AUDIT']=str(alt_audit)+':'+str(audit)
                elif args.connection:
                    mode='31' if label=='connection_redirect' else '3' if label=='connection_zero_defaults' else '0'
                    selected_audit=bad if label=='connection_wrong_native_rva' else alternatives.get({
                        'connection_zero_defaults':'zero','connection_partial_optin':'partial',
                        'connection_duplicate_rva':'duplicate','connection_outside_rx':'outside',
                        'connection_no_mutex_scope':'no_mutex'}.get(label,''),audit)
                    if label!='connection_preload_only':env_values['LD_AUDIT']=str(selected_audit)
                    if label=='connection_wrong_client':env_values['FIXTURE_FOREIGN_CLIENT']='1'
                    if label=='connection_bad_owned_entry':env_values['FIXTURE_BAD_CONNECTION_ENTRY']='1'
                else:
                    mode='redirect' if label in ('deepbind_redirect','deepbind_dlsym') else 'partial_'+order[0] if label=='deepbind_wrong_native_rva' else 'native'
                    if label!='deepbind_preload_only':env_values['LD_AUDIT']=str(bad if label=='deepbind_wrong_native_rva' else partial if label=='deepbind_partial_optin' else audit)
                    if label=='deepbind_wrong_client':env_values['FIXTURE_FOREIGN_CLIENT']='1'
                    if label=='deepbind_bad_owned_entry':env_values['FIXTURE_BAD_MUTEX_ENTRY']='1'
                program=early if label in ('deepbind_early_client','connection_early_client') else main
                if args.resource_thread:
                    program=early if label=='resource_thread_preinit_wait' else folder/'now' if label=='resource_thread_eager_now' else folder/'wrong' if label=='resource_thread_wrong_receiver' else main
                command,environment=command_for(program,env_values,[mode,*(['epoll' if label in ('resource_thread_redirect','resource_thread_retired_binding') else 'native'] if args.resource_thread else [])])
                value=helper.recorded_run(command,folder,logs,label,15,environment)
                if value.returncode:raise SystemExit(label+': '+value.stdout+value.stderr)
                if 'RESULT=' not in value.stdout:raise SystemExit('Fixture did not complete semantics')
                cases[label]=json.loads(value.stdout.split('RESULT=')[1])
                if args.resource_thread:
                    if 'THREAD=' not in value.stdout:raise SystemExit('Missing direct receiver event-wait semantics')
                    cases[label]['resource_thread']=json.loads(value.stdout.split('THREAD=')[1].splitlines()[0])
                    thread=cases[label]['resource_thread']
                    if thread['epoll_redirected']!=(label in ('resource_thread_redirect','resource_thread_retired_binding')) or not thread['four_arguments_results_errno'] or not thread['unrelated_epoll_native']:
                        raise SystemExit('Receiver/nonreceiver event-wait isolation changed')
                    if label in ('resource_thread_eager_now','resource_thread_environment_now','resource_thread_preinit_wait') and not thread['missed_before_preinit']:
                        raise SystemExit('Real preinit event-wait miss was not retained')
                    if label in ('resource_thread_wrong_native_rva','resource_thread_bad_owned_entry','resource_thread_bridge_refusal','resource_thread_dlsym_first_miss','resource_thread_altvalue_first_miss') and cases[label]['loss_events']!=1:
                        raise SystemExit('First inadmissible event-wait binding was not sticky')
                    if label=='resource_thread_retired_binding':
                        if 'RETIRE=' not in value.stdout or cases[label]['loss_events']!=1:raise SystemExit('Actual client lifetime end did not retire event-wait evidence')
                        cases[label]['post_lifetime']=json.loads(value.stdout.split('RETIRE=')[1].splitlines()[0])
                if args.connection and not args.resource_thread:
                    result=cases[label]
                    if not result['nonclient_all_five_native'] or result['unscoped_notifications']:
                        raise SystemExit('Unrelated native callers were interposed by the audit-only observer')
                    if label=='connection_redirect' and result['bindings_mask']!=31:
                        raise SystemExit('Not all five legitimate bindings were admitted')
                    if label=='connection_zero_defaults' and result['bindings_mask']!=3:
                        raise SystemExit('Zero connection scope changed the original two-entry scope')
                    if label=='connection_wrong_native_rva' and (result['loss_events']!=1 or
                       not result['retired_forwarded_mask'] or result['retired_forwarded_mask']!=result['bindings_mask']):
                        raise SystemExit('Native-RVA loss did not retire already redirected entries')
                    if label=='connection_bad_owned_entry' and (result['loss_events']!=1 or not result['bindings_mask']):
                        raise SystemExit('Invalid hidden entry did not establish sticky loss')
            data['libc_name_actual_public_dladdr']=libc_name
            data['libc_identity']=sha(libc)
            wrapper='native_connection_entry.S' if args.connection else 'native_mutex_entry.S'
            data['entry_abi']='actual shared '+wrapper+' + versioned real libc forwarding' if qemu else 'host intrinsic caller PC + versioned real libc forwarding (not native ARM stack proof)'
            if args.resource_thread:data['event_wait_entry_abi']='Controlled four-argument C++ veneer with exact incoming caller PC and original native provider; not production cold ARM entry proof'
            data['actual_native_flags']='RTLD_NOW|RTLD_DEEPBIND (10), not altered'
            data['audit_provider_imports']=[]
            data['fixture_only']=True
            data['production_ready_proven']=False
    report['status']='passed'
    print(json.dumps({'status':'passed','focused_deepbind_cases':{a:len(d['cases']) for a,d in report['architectures'].items()},'native_openvr_init_executed':False}))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    global CORE,HEADER,AUDIT_ROOT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path)
    parser.add_argument('--providers',type=Path)
    parser.add_argument('--record',type=Path)
    parser.add_argument('--native-only',action='store_true',help='Use only the explicit AArch64/provider pair; reuse already passing host evidence')
    parser.add_argument('--deepbind',action='store_true',help='Run only the new actual NOW|DEEPBIND client/libc binding controls')
    parser.add_argument('--connection',action='store_true',help='Run only opt-in five-provider NOW|DEEPBIND controls; no private memory reads')
    parser.add_argument('--resource-thread',action='store_true',help='Run only explicit receiver-to-libc event-wait binding controls; fixture bridge is not production readiness')
    parser.add_argument('--audit-source-dir',type=Path,help='Explicit private directory containing exactly the two borrowed own import/audit sources; never native inputs')
    parser.add_argument('--case',action='append',choices=('normal','wrong_process','missing_api','eager_now',
        'environment_now','constructor_binding','bad_owned_entry','bad_native_rva','inactive_default','weak_default_inactive',*DEEP_CASES,*CONNECTION_CASES,*RESOURCE_THREAD_CASES),
        help='focused case selection; the receipt records its limited scope')
    args=parser.parse_args()
    if bool(args.qemu)!=bool(args.providers): parser.error('explicit QEMU and providers must be supplied together')
    if args.native_only and not args.qemu:parser.error('--native-only requires the explicit QEMU/provider pair')
    if args.connection and args.deepbind:parser.error('--connection and --deepbind are separate focused scopes')
    if args.resource_thread and (args.connection or args.deepbind):parser.error('--resource-thread is a separate explicit fixture scope')
    if args.case:
        if args.connection and any(case not in CONNECTION_CASES for case in args.case):
            parser.error('--connection accepts only connection fixture cases')
        if args.deepbind and any(case not in DEEP_CASES for case in args.case):
            parser.error('--deepbind accepts only mutex deepbind fixture cases')
        if args.resource_thread and any(case not in RESOURCE_THREAD_CASES for case in args.case):parser.error('--resource-thread accepts only its own fixture cases')
        if not args.resource_thread and any(case in RESOURCE_THREAD_CASES for case in args.case):parser.error('event-wait cases require explicit --resource-thread')
        if not (args.connection or args.deepbind or args.resource_thread) and any(case in DEEP_CASES+CONNECTION_CASES for case in args.case):
            parser.error('selected deepbind cases require their explicit --deepbind or --connection scope')
    if args.audit_source_dir:
        AUDIT_ROOT=private_audit_source(args.audit_source_dir);HEADER=AUDIT_ROOT/'import/import_startup_audit.h';CORE=AUDIT_ROOT/'import/import_startup_audit.cpp'
    # Reuse the proven client/libc fixtures alongside the eighth receiver-only
    # binding. The public CLI scopes remain mutually exclusive above.
    if args.resource_thread:args.connection=True
    helper_path=ROOT/'tools/run_import_native_forwarding.py'
    spec=importlib.util.spec_from_file_location('owned_runner',helper_path)
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    report={'status':'failed','frame_access':False,'native_openvr_init_executed':False,'source_sha256':{'core':sha(CORE),'header':sha(HEADER),'fixture':sha(Path(__file__).resolve())},'architectures':{}}
    report['borrowed_own_source_override']=bool(args.audit_source_dir)
    report['selected_cases']=args.case or list(RESOURCE_THREAD_CASES) if args.resource_thread else args.case or list(CONNECTION_CASES) if args.connection else \
        args.case or list(DEEP_CASES) if args.deepbind else args.case or 'complete own-source API audit fixture'
    clean=os.environ.copy()
    for name in ('LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH','QEMU_LD_PREFIX'): clean.pop(name,None)
    try:
        if args.deepbind or args.connection:
            run_deepbind(args,helper,report,clean)
            return
        architectures=[] if args.native_only else [('host','g++',None)]
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
                common=[compiler,'-std=c++17','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-I',str(AUDIT_ROOT),'-I',str(ROOT)]
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
        source_changed=report['source_sha256']['core']!=sha(CORE) or report['source_sha256']['header']!=sha(HEADER)
        if source_changed:
            report['status']='failed';report['failure']='Own audit source changed during fixture execution'
        cache=ROOT/'tools/__pycache__'
        if cache.exists():
            for path in cache.glob('run_import_native_forwarding.*.pyc'):path.unlink()
            if not any(cache.iterdir()):cache.rmdir()
        if args.record:
            args.record.parent.mkdir(parents=True,exist_ok=True)
            args.record.write_text(json.dumps(report,indent=2)+'\n');args.record.chmod(0o600)
        if source_changed:raise SystemExit(report['failure'])


if __name__=='__main__':main()
