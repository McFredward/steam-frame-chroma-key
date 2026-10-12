#!/usr/bin/env python3
"""Bounded raw-read/write audit controls and actual AArch64 veneer checks.

The native loader controls reuse the approved own-source deepbind fixture.
Synthetic callback cases separately test missing history and malformed copied
provider views. Neither scope proves resource acceptance or Frame operation.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


base = load('raw_base_audit', ROOT / 'tests/test_import_startup_audit.py')
abi = load('raw_entry_abi', ROOT / 'tests/test_import_entry_abi.py')
common = load('raw_run_common', ROOT / 'tools/run_import_native_forwarding.py')

CLIENT_RAW = r'''
#include <unistd.h>
#include <cstring>
extern "C" void fixture_raw_label(unsigned,std::uintptr_t);
extern "C" void fixture_raw_event(unsigned,std::uintptr_t,std::uintptr_t,std::uintptr_t,ssize_t,int);
__asm__(".text\n" CALL_ASM("raw_read","read") CALL_ASM("raw_write","write"));
// Retain exact original caller labels rather than the fixture dispatcher PC.
extern "C" ssize_t fixture_call_raw_read(int,void*,size_t);
extern "C" ssize_t fixture_call_raw_write(int,const void*,size_t);
extern "C" void fixture_return_raw_read();
extern "C" void fixture_return_raw_write();
extern "C" void fixture_raw_run() {
    fixture_raw_label(0,reinterpret_cast<std::uintptr_t>(&fixture_return_raw_read));
    fixture_raw_label(1,reinterpret_cast<std::uintptr_t>(&fixture_return_raw_write));
    int fds[2];if(pipe(fds))std::abort();char input[4]="abc",output[4]{};
    for(unsigned n=0;n<3;++n) {
        const int wf=n==1?-1:fds[1],rf=n==1?-1:fds[0];const size_t size=n==2?0:3;
        errno=67;auto result=fixture_call_raw_write(wf,input,size);auto saved=errno;
        if(result!=(n==1?-1:ssize_t(size))||saved!=(n==1?EBADF:67))std::abort();
        fixture_raw_event(1,static_cast<std::uint32_t>(wf),reinterpret_cast<std::uintptr_t>(input),size,result,saved);
        errno=71;result=fixture_call_raw_read(rf,output,size);saved=errno;
        if(result!=(n==1?-1:ssize_t(size))||saved!=(n==1?EBADF:71))std::abort();
        fixture_raw_event(0,static_cast<std::uint32_t>(rf),reinterpret_cast<std::uintptr_t>(output),size,result,saved);
    }
    if(std::memcmp(input,output,3)||close(fds[0])||close(fds[1]))std::abort();
}
'''

OBSERVER_RAW = r'''
#include <unistd.h>
#include <cstdio>
static std::uintptr_t raw_native[2],raw_labels[2],raw_args[2][3];
static unsigned raw_before[2],raw_after[2],raw_events[2],raw_binds;
static ssize_t raw_results[2];static int raw_errors[2];
extern "C" __attribute__((visibility("hidden"))) ssize_t frame_import_read_entry(int fd,void* p,size_t n) {
    frame_import_native::Entry e{};e.x[0]=static_cast<std::uint32_t>(fd);e.x[1]=reinterpret_cast<std::uintptr_t>(p);
    e.x[2]=n;e.x[30]=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));return static_cast<ssize_t>(frame_import_read_dispatch(&e));
}
extern "C" __attribute__((visibility("hidden"))) ssize_t frame_import_write_entry(int fd,const void* p,size_t n) {
    frame_import_native::Entry e{};e.x[0]=static_cast<std::uint32_t>(fd);e.x[1]=reinterpret_cast<std::uintptr_t>(p);
    e.x[2]=n;e.x[30]=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));return static_cast<ssize_t>(frame_import_write_dispatch(&e));
}
__attribute__((constructor)) static void raw_initialize() {
    raw_native[0]=reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,"read"));
    raw_native[1]=reinterpret_cast<std::uintptr_t>(dlsym(RTLD_NEXT,"write"));
    if(!raw_native[0]||!raw_native[1])std::abort();
}
static bool raw_notify(frame_import_forwarding::Notification v) noexcept {
    using O=frame_import_forwarding::Operation;
    if(v.operation!=O::Read&&v.operation!=O::Write)return false;
    const unsigned n=v.operation==O::Read?0:1;
    if(retired)return true;
    if(v.entry.x[30]!=raw_labels[n]){++unscoped_notifications;return true;}
    if(v.phase==frame_import_forwarding::Phase::Before){++raw_before[n];for(unsigned i=0;i<3;++i)raw_args[n][i]=v.entry.x[i];}
    else{++raw_after[n];raw_results[n]=static_cast<ssize_t>(v.result_bits);raw_errors[n]=v.preserved_errno;}
    return true;
}
static bool raw_bridge(BridgeRequest r,BridgeResponse& out) noexcept {
    if(r.stage!=Stage::Bind||(r.provider.function!=Function::Read&&r.provider.function!=Function::Write))return false;
    const unsigned n=r.provider.function==Function::Read?0:1;++raw_binds;
    out.ready=!retired&&r.provider.original==raw_native[n]&&r.provider.libc.bias==libc_bias&&
        r.provider.client.observed_open_generation&&r.provider.libc.observed_open_generation&&
        r.provider.client.observed_open_generation!=r.provider.libc.observed_open_generation;
    out.hidden_entry=n?reinterpret_cast<std::uintptr_t>(&frame_import_write_entry):reinterpret_cast<std::uintptr_t>(&frame_import_read_entry);
    return true;
}
extern "C" void fixture_raw_label(unsigned n,std::uintptr_t pc){if(n>1)std::abort();raw_labels[n]=pc;}
extern "C" void fixture_raw_event(unsigned n,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,ssize_t result,int saved) {
    if(n>1)std::abort();
    ++raw_events[n];
    if(raw_before[n]&&(raw_before[n]!=raw_after[n]||raw_args[n][0]!=a||raw_args[n][1]!=b||raw_args[n][2]!=c||raw_results[n]!=result||raw_errors[n]!=saved))std::abort();
}
extern "C" void fixture_raw_check(bool expected) {
    for(unsigned n=0;n<2;++n)if(raw_events[n]!=3||raw_before[n]!=(expected?3u:0u)||raw_after[n]!=raw_before[n])std::abort();
    if(raw_binds!=(expected?2u:0u))std::abort();
    if(reinterpret_cast<std::uintptr_t>(dlsym(RTLD_DEFAULT,"read"))!=raw_native[0]||
       reinterpret_cast<std::uintptr_t>(dlsym(RTLD_DEFAULT,"write"))!=raw_native[1])std::abort();
    std::printf("RAW={\"read_write_redirected\":%s,\"calls_each\":3,\"ssize_t_errno_payload_preserved\":true,\"public_providers_original\":true}\n",expected?"true":"false");
}
'''

# These cases drive real compiled callbacks with controlled copied module views.
# They establish audit admission semantics, never native pointer dereference or
# the actual loader timeline claimed by the separate ELF controls above.
CALLBACK = r'''
#include "import/import_startup_audit.h"
#include <link.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace frame_import_startup_audit;
extern "C" unsigned la_objopen(link_map*,Lmid_t,std::uintptr_t*);
extern "C" void la_preinit(std::uintptr_t*);
extern "C" std::uintptr_t la_symbind64(Elf64_Sym*,unsigned,std::uintptr_t*,std::uintptr_t*,unsigned*,const char*);
extern "C" unsigned la_objclose(std::uintptr_t*);
static Preparation p;static unsigned bindings,losses,retires;static bool reject,bad_entry;
static std::uintptr_t hidden;
static BridgeResponse fixture_bridge(BridgeRequest r) noexcept {
    if(r.stage==Stage::Prepare)return {!r.missed_before_preinit,0};
    if(r.stage==Stage::Loss){++losses;return {};}
    if(r.stage==Stage::Retire){++retires;return {};}
    if(r.provider.function!=Function::Read&&r.provider.function!=Function::Write)std::abort();
    ++bindings;hidden=r.provider.observer.bias+0x100;
    return {!reject,bad_entry?r.provider.original:hidden};
}
namespace frame_import_startup_audit {const Preparation* preparation() noexcept{return &p;}}
static void require(bool okay){if(!okay)std::abort();}
int main(int argc,char** argv) {
    require(argc==2);const char* scenario=argv[1];
    const auto bridge=reinterpret_cast<std::uintptr_t>(&fixture_bridge);
    p.receiver={"",0x100,0x1000};p.api={"api",0x100,0x1000};p.observer={"observer",0x100,0x10000};
    p.bridge_rva=0x1000;p.init_rva=0x200;p.shutdown_rva=0x300;p.owned_prepared_files=true;
    p.client={"client",0x100,0x1000};p.libc={"libc",0x100,0x1000};
    p.native_mutex_init_rva=0x200;p.native_mutex_destroy_rva=0x300;p.native_mutex_lock_rva=0x400;
    p.native_mutex_unlock_rva=0x500;p.native_connect_rva=0x600;p.native_epoll_wait_rva=0x700;
    p.native_read_rva=0x800;p.native_write_rva=0x900;
    bool prepared=true,redirect=true;
    if(!std::strcmp(scenario,"zero")){p.native_read_rva=p.native_write_rva=0;redirect=false;}
    else if(!std::strcmp(scenario,"partial_read")){p.native_read_rva=0;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"partial_write")){p.native_write_rva=0;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"no_epoll")){p.native_epoll_wait_rva=0;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"partial_connect")){p.native_connect_rva=0;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"partial_mutex")){p.native_mutex_init_rva=0;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"duplicate")){p.native_read_rva=p.native_write_rva;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"duplicate_prior")){p.native_read_rva=p.native_epoll_wait_rva;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"outside")){p.native_write_rva=0x1000;prepared=redirect=false;}
    else if(!std::strcmp(scenario,"wrong_name")){p.client.loader_name="observer";prepared=redirect=false;}
    link_map maps[5]{};std::uintptr_t cookies[5]{};const char* names[]{"","api","observer","client","libc"};
    for(unsigned i=0;i<5;++i){maps[i].l_name=const_cast<char*>(names[i]);maps[i].l_addr=i==2?bridge-0x1000:0x10000u*(i+1);la_objopen(&maps[i],LM_ID_BASE,&cookies[i]);require((cookies[i]!=0)==prepared);}
    std::uintptr_t reference=cookies[3],definition=cookies[4];
    if(!std::strcmp(scenario,"wrong_client")){reference=cookies[0];redirect=false;}
    else if(!std::strcmp(scenario,"wrong_provider")){definition=cookies[1];redirect=false;}
    Elf64_Sym symbol{};symbol.st_value=maps[4].l_addr+0x800;unsigned flags=0;
    if(!std::strcmp(scenario,"early")){require(la_symbind64(&symbol,0,&reference,&definition,&flags,"read")==symbol.st_value);redirect=false;}
    la_preinit(&cookies[0]);
    if(!std::strcmp(scenario,"client_retired")){la_objclose(&cookies[3]);redirect=false;}
    else if(!std::strcmp(scenario,"provider_retired")){la_objclose(&cookies[4]);redirect=false;}
    else if(!std::strcmp(scenario,"client_reopened")){la_objopen(&maps[3],LM_ID_BASE,&cookies[3]);redirect=false;}
    else if(!std::strcmp(scenario,"provider_reopened")){la_objopen(&maps[4],LM_ID_BASE,&cookies[4]);redirect=false;}
    else if(!std::strcmp(scenario,"dlsym")){flags=LA_SYMB_DLSYM;redirect=false;}
    else if(!std::strcmp(scenario,"altvalue")){flags=LA_SYMB_ALTVALUE;redirect=false;}
    else if(!std::strcmp(scenario,"wrong_rva")){++symbol.st_value;redirect=false;}
    else if(!std::strcmp(scenario,"bridge_refused")){reject=true;redirect=false;}
    else if(!std::strcmp(scenario,"hidden_outside")){bad_entry=true;redirect=false;}
    const auto result=la_symbind64(&symbol,0,&reference,&definition,&flags,"read");
    require(result==(redirect?hidden:symbol.st_value));
    if(!std::strcmp(scenario,"rebound")){require(bindings==1);++symbol.st_value;flags=0;require(la_symbind64(&symbol,0,&reference,&definition,&flags,"read")==symbol.st_value);require(losses==1);}
    else if(redirect){flags=0;require(la_symbind64(&symbol,0,&reference,&definition,&flags,"read")==hidden);require(bindings==1);symbol.st_value=maps[4].l_addr+0x900;require(la_symbind64(&symbol,0,&reference,&definition,&flags,"write")==hidden);require(bindings==2);}
    if(!std::strcmp(scenario,"dlsym")||!std::strcmp(scenario,"altvalue")){require(losses==1);flags=0;require(la_symbind64(&symbol,0,&reference,&definition,&flags,"read")==symbol.st_value);require(bindings==0);}
    require(sizeof(Preparation)==216&&unsigned(Function::Read)==9&&unsigned(Function::Write)==10);
    std::printf("RESULT={\"scenario\":\"%s\",\"redirected\":%s,\"bindings\":%u,\"losses\":%u,\"retirements\":%u,\"copied_callback_control_only\":true}\n",scenario,redirect?"true":"false",bindings,losses,retires);
}
'''


def abi_check(folder, logs, env):
    source = ROOT / 'import/native_connection_entry.S'
    def assemble(target, definitions, label):
        # The wrapper needs preprocessing only. Use the ordinary host compiler
        # for that step and the existing cross-binutils for actual ARM assembly;
        # CI need not install a full cross compiler for a source/CFI control.
        expanded = target.with_suffix('.s')
        result = common.recorded_run(['gcc', '-E', '-P', '-x', 'assembler-with-cpp',
            *definitions, str(source), '-o', str(expanded)], folder, logs, label+'_preprocess', 30, env)
        if result.returncode:
            return result
        return common.recorded_run(['aarch64-linux-gnu-as', '-g', str(expanded), '-o', str(target)],
                                   folder, logs, label, 30, env)
    obj = folder / 'raw-entry.o'
    result = assemble(obj, ['-DFRAME_CLIENT_RESOURCE_THREAD_PREPARATION=1',
                           '-DFRAME_CLIENT_RESOURCE_RAW_TRANSACTION=1'], 'raw_arm_entry_compile')
    if result.returncode:
        raise RuntimeError(result.stderr)
    data = obj.read_bytes()
    parsed = abi.Object(data)
    names = ('init', 'destroy', 'lock', 'unlock', 'connect', 'epoll', 'read', 'write')
    abi.ENTRIES = names
    symbols = {s.name: s for s in parsed.symbols if s.binding != 0}
    expected = set()
    for name in names:
        stem = 'frame_client_startup_epoll_wait' if name == 'epoll' else 'frame_import_' + name
        expected.update((stem + '_entry', stem + '_dispatch'))
    if set(symbols) != expected:
        raise RuntimeError('Raw object contains unexpected public aliases or functions')
    frames = abi.unwind_tables(parsed)
    text = parsed.section('.text')
    relocations = parsed.relocations(text)
    rng = random.Random(0x524157)
    covered = 0
    for name in names:
        stem = 'frame_client_startup_epoll_wait' if name == 'epoll' else 'frame_import_' + name
        entry, dispatch = symbols[stem + '_entry'], symbols[stem + '_dispatch']
        if (entry.visibility != 2 or entry.kind != 2 or entry.size != 156 or entry.value != covered or
                dispatch.section != 0 or frames[entry.value][0] != entry.size):
            raise RuntimeError('Actual raw entry symbol/CFI contract changed')
        function = abi.bounded(parsed.section_data(text), entry.value, entry.size)
        calls = [entry.value + i * 4 for i, op in enumerate(abi.instructions(function)) if op == ('bl',)]
        matching = [r for r in relocations if entry.value <= r.offset < entry.value + entry.size]
        if len(calls) != 1 or len(matching) != 1 or matching[0].symbol != dispatch.index or matching[0].offset != calls[0] or matching[0].kind != 283:
            raise RuntimeError('Actual raw dispatcher relocation changed')
        for i in range(128):
            native_result = (0, 1, abi.MASK64, 0xffffffff, 0x80000000, 1 << 63)[i] if i < 6 else None
            abi.run_function(function, entry.value, frames[entry.value][1], rng, native_result)
        covered += entry.size
    if covered != text.size:
        raise RuntimeError('Raw object has unreviewed text outside entries')
    direct = folder / 'raw-direct.o'
    result = common.recorded_run(['aarch64-linux-gnu-as', '--defsym', 'FRAME_CONNECTION_ENTRY_BUILD=1',
        '--defsym', 'FRAME_RESOURCE_THREAD_ENTRY_BUILD=1', '--defsym', 'FRAME_RESOURCE_RAW_ENTRY_BUILD=1',
        str(ROOT / 'import/native_import_entry.S'), '-o', str(direct)], folder, logs, 'raw_direct_assembler', 30, env)
    if result.returncode:
        raise RuntimeError('Shared source is no longer directly assemblable: ' + result.stderr)
    direct_object = abi.Object(direct.read_bytes())
    if direct_object.section_data(direct_object.section('.text')) != parsed.section_data(text):
        raise RuntimeError('CPP selector and pure-assembler selector produce different code')
    disabled = folder / 'cold-only.o'
    result = assemble(disabled, ['-DFRAME_CLIENT_RESOURCE_THREAD_PREPARATION=1'], 'cold_selector_without_raw')
    if result.returncode:
        raise RuntimeError(result.stderr)
    cold = abi.Object(disabled.read_bytes())
    cold_names = {s.name for s in cold.symbols if s.binding != 0}
    raw_names = {'frame_import_read_entry', 'frame_import_read_dispatch', 'frame_import_write_entry', 'frame_import_write_dispatch'}
    if cold_names != expected - raw_names:
        raise RuntimeError('Zero raw selector changed the old cold entry set')
    result = assemble(folder/'invalid-selector.o', ['-DFRAME_CLIENT_RESOURCE_RAW_TRANSACTION=1'], 'raw_selector_without_cold')
    if result.returncode == 0 or 'Raw transaction entry requires cold resource-thread entry' not in result.stderr:
        raise RuntimeError('Raw selector can bypass cold scope')
    return {'functions': len(names), 'executions': len(names) * 128, 'private_standard_entries': True,
            'every_instruction_cfi': True, 'public_libc_aliases': [], 'object_sha256': hashlib.sha256(data).hexdigest(),
            'zero_raw_selector_preserves_old_entries': True, 'raw_without_cold_rejected': True,
            'cpp_and_direct_assembler_code_equal': True, 'native_execution': False, 'native_cancellation': False}


def elf_checks(folder, run):
    """Actual host loader namespaces, lazy MAIN and NOW|DEEPBIND client."""
    observer, main = base.resource_thread_sources()
    observer = observer.replace('static void notify(frame_import_forwarding::Notification value,void*) noexcept {',
        'static bool raw_notify(frame_import_forwarding::Notification) noexcept;\n'
        'static bool raw_bridge(BridgeRequest,BridgeResponse&) noexcept;\n'
        'static void notify(frame_import_forwarding::Notification value,void*) noexcept {\nif(raw_notify(value))return;')
    observer = observer.replace('BridgeResponse out{};if(fixture_resource_thread_bridge(r,out))return out;',
        'BridgeResponse out{};if(raw_bridge(r,out))return out;if(fixture_resource_thread_bridge(r,out))return out;')
    observer += OBSERVER_RAW
    main = main.replace('static void* early;', 'extern "C" void fixture_raw_check(bool);\nstatic void* early;')
    main = main.replace('if(early){dlclose(early);early=nullptr;}dlclose(handle);',
        'const auto raw=reinterpret_cast<void(*)()>(dlsym(handle,"fixture_raw_run"));if(!raw)std::abort();\n'
        'raw();fixture_raw_check(argc>3&&!std::strcmp(argv[3],"raw"));\n'
        'if(early){dlclose(early);early=nullptr;}dlclose(handle);')
    paths = {name: folder / ('raw-' + name + '.cpp') for name in ('client', 'observer', 'main', 'api')}
    def diagnostic_source(code):
        # A fixture assertion reports its own generated line; expected failures
        # must not become an unexplained SIGABRT or an unbounded core dump.
        return '#include <cstdio>\n#include <cstdlib>\n[[noreturn]] static void raw_fixture_fail(unsigned line){std::fprintf(stderr,"RAW_FIXTURE_LINE=%u\\n",line);std::abort();}\n' + code.replace('std::abort()', 'raw_fixture_fail(__LINE__)')
    paths['client'].write_text(diagnostic_source(base.CONNECTION_CLIENT + CLIENT_RAW))
    paths['observer'].write_text(diagnostic_source(observer))
    paths['main'].write_text(diagnostic_source(main))
    paths['api'].write_text('#include <cstdint>\nextern "C" std::uint32_t VR_InitInternal2(void*,std::uint32_t,const char*){return 0;}\nextern "C" void VR_ShutdownInternal(){}\n')
    targets = {n: folder / ('raw-lib' + n + '.so') for n in ('client', 'foreign', 'observer', 'api')}
    targets['main'] = folder / 'raw-main'
    options = ['g++', '-std=c++17', '-O2', '-g', '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT)]
    for name in ('client', 'foreign'):
        run('elf_' + name + '_compile', options + ['-shared', '-fPIC', str(paths['client']), '-pthread', '-ldl', '-o', str(targets[name])])
    run('elf_observer_compile', options + ['-shared', '-fPIC', '-ftls-model=initial-exec',
        '-DRECEIVER_FILE=' + json.dumps(str(targets['main'])),
        '-DRECEIVER_EARLY_FILE=' + json.dumps(str(targets['main'])),
        '-DRECEIVER_NOW_FILE=' + json.dumps(str(targets['main'])), str(paths['observer']),
        str(ROOT / 'import/import_forwarding.cpp'), '-pthread', '-ldl', '-o', str(targets['observer'])])
    run('elf_api_compile', options + ['-shared', '-fPIC', str(paths['api']), '-o', str(targets['api'])])
    run('elf_main_compile', options + ['-rdynamic', '-Wl,-z,lazy',
        '-DCLIENT_FILE=' + json.dumps(str(targets['client'])), '-DFOREIGN_FILE=' + json.dumps(str(targets['foreign'])),
        str(paths['main']), '-Wl,--no-as-needed', str(targets['observer']), str(targets['api']),
        '-Wl,-rpath,' + str(folder), '-pthread', '-ldl', '-o', str(targets['main'])])
    result = run('elf_actual_libc', [str(targets['main']), 'provider'])
    libc_name = result.stdout.strip()
    libc = Path(libc_name).resolve(strict=True)
    def geometry(path):
        rows = []
        for line in subprocess.check_output(['readelf', '-lW', str(path)], text=True).splitlines():
            words = line.split()
            if words and words[0] == 'LOAD' and 'E' in words[6:-1]:
                rows.append((int(words[2], 16), int(words[2], 16) + int(words[4], 16)))
        if len(rows) != 1:
            raise RuntimeError('Fixture requires one file-backed executable segment')
        return rows[0]
    def symbol(path, name):
        flags = ['-D'] if path == libc else ['-a']
        for line in subprocess.check_output(['nm', *flags, '--defined-only', str(path)], text=True).splitlines():
            words = line.split()
            if len(words) >= 3 and words[-1].split('@')[0] == name:
                return int(words[0], 16)
        raise RuntimeError('Fixture symbol missing: ' + name)
    rx = {name: geometry(targets[name]) for name in ('main', 'client', 'observer', 'api')}
    rx['libc'] = geometry(libc)
    names = ('pthread_mutex_init', 'pthread_mutex_destroy', 'pthread_mutex_lock', 'pthread_mutex_unlock', 'connect', 'epoll_wait', 'read', 'write')
    providers = [symbol(libc, name) for name in names]
    bridge = symbol(targets['observer'], 'fixture_connection_bridge')
    api_rvas = [symbol(targets['api'], name) for name in ('VR_InitInternal2', 'VR_ShutdownInternal')]
    public = {line.split()[-1].split('@')[0] for line in subprocess.check_output(['nm', '-D', '--defined-only', str(targets['observer'])], text=True).splitlines() if line.split()}
    if public.intersection(names):
        raise RuntimeError('Observer introduced public libc replacements')
    def spec(name, extent):
        return '{' + json.dumps(name) + ',' + str(extent[0]) + ',' + str(extent[1]) + '}'
    cases = {}
    for mode in ('full', 'zero', 'partial_read', 'partial_write', 'no_epoll', 'foreign_client'):
        selected = list(providers)
        if mode == 'zero':
            selected[-2:] = [0, 0]
        elif mode == 'partial_read':
            selected[-2] = 0
        elif mode == 'partial_write':
            selected[-1] = 0
        elif mode == 'no_epoll':
            selected[-3] = 0
        config = folder / ('raw-config-' + mode + '.cpp')
        config.write_text('#include "import/import_startup_audit.h"\nnamespace frame_import_startup_audit {const Preparation* preparation() noexcept {static const Preparation p={' +
            spec('', rx['main']) + ',' + spec(str(targets['api']), rx['api']) + ',' + spec(str(targets['observer']), rx['observer']) + ',' +
            ','.join(map(str, [bridge, *api_rvas])) + ',true,' + spec(str(targets['client']), rx['client']) + ',' + spec(libc_name, rx['libc']) + ',' +
            ','.join(map(str, selected)) + '};return &p;}}\n')
        audit = folder / ('raw-audit-' + mode + '.so')
        run('elf_audit_compile_' + mode, options + ['-shared', '-fPIC', '-nostdlib', '-fvisibility=hidden', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
            str(ROOT / 'import/import_startup_audit.cpp'), str(config), '-o', str(audit)])
        if subprocess.check_output(['nm', '-u', str(audit)], text=True).strip():
            raise RuntimeError('Audit DSO imports namespace providers')
        valid = mode in ('full', 'zero', 'foreign_client')
        overlay = {'LD_AUDIT': str(audit), 'LD_PRELOAD': str(targets['observer']), 'LD_LIBRARY_PATH': str(folder)}
        if mode == 'foreign_client':
            overlay['FIXTURE_FOREIGN_CLIENT'] = '1'
        actual = run('elf_raw_' + mode, [str(targets['main']), '31' if mode in ('full', 'zero') else '0',
            'epoll' if valid else 'native', 'raw' if mode == 'full' else 'native'], overlay=overlay)
        proofs = [json.loads(line[4:]) for line in actual.stdout.splitlines() if line.startswith('RAW=')]
        if len(proofs) != 1 or proofs[0]['read_write_redirected'] != (mode == 'full'):
            raise RuntimeError('Actual ELF raw redirection proof missing')
        cases[mode] = proofs[0]
    return {'cases': cases, 'actual_host_loader': True, 'original_client_now_deepbind': True,
            'original_native_read_write_calls': True, 'public_libc_aliases_absent': list(names), 'native_openvr': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', required=True, type=Path)
    parser.add_argument('--scope', choices=('all', 'callbacks', 'abi', 'elf'), default='all',
                        help='Rerun only affected controls after a bounded repair')
    args = parser.parse_args()
    env = {k: v for k, v in os.environ.items() if k not in ('LD_AUDIT', 'LD_PRELOAD', 'LD_LIBRARY_PATH') and not k.startswith('GHVR_FRAME_')}
    report = {'status': 'failed', 'scope': args.scope, 'logs': {}, 'frame_access': False, 'network_access': False,
              'production_admission': False, 'source_sha256': {str(p.relative_to(ROOT)): common.digest(p) for p in
                  (ROOT / 'import/import_startup_audit.h', ROOT / 'import/import_startup_audit.cpp', ROOT / 'import/native_import_entry.S', ROOT / 'import/native_connection_entry.S', Path(__file__))}}
    try:
        with tempfile.TemporaryDirectory(prefix='frame-raw-audit-') as name:
            folder = Path(name)
            folder.chmod(0o700)
            def run(label, command, timeout=30, overlay=None):
                result = common.recorded_run(command, folder, report['logs'], label, timeout, {**env, **(overlay or {})})
                if result.returncode:
                    raise RuntimeError(label + ': ' + result.stdout[-2000:] + result.stderr[-4000:])
                return result
            if args.scope in ('all', 'callbacks'):
                cpp = folder / 'callback.cpp'
                cpp.write_text(CALLBACK)
                executable = folder / 'callback'
                run('callback_compile', ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), str(ROOT / 'import/import_startup_audit.cpp'), '-o', str(executable)])
                report['callback_cases'] = {}
                for scenario in ('normal', 'zero', 'partial_read', 'partial_write', 'no_epoll', 'partial_connect', 'partial_mutex', 'duplicate', 'duplicate_prior', 'outside', 'wrong_name', 'wrong_client', 'wrong_provider', 'early', 'client_retired', 'provider_retired', 'client_reopened', 'provider_reopened', 'dlsym', 'altvalue', 'wrong_rva', 'bridge_refused', 'hidden_outside', 'rebound'):
                    result = run('callback_' + scenario, [str(executable), scenario])
                    report['callback_cases'][scenario] = json.loads(result.stdout.split('RESULT=')[1].splitlines()[0])
            if args.scope in ('all', 'abi'):
                report['arm_entry'] = abi_check(folder, report['logs'], env)
            if args.scope in ('all', 'elf'):
                report['actual_elf'] = elf_checks(folder, run)
            report['status'] = 'passed'
            print(json.dumps({'status': report['status'], 'scope': args.scope,
                'callback_cases': len(report.get('callback_cases', {})), 'arm_entry': report.get('arm_entry'),
                'actual_elf_cases': len(report.get('actual_elf', {}).get('cases', {}))}))
    except BaseException as error:
        report['failure'] = str(error)
        raise
    finally:
        report['cleanup'] = 'Owned compiler and fixture groups reaped; all temporary owned executables and objects removed. No Valve binaries copied.'
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
