// Controlled unrelated process using the actual final observer's hidden ARM
// entry. This verifies native epoll ABI/errno/cancellation, not production
// thread bootstrap, receiver identity, native OpenVR or resource admission.
#include "import/import_startup_audit.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>
#include <pthread.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

namespace {
unsigned checks=0;
void require(bool good,const char* message) {
    ++checks;
    if(!good){std::fprintf(stderr,"resource thread ABI check %u: %s\n",checks,message);std::abort();}
}
using Wait=int(*)(int,struct epoll_event*,int,int);
using Bridge=frame_import_startup_audit::BridgeResponse(*)(frame_import_startup_audit::BridgeRequest);
struct Cancellation {
    Wait entry=nullptr;
    int descriptor=-1;
    std::atomic<unsigned> entered{0},destructors{0},returned{0};
};
struct UnwindReceipt {
    Cancellation& value;
    ~UnwindReceipt(){value.destructors.fetch_add(1,std::memory_order_release);}
};
void* blocked(void* pointer) {
    auto& value=*static_cast<Cancellation*>(pointer);
    UnwindReceipt receipt{value};
    struct epoll_event event{};
    value.entered.store(1,std::memory_order_release);
    (void)value.entry(value.descriptor,&event,1,-1);
    value.returned.store(1,std::memory_order_release);
    return nullptr;
}
void pause_short() {
    const timespec delay{0,1000000};
    (void)nanosleep(&delay,nullptr);
}
} // namespace

