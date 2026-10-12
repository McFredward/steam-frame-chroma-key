// Actual two-entry ARM64 execution with controlled own-source API providers.
// This fixture does not initialize OpenVR, inspect Valve heaps or use a GPU.
#include "import/import_startup_forwarding.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "This fixture executes actual ARM64 startup veneers"
#endif
using Entry=frame_import_native::Entry;
struct ProbeResult { Entry entry{}; std::array<std::uint64_t,8> d{}; };
static_assert(sizeof(ProbeResult)==320 && offsetof(ProbeResult,d)==256,"Shared native probe ABI changed");
// Generated from the existing, executed own-source probe in
// test_import_native_forwarding.cpp; the runner pins that source in its receipt.
#include "native_probe_assembly.h"
extern "C" std::uint64_t fixture_abi_probe(std::uintptr_t,Entry*,ProbeResult*);
extern "C" std::uint32_t frame_import_vr_init_entry(std::uint32_t*,std::uint32_t,const char*);
extern "C" void frame_import_vr_shutdown_entry();

namespace {
using namespace frame_import_startup_forwarding;
unsigned checks=0;
std::atomic<unsigned> before{0},after{0},abandon{0},bad{0};
std::atomic<bool> entered{false};
thread_local const Entry* expected=nullptr;
int pipe_fd[2]{-1,-1};
void require(bool ok,const char* message) {
    ++checks;if(!ok){std::fprintf(stderr,"startup ARM64 check %u: %s\n",checks,message);std::abort();}
}
void notify(Notification n,void* context) noexcept {
    if(context!=&checks || !n.entry.sp || n.entry.sp%16 || !n.entry.x[30])++bad;
    if(expected && (expected->x!=n.entry.x || expected->sp!=n.entry.sp))++bad;
    if(n.phase==Phase::Before)++before;
    else if(n.phase==Phase::After)++after;
    else ++abandon;
    n.entry.x.fill(0);errno=EDOM; // Copied callback state cannot corrupt the call.
}
std::uint32_t provider(std::uint32_t* error,std::uint32_t type,const char* startup) {
    if(type==77)throw 173;
    if(type==88){entered=true;char byte=0;const auto read_count=::read(pipe_fd[0],&byte,1);if(read_count!=1)++bad;return 0;}
    if(!error || type!=0x12345678 || !startup || std::strcmp(startup,"owned startup fixture"))++bad;
    if(errno!=ECHILD)++bad;
    *error=0x91;errno=E2BIG;return 0x89abcdef;
}
void shutdown_provider(){if(errno!=ENOTTY)++bad;errno=EPIPE;}
void* cancel_thread(void*) {
    std::uint32_t error=0;
    (void)frame_import_vr_init_entry(&error,88,"cancel");
    return nullptr;
}
PinnedModule module(std::uintptr_t first,std::uintptr_t last) {
    PinnedModule m{};m.bias=first&~std::uintptr_t{4095};m.rx_begin=m.bias;
    m.rx_end=(last&~std::uintptr_t{4095})+4096;m.file_identity.fill(0x51);
    return m; // Controlled own fixture geometry, not native Frame identity.
}
frame_import_startup_audit::ModuleView view(const PinnedModule& m,unsigned label) {
    return {m.bias,m.rx_begin,m.rx_end,label};
}
}
int main() {
    const auto init= reinterpret_cast<std::uintptr_t>(&provider);
    const auto shutdown=reinterpret_cast<std::uintptr_t>(&shutdown_provider);
    const auto low=init<shutdown?init:shutdown,high=init>shutdown?init:shutdown;
    const auto image=module(low,high);
    PreparedModules modules{image,image,image};
    require(prepare_once({provider,shutdown_provider},{notify,&checks},modules),"controlled immutable providers prepared");
    frame_import_startup_audit::BoundProvider views{};
    views.receiver=view(image,1);views.api=view(image,2);views.observer=view(image,3);
    require(attach_views(views),"controlled views attached once");
    std::uint32_t error=0;
    Entry input{};for(unsigned i=0;i<31;++i)input.x[i]=0x689871aae5547700ULL+i;
    input.x[0]=reinterpret_cast<std::uintptr_t>(&error);input.x[1]=0xaabbccdd12345678ULL;
    input.x[2]=reinterpret_cast<std::uintptr_t>("owned startup fixture");
    ProbeResult result{};expected=&input;errno=ECHILD;
    const auto token=fixture_abi_probe(reinterpret_cast<std::uintptr_t>(&frame_import_vr_init_entry),&input,&result);
    expected=nullptr;
    require(token==0x89abcdef && result.entry.x[0]==token && error==0x91,"original uint32 args/output/token ABI");
    require(errno==E2BIG,"actual provider errno survives callbacks");
    for(unsigned i=1;i<31;++i)require(result.entry.x[i]==input.x[i],"actual GP entry restore");
    require(result.entry.sp==input.sp,"actual SP restored");
    for(unsigned i=0;i<8;++i)require(result.d[i]==0x808+0x101*i,"callee-saved vector state restored");
    expected=&input;errno=ENOTTY;
    (void)fixture_abi_probe(reinterpret_cast<std::uintptr_t>(&frame_import_vr_shutdown_entry),&input,&result);
    expected=nullptr;
    require(errno==EPIPE,"void shutdown provider errno preserved");
    for(unsigned i=1;i<31;++i)require(result.entry.x[i]==input.x[i],"shutdown GP entry restore");
    require(before==2 && after==2 && !abandon && !bad,"actual original-entry snapshots and completions");
    bool caught=false;
    try{(void)frame_import_vr_init_entry(&error,77,"throw");}catch(int value){caught=value==173;}
    require(caught && abandon==1 && after==2,"provider exception unwinds actual veneer with Abandon");
    require(::pipe(pipe_fd)==0,"cancellable provider pipe");
    pthread_t thread{};require(pthread_create(&thread,nullptr,cancel_thread,nullptr)==0,"native cancellation thread");
    while(!entered.load())sched_yield();
    require(pthread_cancel(thread)==0,"cancel actual current-provider read");
    void* outcome=nullptr;require(pthread_join(thread,&outcome)==0,"cancellation reaped");
    require(outcome==PTHREAD_CANCELED && abandon==2 && after==2,"forced unwind crosses actual startup veneer and cleanup");
    require(::close(pipe_fd[0])==0 && ::close(pipe_fd[1])==0,"owned descriptors closed");
    require(!bad && !loss_status().lost,"no false completion/register/errno loss");
    std::printf("RESULT={\"checks\":%u,\"veneers\":2,\"actual_arm64\":true,\"controlled_api_providers\":true,\"exception_and_forced_unwind\":true,\"native_openvr\":false,\"frame_kernel\":false,\"headset\":false}\n",checks);
}
