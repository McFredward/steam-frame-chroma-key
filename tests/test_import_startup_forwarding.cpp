#include "import/import_startup_forwarding.h"
#include "receiver/sha256.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <link.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

// Actual controlled host function providers and synthetic owned Entry/auditor
// labels exercise the forwarding ABI. This is not native OpenVR interposition,
// actual audit lifetime proof or execution of the AArch64 entry veneer.
using namespace frame_import_startup_forwarding;
using frame_import_native::Entry;
namespace audit=frame_import_startup_audit;
namespace {
std::atomic<unsigned> checks{0};
void require(bool ok,const char* message) {
    const auto count=checks.fetch_add(1)+1;
    if(!ok){std::fprintf(stderr,"%u: %s\n",count,message);std::abort();}
}
struct NativeUnwind { unsigned value; };
enum class Mode { Normal, ThrowInit, ThrowShutdown, Cancel };
Mode mode=Mode::Normal;
constexpr std::uint32_t token=0xfedcba98,error_value=0xabcdef12;
constexpr char startup[]="controlled startup";
struct State {
    Notification notifications[128]{};
    std::atomic<unsigned> observed{0},init_calls{0},shutdown_calls{0};
    std::atomic<bool> entered{false},release{false},recursive_callback{false};
    Entry* mutate_original=nullptr;
    Entry recursive_entry{};
    bool native_recursion=false;
} state;
std::uint32_t native_init(std::uint32_t* error,std::uint32_t type,const char* text) {
    state.init_calls.fetch_add(1);
    require(error && type==1 && text==startup,"Native init arguments changed");
    require(errno==ENOMSG,"Before callback changed native incoming errno");
    *error=error_value;
    if(state.native_recursion){state.native_recursion=false;
        require(frame_import_vr_init_dispatch(&state.recursive_entry)==token,"Nested native result changed");}
    if(mode==Mode::ThrowInit){errno=EILSEQ;throw NativeUnwind{42};}
    if(mode==Mode::Cancel){
        state.entered.store(true,std::memory_order_release);
        while(!state.release.load(std::memory_order_acquire))sched_yield();
        errno=EINTR;pthread_testcancel();
    }
    errno=EBUSY;return token;
}
void native_shutdown() {
    state.shutdown_calls.fetch_add(1);
    require(errno==ENOMSG,"Before callback changed shutdown errno");
    const auto count=state.observed.load();
    require(count && state.notifications[count-1].operation==Operation::Shutdown &&
            state.notifications[count-1].phase==Phase::Before,"Shutdown ran before its Before callback");
    if(mode==Mode::ThrowShutdown){errno=EOVERFLOW;throw NativeUnwind{43};}
    errno=ECHILD;
}
std::uint32_t wrong_provider(std::uint32_t*,std::uint32_t,const char*) {std::abort();}
void wrong_notify(Notification,void*) noexcept {std::abort();}
void observe(Notification n,void* owner) noexcept {
    if(owner!=&state)std::abort();
    const auto at=state.observed.fetch_add(1);
    if(at>=128)std::abort();
    state.notifications[at]=n;
    if(state.mutate_original)*state.mutate_original={};
    if(n.phase==Phase::Before && n.operation==Operation::Init &&
       state.recursive_callback.exchange(false)) {
        if(frame_import_vr_init_dispatch(&state.recursive_entry)!=token)std::abort();
    }
    n.entry={};n.result_token=0; // A notification is an independent value copy.
    errno=EDOM;
}
Entry entry(std::uint32_t* error) {
    Entry e{};e.x[0]=reinterpret_cast<std::uintptr_t>(error);
    e.x[1]=0xfeed000000000001ULL;e.x[2]=reinterpret_cast<std::uintptr_t>(startup);
    e.x[19]=0x111119;e.x[29]=0x100000;e.x[30]=0x50002ae94;e.sp=0x100000;return e;
}
bool same(const Entry& a,const Entry& b){return a.sp==b.sp && a.x==b.x;}
PinnedModule host_module(const char* path) {
    Dl_info info{};require(dladdr(reinterpret_cast<void*>(native_init),&info)!=0 && info.dli_fbase,
                          "Actual host provider module missing");
    struct Find { std::uintptr_t target;PinnedModule result; } find{reinterpret_cast<std::uintptr_t>(native_init),{}};
    dl_iterate_phdr([](dl_phdr_info* image,std::size_t,void* ptr){
        auto& out=*static_cast<Find*>(ptr);
        for(unsigned i=0;i<image->dlpi_phnum;++i){
            const auto& h=image->dlpi_phdr[i];
            const auto begin=image->dlpi_addr+h.p_vaddr,end=begin+h.p_memsz;
            if(h.p_type==PT_LOAD && (h.p_flags&PF_X) && out.target>=begin && out.target<end){
                out.result.rx_begin=begin;out.result.rx_end=end;return 1;
            }
        }return 0;
    },&find);
    find.result.bias=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    frame_receiver::Sha256 hash;std::ifstream file(path,std::ios::binary);
    require(bool(file),"Fixture executable file missing");char bytes[4096];
    while(file){file.read(bytes,sizeof(bytes));hash.update(bytes,static_cast<std::size_t>(file.gcount()));}
    find.result.file_identity=hash.finish();return find.result;
}
audit::ModuleView view(const PinnedModule& p,unsigned actual_fixture_label) {
    return {p.bias,p.rx_begin,p.rx_end,actual_fixture_label};
}
void* cancel_init(void* ptr) {errno=ENOMSG;frame_import_vr_init_dispatch(static_cast<Entry*>(ptr));return nullptr;}
bool wait_entered(){for(unsigned i=0;i<2000;++i){if(state.entered.load(std::memory_order_acquire))return true;usleep(1000);}return false;}
}
int main(int argc,char** argv) {
    require(argc>0,"Missing executable path");
    const auto module=host_module(argv[0]);
    PreparedModules modules{module,module,module};
    // Labels here are intentionally fixture inputs, not claimed la_objopen facts.
    audit::BoundProvider views{view(module,9),view(module,10),view(module,11),audit::Function::None,0};
    auto bind=views;bind.function=audit::Function::Init;bind.original=reinterpret_cast<std::uintptr_t>(native_init);
    require(!providers_prepared() && !provider_matches(bind) && !attach_views(views),
            "Unprepared bridge exposed a callable route");
    Providers p{native_init,native_shutdown};Callbacks c{observe,&state};
    auto bad=p;bad.init=nullptr;require(!prepare_once(bad,c,modules),"Missing init provider accepted");
    bad=p;bad.shutdown=nullptr;require(!prepare_once(bad,c,modules),"Missing shutdown provider accepted");
    require(!prepare_once(p,{},modules),"Missing callback descriptor accepted");
    auto wrong=modules;wrong.api.file_identity={};require(!prepare_once(p,c,wrong),"Unprepared identity accepted");
    wrong=modules;wrong.api.rx_end=reinterpret_cast<std::uintptr_t>(native_init);
    require(!prepare_once(p,c,wrong),"Provider outside prepared RX accepted");
    require(prepare_once(p,c,modules) && providers_prepared(),"Provider preparation failed");
    p.init=wrong_provider;c.notify=wrong_notify;c.context=nullptr;modules.api={};
    require(!prepare_once(p,c,modules),"Once-prepared providers replaced");
    require(!provider_matches(bind),"Binding exposed entry before real audit views attached");
    auto v=views;v.api.observed_open_generation=0;require(!attach_views(v),"Invented zero lifetime accepted");
    v=views;v.observer.rx_end-=1;require(!attach_views(v),"Mismatched observer geometry accepted");
    v=views;v.function=audit::Function::Init;require(!attach_views(v),"Bind metadata used for attachment");
    require(attach_views(views) && !attach_views(views),"Audit views not copied exactly once");
    require(provider_matches(bind),"Actual init provider binding refused");
    auto shutdown=bind;shutdown.function=audit::Function::Shutdown;shutdown.original=reinterpret_cast<std::uintptr_t>(native_shutdown);
    require(provider_matches(shutdown),"Actual shutdown provider binding refused");
    for(unsigned i=0;i<3;++i){auto mismatch=bind;
        auto* part=i==0?&mismatch.receiver:(i==1?&mismatch.api:&mismatch.observer);
        ++part->observed_open_generation;require(!provider_matches(mismatch),"Different native module lifetime accepted");}
    auto mismatch=bind;mismatch.original+=4;require(!provider_matches(mismatch),"Different original provider accepted");
    mismatch=bind;mismatch.function=audit::Function::None;require(!provider_matches(mismatch),"Missing provider kind accepted");

    std::uint32_t error=0;auto e=entry(&error);const auto original=e;
    state.recursive_entry=entry(&error);state.mutate_original=&e;
    errno=ENOMSG;require(frame_import_vr_init_dispatch(&e)==token,"Native unsigned token changed");
    require(error==error_value && errno==EBUSY,"Native error output or errno changed");
    require(state.observed==2 && same(state.notifications[0].entry,original) && same(state.notifications[1].entry,original),
            "Borrowed original Entry mutated active notification/provider arguments");
    require(state.notifications[0].phase==Phase::Before && state.notifications[0].preserved_errno==ENOMSG &&
            state.notifications[1].phase==Phase::After && state.notifications[1].result_token==token &&
            state.notifications[1].preserved_errno==EBUSY,"Actual init outcome not copied");
    state.mutate_original=nullptr;e=original;
    unsigned at=state.observed;errno=ENOMSG;frame_import_vr_shutdown_dispatch(&e);
    require(errno==ECHILD && state.observed==at+2 && state.notifications[at+1].phase==Phase::After,
            "Shutdown native outcome changed");

    mode=Mode::ThrowInit;at=state.observed;errno=ENOMSG;bool caught=false;
    try{frame_import_vr_init_dispatch(&e);}catch(const NativeUnwind& ex){caught=ex.value==42;}
    require(caught && errno==EILSEQ && state.observed==at+2 && state.notifications[at+1].phase==Phase::Abandon &&
            state.notifications[at+1].preserved_errno==EILSEQ,"Init unwind fabricated After or changed exception/errno");
    mode=Mode::ThrowShutdown;at=state.observed;errno=ENOMSG;caught=false;
    try{frame_import_vr_shutdown_dispatch(&e);}catch(const NativeUnwind& ex){caught=ex.value==43;}
    require(caught && errno==EOVERFLOW && state.observed==at+2 && state.notifications[at+1].phase==Phase::Abandon,
            "Shutdown unwind stopped or fabricated completion");
    mode=Mode::Normal;state.native_recursion=true;at=state.observed;errno=ENOMSG;
    require(frame_import_vr_init_dispatch(&e)==token && errno==EBUSY,"Reentrant native provider changed outcome");
    require(state.observed==at+4 && state.notifications[at].phase==Phase::Before &&
            state.notifications[at+1].phase==Phase::Before && state.notifications[at+2].phase==Phase::After &&
            state.notifications[at+3].phase==Phase::After && !loss_status().lost,"Native recursion not reported in order");

    mode=Mode::Cancel;at=state.observed;pthread_t thread;
    require(pthread_create(&thread,nullptr,cancel_init,&e)==0,"Cancellation fixture start failed");
    require(wait_entered(),"Native provider did not enter controlled cancellation boundary");
    require(pthread_cancel(thread)==0,"Native provider cancellation failed");
    state.release.store(true,std::memory_order_release);void* result=nullptr;
    require(pthread_join(thread,&result)==0 && result==PTHREAD_CANCELED,"Native forced unwind failed to escape dispatch");
    require(state.observed==at+2 && state.notifications[at+1].phase==Phase::Abandon &&
            state.notifications[at+1].preserved_errno==EINTR,"Cancellation lacked bounded Abandon or fabricated After");

    mode=Mode::Normal;state.recursive_callback=true;const auto calls=state.init_calls.load();at=state.observed;errno=ENOMSG;
    require(frame_import_vr_init_dispatch(&e)==token && errno==EBUSY && state.init_calls==calls+2,
            "Callback recursion changed real native forwarding");
    const auto loss=loss_status();require(loss.lost && loss.suppressed_lower_bound>=2 && state.observed==at+2,
                                        "Recursive notification loss was silently certified");
    require(!provider_matches(bind) && !attach_views(views),"Lost evidence still exposed new binding");
    retire_evidence();errno=ENOMSG;require(frame_import_vr_init_dispatch(&e)==token && errno==EBUSY,
                                        "Evidence retirement changed an already-bound native call");
    std::printf("%u startup forwarding checks passed: real controlled host calls, synthetic entry/audit labels; no OpenVR or Frame activation\n",checks.load());
}
