// Execute the actual final observer's private ARM read/write entries in an
// unrelated inert process. Native provider bytes, errno and cancellation are
// checked; no game, OpenVR, cold registration or resource admission occurs.
#include "import/import_startup_audit.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <initializer_list>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

namespace {
unsigned checks = 0;
void require(bool good, const char* message) {
    ++checks;
    if (!good) { std::fprintf(stderr, "raw forwarding check %u: %s\n", checks, message); std::abort(); }
}
using Read = ssize_t (*)(int, void*, std::size_t);
using Write = ssize_t (*)(int, const void*, std::size_t);
using Bridge = frame_import_startup_audit::BridgeResponse (*)(frame_import_startup_audit::BridgeRequest);
std::uintptr_t entry_offset(const char* text) {
    char* end = nullptr; errno = 0;
    const auto value = std::strtoull(text, &end, 0);
    require(!errno && end && !*end && value && value % 4 == 0 && value < 8 * 1024 * 1024,
            "bounded offline-reviewed hidden entry offset");
    return value;
}
void pause_short() { const timespec pause{0, 1000000}; (void)nanosleep(&pause, nullptr); }
struct Cancellation {
    Read reader = nullptr;
    Write writer = nullptr;
    int descriptor = -1;
    std::array<char, 4096> bytes{};
    std::atomic<unsigned> entered{0}, returned{0}, destructors{0};
};
struct UnwindReceipt {
    Cancellation& owner;
    ~UnwindReceipt() { owner.destructors.fetch_add(1, std::memory_order_release); }
};
void* blocked(void* context) {
    auto& owner = *static_cast<Cancellation*>(context);
    UnwindReceipt receipt{owner};
    owner.entered.store(1, std::memory_order_release);
    if (owner.reader) (void)owner.reader(owner.descriptor, owner.bytes.data(), 1);
    else (void)owner.writer(owner.descriptor, owner.bytes.data(), owner.bytes.size());
    owner.returned.store(1, std::memory_order_release);
    return nullptr;
}
void cancel_blocked(Cancellation& owner, const char* marker) {
    pthread_t worker{};
    require(pthread_create(&worker, nullptr, blocked, &owner) == 0, "native cancellation worker started");
    for (unsigned i = 0; i < 2000 && !owner.entered.load(std::memory_order_acquire); ++i) pause_short();
    require(owner.entered.load(std::memory_order_acquire) == 1, "worker reached controlled entry stage");
    for (unsigned i = 0; i < 20; ++i) pause_short();
    require(owner.returned.load(std::memory_order_acquire) == 0, "empty/full real pipe keeps original operation blocked");
    std::puts(marker); std::fflush(stdout);
    require(pthread_cancel(worker) == 0, "actual pthread cancellation requested");
    void* result = nullptr;
    require(pthread_join(worker, &result) == 0 && result == PTHREAD_CANCELED,
            "native forced unwind crosses actual final DSO and hidden ARM entry");
    require(owner.returned.load(std::memory_order_acquire) == 0 &&
            owner.destructors.load(std::memory_order_acquire) == 1,
            "C++ RAII destructor executes exactly once through final ARM CFI");
}
void close_pipe(const int* descriptors) {
    require(close(descriptors[0]) == 0 && close(descriptors[1]) == 0, "owned pipe descriptors closed");
}
}

