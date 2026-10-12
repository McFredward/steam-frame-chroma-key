// Execute actual AArch64 veneers/dispatchers under an explicitly supplied QEMU
// user-mode runtime. libc is the private, hash-checked Frame provider; the kernel
// is the host kernel. Vulkan providers here are controlled stubs, not a GPU.
#include "import/import_forwarding.h"
#include "import/import_vulkan_forwarding.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "This fixture executes the actual AArch64 entry ABI only"
#endif

extern "C" int frame_import_init_entry(pthread_mutex_t*, const pthread_mutexattr_t*);
extern "C" int frame_import_destroy_entry(pthread_mutex_t*);
extern "C" int frame_import_lock_entry(pthread_mutex_t*);
extern "C" int frame_import_unlock_entry(pthread_mutex_t*);
extern "C" ssize_t frame_import_write_entry(int, const void*, std::size_t);
extern "C" ssize_t frame_import_read_entry(int, void*, std::size_t);
extern "C" int frame_import_close_entry(int);
extern "C" int frame_import_connect_entry(int, const sockaddr*, socklen_t);
extern "C" VkResult frame_import_image_entry(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
extern "C" VkResult frame_import_bind_entry(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
extern "C" VkResult frame_import_view_entry(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);

extern "C" int actual_init(pthread_mutex_t*, const pthread_mutexattr_t*);
extern "C" int actual_destroy(pthread_mutex_t*);
extern "C" int actual_lock(pthread_mutex_t*);
extern "C" int actual_unlock(pthread_mutex_t*);
extern "C" ssize_t actual_write(int, const void*, std::size_t);
extern "C" ssize_t actual_read(int, void*, std::size_t);
extern "C" int actual_close(int);
extern "C" int actual_connect(int, const sockaddr*, socklen_t);
__asm__(".symver actual_init,__pthread_mutex_init@GLIBC_2.17");
__asm__(".symver actual_destroy,__pthread_mutex_destroy@GLIBC_2.17");
__asm__(".symver actual_lock,__pthread_mutex_lock@GLIBC_2.17");
__asm__(".symver actual_unlock,__pthread_mutex_unlock@GLIBC_2.17");
__asm__(".symver actual_write,__write@GLIBC_2.17");
__asm__(".symver actual_read,__read@GLIBC_2.17");
__asm__(".symver actual_close,__close@GLIBC_2.17");
__asm__(".symver actual_connect,__connect@GLIBC_2.17");

using frame_import_native::Entry;
struct ProbeResult { Entry entry{}; std::array<std::uint64_t, 8> d{}; };
static_assert(sizeof(ProbeResult) == 320 && offsetof(ProbeResult, d) == 256,
              "Fixture assembly output layout changed");
extern "C" std::uint64_t fixture_abi_probe(std::uintptr_t function, Entry* inputs, ProbeResult* results);

// Load real machine registers before BLR, not a synthetic dispatcher Entry. The
// branch register, FP, LR and SP are filled into the owned expected input first.
// Preserve our own caller's ABI and capture all post-call GP/d8..d15 state before
// any C++ compiler can spill/reload it. Cancellation tests use direct prototypes
// separately, so this fixture trampoline cannot substitute for veneer unwinding.
__asm__(R"(
 .text
 .balign 4
 .global fixture_abi_probe
 .hidden fixture_abi_probe
 .type fixture_abi_probe,%function
fixture_abi_probe:
 .cfi_startproc
 sub sp,sp,#640
 .cfi_def_cfa_offset 640
 stp x29,x30,[sp]
 .cfi_offset 29,-640
 .cfi_offset 30,-632
 mov x29,sp
 stp x19,x20,[sp,#16]
 stp x21,x22,[sp,#32]
 stp x23,x24,[sp,#48]
 stp x25,x26,[sp,#64]
 stp x27,x28,[sp,#80]
 stp q8,q9,[sp,#96]
 stp q10,q11,[sp,#128]
 stp q12,q13,[sp,#160]
 stp q14,q15,[sp,#192]
 stp x0,x1,[sp,#224]
 str x2,[sp,#240]
 str x0,[x1,#128]
 mov x9,sp
 str x9,[x1,#232]
 str x9,[x1,#248]
 adr x9,.Lnative_probe_return
 str x9,[x1,#240]
 mov x9,#0x808
 fmov d8,x9
 mov x9,#0x909
 fmov d9,x9
 mov x9,#0xa0a
 fmov d10,x9
 mov x9,#0xb0b
 fmov d11,x9
 mov x9,#0xc0c
 fmov d12,x9
 mov x9,#0xd0d
 fmov d13,x9
 mov x9,#0xe0e
 fmov d14,x9
 mov x9,#0xf0f
 fmov d15,x9
 ldr x17,[sp,#232]
 ldp x0,x1,[x17]
 ldp x2,x3,[x17,#16]
 ldp x4,x5,[x17,#32]
 ldp x6,x7,[x17,#48]
 ldp x8,x9,[x17,#64]
 ldp x10,x11,[x17,#80]
 ldp x12,x13,[x17,#96]
 ldp x14,x15,[x17,#112]
 ldp x18,x19,[x17,#144]
 ldp x20,x21,[x17,#160]
 ldp x22,x23,[x17,#176]
 ldp x24,x25,[x17,#192]
 ldp x26,x27,[x17,#208]
 ldr x28,[x17,#224]
 ldr x16,[x17,#128]
 ldr x17,[x17,#136]
 blr x16
.Lnative_probe_return:
 stp x0,x1,[sp,#256]
 stp x2,x3,[sp,#272]
 stp x4,x5,[sp,#288]
 stp x6,x7,[sp,#304]
 stp x8,x9,[sp,#320]
 stp x10,x11,[sp,#336]
 stp x12,x13,[sp,#352]
 stp x14,x15,[sp,#368]
 stp x16,x17,[sp,#384]
 stp x18,x19,[sp,#400]
 stp x20,x21,[sp,#416]
 stp x22,x23,[sp,#432]
 stp x24,x25,[sp,#448]
 stp x26,x27,[sp,#464]
 stp x28,x29,[sp,#480]
 str x30,[sp,#496]
 mov x9,sp
 str x9,[sp,#504]
 add x9,sp,#512
 stp d8,d9,[x9]
 stp d10,d11,[x9,#16]
 stp d12,d13,[x9,#32]
 stp d14,d15,[x9,#48]
 ldr x9,[sp,#240]
 add x10,sp,#256
 mov x11,#40
.Lnative_probe_copy:
 ldr x12,[x10],#8
 str x12,[x9],#8
 subs x11,x11,#1
 b.ne .Lnative_probe_copy
 ldp q8,q9,[sp,#96]
 ldp q10,q11,[sp,#128]
 ldp q12,q13,[sp,#160]
 ldp q14,q15,[sp,#192]
 ldp x19,x20,[sp,#16]
 ldp x21,x22,[sp,#32]
 ldp x23,x24,[sp,#48]
 ldp x25,x26,[sp,#64]
 ldp x27,x28,[sp,#80]
 ldr x0,[sp,#256]
 ldp x29,x30,[sp]
 .cfi_restore 29
 .cfi_restore 30
 add sp,sp,#640
 .cfi_def_cfa_offset 0
 ret
 .cfi_endproc
 .size fixture_abi_probe,.-fixture_abi_probe
)");

namespace {
unsigned checks = 0;
std::uint64_t register_seed = 0x689871aae55477d1ULL;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "native ARM64 check %u: %s\n", checks, message); std::abort(); }
}
template<class T> std::uint64_t number(T value) { return (std::uint64_t)(std::uintptr_t)value; }
template<class T> T handle(std::uint64_t value) { return (T)(std::uintptr_t)value; }
thread_local const Entry* expected_entry = nullptr;
struct State {
    std::atomic<unsigned> before{0}, after{0}, bad{0};
    std::atomic<bool> entered_read{false}, entered_write{false}, entered_close{false};
};
State state;
void verify_entry(const Entry& entry) noexcept {
    if (!entry.sp || entry.sp % 16 || !entry.x[29] || !entry.x[30]) state.bad.fetch_add(1);
    if (expected_entry && (entry.x != expected_entry->x || entry.sp != expected_entry->sp)) state.bad.fetch_add(1);
}
void observe(frame_import_forwarding::Notification event, void* context) noexcept {
    if (context != &state) state.bad.fetch_add(1);
    verify_entry(event.entry);
    if (event.phase == frame_import_forwarding::Phase::Before) {
        state.before.fetch_add(1);
        if (event.operation == frame_import_forwarding::Operation::Read) state.entered_read.store(true);
        if (event.operation == frame_import_forwarding::Operation::Write) state.entered_write.store(true);
        if (event.operation == frame_import_forwarding::Operation::Close) state.entered_close.store(true);
    } else state.after.fetch_add(1);
    event.entry.x.fill(0); // Immutable-by-value notification must not affect return state.
    errno = EDOM;
}
std::uint64_t observe_vulkan(frame_import_vulkan::Notification event, void* context) noexcept {
    if (context != &state) state.bad.fetch_add(1);
    verify_entry(event.entry);
    if (event.phase == frame_import_vulkan::Phase::Before) state.before.fetch_add(1);
    else state.after.fetch_add(1);
    event.entry.x.fill(0); errno = EDOM;
    return 91;
}

std::uint64_t probe(std::uintptr_t function, std::uint64_t a0, std::uint64_t a1 = 0,
                    std::uint64_t a2 = 0, std::uint64_t a3 = 0) {
    Entry inputs{};
    // Deterministic independent register inputs vary across actual entry calls.
    for (unsigned i = 0; i < 31; ++i) {
        register_seed ^= register_seed << 13;
        register_seed ^= register_seed >> 7;
        register_seed ^= register_seed << 17;
        inputs.x[i] = register_seed;
    }
    inputs.x[0] = a0; inputs.x[1] = a1; inputs.x[2] = a2; inputs.x[3] = a3;
    expected_entry = &inputs;
    ProbeResult output{};
    const auto result = fixture_abi_probe(function, &inputs, &output);
    expected_entry = nullptr;
    require(output.entry.x[0] == result, "machine x0 result changed");
    require(output.entry.sp == inputs.sp, "machine SP changed");
    for (unsigned i = 1; i < 31; ++i) require(output.entry.x[i] == inputs.x[i], "machine saved GP/LR changed");
    for (unsigned i = 0; i < 8; ++i) require(output.d[i] == 0x808 + i * 0x101, "machine d8..d15 lane changed");
    require(state.bad == 0, "actual entry before C++ prologue was not captured exactly");
    return result;
}

void providers(const char* expected_path) {
    const void* values[] = {reinterpret_cast<void*>(actual_init), reinterpret_cast<void*>(actual_destroy),
        reinterpret_cast<void*>(actual_lock), reinterpret_cast<void*>(actual_unlock),
        reinterpret_cast<void*>(actual_write), reinterpret_cast<void*>(actual_read),
        reinterpret_cast<void*>(actual_close), reinterpret_cast<void*>(actual_connect)};
    for (const auto value : values) {
        Dl_info information{};
        require(dladdr(value, &information) && information.dli_fname,
                "actual alias provider unavailable");
        require(std::strcmp(information.dli_fname, expected_path) == 0,
                "alias resolved to another libc rather than the supplied pinned Frame libc");
    }
    std::printf("PROVIDER_LIBC=%s\n", expected_path);
}

VkDevice expected_device = handle<VkDevice>(0x765400);
const void* expected_info = nullptr;
const void* expected_allocator = nullptr;
const void* expected_output = nullptr;
VkResult vk_result = VK_SUCCESS;
bool throw_vulkan = false;
struct NativeException {};
VkResult VKAPI_CALL image_provider(VkDevice device, const VkImageCreateInfo* info,
                                  const VkAllocationCallbacks* allocator, VkImage* output) {
    require(device == expected_device && info == expected_info && allocator == expected_allocator &&
            output == expected_output, "actual image veneer changed provider arguments");
    if (throw_vulkan) throw NativeException{};
    *output = handle<VkImage>(0x12345678); errno = EBUSY; return vk_result;
}
VkResult VKAPI_CALL bind_provider(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    require(device == expected_device && number(image) == 0x12345678 && number(memory) == 0x98765432 &&
            offset == 0x100000010ULL, "actual bind veneer truncated provider arguments");
    errno = EINTR; return vk_result;
}
VkResult VKAPI_CALL view_provider(VkDevice device, const VkImageViewCreateInfo* info,
                                 const VkAllocationCallbacks* allocator, VkImageView* output) {
    require(device == expected_device && info == expected_info && allocator == expected_allocator &&
            output == expected_output, "actual view veneer changed provider arguments");
    *output = handle<VkImageView>(0xaaaa5555); errno = ENOSPC; return vk_result;
}
frame_import_vulkan::Providers controlled{77, image_provider, bind_provider, view_provider, nullptr};

struct Cleanup {
    std::atomic<bool>& cleaned;
    ~Cleanup() { cleaned.store(true); }
};
struct CancelWork {
    int fd = -1;
    char byte = 'q';
    std::atomic<bool> cleaned{false}, ready{false}, release{false};
};
void* blocking_read(void* opaque) {
    auto& work = *static_cast<CancelWork*>(opaque);
    Cleanup cleanup{work.cleaned};
    (void)frame_import_read_entry(work.fd, &work.byte, 1);
    return nullptr;
}
void* blocking_write(void* opaque) {
    auto& work = *static_cast<CancelWork*>(opaque);
    Cleanup cleanup{work.cleaned};
    (void)frame_import_write_entry(work.fd, &work.byte, 1);
    return nullptr;
}
void* pending_close(void* opaque) {
    auto& work = *static_cast<CancelWork*>(opaque);
    Cleanup cleanup{work.cleaned};
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
    work.ready.store(true);
    while (!work.release.load()) sched_yield();
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    (void)frame_import_close_entry(work.fd);
    return nullptr;
}
bool bounded_wait(const std::atomic<bool>& flag) {
    for (unsigned i = 0; i < 2000; ++i) {
        if (flag.load()) return true;
        usleep(1000);
    }
    return false;
}
void cancellation() {
    for (unsigned operation = 0; operation < 3; ++operation) {
        std::printf("CANCELLATION_BEGIN=%s\n", operation == 0 ? "read" : operation == 1 ? "write" : "close");
        std::fflush(stdout); // Retain the reached stage if a CFI negative control faults.
        int ends[2]; require(pipe(ends) == 0, "cancellation pipe failed");
        if (operation == 1) {
            const auto flags = fcntl(ends[1], F_GETFL);
            require(flags >= 0 && fcntl(ends[1], F_SETFL, flags | O_NONBLOCK) == 0, "pipe fill setup failed");
            char full[4096]{}; while (actual_write(ends[1], full, sizeof(full)) > 0) {}
            require(errno == EAGAIN && fcntl(ends[1], F_SETFL, flags) == 0, "pipe fill failed");
        }
        CancelWork work{}; work.fd = ends[operation == 0 ? 0 : 1];
        state.entered_read = false; state.entered_write = false; state.entered_close = false;
        const auto before = state.before.load(), after = state.after.load();
        pthread_t thread;
        require(pthread_create(&thread, nullptr,
            operation == 0 ? blocking_read : operation == 1 ? blocking_write : pending_close, &work) == 0,
            "native ARM64 cancellation thread failed");
        if (operation == 2) {
            require(bounded_wait(work.ready), "pending-close thread did not prepare");
            require(pthread_cancel(thread) == 0, "pending-close cancellation failed");
            work.release = true;
        } else {
            require(bounded_wait(operation == 0 ? state.entered_read : state.entered_write), "blocked entry did not begin");
            require(pthread_cancel(thread) == 0, "blocked cancellation failed");
        }
        void* returned = nullptr;
        require(pthread_join(thread, &returned) == 0 && returned == PTHREAD_CANCELED,
                "forced unwind did not cross actual native veneer/dispatch");
        require(work.cleaned.load(), "forced unwind did not execute caller RAII cleanup");
        require(state.before == before + 1 && state.after == after,
                "cancellation omitted Before or fabricated After");
        close(ends[0]); if (fcntl(ends[1], F_GETFD) >= 0) close(ends[1]);
    }
    std::puts("CANCELLATION=read,write,close:forced-unwind,caller-cleanup,no-false-after");
}
} // namespace

extern "C" std::uint64_t frame_import_image_dispatch(const Entry* entry) {
    return frame_import_vulkan::image(*entry, controlled);
}
extern "C" std::uint64_t frame_import_bind_dispatch(const Entry* entry) {
    return frame_import_vulkan::bind(*entry, controlled);
}
extern "C" std::uint64_t frame_import_view_dispatch(const Entry* entry) {
    return frame_import_vulkan::view(*entry, controlled);
}

int main(int argc, char** argv) {
    require(argc == 2, "expected explicit pinned provider path");
    providers(argv[1]);
    require(frame_import_forwarding::register_callbacks_before_threads({observe, &state},
            frame_import_forwarding::InitializationGate::OwnedPreparedContext), "libc fixture registration failed");
    require(frame_import_vulkan::register_callbacks_before_threads({observe_vulkan, &state}, true),
            "Vulkan fixture registration failed");
    pthread_mutexattr_t attributes;
    require(pthread_mutexattr_init(&attributes) == 0 && pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK) == 0,
            "error-check attributes failed");
    pthread_mutex_t mutex;
    errno = 71;
    require(probe(number(frame_import_init_entry), number(&mutex), number(&attributes)) == 0 && errno == 71,
            "init entry result/errno changed");
    require(probe(number(frame_import_lock_entry), number(&mutex)) == 0 && errno == 71, "lock entry changed");
    require(probe(number(frame_import_lock_entry), number(&mutex)) == EDEADLK && errno == 71, "lock failure changed");
    require(probe(number(frame_import_destroy_entry), number(&mutex)) == EBUSY && errno == 71, "busy destroy changed");
    require(probe(number(frame_import_unlock_entry), number(&mutex)) == 0 && errno == 71, "unlock entry changed");
    require(probe(number(frame_import_unlock_entry), number(&mutex)) == EPERM && errno == 71, "unlock failure changed");
    require(probe(number(frame_import_destroy_entry), number(&mutex)) == 0 && errno == 71, "destroy entry changed");
    pthread_mutexattr_destroy(&attributes);
    int ends[2]; require(pipe(ends) == 0, "success pipe failed");
    const char bytes[] = "actual ARM64 entry";
    errno = 83;
    require(probe(number(frame_import_write_entry), ends[1], number(bytes), sizeof(bytes)) == sizeof(bytes) && errno == 83,
            "write entry result/errno changed");
    char destination[64]{};
    require(probe(number(frame_import_read_entry), ends[0], number(destination), sizeof(destination)) == sizeof(bytes) &&
            errno == 83 && !std::memcmp(destination, bytes, sizeof(bytes)), "partial read/arguments changed");
    require(probe(number(frame_import_close_entry), ends[1]) == 0 && errno == 83, "close entry changed");
    require(probe(number(frame_import_read_entry), ends[0], number(destination), sizeof(destination)) == 0, "EOF changed");
    close(ends[0]);
    errno = 0;
    require(probe(number(frame_import_write_entry), ~std::uint64_t{0}, 0, 0) == ~std::uint64_t{0} && errno == EBADF,
            "ssize_t failure was truncated");
    require(probe(number(frame_import_close_entry), ~std::uint64_t{0}) == 0xffffffff && errno == EBADF,
            "int failure was not w0 bits");
    const int listener = socket(AF_UNIX, SOCK_STREAM, 0), client = socket(AF_UNIX, SOCK_STREAM, 0);
    require(listener >= 0 && client >= 0, "abstract socket failed");
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1, "import-arm64-%ld", (long)getpid());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + std::strlen(address.sun_path + 1));
    require(bind(listener, (sockaddr*)&address, length) == 0 && listen(listener, 1) == 0, "abstract listener failed");
    errno = 89;
    require(probe(number(frame_import_connect_entry), client, number(&address), length) == 0 && errno == 89,
            "connect entry arguments/result changed");
    const int connected = accept(listener, nullptr, nullptr);
    require(connected >= 0, "connect did not reach expected listener");
    close(connected); close(client); close(listener);
    errno = 0;
    const auto direct_invalid_connect = actual_connect(-1, nullptr, 0);
    const auto direct_invalid_connect_errno = errno;
    errno = 0;
    const auto invalid_connect = probe(number(frame_import_connect_entry), ~std::uint64_t{0}, 0, 0);
    require(invalid_connect == static_cast<std::uint32_t>(direct_invalid_connect) && errno == direct_invalid_connect_errno,
            "connect failure changed");
    std::printf("CONNECT_INVALID_ADDRESS=direct:%d,errno:%d;veneer:%u,errno:%d\n", direct_invalid_connect,
                direct_invalid_connect_errno, static_cast<unsigned>(invalid_connect), errno);
    errno = 0;
    require(probe(number(frame_import_connect_entry), ~std::uint64_t{0}, number(&address), length) == 0xffffffff && errno == EBADF,
            "connect invalid descriptor with valid address changed");

    VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.format = VK_FORMAT_R8G8B8A8_UNORM; image.extent = {16, 32, 1}; image.arrayLayers = 1;
    VkAllocationCallbacks allocator{}; VkImage created{};
    expected_info = &image; expected_allocator = &allocator; expected_output = &created;
    require(probe(number(frame_import_image_entry), number(expected_device), number(&image), number(&allocator), number(&created)) ==
            VK_SUCCESS && number(created) == 0x12345678 && errno == EBUSY, "image native ABI changed");
    vk_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    require(probe(number(frame_import_image_entry), number(expected_device), number(&image), number(&allocator), number(&created)) ==
            static_cast<std::uint32_t>(vk_result) && errno == EBUSY, "negative VkResult changed");
    vk_result = VK_SUCCESS;
    require(probe(number(frame_import_bind_entry), number(expected_device), number(created), 0x98765432, 0x100000010ULL) ==
            VK_SUCCESS && errno == EINTR, "bind native ABI changed");
    VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = created; view.format = image.format; VkImageView created_view{};
    expected_info = &view; expected_output = &created_view;
    require(probe(number(frame_import_view_entry), number(expected_device), number(&view), number(&allocator), number(&created_view)) ==
            VK_SUCCESS && number(created_view) == 0xaaaa5555 && errno == ENOSPC, "view native ABI changed");
    expected_info = &image; expected_output = &created;
    throw_vulkan = true;
    const auto before = state.before.load(), after = state.after.load();
    bool caught = false;
    try { (void)frame_import_image_entry(expected_device, &image, &allocator, &created); }
    catch (const NativeException&) { caught = true; }
    throw_vulkan = false;
    require(caught && state.before == before + 1 && state.after == after, "provider exception did not cross real veneer or fake After appeared");
    cancellation();
    require(state.bad == 0 && !frame_import_forwarding::notification_loss().loss &&
            !frame_import_vulkan::notification_loss(), "entry capture/notification lost evidence");
    std::printf("RESULT={\"checks\":%u,\"aarch64_executed\":true,\"veneers\":11,\"libc_aliases\":8,"
                "\"read_write_close_forced_unwind\":true,\"native_vulkan_gpu\":false,\"frame_kernel\":false,\"headset\":false}\n", checks);
}
