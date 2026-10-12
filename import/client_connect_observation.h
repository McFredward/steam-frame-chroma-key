#pragma once

#include "import_forwarding.h"
#include "import_startup_gate.h"

// Register-only consumer of the actual forwarding callbacks. It never reads
// native memory, queries a socket, creates a Registry connection, or authorizes
// installation. Root supplies a target only after authentic successful birth
// and keeps module/registry admission live around every call to observe().
namespace frame_client_connect {

struct Target {
    frame_import_startup::MappedClient module{};
    frame_import_pipe::ObjectToken object{};
};
enum class Kind : unsigned { None, NativeConnectCall, Rejected };
enum class Reason : unsigned {
    None, Target, UnpreparedScope, WrongThread, Invocation, Entry, Arguments,
    Order, NativeLockFailure, NativeUnlockFailure, ResultEncoding, Unsupported,
    Destroyed, Abandoned, Loss
};
struct Outcome {
    Kind kind = Kind::None;
    frame_import_native::Entry entry{};
    int argument_fd = -1, native_result = 0, preserved_errno = 0;
    std::uint64_t result_bits = 0;
    Reason reason = Reason::None;
    // A successful local call is not continuous descriptor ownership, a peer
    // association, a source/video frame, or proof of compositor consumption.
    bool continuous_connection_owned = false;
    bool native_cross_process_namespace = false;
    bool source_frame_associated = false;
    bool gpu_consumed_coherent = false;
};

class Gate;
class Scope {
public:
    // Root stores one scope in initial-exec TLS, never shares its mutable state,
    // and never replaces it during an active invocation. No syscall/lookup is
    // needed. A wrong-thread callback must also retire root's global admission.
    static Scope prepare_current() noexcept;
    bool current() const noexcept;
    bool ownership_active() const noexcept;
    bool expired() const noexcept;
    bool complete() const noexcept;
private:
    friend class Gate;
    enum class State : unsigned {
        Idle, LockPending, Owned, ConnectPending, SuccessUnlock, FailureUnlock,
        UnlockPending, Complete, Expired
    };
    std::uintptr_t owner_ = 0;
    const Gate* gate_owner_ = nullptr;
    State state_ = State::Idle;
    bool lock_owned_ = false;
    frame_import_native::Entry pending_{};
    std::uintptr_t frame_ = 0;
    int argument_fd_ = -1;
};

class Gate {
public:
    explicit Gate(Target) noexcept;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    Outcome observe(frame_import_forwarding::Notification, Scope&) const noexcept;
    // Root invokes this for callback/queue/audit loss before retiring admission.
    // Wrong-thread calls cannot mutate another thread's scope.
    void expire(Scope&) const noexcept;
private:
    const Target target_;
    const bool target_valid_;
};

} // namespace frame_client_connect