int main(int argc, char** argv) {
    require(argc == 5, "explicit final observer, native libc and both private entry offsets required");
    const auto read_offset = entry_offset(argv[3]), write_offset = entry_offset(argv[4]);
    require(read_offset != write_offset, "private read and write entries are distinct");
    const auto native_read = reinterpret_cast<Read>(dlvsym(RTLD_DEFAULT, "__read", "GLIBC_2.17"));
    const auto native_write = reinterpret_cast<Write>(dlvsym(RTLD_DEFAULT, "__write", "GLIBC_2.17"));
    require(native_read && native_write && dlsym(RTLD_DEFAULT, "read") == reinterpret_cast<void*>(native_read) &&
            dlsym(RTLD_DEFAULT, "write") == reinterpret_cast<void*>(native_write),
            "unrelated public read/write keep their actual original providers");
    const std::uintptr_t originals[]{reinterpret_cast<std::uintptr_t>(native_read),
                                      reinterpret_cast<std::uintptr_t>(native_write)};
    constexpr std::uintptr_t expected[]{0xdf330, 0xdfee0};
    for (unsigned i = 0; i < 2; ++i) {
        Dl_info libc{};
        require(dladdr(reinterpret_cast<void*>(originals[i]), &libc) && libc.dli_fname &&
                !std::strcmp(libc.dli_fname, argv[2]) &&
                originals[i] - reinterpret_cast<std::uintptr_t>(libc.dli_fbase) == expected[i],
                "original provider matches exact retained native libc mapping and offset");
    }
    const auto object = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(object, "actual final observer loads inert without profile or native game initialization");
    const auto bridge = reinterpret_cast<Bridge>(dlsym(object, "frame_client_startup_bridge"));
    Dl_info own{};
    require(bridge && dladdr(reinterpret_cast<void*>(bridge), &own) && own.dli_fname &&
            !std::strcmp(own.dli_fname, argv[1]), "sole public bridge identifies actual final observer base");
    require(!dlsym(object, "frame_import_read_entry") && !dlsym(object, "frame_import_write_entry"),
            "both native entries remain private, without public libc interposition");
    const auto reader = reinterpret_cast<Read>(reinterpret_cast<std::uintptr_t>(own.dli_fbase) + read_offset);
    const auto writer = reinterpret_cast<Write>(reinterpret_cast<std::uintptr_t>(own.dli_fbase) + write_offset);
    for (const auto offset : {read_offset, write_offset}) {
        Dl_info hidden{};
        require(dladdr(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(own.dli_fbase) + offset), &hidden) &&
                hidden.dli_fbase == own.dli_fbase, "reviewed hidden entry belongs to the supplied actual artifact");
    }
    for (const auto stage : {frame_import_startup_audit::Stage::Prepare, frame_import_startup_audit::Stage::Bind,
                            frame_import_startup_audit::Stage::Retire}) {
        frame_import_startup_audit::BridgeRequest request{}; request.stage = stage;
        errno = EDOM; const auto response = bridge(request);
        require(!response.ready && !response.hidden_entry && errno == EDOM, "unprepared bridge grants no capture admission");
    }
    require(!dlsym(RTLD_DEFAULT, "VR_InitInternal2") && !dlsym(RTLD_DEFAULT, "VRClientCoreFactory"),
            "no native OpenVR/game initialization in this fixture");

    std::array<char, 32> observed{}, original{};
    observed.fill(static_cast<char>(0xa5)); original = observed;
    // Actual int prototypes exercise all significant low-W FD bits; no
    // mismatched 64-bit function-pointer ABI or artificial register call.
    for (const int descriptor : {-1, INT_MIN, INT_MAX}) {
        errno = EOVERFLOW; const auto actual = reader(descriptor, observed.data(), 1); const auto actual_errno = errno;
        errno = EOVERFLOW; const auto direct = native_read(descriptor, original.data(), 1); const auto direct_errno = errno;
        require(actual == -1 && actual == direct && actual_errno == EBADF && actual_errno == direct_errno,
                "full original int descriptor and native failed read/errno preserved");
        require(observed == original, "failed read leaves owned caller bytes unchanged");
        errno = ENOTTY; const auto write_actual = writer(descriptor, observed.data(), 1); const auto write_errno = errno;
        errno = ENOTTY; const auto write_direct = native_write(descriptor, original.data(), 1); const auto write_direct_errno = errno;
        require(write_actual == -1 && write_actual == write_direct && write_errno == EBADF && write_errno == write_direct_errno,
                "full original int descriptor and native failed write/errno preserved");
    }

    int descriptors[2]{};
    require(pipe(descriptors) == 0, "controlled real pipe created, without networking");
    errno = ENOTTY; require(reader(descriptors[0], observed.data(), 0) == 0 && errno == ENOTTY,
                           "zero-byte original read preserves incoming errno");
    require(observed == original, "zero-byte original read leaves caller buffer unchanged");
    errno = ECHILD; require(writer(descriptors[1], observed.data(), 0) == 0 && errno == ECHILD,
                           "zero-byte original write preserves incoming errno");
    constexpr char payload[] = "native exact pipe bytes";
    errno = ERANGE; const auto written = writer(descriptors[1], payload, sizeof(payload)); const auto write_errno = errno;
    require(written == static_cast<ssize_t>(sizeof(payload)) && write_errno == ERANGE, "positive original write retains count and errno");
    observed.fill(static_cast<char>(0xa5)); errno = EDOM;
    const auto read = reader(descriptors[0], observed.data() + 3, sizeof(payload)); const auto read_errno = errno;
    require(read == static_cast<ssize_t>(sizeof(payload)) && read_errno == EDOM &&
            !std::memcmp(observed.data() + 3, payload, sizeof(payload)), "positive original read delivers exact native bytes");
    require(observed[0] == static_cast<char>(0xa5) && observed[2] == static_cast<char>(0xa5) &&
            observed[3 + sizeof(payload)] == static_cast<char>(0xa5), "read pointer and size preserve surrounding bytes");
    constexpr char partial[] = "split native transfer";
    require(native_write(descriptors[1], partial, sizeof(partial)) == static_cast<ssize_t>(sizeof(partial)), "native provider prepares owned split bytes");
    std::array<char, sizeof(partial)> split{};
    require(reader(descriptors[0], split.data(), 3) == 3 &&
            reader(descriptors[0], split.data() + 3, split.size() - 3) == static_cast<ssize_t>(split.size() - 3) &&
            !std::memcmp(split.data(), partial, sizeof(partial)), "original short read pointer and remaining size preserved");
    close_pipe(descriptors);

    int read_pipe[2]{}; require(pipe(read_pipe) == 0, "empty controlled read pipe created");
    Cancellation read_cancel; read_cancel.reader = reader; read_cancel.descriptor = read_pipe[0];
    cancel_blocked(read_cancel, "CANCELLATION_BEGIN=read"); close_pipe(read_pipe);

    int write_pipe[2]{}; require(pipe(write_pipe) == 0, "controlled full write pipe created");
    const auto flags = fcntl(write_pipe[1], F_GETFL);
    require(flags >= 0 && fcntl(write_pipe[1], F_SETFL, flags | O_NONBLOCK) == 0,
            "controlled pipe becomes nonblocking only while filling it");
    std::array<char, 4096> filler{}; std::size_t filled = 0; bool full = false;
    for (unsigned i = 0; i < 1024; ++i) {
        const auto n = native_write(write_pipe[1], filler.data(), filler.size());
        if (n < 0) { full = errno == EAGAIN || errno == EWOULDBLOCK; break; }
        require(n > 0 && static_cast<std::size_t>(n) <= filler.size(), "native fill advances within owned bytes");
        filled += static_cast<std::size_t>(n);
    }
    require(full && filled > 0 && filled <= 1024 * filler.size(), "real pipe full under fixed controlled fill bound");
    require(fcntl(write_pipe[1], F_SETFL, flags) == 0, "original blocking write mode restored before cancellation");
    Cancellation write_cancel; write_cancel.writer = writer; write_cancel.descriptor = write_pipe[1];
    cancel_blocked(write_cancel, "CANCELLATION_BEGIN=write"); close_pipe(write_pipe);

    require(dlsym(RTLD_DEFAULT, "read") == reinterpret_cast<void*>(native_read) &&
            dlsym(RTLD_DEFAULT, "write") == reinterpret_cast<void*>(native_write),
            "public providers unchanged after actual private entry execution");
    require(dlclose(object) == 0, "own inert final observer unloaded");
    std::printf("RESULT={\"checks\":%u,\"actual_final_observer\":true,\"actual_native_libc\":true,"
                "\"real_pipe_bytes\":true,\"read_forced_unwind\":true,\"write_forced_unwind\":true,"
                "\"public_libc_interposition\":false,\"native_transaction_acquisition\":false,"
                "\"cold_registration\":false,\"production_ready\":false,\"native_openvr\":false,"
                "\"frame_kernel\":false,\"headset\":false}\n", checks);
}
