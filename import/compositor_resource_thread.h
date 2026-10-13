#pragma once

#include "import_startup_gate.h"

// Finite scalar-only cold gate for the pinned compositor RPC thread. The root
// supplies authenticated original entry/provider/module admission before MAIN,
// independently safe current-thread preparation, and its actual-copy ledger.
// This component installs no hook, obtains no native mapping or socket lease,
// and supplies no namespace, video-frame, eye/UV or GPU-consumption proof. The
// poll route is shared by IPC workers: its first candidate is not guaranteed to
// be the receiver's eventual import worker. A wrong candidate stays refused.
namespace frame_compositor_resource_thread {

using Entry = frame_import_native::Entry;
using Stack = frame_import_startup::OwnedThreadStack;
using Region = frame_native_client_birth::StackRegion;
using Prepare = Stack (*)(void*) noexcept;
using Read = bool (*)(std::uintptr_t, void*, std::size_t, void*) noexcept;
constexpr unsigned char kMarkerBit = 8;
constexpr std::uintptr_t kPollReturn = 0x172388, kBeginReturn = 0x153fc0;
constexpr std::size_t kPollCopyBytes = 32, kBeginCopyBytes = 16;
static_assert(std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<std::size_t>::is_always_lock_free);

enum class Kind : unsigned { None, ColdPrepared, BeginAdmitted, Refused };
enum class Reason : unsigned {
    None, UnpreparedImage, NotArmed, LateArm, Entry, MarkerAlreadyUsed,
    MissingPrepare, MissingReader, Preparing, Stack, Reader, SourceLink,
    ChangedEntry, WrongThread, NativeFailure, MissingColdPreparation,
    RepeatedBegin, Cancelled, Retired
};
struct Outcome {
    Kind kind = Kind::None;
    Reason reason = Reason::None;
    Entry entry{};
    // Per-call admission requests and successful exact reader calls. A false
    // reader may have copied a partial span: the root must retain its own actual
    // acquisition count, including ownership postcheck failures.
    std::size_t bytes_requested = 0, bytes_confirmed = 0;
};

class Gate {
public:
    explicit Gate(frame_import_startup::PreparedImage compositor) noexcept;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    // Actual root before-MAIN/provider admission is external, never a generic
    // readiness flag. Calling again cannot rearm a claimed or failed candidate.
    bool arm_before_compositor_threads() noexcept;
    // The root supplies its verified existing current-thread TLS marker; an
    // arbitrary referenced byte is not a marker ownership capability.
    Outcome before_first_poll(const Entry&, unsigned char& existing_thread_marker,
                              Prepare, void* prepare_owner, Read, void* reader_owner) noexcept;
    // Root invokes only after its paired original import lock returns. No raw
    // timeline stack copies precede this separate genuine parent-link check.
    Outcome after_first_begin(const Entry&, int original_lock_result,
                              const unsigned char& existing_thread_marker,
                              Read, void* reader_owner) noexcept;
    bool current(const unsigned char& existing_thread_marker) const noexcept;
    Region region_current(const unsigned char& existing_thread_marker) const noexcept;
    bool window_current(const unsigned char& existing_thread_marker,
                        std::uintptr_t, std::size_t) const noexcept;
    std::uintptr_t begin_frame_current(const unsigned char& existing_thread_marker) const noexcept;
    // Root loss/unload/fork/exit/unwind routes change atomics only. Retirement
    // never clears another thread's TLS byte, resets budgets or recycles a claim.
    void cancel() noexcept;
    void retire() noexcept;
    Reason reason() const noexcept { return reason_.load(std::memory_order_acquire); }
    std::size_t bytes_requested() const noexcept { return requested_.load(std::memory_order_acquire); }
    std::size_t bytes_confirmed() const noexcept { return confirmed_.load(std::memory_order_acquire); }
private:
    enum class State : unsigned { Unprepared, Dormant, Armed, Preparing, ColdPrepared,
                                  CheckingBegin, Ready, Refused, Retired };
    const frame_import_startup::PreparedImage compositor_;
    std::atomic<State> state_{State::Unprepared};
    std::atomic<Reason> reason_{Reason::UnpreparedImage};
    std::atomic<std::size_t> requested_{0}, confirmed_{0};
    // One preparation owner publishes these once. The begin owner is that same
    // current thread. Other threads/retirement only access the atomic states.
    Stack stack_{};
    const unsigned char* marker_address_ = nullptr;
    std::uintptr_t poll_frame_ = 0, dispatch_frame_ = 0, begin_frame_ = 0;
    bool owned(const unsigned char&) const noexcept;
    void refuse(Reason) noexcept;
    Outcome failure(const Entry&, Reason, std::size_t = 0, std::size_t = 0) noexcept;
};
} // namespace frame_compositor_resource_thread
