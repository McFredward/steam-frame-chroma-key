#if defined(FRAME_STARTUP_FIXTURE_LIBRARY)
// Controlled host DSO: synthetic source offsets and data, no Valve code. Link
// .fixture_rx at 0x90000 and .fixture_hmd at 0x647d78. The marker is actual mapped
// executable metadata for real dladdr; its bytes are never executed as native.
asm(".section .fixture_rx,\"ax\",@progbits\n"
    ".globl fixture_client_code\nfixture_client_code:\n"
    ".zero 0x1b4dbc\n"
    ".globl fixture_client_init_pc\nfixture_client_init_pc:\n.byte 0x90,0xc3,0,0\n"
    ".section .fixture_hmd,\"aw\",@progbits\n"
    ".globl fixture_client_hmd\nfixture_client_hmd:\n.zero 0x4000\n.previous\n");
#else
#include "import/import_startup_gate.h"
#include "receiver/sha256.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <pthread.h>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace {
unsigned checks=0;
thread_local bool forbid_allocations=false;
std::atomic<unsigned> callback_allocations{0};
void check_at(bool ok,unsigned line) { ++checks;if(!ok) {std::fprintf(stderr,"startup gate check%u failed at%u\n",checks,line);std::abort();} }
#define check(ok) check_at((ok),__LINE__)
}
void* operator new(std::size_t size) {
    if(forbid_allocations) callback_allocations.fetch_add(1);
    if(auto* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
using namespace frame_import_startup;
constexpr std::uintptr_t receiver_bias=0x500000,api_bias=0x600000;
constexpr std::size_t fixture_code_bytes=0x1b4dc0;
std::array<std::uint8_t,32> digest(const char* text) {
    std::array<std::uint8_t,32> out{};
    const auto nibble=[](char c){return unsigned(c>='a'?c-'a'+10:c-'0');};
    for(unsigned i=0;i<32;++i)out[i]=static_cast<std::uint8_t>((nibble(text[2*i])<<4)|nibble(text[2*i+1]));
    return out;
}
OwnedThreadStack stack_bounds() {
    // These tests need actual native stack ownership. ASan's optional fake
    // use-after-return stack must be disabled for this fixture; replacing the
    // cached pthread bounds with fake-stack guesses would defeat the test.
    pthread_attr_t attr;check(pthread_getattr_np(pthread_self(),&attr)==0);
    void* begin=nullptr;std::size_t size=0;check(pthread_attr_getstack(&attr,&begin,&size)==0);
    check(pthread_attr_destroy(&attr)==0);
    const auto low=reinterpret_cast<std::uintptr_t>(begin);
    auto result=OwnedThreadStack::prepare_current(low,low+size);check(result.current());return result;
}
struct Control {
    Gate* gate=nullptr;
    frame_import_native::Entry native{};
    std::array<std::uint8_t,32> expected_hash{};
    unsigned lookup_calls=0,validator_calls=0;
    bool lookup_failure=false,wrong_mapping=false,wrong_digest=false,recursive=false,unload_during_validation=false;
};
thread_local Control* control=nullptr;
int lookup(const void* pc,Dl_info* info) {
    ++control->lookup_calls;
    if(control->recursive)check(!control->gate->native_init_before(control->native));
    if(control->lookup_failure){errno=ENOTTY;return 0;}
    const int result=::dladdr(pc,info);errno=EDOM;return result;
}
MappedClient validate(const Dl_info& info,const frame_import_native::Entry& e,void* context)noexcept {
    auto& owner=*static_cast<Control*>(context);++owner.validator_calls;
    const auto base=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    if(!base || e.x[30]!=base+frame_native_client_birth::kInitReturn)return {};
    // A real mapped RX-byte hash is compared to the file-backed fixture profile.
    // This controlled host profile is not the real SteamOS module verifier.
    frame_receiver::Sha256 hash;
    hash.update(reinterpret_cast<const void*>(base+0x90000),fixture_code_bytes);
    const auto actual=hash.finish();
    if(actual!=owner.expected_hash)return {};
    if(owner.unload_during_validation)owner.gate->before_unload();
    MappedClient out{base,base+0x90000,base+0x244dc0,77,actual};
    if(owner.wrong_mapping)out.load_bias+=4096;
    if(owner.wrong_digest)out.code_identity[0]^=1;
    return out;
}
std::array<std::uint8_t,32> file_code_hash(const char* path) {
    std::ifstream input(path,std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    check(data.size()>64 && data[0]==0x7f && data[4]==2);
    auto get=[&](std::size_t at,auto& value){check(at+sizeof(value)<=data.size());std::memcpy(&value,data.data()+at,sizeof(value));};
    std::uint64_t ph=0;std::uint16_t size=0,count=0;get(32,ph);get(54,size);get(56,count);
    for(unsigned i=0;i<count;++i){
        const auto at=ph+i*size;std::uint32_t type=0,flags=0;std::uint64_t off=0,va=0,filesz=0;
        get(at,type);get(at+4,flags);get(at+8,off);get(at+16,va);get(at+32,filesz);
        if(type==1 && (flags&1) && va<=0x90000 && va+filesz>=0x244dc0){
            const auto begin=off+0x90000-va;check(begin+fixture_code_bytes<=data.size());
            frame_receiver::Sha256 hash;hash.update(data.data()+begin,fixture_code_bytes);return hash.finish();
        }
    }
    check(false);return {};
}
struct Fixture {
    alignas(16)std::array<std::uint8_t,8192> native_stack{};
    frame_import_pipe::Registry registry{500};
    Control owner{};
    Prepared prepared{};
    std::unique_ptr<Gate> gate;
    frame_import_native::Entry init{},native{};
    void* library=nullptr;
    const char* path;
    Fixture(const char* path,const std::array<std::uint8_t,32>& hash):path(path) {
        owner.expected_hash=hash;
        prepared.receiver={receiver_bias,receiver_bias,receiver_bias+0x240000,digest("31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153")};
        prepared.api={api_bias,api_bias,api_bias+0x30000,digest("4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4")};
        prepared.native_init_provider=api_bias+0x5768;prepared.native_shutdown_provider=api_bias+0x5330;
        prepared.client_code_identity=hash;prepared.stack=stack_bounds();prepared.client=InitialClient::NotLoadedAtPreflight;
        prepared.process_generation=500;prepared.lookup=lookup;prepared.validate=validate;prepared.validator_owner=&owner;
        gate=std::make_unique<Gate>(prepared,registry);owner.gate=gate.get();control=&owner;
        const auto stack=reinterpret_cast<std::uintptr_t>(native_stack.data());
        init.sp=init.x[29]=stack+1024;init.x[0]=stack+1264;init.x[1]=1;init.x[2]=stack+1152;init.x[30]=receiver_bias+0x2ae94;
    }
    ~Fixture(){gate->before_unload();if(library)check(::dlclose(library)==0);control=nullptr;}
    void load_after_before() {
        check(gate->before_vr_init(init));
        library=::dlopen(path,RTLD_NOW|RTLD_LOCAL);check(library);
        auto* site=::dlsym(library,"fixture_client_init_pc");check(site);
        Dl_info info{};check(::dladdr(site,&info));const auto base=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        check(reinterpret_cast<std::uintptr_t>(site)==base+frame_native_client_birth::kInitReturn);
        const auto sp=reinterpret_cast<std::uintptr_t>(native_stack.data());
        native.sp=native.x[29]=sp;native.x[30]=reinterpret_cast<std::uintptr_t>(site);
        native.x[0]=native.x[19]=base+frame_native_client_birth::kPipeObject;native.x[1]=native.x[0]+48;
        for(const auto& pair:std::array<std::pair<unsigned,std::uint64_t>,5>{{{0,sp+32},{8,base+0x1b957c},{16,base+0x647d78},{32,sp+416},{40,base+0xa9250}}})
            std::memcpy(native_stack.data()+pair.first,&pair.second,8);
        owner.native=native;
    }
    frame_import_pipe::ObjectToken finish_birth() {
        auto* attrs=reinterpret_cast<pthread_mutexattr_t*>(native.x[1]);check(pthread_mutexattr_init(attrs)==0);
        check(pthread_mutexattr_settype(attrs,PTHREAD_MUTEX_RECURSIVE)==0);
        errno=ECHILD;forbid_allocations=true;check(gate->native_init_before(native));check(errno==ECHILD);
        const int actual=pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(native.x[0]),attrs);
        const auto object=gate->native_init_after(native,actual);forbid_allocations=false;
        check(actual==0 && object.valid());check(pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(native.x[0]))==0);
        check(pthread_mutexattr_destroy(attrs)==0);return object;
    }
};
}
int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"controlled host DSO path required\n");return 77;}
    const auto hash=file_code_hash(argv[1]);
    // The receiver's actual Steam-runtime API replaced the distinct /opt API.
    // A known old hash, old RVA pair or mixed identity never arms startup.
    for(unsigned old=1;old<=3;++old) {
        Fixture f(argv[1],hash);
        auto prepared=f.prepared;
        if(old&1) prepared.api.file_identity=digest("a07163c436b3d791a0aa2442029d2ef0ba40a772ebf1dc6006d610c3a7934ee2");
        if(old&2) {prepared.native_init_provider=api_bias+0x5ab8;prepared.native_shutdown_provider=api_bias+0x5680;}
        Gate refused(prepared,f.registry);
        check(refused.refusal()==Refusal::Unprepared);
        check(!refused.before_vr_init(f.init)&&refused.lookups()==0&&f.registry.counters.births==0);
    }
    { Fixture f(argv[1],hash);auto wrong=f.init;wrong.x[30]+=4;
      check(!f.gate->before_vr_init(wrong));check(!f.gate->after_vr_init(wrong,1));check(f.gate->lookups()==0);
      f.load_after_before();auto unrelated=f.native;unrelated.x[30]+=4;
      check(!f.gate->native_init_before(unrelated));check(f.owner.lookup_calls==0 && f.owner.validator_calls==0);
      const auto before=f.native;const auto object=f.finish_birth();check(f.native.x==before.x && f.native.sp==before.sp);
      check(f.registry.lookup(object).reason==frame_import_pipe::Reason::PendingAssignment);
      MappedClient mapped;check(f.gate->mapping(mapped));check(mapped.module_lifetime_label==77 && mapped.code_identity==hash);
      check(f.gate->after_vr_init(f.init,91));check(f.gate->mapping(mapped));check(f.gate->lookups()==1);
      check(!f.gate->native_init_before(f.native) && f.gate->lookups()==1);
      f.gate->before_shutdown();check(!f.gate->mapping(mapped) && !f.registry.healthy()); }
    { Fixture f(argv[1],hash);f.load_after_before();f.owner.lookup_failure=true;
      errno=E2BIG;check(!f.gate->native_init_before(f.native) && errno==E2BIG);
      check(f.gate->refusal()==Refusal::LookupFailed && f.registry.counters.births==0); }
    for(unsigned variant:{0U,1U}){
      Fixture f(argv[1],hash);f.load_after_before();f.owner.wrong_mapping=variant==0;f.owner.wrong_digest=variant==1;
      check(!f.gate->native_init_before(f.native));check(f.gate->refusal()==Refusal::Mapping && f.registry.counters.births==0); }
    { Fixture f(argv[1],hash);f.load_after_before();f.owner.recursive=true;
      check(!f.gate->native_init_before(f.native));check(f.gate->refusal()==Refusal::Recursion && f.owner.lookup_calls==1 && f.owner.validator_calls==0); }
    { Fixture f(argv[1],hash);f.load_after_before();f.owner.unload_during_validation=true;
      check(!f.gate->native_init_before(f.native));check(f.gate->refusal()==Refusal::Unload && f.registry.counters.births==0); }
    { Fixture f(argv[1],hash);f.load_after_before();const std::uint64_t wrong=1;std::memcpy(f.native_stack.data()+8,&wrong,8);
      check(!f.gate->native_init_before(f.native));check(f.gate->refusal()==Refusal::NativeWindow && f.registry.counters.births==0); }
    { Fixture f(argv[1],hash);f.load_after_before();check(f.gate->native_init_before(f.native));
      check(!f.gate->native_init_after(f.native,EINVAL).valid());check(f.gate->refusal()==Refusal::NativeInitFailed && f.registry.counters.births==0); }
    { Fixture f(argv[1],hash);f.load_after_before();f.finish_birth();
      check(!f.gate->after_vr_init(f.init,0));MappedClient m;check(!f.gate->mapping(m) && f.gate->refusal()==Refusal::NativeInitializationFailed); }
    { Fixture f(argv[1],hash);f.load_after_before();check(!f.gate->after_vr_init(f.init,7));check(f.gate->refusal()==Refusal::MissingConstructor); }
    { Fixture f(argv[1],hash);f.load_after_before();check(f.gate->native_init_before(f.native));
      f.gate->abandon_initialization();check(f.registry.counters.pending_reservations==0 && !f.registry.healthy()); }
    { Fixture f(argv[1],hash);f.load_after_before();bool admitted=true;
      std::thread other([&]{admitted=f.gate->native_init_before(f.native);});other.join();
      check(!admitted && f.owner.lookup_calls==0);f.finish_birth();check(f.gate->after_vr_init(f.init,1)); }
    { Fixture f(argv[1],hash);f.load_after_before();f.finish_birth();check(f.gate->after_vr_init(f.init,1));
      check(!f.gate->before_vr_init(f.init) && f.gate->refusal()==Refusal::RepeatedInitialization); }
    { Fixture f(argv[1],hash);f.prepared.client=InitialClient::Unknown;
      Gate unavailable(f.prepared,f.registry);check(!unavailable.before_vr_init(f.init));check(unavailable.lookups()==0); }
    { Fixture f(argv[1],hash);bool admitted=true;
      std::thread other([&]{admitted=f.gate->before_vr_init(f.init);});other.join();
      check(!admitted && f.gate->refusal()==Refusal::WrongThread && f.gate->lookups()==0);
      check(!f.gate->before_vr_init(f.init)); }
    { Fixture f(argv[1],hash);f.load_after_before();auto e=f.native;
      void* guard=::mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);check(guard!=MAP_FAILED);
      e.sp=e.x[29]=reinterpret_cast<std::uintptr_t>(guard);
      check(!f.gate->native_init_before(e) && f.owner.lookup_calls==0);check(::munmap(guard,4096)==0); }
    { Fixture f(argv[1],hash);f.load_after_before();auto e=f.native;
      e.sp=e.x[29]=f.prepared.stack.region().end-16;
      check(!f.gate->native_init_before(e) && f.owner.lookup_calls==0 && f.owner.validator_calls==0);
      e=f.native;e.x[1]+=8;
      check(!f.gate->native_init_before(e) && f.owner.lookup_calls==0 && f.owner.validator_calls==0); }
    check(callback_allocations==0);
    std::printf("%u startup gate checks passed: synthetic native source frames, real host dladdr/RX hashing/pthread init; no live AArch64/SteamOS/bootstrap or namespace proof\n",checks);
}
#endif
