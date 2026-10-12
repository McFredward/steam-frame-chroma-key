#include "import/import_forwarding.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

using frame_import_native::Entry;
using namespace frame_import_forwarding;

// Real host-provider aliases, independently checked with dladdr below. The
// fixture supplies owned synthetic Entry values; it does not execute the
// production AArch64 veneer or prove a native Frame call route.
extern "C" int fixture_real_init(pthread_mutex_t*, const pthread_mutexattr_t*);
extern "C" int fixture_real_destroy(pthread_mutex_t*);
extern "C" int fixture_real_lock(pthread_mutex_t*);
extern "C" int fixture_real_unlock(pthread_mutex_t*);
extern "C" ssize_t fixture_real_write(int, const void*, std::size_t);
extern "C" int fixture_real_close(int);
#if defined(__x86_64__)
#define HOST_VERSION "GLIBC_2.2.5"
#elif defined(__aarch64__)
#define HOST_VERSION "GLIBC_2.17"
#else
#error "Unsupported fixture ABI"
#endif
__asm__(".symver fixture_real_init,__pthread_mutex_init@" HOST_VERSION);
__asm__(".symver fixture_real_destroy,__pthread_mutex_destroy@" HOST_VERSION);
__asm__(".symver fixture_real_lock,__pthread_mutex_lock@" HOST_VERSION);
__asm__(".symver fixture_real_unlock,__pthread_mutex_unlock@" HOST_VERSION);
__asm__(".symver fixture_real_write,__write@" HOST_VERSION);
__asm__(".symver fixture_real_close,__close@" HOST_VERSION);
#undef HOST_VERSION

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
Entry arguments(std::uint64_t a0, std::uint64_t a1 = 0, std::uint64_t a2 = 0) {
    Entry entry{};
    entry.x[0] = a0; entry.x[1] = a1; entry.x[2] = a2;
    entry.x[19] = 0xface19;
    entry.x[29] = 0x123400;
    entry.x[30] = 0x567800;
    entry.sp = 0x901200;
    return entry;
}
Entry mutex_arguments(pthread_mutex_t* mutex, const pthread_mutexattr_t* attributes = nullptr) {
    return arguments(reinterpret_cast<std::uintptr_t>(mutex),
                     reinterpret_cast<std::uintptr_t>(attributes));
}
int native_integer(std::uint64_t bits) {
    const std::uint32_t word = static_cast<std::uint32_t>(bits);
    int result;
    std::memcpy(&result, &word, sizeof(result));
    return result;
}
std::uint64_t int_bits(int result) { return static_cast<std::uint32_t>(result); }

struct State {
    std::atomic<unsigned> calls{0}, wrong_context{0};
    Notification values[256]{};
    std::atomic<bool> nested{false};
    std::atomic<bool> waiting_write{false}, waiting_close{false};
    pthread_mutex_t nested_mutex = PTHREAD_MUTEX_INITIALIZER;
};
State state{};
bool early_passed = false;

__attribute__((constructor)) void startup_calls() {
    pthread_mutex_t mutex;
    auto entry = mutex_arguments(&mutex);
    errno = 93;
    if (callbacks_registered() || frame_import_init_dispatch(&entry) || errno != 93 ||
        frame_import_lock_dispatch(&entry) || frame_import_unlock_dispatch(&entry) ||
        frame_import_destroy_dispatch(&entry) || errno != 93) std::abort();
    early_passed = true;
}

void observe(Notification value, void* context) noexcept {
    if (context != &state) state.wrong_context.fetch_add(1, std::memory_order_relaxed);
    const unsigned at = state.calls.fetch_add(1, std::memory_order_relaxed);
    if (at < 256) state.values[at] = value;
    if (value.phase == Phase::Before && state.nested.exchange(false, std::memory_order_relaxed)) {
        auto nested_entry = mutex_arguments(&state.nested_mutex);
        if (frame_import_lock_dispatch(&nested_entry) || frame_import_unlock_dispatch(&nested_entry))
            std::abort();
    }
    if (value.phase == Phase::Before && value.operation == Operation::Write)
        state.waiting_write.store(true, std::memory_order_release);
    if (value.phase == Phase::Before && value.operation == Operation::Close)
        state.waiting_close.store(true, std::memory_order_release);
    errno = EDOM; // Dispatch must preserve the provider's errno, not this value.
}
void wrong_callback(Notification, void*) noexcept { std::abort(); }

void providers() {
    const void* addresses[] = {
        reinterpret_cast<const void*>(fixture_real_init), reinterpret_cast<const void*>(fixture_real_destroy),
        reinterpret_cast<const void*>(fixture_real_lock), reinterpret_cast<const void*>(fixture_real_unlock),
        reinterpret_cast<const void*>(fixture_real_write), reinterpret_cast<const void*>(fixture_real_close)
    };
    const char* path = nullptr;
    for (const auto address : addresses) {
        Dl_info info{};
        require(dladdr(address, &info) != 0 && info.dli_fname, "Provider address not resolved");
        require(std::strstr(info.dli_fname, "libc.so.6") != nullptr,
                "Provider did not resolve to the actual host libc");
        if (!path) path = info.dli_fname;
        require(std::strcmp(path, info.dli_fname) == 0, "Different forwarding provider images");
    }
}

