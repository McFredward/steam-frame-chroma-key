#if defined(IMPORT_LOADER_TEST_DSO)
namespace { void (*close_probe)(void*) = nullptr; void* close_probe_owner = nullptr; }
extern "C" __attribute__((visibility("default"))) int loader_lease_test_function(int input) { return input+173; }
extern "C" __attribute__((visibility("default"))) void loader_lease_close_probe(void (*probe)(void*),void* owner) {
    close_probe=probe; close_probe_owner=owner;
}
__attribute__((destructor)) static void actual_native_close_destructor() {
    if(close_probe) close_probe(close_probe_owner);
}
#else
#include "../import/import_loader_reader.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace frame_import_loader;
std::size_t checks=0;
void expect(bool value,const char* label) {
    ++checks;
    if(!value) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); }
}
frame_import_module::Profile profile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    frame_import_module::ProfileBlob blob{};
    file.read(reinterpret_cast<char*>(blob.data()),blob.size());
    expect(bool(file),"private prepared profile available");
    frame_import_module::Profile p{};
    expect(frame_import_module::decode_profile(blob,p),"hardpinned profile decoded");
    return p;
}
struct Capture {
    std::uintptr_t address=0,base=0,begin=0,end=0;
    std::array<unsigned char,16> bytes{};
    std::size_t phnum=0;
    bool read=false, bad_before=true, bad_after=true, zero=true, overflow=true, large=true,
         nonexec=true, null_destination=true, last=false, crossed=true;
};
void capture(const ScopedModule& module,void* opaque) noexcept {
    auto& c=*static_cast<Capture*>(opaque);
    c.base=module.base(); c.begin=module.code_begin(); c.end=module.code_end();
    c.phnum=module.program_header_count();
    c.read=module.copy(c.address,c.bytes.data(),c.bytes.size());
    c.bad_before=module.copy(c.begin-1,c.bytes.data(),1);
    c.bad_after=module.copy(c.end,c.bytes.data(),1);
    c.zero=module.copy(c.address,c.bytes.data(),0);
    c.overflow=module.copy(std::numeric_limits<std::uintptr_t>::max()-2,c.bytes.data(),16);
    c.large=module.copy(c.address,c.bytes.data(),4097);
    c.nonexec=module.copy(c.base,c.bytes.data(),1);
    c.null_destination=module.copy(c.address,nullptr,1);
    unsigned char last=0;
    c.last=module.copy(c.end-1,&last,1);
    c.crossed=module.copy(c.end-1,c.bytes.data(),2);
}
// Negative metadata controls still use the real loader's locked callback. They
// never manufacture a positive native lease or arbitrary PHDR pointer.
int mode=0;
struct Forward { int (*callback)(dl_phdr_info*,std::size_t,void*); void* data; };
int mutate(dl_phdr_info* info,std::size_t size,void* opaque) {
    auto& f=*static_cast<Forward*>(opaque);
    dl_phdr_info changed=*info;
    std::array<ElfW(Phdr),frame_import_module::kMaxSegments> phdr{};
    if(info->dlpi_phnum<=phdr.size() && info->dlpi_phdr) {
        std::memcpy(phdr.data(),info->dlpi_phdr,info->dlpi_phnum*sizeof(phdr[0]));
        changed.dlpi_phdr=phdr.data();
    }
    switch(mode) {
        case 1: size=0; break;
        case 2: changed.dlpi_phnum=9; break;
        case 3: changed.dlpi_phdr=nullptr; break;
        case 4:
            for(auto& p:phdr) if(p.p_type==PT_LOAD && (p.p_flags&PF_X)) p.p_flags|=PF_W;
            break;
        case 5:
            for(auto& p:phdr) if(p.p_type==PT_LOAD) p.p_memsz=0;
            break;
        case 6:
            for(auto& p:phdr) if(p.p_type==PT_LOAD) p.p_vaddr=frame_import_module::kMaxModuleBytes+1;
            break;
        case 7:
            for(auto& p:phdr) if(p.p_type==PT_LOAD) p.p_flags&=~PF_R;
            break;
        case 8:
            for(auto& p:phdr) if(p.p_type==PT_LOAD) p.p_memsz=std::numeric_limits<ElfW(Xword)>::max();
            break;
    }
    return f.callback(&changed,size,f.data);
}
int excessive_iterator(int (*callback)(dl_phdr_info*,std::size_t,void*),void* data) {
    dl_phdr_info other{}; other.dlpi_addr=1;
    for(unsigned i=0;i<300;++i) {
        const auto result=callback(&other,sizeof(other),data);
        if(result) return result;
    }
    return 0;
}
int changed_iterator(int (*callback)(dl_phdr_info*,std::size_t,void*),void* data) {
    Forward f{callback,data};
    const auto result=::dl_iterate_phdr(mutate,&f);
    return mode==9 ? -1 : result;
}
struct Reentry { Reader* reader=nullptr; Dl_info info{}; std::uintptr_t pc=0; Refusal refusal{}; };
void reenter(const ScopedModule&,void* pointer) noexcept {
    auto& r=*static_cast<Reentry*>(pointer);
    r.refusal=r.reader->inspect(r.info,r.pc,capture,nullptr);
}
struct CloseControl {
    std::atomic<bool> callback_entered{false}, close_attempt{false}, close_done{false};
    bool done_inside=false, readable_during_close=false;
    std::uintptr_t pc=0;
    int close_result=-1;
};
void native_close_entered(void* opaque) {
    static_cast<CloseControl*>(opaque)->close_attempt.store(true,std::memory_order_release);
}
void hold_for_close(const ScopedModule& module,void* pointer) noexcept {
    auto& c=*static_cast<CloseControl*>(pointer);
    c.callback_entered.store(true,std::memory_order_release);
    // Deliberately bounded offline test instrumentation; production verification
    // has no such wait, synchronization or external visitor.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!c.close_attempt.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    const auto observation_end=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
    while(std::chrono::steady_clock::now()<observation_end) {
        std::array<unsigned char,16> own{};
        c.readable_during_close=module.copy(c.pc,own.data(),own.size());
        if(!c.readable_during_close || c.close_done.load(std::memory_order_acquire)) break;
        std::this_thread::yield();
    }
    c.done_inside=c.close_done.load(std::memory_order_acquire);
}
}
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) { std::cerr<<"Usage: loader-reader host-fixture.so [private-profile.bin]\n"; return 2; }
    // The public host lease fixture needs no proprietary input. The optional
    // private reviewed profile also exercises the exact prepared-profile route.
    const auto p=argc==3 ? profile(argv[2]) : frame_import_module::Profile{};
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    expect(handle!=nullptr,"real independently unloadable host DSO loaded");
    void* function=dlsym(handle,"loader_lease_test_function");
    expect(function!=nullptr,"actual host DSO function located");
    Dl_info original{};
    expect(dladdr(function,&original)!=0,"actual public dladdr result");
    const auto pc=reinterpret_cast<std::uintptr_t>(function);
    Reader reader(p,::dl_iterate_phdr,99);
    Capture c; c.address=pc;
    errno=E2BIG;
    const auto first=reader.inspect(original,pc,capture,&c);
    if(first!=Refusal::None) std::cerr<<"initial refusal="<<static_cast<unsigned>(first)<<"\n";
    expect(first==Refusal::None,"real locked loader range inspection");
    expect(errno==E2BIG,"native errno preserved");
    expect(c.read && c.base==reinterpret_cast<std::uintptr_t>(original.dli_fbase) && c.phnum<=8 &&
           c.begin<=pc && pc<c.end,"actual matching module copied under loader callback");
    expect(!c.bad_before && !c.bad_after && !c.zero && !c.overflow && !c.large && !c.nonexec &&
           !c.null_destination && c.last && !c.crossed,"only bounded actual RX spans copied");
    Capture again; again.address=pc;
    Reader exactcopy(p,::dl_iterate_phdr,98);
    expect(exactcopy.inspect(original,pc,capture,&again)==Refusal::None,"bounded range controls repeat with new explicit scope");
    std::array<unsigned char,16> prior{}; std::memcpy(prior.data(),function,prior.size());
    expect(prior==c.bytes && prior==again.bytes,"scoped production reader copies exact actual mapped function bytes");
    expect(reader.inspect(original,pc,capture,&c)==Refusal::Repeated,"startup read is single-use");
    Reader identity(p,::dl_iterate_phdr,100);
    const auto bad_identity=identity.verify(original,pc);
    expect(bad_identity.refusal==Refusal::ModuleIdentity && !bad_identity.valid() &&
           !bad_identity.module.load_bias,"host DSO cannot impersonate current Frame client");
    Reader wrongthread(p,::dl_iterate_phdr,101);
    Refusal other=Refusal::None;
    std::thread thread([&] { other=wrongthread.inspect(original,pc,capture,&c); }); thread.join();
    expect(other==Refusal::WrongThread,"prepared reader rejects another thread");
    expect(wrongthread.inspect(original,pc,capture,&c)==Refusal::None,"wrong thread does not consume startup owner");
    for(auto changed:{0u,1u,2u}) {
        Reader r(p,::dl_iterate_phdr,102);
        auto target=original; auto caller=pc;
        if(changed==0) target.dli_fbase=nullptr;
        if(changed==1) target.dli_fbase=reinterpret_cast<void*>(1);
        if(changed==2) caller=reinterpret_cast<std::uintptr_t>(original.dli_fbase);
        const auto result=r.inspect(target,caller,capture,&c);
        expect(result==(changed==0 ? Refusal::InvalidTarget : changed==1 ? Refusal::NotFound : Refusal::CallerOutsideCode),"wrong exact base/caller refused");
    }
    Reader absent(p,nullptr,103), nolabel(p,::dl_iterate_phdr,0);
    expect(absent.inspect(original,pc,capture,&c)==Refusal::Unprepared,"missing provider refused");
    expect(nolabel.inspect(original,pc,capture,&c)==Refusal::Unprepared,"missing bookkeeping label refused");
    for(mode=1;mode<=9;++mode) {
        Reader controlled(p,changed_iterator,104);
        const auto result=controlled.inspect(original,pc,capture,&c);
        expect(result==(mode==1 ? Refusal::InvalidMetadata : mode==9 ? Refusal::NativeEnumeration : Refusal::UnsupportedLayout),"controlled invalid loader metadata refused");
    }
    Reader excess(p,excessive_iterator,110);
    expect(excess.inspect(original,pc,capture,&c)==Refusal::EnumerationLimit,"enumeration stops at fixed callback budget without target reads");
    Reader outer(p,::dl_iterate_phdr,105), inner(p,::dl_iterate_phdr,106);
    Reentry reentry{&inner,original,pc,Refusal::None};
    expect(outer.inspect(original,pc,reenter,&reentry)==Refusal::None && reentry.refusal==Refusal::Recursion,
           "recursive adapter callback rejected before native enumeration");
    expect(inner.inspect(original,pc,capture,&c)==Refusal::None,"rejected recursion does not consume another reader");
    Reader concurrent(p,::dl_iterate_phdr,107);
    CloseControl close; close.pc=pc;
    using SetProbe=void (*)(void (*)(void*),void*);
    auto set_probe=reinterpret_cast<SetProbe>(dlsym(handle,"loader_lease_close_probe"));
    expect(set_probe!=nullptr,"fixture native destructor probe resolved");
    set_probe(native_close_entered,&close);
    std::thread closer([&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!close.callback_entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
            std::this_thread::yield();
        close.close_result=dlclose(handle);
        close.close_done.store(true,std::memory_order_release);
    });
    expect(concurrent.inspect(original,pc,hold_for_close,&close)==Refusal::None,"actual loader lease during concurrent native dlclose");
    closer.join();
    expect(close.close_attempt && !close.done_inside && close.readable_during_close,"actual native close reached destructor, then blocked while exact code remained readable");
    expect(close.close_done && close.close_result==0,"native close finishes after callback release");
    Dl_info after{};
    expect(dladdr(function,&after)==0,"fixture really unloaded after callback, not NODELETE/pinned");
    Reader retired(p,::dl_iterate_phdr,108);
    expect(retired.inspect(original,pc,capture,&c)==Refusal::NotFound,"old dladdr base cannot read an unloaded module");
    std::cout<<"import loader reader: "<<checks<<" checks passed; host glibc lease only\n";
}
#endif
