// Executes hidden libc veneers from the exact final diagnostic ELF while its
// production preparation is inert. This is ARM/provider ABI evidence, not a
// compositor import, Vulkan GPU, Steam Link mask or headset result.
#include "import/compositor_startup_audit.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <initializer_list>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vulkan/vk_layer.h>

namespace {
unsigned checks=0;
void expect(bool ok){++checks;if(!ok){std::fprintf(stderr,"actual compositor ELF check %u failed\n",checks);std::abort();}}
using Mutex=int(*)(pthread_mutex_t*);
using Read=ssize_t(*)(int,void*,size_t);
Read actual_read=nullptr;
int pipe_fds[2]{};
void* blocked(void*) {char byte=0;actual_read(pipe_fds[0],&byte,1);return nullptr;}
}
int main(int argc,char** argv) {
    expect(argc==10); // Actual library plus eight hidden libc entry RVAs.
    void* dso=dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD);expect(dso);
    auto bridge=reinterpret_cast<frame_compositor_audit::Bridge>(dlsym(dso,"frame_compositor_raw_bridge"));expect(bridge);
    Dl_info info{};expect(dladdr(reinterpret_cast<void*>(bridge),&info));
    const auto base=reinterpret_cast<uintptr_t>(info.dli_fbase);
    // The same final DSO is also an implicit layer. Its negotiated entry names
    // must never interpose the application's ordinary Vulkan-loader API.
    for (const char* name : {"vkGetInstanceProcAddr", "vkGetDeviceProcAddr", "vkNegotiateLoaderLayerInterfaceVersion"}) {
        expect(!dlsym(dso, name));
    }
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(dso,"frame_compositor_raw_vkGetInstanceProcAddr"));
    auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(dso,"frame_compositor_raw_vkGetDeviceProcAddr"));
    auto negotiate=reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(
        dlsym(dso,"frame_compositor_raw_vkNegotiateLoaderLayerInterfaceVersion"));
    expect(gipa && gdpa && negotiate);
    VkNegotiateLayerInterface interface{};
    interface.sType=LAYER_NEGOTIATE_INTERFACE_STRUCT;
    interface.loaderLayerInterfaceVersion=2;
    expect(negotiate(&interface)==VK_SUCCESS && interface.loaderLayerInterfaceVersion==2);
    expect(interface.pfnGetInstanceProcAddr==gipa && interface.pfnGetDeviceProcAddr==gdpa);
    uintptr_t entries[8]{};for(unsigned i=0;i<8;++i){entries[i]=base+strtoull(argv[i+2],nullptr,16);expect(entries[i]>base);}
    expect(!dlsym(dso,"frame_compositor_raw_ready") && !dlsym(dso,"frame_import_lock_entry"));
    for(auto stage:{frame_compositor_audit::Stage::Prepare,frame_compositor_audit::Stage::Bind,frame_compositor_audit::Stage::ForwardOnly}) {
        frame_compositor_audit::BridgeRequest request{};request.stage=stage;errno=EDOM;
        const auto result=bridge(request);expect(!result.ready && !result.hidden_entry && errno==EDOM);
    }
    const char* names[]{"pthread_mutex_init","pthread_mutex_destroy","pthread_mutex_lock","pthread_mutex_unlock","write","close","read","connect"};
    const uintptr_t rvas[]{0x86a00,0x86980,0x87370,0x88f74,0xdfee0,0xda610,0xdf330,0xebcb0};
    for(unsigned i=0;i<8;++i) {
        auto p=dlsym(RTLD_DEFAULT,names[i]);Dl_info provider{};expect(p && dladdr(p,&provider));
        // No public process-wide override: only the selective audit may route
        // eligible compositor entries to the hidden forwarding functions.
        expect(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(provider.dli_fbase)==rvas[i]);
    }
    pthread_mutex_t mutex{};
    auto init=reinterpret_cast<int(*)(pthread_mutex_t*,const pthread_mutexattr_t*)>(entries[0]);
    auto destroy=reinterpret_cast<Mutex>(entries[1]),lock=reinterpret_cast<Mutex>(entries[2]),unlock=reinterpret_cast<Mutex>(entries[3]);
    errno=ERANGE;expect(init(&mutex,nullptr)==0 && errno==ERANGE);
    errno=EDOM;expect(lock(&mutex)==0 && errno==EDOM);expect(destroy(&mutex)==EBUSY);
    expect(unlock(&mutex)==0);expect(destroy(&mutex)==0);
    auto write_entry=reinterpret_cast<ssize_t(*)(int,const void*,size_t)>(entries[4]);
    auto close_entry=reinterpret_cast<int(*)(int)>(entries[5]);actual_read=reinterpret_cast<Read>(entries[6]);
    auto connect_entry=reinterpret_cast<int(*)(int,const sockaddr*,socklen_t)>(entries[7]);
    char byte=0;
    expect(write_entry(-1,&byte,1)==-1 && errno==EBADF);
    expect(close_entry(-1)==-1 && errno==EBADF);
    expect(actual_read(-1,&byte,1)==-1 && errno==EBADF);
    const int observed_connect=connect_entry(-1,nullptr,0),observed_errno=errno;
    const int native_connect=connect(-1,nullptr,0),native_errno=errno;
    expect(observed_connect==-1 && observed_connect==native_connect && observed_errno==native_errno);
    expect(pipe(pipe_fds)==0);pthread_t thread{};expect(!pthread_create(&thread,nullptr,blocked,nullptr));
    usleep(20000);expect(!pthread_cancel(thread));void* result=nullptr;
    expect(!pthread_join(thread,&result) && result==PTHREAD_CANCELED);
    expect(!close(pipe_fds[0]) && !close(pipe_fds[1]));
    std::printf("RESULT={\"checks\":%u,\"actual_final_elf\":true,\"native_libc\":true,\"read_forced_unwind\":true,\"production_admission\":false,\"headset\":false}\n",checks);
}