struct CancelWork {
    Entry entry{};
    std::atomic<bool> ready{false}, release{false};
};
void* blocked_write(void* pointer) {
    auto& work = *static_cast<CancelWork*>(pointer);
    frame_import_write_dispatch(&work.entry);
    return nullptr;
}
void* pending_close(void* pointer) {
    auto& work = *static_cast<CancelWork*>(pointer);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
    work.ready.store(true, std::memory_order_release);
    while (!work.release.load(std::memory_order_acquire)) sched_yield();
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    frame_import_close_dispatch(&work.entry);
    return nullptr;
}
bool bounded_wait(const std::atomic<bool>& flag) {
    for (unsigned i = 0; i < 2000; ++i) {
        if (flag.load(std::memory_order_acquire)) return true;
        usleep(1000);
    }
    return false;
}

void cancellation() {
    int pipefd[2];
    require(pipe(pipefd) == 0, "Cancellation pipe failed");
    const int original = fcntl(pipefd[1], F_GETFL);
    require(original >= 0 && fcntl(pipefd[1], F_SETFL, original | O_NONBLOCK) == 0,
            "Cannot fill cancellation pipe");
    char full[4096]{};
    while (write(pipefd[1], full, sizeof(full)) > 0) {}
    require(errno == EAGAIN, "Unexpected pipe fill result");
    require(fcntl(pipefd[1], F_SETFL, original) == 0, "Cannot restore blocking write");
    const char byte = 'q';
    CancelWork blocked{};
    blocked.entry = arguments(pipefd[1], reinterpret_cast<std::uintptr_t>(&byte), 1);
    state.waiting_write.store(false, std::memory_order_relaxed);
    const unsigned before = state.calls.load();
    pthread_t thread;
    require(pthread_create(&thread, nullptr, blocked_write, &blocked) == 0, "Write thread failed");
    require(bounded_wait(state.waiting_write), "Write dispatch did not enter");
    require(pthread_cancel(thread) == 0, "Write cancel failed");
    void* returned = nullptr;
    require(pthread_join(thread, &returned) == 0 && returned == PTHREAD_CANCELED,
            "Write forced unwind did not cross the dispatcher");
    require(state.calls.load() == before + 1, "Cancelled write emitted a fake completion");
    close(pipefd[0]); close(pipefd[1]);

    int closed[2];
    require(pipe(closed) == 0, "Close cancellation pipe failed");
    CancelWork closing{};
    closing.entry = arguments(closed[1]);
    state.waiting_close.store(false, std::memory_order_relaxed);
    const unsigned close_before = state.calls.load();
    require(pthread_create(&thread, nullptr, pending_close, &closing) == 0, "Close thread failed");
    require(bounded_wait(closing.ready), "Close thread preparation failed");
    require(pthread_cancel(thread) == 0, "Pending close cancel failed");
    closing.release.store(true, std::memory_order_release);
    returned = nullptr;
    require(pthread_join(thread, &returned) == 0 && returned == PTHREAD_CANCELED,
            "Close forced unwind did not cross the dispatcher");
    require(state.waiting_close.load() && state.calls.load() == close_before + 1,
            "Cancelled close omitted entry or emitted a fake completion");
    close(closed[0]);
    // Whether the cancellation occurred before or after closing is libc-owned.
    if (fcntl(closed[1], F_GETFD) >= 0) close(closed[1]);
}

void partial_write() {
    int pipefd[2];
    require(pipe(pipefd) == 0, "Partial-write pipe failed");
    const int flags = fcntl(pipefd[1], F_GETFL);
    require(flags >= 0 && fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK) == 0,
            "Cannot prepare nonblocking partial write");
    char bytes[8192]{};
    while (fixture_real_write(pipefd[1], bytes, sizeof(bytes)) > 0) {}
    require(errno == EAGAIN && read(pipefd[0], bytes, 4096) == 4096,
            "Cannot make exactly one pipe page available");
    auto entry = arguments(pipefd[1], reinterpret_cast<std::uintptr_t>(bytes), sizeof(bytes));
    errno = 89;
    const auto result = frame_import_write_dispatch(&entry);
    require(result == 4096 && errno == 89,
            "Dispatcher retried or altered a real positive partial write");
    close(pipefd[0]); close(pipefd[1]);
}

void saturated_loss() {
    auto entry = arguments(std::numeric_limits<std::uint64_t>::max(), 0, 0);
    for (unsigned i = 0; i < 300; ++i) {
        state.nested.store(true, std::memory_order_relaxed);
        if (frame_import_write_dispatch(&entry) != std::numeric_limits<std::uint64_t>::max() ||
            errno != EBADF) std::abort();
    }
    const auto loss = notification_loss();
    require(loss.loss && loss.saturated && loss.suppressed_lower_bound == 1024,
            "Suppression counter wrapped or failed to saturate");
}
} // namespace

