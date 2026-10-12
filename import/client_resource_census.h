#pragma once

#include "client_connect_observation.h"

// Register-only census of one source-specific import/reference timeline.
// No stack/heap, wire, descriptor, kernel or GPU acquisition occurs here.
namespace frame_client_resource {
enum class Boundary : unsigned { Begin, Pending, Reference };
enum class Kind : unsigned { None, Boundary, Rejected };
enum class Reason : unsigned {
    None, Target, Thread, Entry, Arguments, Order, NativeResult, Invocation
};
struct Outcome {
    Kind kind=Kind::None;
    Boundary boundary=Boundary::Begin;
    frame_import_forwarding::Phase phase=frame_import_forwarding::Phase::Before;
    frame_import_native::Entry entry{};
    int native_result=0,preserved_errno=0;
    Reason reason=Reason::None;
    bool startup_thread_stack_owned=false,native_result_available=false;
};
class Gate;
class Scope {
public:
    static Scope prepare_current(bool startup_thread_stack_owned) noexcept;
    bool current() const noexcept;
    bool complete() const noexcept;
    bool expired() const noexcept;
private:
    friend class Gate;
    enum class State : unsigned {
        Idle, BeginPending, AwaitPending, PendingPending, AwaitReference,
        ReferencePending, Complete, Expired
    };
    // Reuse the existing current-thread marker without adding static TLS.
    frame_client_connect::Scope thread_{};
    const Gate* gate_owner_=nullptr;
    State state_=State::Idle;
    frame_import_native::Entry pending_{};
    std::uintptr_t import_frame_=0,output_=0;
    bool startup_thread_stack_owned_=false;
};
class Gate {
public:
    explicit Gate(frame_client_connect::Target) noexcept;
    Gate(const Gate&)=delete;
    Gate& operator=(const Gate&)=delete;
    Outcome observe(frame_import_forwarding::Notification,Scope&) const noexcept;
    void expire(Scope&) const noexcept;
private:
    const frame_client_connect::Target target_;
    const bool valid_;
};
} // namespace frame_client_resource
