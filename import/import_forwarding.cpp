#include "import_forwarding.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

// Distinct, actual-provider aliases prevent recursion through the corresponding
// public entry veneer. All resolution is link-time; there is no dlsym path,
// raw-syscall substitution, constructor bootstrap, or hot-path new lock.
extern "C" int frame_import_libc_init(pthread_mutex_t*, const pthread_mutexattr_t*);
extern "C" int frame_import_libc_destroy(pthread_mutex_t*);
extern "C" int frame_import_libc_lock(pthread_mutex_t*);
extern "C" int frame_import_libc_unlock(pthread_mutex_t*);
extern "C" ssize_t frame_import_libc_write(int, const void*, std::size_t);
extern "C" int frame_import_libc_close(int);

#if defined(__aarch64__)
#define IMPORT_LIBC_VERSION "GLIBC_2.17"
#elif defined(__x86_64__)
#define IMPORT_LIBC_VERSION "GLIBC_2.2.5"
#else
#error "Import forwarding supports the audited AArch64 ABI and x86-64 offline fixture only"
#endif
__asm__(".symver frame_import_libc_init,__pthread_mutex_init@" IMPORT_LIBC_VERSION);
__asm__(".symver frame_import_libc_destroy,__pthread_mutex_destroy@" IMPORT_LIBC_VERSION);
__asm__(".symver frame_import_libc_lock,__pthread_mutex_lock@" IMPORT_LIBC_VERSION);
__asm__(".symver frame_import_libc_unlock,__pthread_mutex_unlock@" IMPORT_LIBC_VERSION);
__asm__(".symver frame_import_libc_write,__write@" IMPORT_LIBC_VERSION);
__asm__(".symver frame_import_libc_close,__close@" IMPORT_LIBC_VERSION);
#undef IMPORT_LIBC_VERSION

namespace frame_import_forwarding {
namespace {
static_assert(sizeof(int) == 4 && sizeof(ssize_t) == 8 && sizeof(std::uintptr_t) == 8,
              "Forwarding argument/result ABI changed");
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "Callback publication must not acquire a hidden lock");

// 0: inactive, 1: descriptor being published, 2: immutable descriptor ready.
std::atomic<unsigned> registration{0};
Callbacks immutable_callbacks{};
thread_local bool notifying __attribute__((tls_model("initial-exec"))) = false;
constexpr unsigned kSuppressedLimit = 1024;
std::atomic<unsigned> suppressed{0};
std::atomic<unsigned> lost{0};

void record_suppression() noexcept {
    lost.store(1, std::memory_order_release);
    unsigned count = suppressed.load(std::memory_order_relaxed);
    if (count < kSuppressedLimit) {
        // One attempt only: no loop or wait on a native callback. A conflicting
        // increment may undercount, but it can never erase the sticky loss.
        suppressed.compare_exchange_strong(count, count + 1, std::memory_order_relaxed,
                                             std::memory_order_relaxed);
    }
}

void notify(Operation operation, Phase phase, const frame_import_native::Entry& entry,
            std::uint64_t result, int preserved_errno) noexcept {
    if (notifying) { record_suppression(); return; }
    const unsigned state = registration.load(std::memory_order_acquire);
    if (state != 2) {
        // Registration must precede native sampling threads. If a signal or
        // caller violates that gate, a partially published descriptor must not
        // turn the missed lifecycle event into apparently complete evidence.
        if (state == 1) record_suppression();
        return;
    }
    Notification value{};
    value.operation = operation;
    value.phase = phase;
    value.entry = entry;
    value.result_bits = result;
    value.preserved_errno = preserved_errno;
    notifying = true;
    immutable_callbacks.notify(value, immutable_callbacks.context);
    notifying = false;
    errno = preserved_errno;
}

int integer_argument(std::uint64_t value) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    int result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

template<class Provider>
std::uint64_t forward_integer(Operation operation, const frame_import_native::Entry& entry,
                              Provider provider) {
    const int incoming_errno = errno;
    notify(operation, Phase::Before, entry, 0, incoming_errno);
    errno = incoming_errno;
    const int result = provider();
    const int provider_errno = errno;
    const auto bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(result));
    notify(operation, Phase::After, entry, bits, provider_errno);
    errno = provider_errno;
    return bits;
}
} // namespace

