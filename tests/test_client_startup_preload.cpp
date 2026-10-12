// Actual final observer ELF in a deliberately unrelated process. This proves
// inert loading and real libc alias transparency, never production Ready.
// The optional connection bundle must leave every unrelated public libc call
// at its actual native provider; source hooks are admitted audit bindings only.
#include "import/import_startup_audit.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>
#include <pthread.h>
#include <sys/socket.h>

namespace {
unsigned checks=0;
void require(bool good,const char* message) {
    ++checks;if(!good){std::fprintf(stderr,"preload check %u: %s\n",checks,message);std::abort();}
}
using Init=int(*)(pthread_mutex_t*,const pthread_mutexattr_t*);
using Destroy=int(*)(pthread_mutex_t*);
using Bridge=frame_import_startup_audit::BridgeResponse(*)(frame_import_startup_audit::BridgeRequest);
}
int main(int argc,char** argv) {
    require(argc==3||argc==4,"explicit actual observer and native libc loader paths required");
    const bool connection=argc==4 && !std::strcmp(argv[3],"--connection");
    require(argc==3||connection,"unknown actual-ELF probe scope");
    const auto init=reinterpret_cast<Init>(dlsym(RTLD_DEFAULT,"pthread_mutex_init"));
    const auto destroy=reinterpret_cast<Destroy>(dlsym(RTLD_DEFAULT,"pthread_mutex_destroy"));
    // The actual forwarding core imports these explicit compatibility versions;
    // an unversioned dlsym probe may intentionally report them as absent.
    const auto real_init=reinterpret_cast<Init>(dlvsym(RTLD_DEFAULT,"__pthread_mutex_init","GLIBC_2.17"));
    const auto real_destroy=reinterpret_cast<Destroy>(dlvsym(RTLD_DEFAULT,"__pthread_mutex_destroy","GLIBC_2.17"));
    require(init&&destroy&&real_init&&real_destroy,"public and real native providers present");
    Dl_info public_info{},real_info{};
    const auto* public_provider=connection?argv[2]:argv[1];
    require(dladdr(reinterpret_cast<void*>(init),&public_info)&&public_info.dli_fname&&
            !std::strcmp(public_info.dli_fname,public_provider),"public init binds only to the expected provider");
    require(dladdr(reinterpret_cast<void*>(destroy),&public_info)&&public_info.dli_fname&&
            !std::strcmp(public_info.dli_fname,public_provider),"public destroy binds only to the expected provider");
    require(dladdr(reinterpret_cast<void*>(real_init),&real_info)&&real_info.dli_fname&&
            !std::strcmp(real_info.dli_fname,argv[2])&&
            reinterpret_cast<std::uintptr_t>(real_init)-reinterpret_cast<std::uintptr_t>(real_info.dli_fbase)==0x86a00,
            "distinct init actually binds to exact pinned native libc entry");
    require(dladdr(reinterpret_cast<void*>(real_destroy),&real_info)&&real_info.dli_fname&&
            !std::strcmp(real_info.dli_fname,argv[2])&&
            reinterpret_cast<std::uintptr_t>(real_destroy)-reinterpret_cast<std::uintptr_t>(real_info.dli_fbase)==0x86980,
            "distinct destroy actually binds to exact pinned native libc entry");
    require(connection?(init==real_init&&destroy==real_destroy):(init!=real_init&&destroy!=real_destroy),
            "connection bundle never interposes public mutex lifecycle calls");
    if(connection) {
        using Mutex=int(*)(pthread_mutex_t*);
        using Connect=int(*)(int,const struct sockaddr*,socklen_t);
        const auto lock=reinterpret_cast<Mutex>(dlsym(RTLD_DEFAULT,"pthread_mutex_lock"));
        const auto unlock=reinterpret_cast<Mutex>(dlsym(RTLD_DEFAULT,"pthread_mutex_unlock"));
        const auto connect_call=reinterpret_cast<Connect>(dlsym(RTLD_DEFAULT,"connect"));
        const auto real_lock=reinterpret_cast<Mutex>(dlvsym(RTLD_DEFAULT,"__pthread_mutex_lock","GLIBC_2.17"));
        const auto real_unlock=reinterpret_cast<Mutex>(dlvsym(RTLD_DEFAULT,"__pthread_mutex_unlock","GLIBC_2.17"));
        const auto real_connect=reinterpret_cast<Connect>(dlvsym(RTLD_DEFAULT,"__connect","GLIBC_2.17"));
        require(lock&&unlock&&connect_call&&real_lock&&real_unlock&&real_connect,"exact connection providers present");
        const std::uintptr_t public_addresses[]{reinterpret_cast<std::uintptr_t>(lock),reinterpret_cast<std::uintptr_t>(unlock),reinterpret_cast<std::uintptr_t>(connect_call)};
        const std::uintptr_t native_addresses[]{reinterpret_cast<std::uintptr_t>(real_lock),reinterpret_cast<std::uintptr_t>(real_unlock),reinterpret_cast<std::uintptr_t>(real_connect)};
        constexpr std::uintptr_t rvas[]{0x87370,0x88f74,0xebcb0};
        for(unsigned i=0;i<3;++i) {
            Dl_info own{},actual{};
            require(public_addresses[i]==native_addresses[i]&&dladdr(reinterpret_cast<void*>(public_addresses[i]),&own)&&
                    own.dli_fname&&!std::strcmp(own.dli_fname,argv[2]),"optional observer leaves unrelated public call at native libc");
            require(dladdr(reinterpret_cast<void*>(native_addresses[i]),&actual)&&actual.dli_fname&&!std::strcmp(actual.dli_fname,argv[2])&&
                    native_addresses[i]-reinterpret_cast<std::uintptr_t>(actual.dli_fbase)==rvas[i],"exact optional original provider belongs to pinned libc");
        }
        // Invalid descriptor only: no endpoint, networking or private buffers.
        errno=ERANGE;const auto observed=connect_call(-1,nullptr,0),observed_errno=errno;
        errno=ERANGE;const auto original=real_connect(-1,nullptr,0),original_errno=errno;
        require(observed==-1&&observed==original&&observed_errno==original_errno,"actual native connect result/errno unchanged");
    }
    const auto bridge=reinterpret_cast<Bridge>(dlsym(RTLD_DEFAULT,"frame_client_startup_bridge"));
    require(bridge,"own bridge present");
    require(!dlsym(RTLD_DEFAULT,"VR_InitInternal2")&&!dlsym(RTLD_DEFAULT,"VR_ShutdownInternal")&&
            !dlsym(RTLD_DEFAULT,"VRClientCoreFactory"),"inert observer never manufactures native API/client presence");
    for(auto stage:{frame_import_startup_audit::Stage::Prepare,frame_import_startup_audit::Stage::Bind,
                    frame_import_startup_audit::Stage::Retire}) {
        frame_import_startup_audit::BridgeRequest request{};request.stage=stage;
        errno=EDOM;const auto response=bridge(request);
        require(!response.ready&&!response.hidden_entry&&errno==EDOM,"unprepared bridge remains inert and preserves errno");
    }
    pthread_mutexattr_t attr{};
    require(pthread_mutexattr_init(&attr)==0,"real attributes initialized");
    require(pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK)==0,"real error-check type set");
    pthread_mutex_t observed{},native{};
    errno=ERANGE;const auto observed_result=init(&observed,&attr),observed_errno=errno;
    errno=ERANGE;const auto native_result=real_init(&native,&attr),native_errno=errno;
    require(observed_result==0&&observed_result==native_result&&observed_errno==native_errno,"original init result and errno unchanged");
    errno=EOVERFLOW;const auto observed_lock=pthread_mutex_lock(&observed),observed_lock_errno=errno;
    errno=EOVERFLOW;const auto native_lock=pthread_mutex_lock(&native),native_lock_errno=errno;
    require(observed_lock==0&&observed_lock==native_lock&&observed_lock_errno==native_lock_errno,
            "actual initialized mutex lock results and errno remain native");
    errno=ECHILD;const auto busy=destroy(&observed),busy_errno=errno;
    errno=ECHILD;const auto real_busy=real_destroy(&native),real_busy_errno=errno;
    require(busy==EBUSY&&busy==real_busy&&busy_errno==real_busy_errno,"native busy destroy result and errno unchanged");
    errno=ERANGE;const auto observed_unlock=pthread_mutex_unlock(&observed),observed_unlock_errno=errno;
    errno=ERANGE;const auto native_unlock=pthread_mutex_unlock(&native),native_unlock_errno=errno;
    require(observed_unlock==0&&observed_unlock==native_unlock&&observed_unlock_errno==native_unlock_errno,
            "original busy mutex unlock results and errno remain native");
    errno=ENOTTY;const auto done=destroy(&observed),done_errno=errno;
    errno=ENOTTY;const auto real_done=real_destroy(&native),real_done_errno=errno;
    require(done==0&&done==real_done&&done_errno==real_done_errno,"original successful destroy result and errno unchanged");
    require(pthread_mutexattr_destroy(&attr)==0,"owned attributes destroyed");
    std::printf("RESULT={\"checks\":%u,\"actual_final_preload\":true,\"actual_native_libc\":true,"
                "\"public_libc_interposition\":%s,\"production_ready\":false,\"native_openvr\":false,\"headset\":false}\n",
                checks,connection?"false":"true");
}
