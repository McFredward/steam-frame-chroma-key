#pragma once

#include "native_import_route.h"

#include <cstdint>

// This component forwards real libc calls. It does not read native memory,
// identify a binary, authorize a capture, or activate a production observer.
namespace frame_import_forwarding {

enum class Operation : std::uint8_t { Init, Destroy, Lock, Unlock, Write, Close };
enum class Phase : std::uint8_t { Before, After };

struct Notification {
    Operation operation{};
    Phase phase{};
    // A value copy of the original public-entry context. It must not be
    // confused with the C++ dispatcher's own SP/FP or retained by pointer.
    frame_import_native::Entry entry{};
    std::uint64_t result_bits = 0;  // Only available for Phase::After.
    int preserved_errno = 0;
};

using Notify = void (*)(Notification, void*) noexcept;
struct Callbacks {
    Notify notify = nullptr;
    // The caller must supply a static/process-lifetime owned context. There is
    // deliberately no replacement, unsubscribe, or destruction operation.
    void* context = nullptr;
};

enum class InitializationGate : std::uint8_t {
    Unprepared = 0,
    OwnedPreparedContext = 1
};

// Copy the immutable descriptor once, outside callbacks and before sampling
// threads start. This gate expresses a caller precondition, not permission or
// proof of private native memory ownership. No caller in this component enables
// it automatically. Notifications remain disabled until explicit registration.
// Callback code must perform bounded nonblocking work, without allocations,
// I/O, cancellation points, exceptions or native-state mutations.
bool register_callbacks_before_threads(Callbacks, InitializationGate) noexcept;
bool callbacks_registered() noexcept;

struct LossStatus {
    bool loss = false; // Sticky for this process; any loss rejects proof.
    // A bounded lower bound: concurrent increments may lose the single CAS,
    // while the independent sticky flag always records the loss.
    std::uint32_t suppressed_lower_bound = 0;
    bool saturated = false;
};
LossStatus notification_loss() noexcept;

} // namespace frame_import_forwarding

// The root-owned entry veneers provide a nonnull, owned 256-byte snapshot before
// any C++ prologue. Integer libc results are encoded as zero-extended w0 bits;
// write preserves the full signed ssize_t bit pattern. These functions are NOT
// noexcept: libc cancellation/forced unwind must cross their normal CFI.
extern "C" std::uint64_t frame_import_init_dispatch(const frame_import_native::Entry*);
extern "C" std::uint64_t frame_import_destroy_dispatch(const frame_import_native::Entry*);
extern "C" std::uint64_t frame_import_lock_dispatch(const frame_import_native::Entry*);
extern "C" std::uint64_t frame_import_unlock_dispatch(const frame_import_native::Entry*);
extern "C" std::uint64_t frame_import_write_dispatch(const frame_import_native::Entry*);
extern "C" std::uint64_t frame_import_close_dispatch(const frame_import_native::Entry*);

// Vulkan image/bind/view dispatch is intentionally absent. Its real per-device
// forwarding providers must be established by the layer integration, rather
// than invented here or replaced with synthetic native failure returns.