int main(int argc,char** argv) {
    require(argc==4,"explicit final observer, pinned libc path and private entry RVA required");
    char* end=nullptr;errno=0;
    const auto entry_rva=std::strtoull(argv[3],&end,0);
    require(!errno && end && !*end && entry_rva && entry_rva%4==0 && entry_rva<8*1024*1024,
            "bounded offline-reviewed entry RVA");
    const auto native=reinterpret_cast<Wait>(dlvsym(RTLD_DEFAULT,"epoll_wait","GLIBC_2.17"));
    const auto public_wait=reinterpret_cast<Wait>(dlsym(RTLD_DEFAULT,"epoll_wait"));
    Dl_info libc{};
    require(native && public_wait==native,"unrelated public epoll_wait remains original");
    require(dladdr(reinterpret_cast<void*>(native),&libc) && libc.dli_fname &&
            !std::strcmp(libc.dli_fname,argv[2]) &&
            reinterpret_cast<std::uintptr_t>(native)-reinterpret_cast<std::uintptr_t>(libc.dli_fbase)==0xe9e10,
            "actual public provider is exact retained libc epoll_wait");
    const auto object=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    require(object,"actual observer loads inert without profile");
    const auto bridge=reinterpret_cast<Bridge>(dlsym(object,"frame_client_startup_bridge"));
    Dl_info own{};
    require(bridge && dladdr(reinterpret_cast<void*>(bridge),&own) && own.dli_fname &&
            !std::strcmp(own.dli_fname,argv[1]),"sole own public bridge identifies actual artifact base");
    require(!dlsym(object,"frame_client_startup_epoll_wait_entry"),"native epoll entry is private, not a public override");
    const auto entry=reinterpret_cast<Wait>(reinterpret_cast<std::uintptr_t>(own.dli_fbase)+entry_rva);
    Dl_info hidden{};
    require(dladdr(reinterpret_cast<void*>(entry),&hidden) && hidden.dli_fbase==own.dli_fbase,
            "reviewed hidden entry lies in actual own ELF mapping");
    for(auto stage:{frame_import_startup_audit::Stage::Prepare,frame_import_startup_audit::Stage::Bind,
                   frame_import_startup_audit::Stage::Retire}) {
        frame_import_startup_audit::BridgeRequest request{};request.stage=stage;
        errno=EDOM;const auto response=bridge(request);
        require(!response.ready && !response.hidden_entry && errno==EDOM,"unprepared bridge cannot mint admission");
    }
    require(!dlsym(RTLD_DEFAULT,"VR_InitInternal2") && !dlsym(RTLD_DEFAULT,"VRClientCoreFactory"),
            "fixture never initializes native OpenVR");

    struct epoll_event observed[2],original[2];
    std::memset(observed,0xa5,sizeof(observed));std::memcpy(original,observed,sizeof(original));
    errno=ERANGE;const auto invalid=entry(-1,observed,1,0),invalid_errno=errno;
    errno=ERANGE;const auto invalid_native=native(-1,original,1,0),invalid_native_errno=errno;
    require(invalid==-1 && invalid==invalid_native && invalid_errno==EBADF && invalid_errno==invalid_native_errno,
            "native invalid-FD result and errno preserved");
    require(!std::memcmp(observed,original,sizeof(observed)),"failed call preserves native event-buffer contents");

    const auto descriptor=epoll_create1(EPOLL_CLOEXEC);
    require(descriptor>=0,"controlled real epoll descriptor created");
    errno=EOVERFLOW;const auto empty=entry(descriptor,observed,2,0),empty_errno=errno;
    errno=EOVERFLOW;const auto empty_native=native(descriptor,original,2,0),empty_native_errno=errno;
    require(empty==0 && empty==empty_native && empty_errno==EOVERFLOW && empty_errno==empty_native_errno,
            "native zero-timeout result preserves incoming errno");
    require(!std::memcmp(observed,original,sizeof(observed)),"empty call retains native event-buffer contents");
    timespec before{},after{};
    require(clock_gettime(CLOCK_MONOTONIC,&before)==0,"controlled finite-wait start recorded");
    errno=ENOTTY;const auto finite=entry(descriptor,observed,1,8),finite_errno=errno;
    require(clock_gettime(CLOCK_MONOTONIC,&after)==0,"controlled finite-wait end recorded");
    require(finite==0 && finite_errno==ENOTTY,"finite native wait returns normally with unchanged errno");
    const auto elapsed=std::int64_t(after.tv_sec-before.tv_sec)*1000000000LL+after.tv_nsec-before.tv_nsec;
    require(elapsed>=4000000,"positive timeout reaches real native wait instead of zero-timeout substitution");
    errno=ERANGE;const auto invalid_count=entry(descriptor,observed,0,0),invalid_count_errno=errno;
    errno=ERANGE;const auto invalid_count_native=native(descriptor,original,0,0),invalid_count_native_errno=errno;
    require(invalid_count==-1 && invalid_count==invalid_count_native && invalid_count_errno==EINVAL &&
            invalid_count_errno==invalid_count_native_errno,"original maxevents argument and native rejection preserved");

    int pipe_descriptors[2]{};
    require(pipe(pipe_descriptors)==0,"controlled local pipe created without networking");
    constexpr std::uint64_t token=0x123456789abcdef0ULL;
    struct epoll_event interest{};interest.events=EPOLLIN;interest.data.u64=token;
    require(epoll_ctl(descriptor,EPOLL_CTL_ADD,pipe_descriptors[0],&interest)==0,"controlled interest registered");
    const char payload='x';require(write(pipe_descriptors[1],&payload,1)==1,"controlled pipe event delivered");
    std::memset(observed,0xa5,sizeof(observed));std::memcpy(original,observed,sizeof(original));
    errno=ECHILD;const auto ready=entry(descriptor,observed,2,0),ready_errno=errno;
    errno=ECHILD;const auto ready_native=native(descriptor,original,2,0),ready_native_errno=errno;
    require(ready==1 && ready==ready_native && ready_errno==ECHILD && ready_errno==ready_native_errno,
            "actual event result and errno match original provider");
    require((observed[0].events&EPOLLIN) && observed[0].events==original[0].events &&
            observed[0].data.u64==token && observed[0].data.u64==original[0].data.u64,
            "native readiness/event token bytes preserved");
    require(!std::memcmp(observed+1,original+1,sizeof(epoll_event)),"unused output event remains untouched");
    char received=0;require(read(pipe_descriptors[0],&received,1)==1 && received==payload,"owned pipe event drained");
    require(epoll_ctl(descriptor,EPOLL_CTL_DEL,pipe_descriptors[0],nullptr)==0,"controlled interest removed");
    require(close(pipe_descriptors[0])==0 && close(pipe_descriptors[1])==0,"owned pipe descriptors closed");

    Cancellation cancellation;cancellation.entry=entry;cancellation.descriptor=descriptor;
    pthread_t thread{};
    require(pthread_create(&thread,nullptr,blocked,&cancellation)==0,"controlled native cancellation worker started");
    for(unsigned tries=0;tries<2000 && !cancellation.entered.load(std::memory_order_acquire);++tries)pause_short();
    require(cancellation.entered.load(std::memory_order_acquire)==1,"worker entered bounded cancellation stage");
    std::puts("CANCELLATION_BEGIN=epoll_wait");std::fflush(stdout);
    require(pthread_cancel(thread)==0,"original pthread cancellation requested");
    void* result=nullptr;
    require(pthread_join(thread,&result)==0 && result==PTHREAD_CANCELED,"native cancellation exits through original unwind");
    require(cancellation.returned.load(std::memory_order_acquire)==0 &&
            cancellation.destructors.load(std::memory_order_acquire)==1,
            "C++ RAII unwound across actual own DSO and ARM entry CFI");
    require(close(descriptor)==0,"owned epoll descriptor closed");
    require(dlsym(RTLD_DEFAULT,"epoll_wait")==reinterpret_cast<void*>(native),"public provider unchanged after fixture calls");
    require(dlclose(object)==0,"owned inert observer unloaded");
    std::printf("RESULT={\"checks\":%u,\"actual_final_observer\":true,\"actual_native_libc\":true,"
                "\"public_libc_interposition\":false,\"real_epoll_event_delivery\":true,"
                "\"epoll_forced_unwind\":true,\"cold_registration\":false,\"production_ready\":false,"
                "\"native_openvr\":false,\"frame_kernel\":false,\"headset\":false}\n",checks);
}
