#pragma once

#include "import_startup_gate.h"

// One source-pinned import worker, selected at the first actual import lock.
// Before stages copied public-entry registers and existing TLS identity, then
// prepares the actual worker outside graphics callbacks, without native private
// copies. It grants no private read permission. Successful paired After checks
// one fixed parent pair.
// Root authentication of entry/provider/module and a true CurrentMarker callback
// is mandatory. Neither a marker nor stack bounds proves the native call route.
namespace frame_compositor_import_thread {
using Entry = frame_import_native::Entry;
using Stack = frame_import_startup::OwnedThreadStack;
using Region = frame_native_client_birth::StackRegion;
using Prepare = Stack (*)(void*) noexcept;
using Read = bool (*)(std::uintptr_t, void*, std::size_t, void*) noexcept;
// Returns the root's genuine existing TLS byte for the calling thread. This is
// an externally authenticated metadata identity, not a stack read capability.
using CurrentMarker = unsigned char* (*)(void*) noexcept;
constexpr unsigned char kMarkerBit = 8;
constexpr std::uintptr_t kBeginReturn = 0x153fc0, kDispatchReturn = 0x24fee0;
constexpr std::size_t kBeginFrameBytes = 320, kParentCopyBytes = 16;
static_assert(std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<std::size_t>::is_always_lock_free);

enum class Kind : unsigned { None, BeginStaged, BeginAdmitted, Refused };
enum class Reason : unsigned {
    None, UnpreparedImage, MissingCurrentMarker, NotArmed, LateArm, Entry,
    MarkerAlreadyUsed, MissingPrepare, MissingReader, Preparing, Stack, Reader,
    SourceLink, ChangedEntry, WrongThread, NativeFailure, MissingBefore,
    RepeatedBegin, Cancelled, Retired
};
struct Outcome {
    Kind kind = Kind::None;
    Reason reason = Reason::None;
    Entry entry{};
    // Confirmed is the successful exact-reader count. Partial copies and copies
    // whose ownership postcheck fails still belong to the root's actual ledger.
    std::size_t bytes_requested = 0, bytes_confirmed = 0;
};
class Gate {
public:
    Gate(frame_import_startup::PreparedImage, CurrentMarker, void* marker_owner) noexcept;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    bool arm_before_compositor_threads() noexcept;
    Outcome before_first_begin(const Entry&, unsigned char& existing_marker,
                               Prepare, void* prepare_owner) noexcept;
    Outcome after_first_begin(const Entry&, int original_lock_result,
                              const unsigned char& existing_marker,
                              Read, void* reader_owner) noexcept;
    // Deferred diagnostic selection: the root staged real Begin/Early pairs
    // and cached this thread's stack before the native lock. At the actual
    // Image call, check only the still-live Begin parent FP/LR pair. No Begin
    // scalar replay or first-time stack discovery occurs at this boundary.
    Outcome admit_staged_image(const Entry& staged_begin, const Entry& current_image,
                               Stack cached_stack, unsigned char& existing_marker,
                               Read, void* reader_owner) noexcept;
    // Metadata pairing only: never use this as native memory read permission.
    bool pending_current(const unsigned char& existing_marker) const noexcept;
    bool current(const unsigned char& existing_marker) const noexcept;
    Region region_current(const unsigned char& existing_marker) const noexcept;
    bool window_current(const unsigned char& existing_marker,
                        std::uintptr_t, std::size_t) const noexcept;
    std::uintptr_t begin_frame_current(const unsigned char& existing_marker) const noexcept;
    // Root unwind, loss, fork, shutdown/unload routes are terminal. No callback
    // changes another thread's TLS byte, resets a budget, or reuses the claim.
    void cancel() noexcept;
    void retire() noexcept;
    Reason reason() const noexcept { return reason_.load(std::memory_order_acquire); }
    std::size_t bytes_requested() const noexcept { return requested_.load(std::memory_order_acquire); }
    std::size_t bytes_confirmed() const noexcept { return confirmed_.load(std::memory_order_acquire); }
private:
    enum class State : unsigned { Unprepared, Dormant, Armed, Staging, PendingBegin,
                                  Preparing, CheckingBegin, Ready, Refused, Retired };
    const frame_import_startup::PreparedImage compositor_;
    const CurrentMarker current_marker_;
    void* const marker_owner_;
    std::atomic<State> state_{State::Unprepared};
    std::atomic<Reason> reason_{Reason::UnpreparedImage};
    std::atomic<std::size_t> requested_{0}, confirmed_{0};
    // Published once by the single owner. Other threads and lifecycle routes
    // touch atomics only; no reader accesses partially prepared owner fields.
    Entry begin_entry_{};
    Stack stack_{};
    const unsigned char* marker_address_ = nullptr;
    bool marker_owned(const unsigned char&) const noexcept;
    bool stack_owned(const unsigned char&) const noexcept;
    void refuse(Reason) noexcept;
    Outcome failure(const Entry&, Reason, std::size_t = 0, std::size_t = 0) noexcept;
};
} // namespace frame_compositor_import_thread
