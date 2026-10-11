#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <thread>

#if !defined(__x86_64__)
#error "The live PLT fixture is x86-64 only; production supports the pinned AArch64 receiver"
#endif

extern "C" int fixture_selected_lock(pthread_mutex_t*);
extern "C" int fixture_selected_unlock(pthread_mutex_t*);
extern "C" int fixture_metadata_lock(pthread_mutex_t*);
extern "C" int fixture_metadata_unlock(pthread_mutex_t*);

// The exported return labels are actual instructions following public PLT calls.
// The assembly stubs retain the System V stack alignment and native ABI, unlike
// injected caller-site arguments that would bypass the real interposed wrappers.
#define STUB(name,target) \
    ".global " name "\n.type " name ",@function\n" name ":\n" \
    "sub $8,%rsp\ncall " target "@PLT\n.global " name "_return\n" name "_return:\nadd $8,%rsp\nret\n"
__asm__(STUB("fixture_selected_lock","pthread_mutex_lock")
        STUB("fixture_selected_unlock","pthread_mutex_unlock")
        STUB("fixture_metadata_lock","pthread_mutex_lock")
        STUB("fixture_metadata_unlock","pthread_mutex_unlock"));

namespace {
pthread_mutex_t early=PTHREAD_MUTEX_INITIALIZER;
__attribute__((constructor)) void early_calls() {
    errno=123;
    if (pthread_mutex_lock(&early) || pthread_mutex_unlock(&early) || errno!=123) std::abort();
}
struct Native {
    alignas(8) unsigned char selected[128]{};
    alignas(8) unsigned char metadata[0x4400]{};
    pthread_mutex_t* m() {return reinterpret_cast<pthread_mutex_t*>(selected+0x38);}
    pthread_mutex_t* n() {return reinterpret_cast<pthread_mutex_t*>(metadata+0x4248);}
    template<typename T> void put(bool meta,std::size_t offset,T value) {
        std::memcpy((meta?metadata:selected)+offset,&value,sizeof(value));
    }
    Native() {
        pthread_mutexattr_t attributes; pthread_mutexattr_init(&attributes);
        pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_ERRORCHECK);
        if (pthread_mutex_init(m(),&attributes) || pthread_mutex_init(n(),&attributes)) std::abort();
        pthread_mutexattr_destroy(&attributes);
        put(false,0x20,std::uint64_t{123456}); put(false,0x28,std::uint32_t{7}); put(false,0x30,std::uint64_t{4294967303});
        put(true,8,std::uint32_t{7}); put(true,12,std::uint8_t{1}); put(true,0x4278,std::uint32_t{1234});
    }
    ~Native() {pthread_mutex_destroy(m());pthread_mutex_destroy(n());}
};
void checked(int result,int expected=0) {if(result!=expected || errno!=91) std::abort();}
} // namespace

int main(int argc,char** argv) {
    Native native;
    const char* mode=argc>1?argv[1]:"valid";
    const bool liveness=!std::strcmp(mode,"liveness");
    if (liveness) {
        // Keep the actual process alive before arm and after writer closure so
        // the harness can verify writer liveness independently of process life.
        if (std::getchar()!='b') return 2;
    }
    // Allow the background writer to observe a precreated arm marker.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    errno=91;
    checked(pthread_mutex_lock(native.m())); checked(pthread_mutex_lock(native.m()),EDEADLK);
    checked(pthread_mutex_unlock(native.m())); checked(pthread_mutex_unlock(native.m()),EPERM);
    checked(fixture_metadata_lock(native.n())); checked(fixture_metadata_unlock(native.n()));
    const unsigned samples=(!std::strcmp(mode,"cap") || liveness)?33:1;
    for(unsigned i=0;i<samples;++i) {
        checked(fixture_selected_lock(native.m()));
        if (!std::strcmp(mode,"incomplete")) {
            std::puts("Leaving one selected scope open intentionally");
            return 0;
        }
        if (!std::strcmp(mode,"mismatch")) native.put(true,8,std::uint32_t{8});
        if (!std::strcmp(mode,"invalid")) native.put(true,12,std::uint8_t{0});
        if (std::strcmp(mode,"missing")) {
            checked(fixture_metadata_lock(native.n()));
            checked(fixture_metadata_unlock(native.n()));
        }
        if (!std::strcmp(mode,"changed")) native.put(false,0x30,std::uint64_t{9});
        if (!std::strcmp(mode,"lock-failure")) checked(fixture_selected_lock(native.m()),EDEADLK);
        if (!std::strcmp(mode,"multiple")) {
            checked(fixture_metadata_lock(native.n())); checked(fixture_metadata_unlock(native.n()));
        }
        checked(fixture_selected_unlock(native.m()));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::puts("PLT forwarding preserved results and errno; synthetic native records only");
    if (liveness) {
        std::fflush(stdout);
        if (std::getchar()!='e') return 2;
    }
}