bool register_callbacks_before_threads(Callbacks callbacks, InitializationGate gate) noexcept {
    if (gate != InitializationGate::OwnedPreparedContext || !callbacks.notify) return false;
    unsigned expected = 0;
    if (!registration.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) return false;
    immutable_callbacks = callbacks;
    registration.store(2, std::memory_order_release);
    return true;
}

bool callbacks_registered() noexcept {
    return registration.load(std::memory_order_acquire) == 2;
}

LossStatus notification_loss() noexcept {
    LossStatus status{};
    status.loss = lost.load(std::memory_order_acquire) != 0;
    status.suppressed_lower_bound = suppressed.load(std::memory_order_relaxed);
    status.saturated = status.suppressed_lower_bound == kSuppressedLimit;
    return status;
}
} // namespace frame_import_forwarding

extern "C" std::uint64_t frame_import_init_dispatch(const frame_import_native::Entry* entry) {
    return frame_import_forwarding::forward_integer(frame_import_forwarding::Operation::Init, *entry, [&] {
        return frame_import_libc_init(reinterpret_cast<pthread_mutex_t*>(entry->x[0]),
                                      reinterpret_cast<const pthread_mutexattr_t*>(entry->x[1]));
    });
}
extern "C" std::uint64_t frame_import_destroy_dispatch(const frame_import_native::Entry* entry) {
    return frame_import_forwarding::forward_integer(frame_import_forwarding::Operation::Destroy, *entry, [&] {
        return frame_import_libc_destroy(reinterpret_cast<pthread_mutex_t*>(entry->x[0]));
    });
}
extern "C" std::uint64_t frame_import_lock_dispatch(const frame_import_native::Entry* entry) {
    return frame_import_forwarding::forward_integer(frame_import_forwarding::Operation::Lock, *entry, [&] {
        return frame_import_libc_lock(reinterpret_cast<pthread_mutex_t*>(entry->x[0]));
    });
}
extern "C" std::uint64_t frame_import_unlock_dispatch(const frame_import_native::Entry* entry) {
    return frame_import_forwarding::forward_integer(frame_import_forwarding::Operation::Unlock, *entry, [&] {
        return frame_import_libc_unlock(reinterpret_cast<pthread_mutex_t*>(entry->x[0]));
    });
}
extern "C" std::uint64_t frame_import_write_dispatch(const frame_import_native::Entry* entry) {
    const int incoming_errno = errno;
    frame_import_forwarding::notify(frame_import_forwarding::Operation::Write,
                                   frame_import_forwarding::Phase::Before, *entry, 0, incoming_errno);
    errno = incoming_errno;
    const ssize_t result = frame_import_libc_write(frame_import_forwarding::integer_argument(entry->x[0]),
                                                   reinterpret_cast<const void*>(entry->x[1]),
                                                   static_cast<std::size_t>(entry->x[2]));
    const int provider_errno = errno;
    const auto bits = static_cast<std::uint64_t>(result);
    frame_import_forwarding::notify(frame_import_forwarding::Operation::Write,
                                   frame_import_forwarding::Phase::After, *entry, bits, provider_errno);
    errno = provider_errno;
    return bits;
}
extern "C" std::uint64_t frame_import_close_dispatch(const frame_import_native::Entry* entry) {
    return frame_import_forwarding::forward_integer(frame_import_forwarding::Operation::Close, *entry, [&] {
        return frame_import_libc_close(frame_import_forwarding::integer_argument(entry->x[0]));
    });
}