int main() {
    require(early_passed && !callbacks_registered(), "Startup was not transparently inactive");
    require(!notification_loss().loss && notification_loss().suppressed_lower_bound == 0,
            "Inactive startup fabricated notification loss");
    providers();
    Callbacks descriptor{observe, &state};
    require(!register_callbacks_before_threads({}, InitializationGate::OwnedPreparedContext),
            "Null callback accepted");
    require(!register_callbacks_before_threads(descriptor, InitializationGate::Unprepared),
            "Unprepared registration accepted");
    require(!register_callbacks_before_threads(descriptor, static_cast<InitializationGate>(42)),
            "Unknown initialization gate accepted");
    require(register_callbacks_before_threads(descriptor, InitializationGate::OwnedPreparedContext),
            "Prepared registration failed");
    descriptor = {wrong_callback, nullptr}; // Registration copied, not borrowed.
    require(callbacks_registered() &&
            !register_callbacks_before_threads(descriptor, InitializationGate::OwnedPreparedContext),
            "Immutable subscriber was replaced");

    pthread_mutexattr_t attributes;
    require(pthread_mutexattr_init(&attributes) == 0 &&
            pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK) == 0,
            "Error-check attributes failed");
    pthread_mutex_t direct, through;
    errno = 71;
    const int direct_init = fixture_real_init(&direct, &attributes);
    const int direct_init_errno = errno;
    auto mutex = mutex_arguments(&through, &attributes);
    errno = 71;
    require(frame_import_init_dispatch(&mutex) == int_bits(direct_init) && errno == direct_init_errno,
            "Mutex init result/errno changed");
    errno = 73;
    require(frame_import_lock_dispatch(&mutex) == 0 && errno == 73, "Mutex lock changed");
    require(native_integer(frame_import_lock_dispatch(&mutex)) == EDEADLK && errno == 73,
            "Native recursive-lock failure changed");
    require(native_integer(frame_import_destroy_dispatch(&mutex)) == EBUSY && errno == 73,
            "Native busy-destroy failure changed");
    require(frame_import_unlock_dispatch(&mutex) == 0 && errno == 73, "Mutex unlock changed");
    require(native_integer(frame_import_unlock_dispatch(&mutex)) == EPERM && errno == 73,
            "Native foreign unlock failure changed");
    require(frame_import_destroy_dispatch(&mutex) == 0 && errno == 73, "Mutex destroy changed");
    fixture_real_destroy(&direct);
    pthread_mutexattr_destroy(&attributes);

    auto failed = arguments(std::numeric_limits<std::uint64_t>::max(), 0, 0);
    errno = 0;
    require(frame_import_write_dispatch(&failed) == std::numeric_limits<std::uint64_t>::max() &&
            errno == EBADF, "Write signed result/errno changed");
    errno = 0;
    require(frame_import_close_dispatch(&failed) == std::numeric_limits<std::uint32_t>::max() &&
            errno == EBADF, "Close w0 result/errno changed");

    int pipefd[2];
    require(pipe(pipefd) == 0, "Success pipe failed");
    const char bytes[] = "owned bytes";
    auto write_entry = arguments(pipefd[1], reinterpret_cast<std::uintptr_t>(bytes), sizeof(bytes));
    state.nested.store(true);
    const unsigned event_before = state.calls.load();
    errno = 79;
    require(frame_import_write_dispatch(&write_entry) == sizeof(bytes) && errno == 79,
            "Successful write result/errno changed");
    require(state.calls.load() == event_before + 2, "Nested callback recursively sampled itself");
    require(notification_loss().loss && notification_loss().suppressed_lower_bound == 4,
            "Suppressed recursive lifecycle calls did not invalidate evidence");
    const auto& observed = state.values[event_before];
    require(observed.phase == Phase::Before && observed.operation == Operation::Write &&
            observed.entry.x == write_entry.x && observed.entry.sp == write_entry.sp &&
            observed.preserved_errno == 79, "Original entry was not copied faithfully");
    char received[sizeof(bytes)]{};
    require(read(pipefd[0], received, sizeof(received)) == sizeof(received) &&
            std::memcmp(bytes, received, sizeof(bytes)) == 0, "Write payload changed");
    auto close_entry = arguments(pipefd[1]);
    errno = 83;
    require(frame_import_close_dispatch(&close_entry) == 0 && errno == 83, "Successful close changed");
    close(pipefd[0]);
    partial_write();
    cancellation();
    require(notification_loss().loss, "Sticky loss was cleared by later native completion");
    saturated_loss();
    require(state.wrong_context.load() == 0, "Borrowed descriptor mutation changed registration");
    std::printf("%u checks passed; real host libc forwarding, synthetic entry contexts, no Frame runtime proof\n",
                checks);
}
