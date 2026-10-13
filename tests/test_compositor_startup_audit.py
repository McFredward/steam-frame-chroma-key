#!/usr/bin/env python3
"""Controlled callback and real host loader fixtures for the compositor audit.

Own fixture geometry/provider values do not authenticate native Frame modules.
No native compositor, OpenVR initialization, network or Frame operation occurs.
All generated compiler/run caches are removed after a small optional receipt.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'import/compositor_startup_audit.cpp'
HEADER = ROOT / 'import/compositor_startup_audit.h'

CALLBACK = r'''
#include "import/compositor_startup_audit.h"
#include <atomic>
#include <link.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace frame_compositor_audit;
extern "C" unsigned la_version(unsigned);
extern "C" unsigned la_objopen(link_map*,Lmid_t,std::uintptr_t*);
extern "C" void la_preinit(std::uintptr_t*);
extern "C" std::uintptr_t la_symbind64(Elf64_Sym*,unsigned,std::uintptr_t*,std::uintptr_t*,unsigned*,const char*);
extern "C" unsigned la_objclose(std::uintptr_t*);
static unsigned checks,prepares,binds,retires,losses,forward_only;
static bool deny,bad_entry;
static std::atomic<unsigned> ready_marker{1};
static BoundProvider attached;
static void expect(bool x,const char* why){++checks;if(!x){std::fprintf(stderr,"%s\n",why);std::abort();}}
extern "C" __attribute__((visibility("hidden"))) void fixture_entry(){}
extern "C" __attribute__((visibility("hidden"))) BridgeResponse fixture_bridge(BridgeRequest r)noexcept{
    if(r.stage==Stage::Loss){++losses;return{};}
    if(r.stage==Stage::Retire){++retires;expect(r.retired_module.observed_open_generation!=0,"retired real callback generation");return{};}
    if(r.stage==Stage::Prepare){++prepares;attached=r.provider;return{!deny&&!r.missed_before_preinit,0};}
    if(r.stage==Stage::ForwardOnly){++forward_only;expect(prepares==0,"early forwarding cannot arm capture");attached=r.provider;}
    ++binds;expect(r.provider.compositor.observed_open_generation==attached.compositor.observed_open_generation&&
        r.provider.libc.observed_open_generation==attached.libc.observed_open_generation&&
        r.provider.observer.observed_open_generation==attached.observer.observed_open_generation,"binding lifetime changed");
    return{!deny,bad_entry?r.provider.original:reinterpret_cast<std::uintptr_t>(&fixture_entry)};
}
static Preparation p{{"",0x1000,0x2000},{"owned-observer",0x1000,reinterpret_cast<std::uintptr_t>(&ready_marker),
    reinterpret_cast<std::uintptr_t>(&ready_marker),reinterpret_cast<std::uintptr_t>(&ready_marker)+sizeof(ready_marker)},
    {"owned-libc",0x1000,0x2000},reinterpret_cast<std::uintptr_t>(&fixture_bridge),0x1100,0x1200,0x1300,true,
    reinterpret_cast<std::uintptr_t>(&ready_marker)};
namespace frame_compositor_audit{const Preparation* preparation()noexcept{return &p;}}
static std::uintptr_t open(unsigned slot,const char* override_name=nullptr,Lmid_t ns=LM_ID_BASE){
    const char* names[]{"","owned-observer","owned-libc"};
    link_map m{};m.l_name=const_cast<char*>(override_name?override_name:names[slot]);
    m.l_addr=slot==0?0x10000000:slot==2?0x20000000:0;
    std::uintptr_t cookie=99;const auto flag=la_objopen(&m,ns,&cookie);
    expect(flag==(cookie?(slot==0?LA_FLG_BINDFROM:slot==2?LA_FLG_BINDTO:0):0),"exact bind direction flags");return cookie;
}
static std::uintptr_t call(unsigned i,std::uintptr_t ref=1,std::uintptr_t def=3,unsigned flags=0,std::uintptr_t delta=0,const char* name=nullptr){
    const char* names[]{"pthread_mutex_lock","pthread_mutex_unlock","write"};
    Elf64_Sym symbol{};symbol.st_value=0x20001100+0x100*i+delta;
    return la_symbind64(&symbol,0,&ref,&def,&flags,name?name:names[i]);
}
int main(int argc,char**argv){
    expect(argc==2,"case required");const char* mode=argv[1];
    expect(la_version(LAV_CURRENT)==LAV_CURRENT&&la_version(0)==0,"supported audit protocol only");
    if(!std::strcmp(mode,"partial"))p.native_write_rva=0;
    if(!std::strcmp(mode,"invalid_identity"))p.owned_prepared_files=false;
    if(!std::strcmp(mode,"duplicate_names"))p.observer.loader_name=p.libc.loader_name;
    if(!std::strcmp(mode,"overflow"))p.compositor.rx_rva_end=~std::uintptr_t(0);
    if(!std::strcmp(mode,"bad_bridge"))p.bridge_rva=p.observer.rx_rva_end;
    if(!std::strcmp(mode,"bad_data"))p.observer.data_rva_end=p.observer.data_rva_begin;
    if(!std::strcmp(mode,"unaligned_marker"))++p.owned_ready_rva;
    if(!std::strcmp(mode,"overlap_data"))p.observer.data_rva_begin=p.observer.rx_rva_begin;
    const auto c=open(0),o=std::strcmp(mode,"late_observer")?open(1):0,l=std::strcmp(mode,"missing")?open(2):0;
    if(!std::strcmp(mode,"late_observer")){expect(call(0)==0x20001100,"missing observer native binding preserved");expect(open(1)==2,"late actual observer lifetime copied");}
    if(!std::strcmp(mode,"eager")){ready_marker.store(0);expect(call(0)==0x20001100,"eager original preserved");ready_marker.store(1);}
    if(!std::strcmp(mode,"zero_marker"))ready_marker.store(0);
    if(!std::strcmp(mode,"early"))for(unsigned i=0;i<3;++i)expect(call(i)==reinterpret_cast<std::uintptr_t>(&fixture_entry),"verified early forwarding entry");
    if(!std::strcmp(mode,"early_loss")){expect(call(0)==reinterpret_cast<std::uintptr_t>(&fixture_entry),"verified early forwarding entry");ready_marker.store(0);
        expect(call(1)==0x20001200,"lost marker stops early forwarding");ready_marker.store(1);}
    if(!std::strcmp(mode,"deny"))deny=true;
    la_preinit(nullptr);
    if(!std::strcmp(mode,"duplicate_preinit"))la_preinit(nullptr);
    if(!std::strcmp(mode,"duplicate"))expect(open(0)==0,"duplicate main lifetime refused");
    if(!std::strcmp(mode,"namespace"))expect(open(0,nullptr,1)==0,"alternate namespace refused");
    if(!std::strcmp(mode,"unload")){auto lc=l;la_objclose(&lc);expect(open(2)==0,"closed lifetime cannot reopen");}
    if(!std::strcmp(mode,"bad_entry"))bad_entry=true;
    if(!std::strcmp(mode,"dlsym"))expect(call(0,1,3,LA_SYMB_DLSYM)==0x20001100,"dlsym call preserved");
    if(!std::strcmp(mode,"altvalue"))expect(call(0,1,3,LA_SYMB_ALTVALUE)==0x20001100,"alternative binding preserved");
    if(!std::strcmp(mode,"wrong_original"))expect(call(0,1,3,0,1)==0x20001101,"wrong provider preserved");
    if(!std::strcmp(mode,"normal")||!std::strcmp(mode,"early")){
        expect(c==1&&o==2&&l==3&&prepares==1,"actual callback slots prepare once");
        for(unsigned j=0;j<100;++j)for(unsigned i=0;i<3;++i){
            expect(call(i)==reinterpret_cast<std::uintptr_t>(&fixture_entry),"all selective originals map owned entry");
            expect(call(i,2)==0x20001100+0x100*i,"observer cannot redirect itself");
            expect(call(i,1,2)==0x20001100+0x100*i,"foreign provider cannot redirect");
            expect(call(i,1,3,0,0,"read")==0x20001100+0x100*i,"unlisted operation unchanged");
        }
        expect(binds==3,"one entry bridge per genuine provider");
        expect(forward_only==(!std::strcmp(mode,"early")?3u:0u),"early forwarding count without native capture");
        auto lc=l;la_objclose(&lc);expect(retires==1&&losses==1,"retire and sticky loss copied once");
        expect(call(0)==0x20001100,"loss prevents new redirect");
    }else{
        for(unsigned i=0;i<3;++i)expect(call(i)==0x20001100+0x100*i,"negative scope remains native");
    }
    std::printf("callback %s: %u checks\n",mode,checks);
}
'''

OBSERVER = r'''
#include "import/compositor_startup_audit.h"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
using namespace frame_compositor_audit;
static int(*lock_native)(pthread_mutex_t*),(*unlock_native)(pthread_mutex_t*);
static ssize_t(*write_native)(int,const void*,size_t);
static unsigned prepares,binds,counts[3],captures[3],losses,missed,forward_only;
static bool armed;
extern "C" { __attribute__((visibility("default"))) std::atomic<unsigned> fixture_compositor_ready{0}; }
static BoundProvider attached;
__attribute__((constructor))static void initialize(){
    const auto saved=errno;
    lock_native=reinterpret_cast<decltype(lock_native)>(dlvsym(RTLD_NEXT,"__pthread_mutex_lock","GLIBC_2.2.5"));
    unlock_native=reinterpret_cast<decltype(unlock_native)>(dlvsym(RTLD_NEXT,"__pthread_mutex_unlock","GLIBC_2.2.5"));
    write_native=reinterpret_cast<decltype(write_native)>(dlvsym(RTLD_NEXT,"__write","GLIBC_2.2.5"));
    if(lock_native&&unlock_native&&write_native)fixture_compositor_ready.store(1,std::memory_order_release);errno=saved;
}
extern "C" __attribute__((visibility("hidden"))) int fixture_lock(pthread_mutex_t*p){++counts[0];if(armed)++captures[0];return lock_native(p);}
extern "C" __attribute__((visibility("hidden"))) int fixture_unlock(pthread_mutex_t*p){++counts[1];if(armed)++captures[1];return unlock_native(p);}
extern "C" __attribute__((visibility("hidden"))) ssize_t fixture_write(int fd,const void*p,size_t n){++counts[2];if(armed)++captures[2];return write_native(fd,p,n);}
extern "C" __attribute__((visibility("default"))) BridgeResponse fixture_compositor_bridge(BridgeRequest r)noexcept{
    if(r.stage==Stage::Loss){++losses;armed=false;fixture_compositor_ready.store(0);return{};}
    if(r.stage==Stage::Retire)return{};
    if(r.stage==Stage::Prepare){++prepares;missed=r.missed_before_preinit;attached=r.provider;armed=lock_native&&unlock_native&&write_native&&!r.missed_before_preinit;return{armed,0};}
    if(r.stage==Stage::ForwardOnly){++forward_only;attached=r.provider;if(armed)return{};}
    ++binds;const auto f=r.provider.function;
    const auto native=f==Function::MutexLock?reinterpret_cast<std::uintptr_t>(lock_native):f==Function::MutexUnlock?
        reinterpret_cast<std::uintptr_t>(unlock_native):reinterpret_cast<std::uintptr_t>(write_native);
    const auto entry=f==Function::MutexLock?reinterpret_cast<std::uintptr_t>(&fixture_lock):f==Function::MutexUnlock?
        reinterpret_cast<std::uintptr_t>(&fixture_unlock):reinterpret_cast<std::uintptr_t>(&fixture_write);
    return{r.provider.original==native&&r.provider.compositor.observed_open_generation==attached.compositor.observed_open_generation&&
        r.provider.libc.observed_open_generation==attached.libc.observed_open_generation,entry};
}
extern "C" __attribute__((visibility("default"))) unsigned fixture_probe(unsigned n){return n<3?counts[n]:n==3?prepares:n==4?binds:n==5?losses:n==6?unsigned(bool(lock_native))+(unsigned(bool(unlock_native))<<1)+(unsigned(bool(write_native))<<2):n==7?missed:n<11?captures[n-8]:forward_only;}
'''

MAIN = r'''
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
#ifdef CONSTRUCTOR_CALL
__attribute__((constructor))static void early(){pthread_mutex_lock(&mutex);pthread_mutex_unlock(&mutex);}
#endif
static void expect(bool value,const char* label){if(!value){std::fprintf(stderr,"%s\n",label);std::abort();}}
int main(int argc,char**argv){
    const bool redirected=argc==2&&argv[1][0]=='1';
    auto probe=reinterpret_cast<unsigned(*)(unsigned)>(dlsym(RTLD_DEFAULT,"fixture_probe"));expect(probe,"probe missing");
    errno=EDOM;expect(pthread_mutex_lock(&mutex)==0&&errno==EDOM,"lock behavior/errno changed");
    expect(pthread_mutex_unlock(&mutex)==0&&errno==EDOM,"unlock behavior/errno changed");
    const char text='x';expect(write(-1,&text,1)==-1&&errno==EBADF,"write native error changed");
    for(unsigned i=0;i<3;++i){
        const unsigned expected=redirected?
#ifdef CONSTRUCTOR_CALL
            (i<2?2:1)
#else
            1
#endif
            :0;
        if(probe(i)!=expected)std::fprintf(stderr,"state counts=%u,%u,%u prepare=%u binds=%u loss=%u providers=%u missed=%u\n",probe(0),probe(1),probe(2),probe(3),probe(4),probe(5),probe(6),probe(7));
        expect(probe(i)==expected,"wrong actual loader redirect");expect(probe(i+8)==unsigned(redirected),"native capture enabled before actual preinit");}
    expect(probe(4)==(redirected?3u:0u),"wrong actual loader bind count");
    std::printf("loader redirect=%d checks=8\n",redirected);
}
'''


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=45, **kwargs)
    if result.returncode:
        raise RuntimeError('Command failed: ' + ' '.join(map(str, command)) + '\n' + result.stdout + result.stderr)
    return result.stdout


def load_range(path, flag):
    output = run(['readelf', '-lW', str(path)])
    ranges = []
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0] == 'LOAD' and flag in ''.join(fields[6:-1]):
            ranges.append((int(fields[2], 16), int(fields[2], 16) + int(fields[5], 16)))
    if len(ranges) != 1:
        raise RuntimeError('Own fixture requires one selected load')
    return ranges[0]


def symbol(path, name):
    for line in run(['nm', '-D', '--defined-only', str(path)]).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2].split('@')[0] == name:
            return int(fields[0], 16)
    raise RuntimeError('Missing own fixture/public provider ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    source_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (CORE, HEADER)}
    report = {'status': 'failed', 'scope': 'own source controlled callbacks and actual host ELF audit; no Frame/native authorization',
              'sources': source_hashes, 'cases': {}, 'regenerable_cache_removed': False}
    try:
        with tempfile.TemporaryDirectory(prefix='frame-compositor-audit-') as temporary:
            folder = Path(temporary)
            callback = folder / 'callback.cpp'; callback.write_text(CALLBACK)
            binary = folder / 'callback'
            run([args.cxx, '-std=c++17', '-no-pie', '-I', str(ROOT), str(callback), str(CORE), '-o', str(binary)])
            for case in ('normal', 'early', 'early_loss', 'partial', 'invalid_identity', 'duplicate_names', 'overflow', 'bad_bridge',
                         'bad_data', 'unaligned_marker', 'overlap_data', 'zero_marker', 'missing', 'late_observer', 'eager', 'deny',
                         'duplicate_preinit', 'duplicate', 'namespace', 'unload', 'bad_entry', 'dlsym', 'altvalue', 'wrong_original'):
                result = run([str(binary), case])
                report['cases']['callback_' + case] = result.strip()
            observer_source = folder / 'observer.cpp'; observer_source.write_text(OBSERVER)
            observer = folder / 'libfixture-compositor.so'
            run([args.cxx, '-std=c++17', '-shared', '-fPIC', '-I', str(ROOT), str(observer_source), '-ldl', '-pthread', '-o', str(observer)])
            main_source = folder / 'main.cpp'; main_source.write_text(MAIN)
            program = folder / 'main'; eager = folder / 'eager'; constructor = folder / 'constructor'
            for file, flags in ((program, []), (eager, ['-Wl,-z,now']), (constructor, ['-DCONSTRUCTOR_CALL=1'])):
                run([args.cxx, '-std=c++17', '-fPIE', '-pie', '-Wl,-z,lazy', str(main_source), '-ldl', '-pthread', *flags, '-o', str(file)])
            libc_line = next(line for line in run(['ldd', str(program)]).splitlines() if 'libc.so.6 =>' in line)
            libc = Path(libc_line.split('=>', 1)[1].strip().split()[0])
            compositor_rx, observer_rx, libc_rx = load_range(program, 'E'), load_range(observer, 'E'), load_range(libc, 'E')
            observer_rw = load_range(observer, 'W')
            bridge = symbol(observer, 'fixture_compositor_bridge')
            marker = symbol(observer, 'fixture_compositor_ready')
            providers = [symbol(libc, name) for name in ('pthread_mutex_lock', 'pthread_mutex_unlock', 'write')]
            prep = folder / 'preparation.cpp'
            def audit(name, enabled=True, partial=False):
                prep.write_text('#include "import/compositor_startup_audit.h"\nnamespace frame_compositor_audit {\n'
                    'static const Preparation p{{"",' + ','.join(map(str, compositor_rx)) + '},{"' + str(observer) + '",' +
                    ','.join(map(str, [*observer_rx, *observer_rw])) + '},{"' + str(libc) + '",' + ','.join(map(str, libc_rx)) + '},' +
                    ','.join(map(str, [bridge, *providers[:2], 0 if partial else providers[2]])) + ',' + ('true' if enabled else 'false') +
                    ',' + str(marker) + '};\nconst Preparation* preparation() noexcept {return &p;}\n}\n')
                file = folder / (name + '.so')
                run([args.cxx, '-std=c++17', '-shared', '-fPIC', '-O2', '-fvisibility=hidden', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
                     '-nostdlib', '-I', str(ROOT), str(CORE), str(prep), '-o', str(file)])
                if run(['nm', '-u', str(file)]).strip():
                    raise RuntimeError('Audit imports a provider')
                exported = [line.split()[-1] for line in run(['nm', '-D', '--defined-only', str(file)]).splitlines()]
                if set(exported) != {'la_version', 'la_objopen', 'la_preinit', 'la_symbind64', 'la_objclose'}:
                    raise RuntimeError('Audit exports an unexpected API')
                return file
            active = audit('active'); inactive = audit('inactive', enabled=False); partial = audit('partial', partial=True)
            clean = {key: value for key, value in os.environ.items() if not key.startswith('LD_')}
            for label, file, selected, expect_redirect, extra in (
                    ('normal', program, active, True, {}), ('eager', eager, active, False, {}),
                    ('environment_now', program, active, False, {'LD_BIND_NOW': '1'}),
                    ('constructor', constructor, active, True, {}), ('inactive', program, inactive, False, {}),
                    ('partial', program, partial, False, {})):
                env = {**clean, 'LD_AUDIT': str(selected), 'LD_PRELOAD': str(observer), **extra}
                report['cases']['loader_' + label] = run([str(file), '1' if expect_redirect else '0'], env=env).strip()
            report['audit_dynamic_imports'] = []
            report['audit_public_symbols'] = sorted(('la_version', 'la_objopen', 'la_preinit', 'la_symbind64', 'la_objclose'))
        report['regenerable_cache_removed'] = True
        report['status'] = 'passed'
        print(json.dumps({'status': 'passed', 'cases': len(report['cases']), 'regenerable_cache_removed': True}))
    except BaseException as error:
        report['failure'] = str(error)
        raise
    finally:
        # TemporaryDirectory reaps its cache for both success and failure.
        report['regenerable_cache_removed'] = True
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps(report, indent=2) + '\n'); args.record.chmod(0o600)


if __name__ == '__main__':
    main()
