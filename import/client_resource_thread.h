#pragma once

#include "import_startup_gate.h"

#include <atomic>

// The root's audited, before-MAIN admission is mandatory. This scalar-only
// core does not install a hook, discover stacks or authenticate module leases.
namespace frame_client_resource_thread {

static_assert(std::atomic<unsigned>::is_always_lock_free,"Resource admission requires lock-free scalar atomics");

constexpr unsigned char kMarkerBit = 8;
constexpr std::uintptr_t kFirstWaitReturn = 0x328d0;
constexpr std::size_t kWorkerFrameBytes = 336;
using Entry = frame_import_native::Entry;
using Prepare = frame_import_startup::OwnedThreadStack (*)(void*) noexcept;

enum class Kind : unsigned { None, Prepared, Refused };
enum class Reason : unsigned {
    None, UnpreparedImage, NotArmed, LateArm, Entry, MarkerAlreadyUsed,
    MissingPrepare, Preparing, Stack, WrongThread, Retired
};
struct Outcome {
    Kind kind = Kind::None;
    Reason reason = Reason::None;
    Entry entry{};
};

class Gate {
public:
    explicit Gate(frame_import_startup::PreparedImage receiver) noexcept;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    // Root calls only from actual admitted la_preinit, after sealed MAIN
    // preparation and before receiver worker threads. It must never late-arm
    // from a generic readiness flag or after an unaccounted first wait.
    bool arm_before_receiver_threads() noexcept;
    Outcome before_first_wait(const Entry&, unsigned char& thread_birth_marker,
                              Prepare, void* context) noexcept;
    bool current(const unsigned char& thread_birth_marker) const noexcept;
    frame_native_client_birth::StackRegion region_current(
        const unsigned char& thread_birth_marker) const noexcept;
    // Called by actual root loss/unload/fork/exit admission hooks. Only atomics
    // change; another thread's marker or cached stack is never touched.
    void retire() noexcept;
    Reason reason() const noexcept { return reason_.load(std::memory_order_acquire); }
private:
    enum class State : unsigned { Unprepared, Dormant, Active, Preparing, Prepared, Refused, Retired };
    const frame_import_startup::PreparedImage receiver_;
    const std::uintptr_t wait_return_;
    std::atomic<State> state_{State::Unprepared};
    std::atomic<Reason> reason_{Reason::UnpreparedImage};
    // One writer owns the Active -> Preparing claim. Values are immutable
    // after release publication and never reset/recycled on retirement.
    frame_import_startup::OwnedThreadStack stack_{};
    const unsigned char* marker_address_ = nullptr;
    std::uintptr_t frame_ = 0, decoder_ = 0;
    void refuse(Reason) noexcept;
};

} // namespace frame_client_resource_thread
