#include "client_connect_observation.h"

#include <cstring>
#include <limits>

namespace frame_client_connect {
namespace {
using Operation = frame_import_forwarding::Operation;
using Phase = frame_import_forwarding::Phase;
using Entry = frame_import_native::Entry;
constexpr std::uintptr_t kMax = (std::uintptr_t{1} << 48) - 1;
constexpr std::uintptr_t kPipe = 0x649ff0, kLock = 0x245ea8,
    kConnect = 0x245f9c, kSuccessUnlock = 0x245fb0,
    kFailureUnlock = 0x245ff0, kExceptionUnlock = 0x246044;
// This is a deliberately bounded observation window, not a claim that every
// native strlen-derived address length is <=110. Larger calls stay unchanged.
constexpr std::uint64_t kMinAddressLength = 3, kMaxAddressLength = 110;
thread_local const unsigned char owner_marker
    __attribute__((tls_model("initial-exec"))) = 0;

bool address(std::uint64_t value, unsigned alignment = 16) noexcept {
    return value >= 4096 && value <= kMax && value % alignment == 0;
}
bool target_valid(const Target& target) noexcept {
    const auto& m = target.module; const auto& o = target.object;
    if (!address(m.load_bias, 4096) || !m.module_lifetime_label ||
        m.load_bias > kMax - kPipe || o.pipe != m.load_bias + kPipe ||
        !o.valid() || !o.registry_identity || !o.process_generation ||
        !o.object_generation || o.domain != frame_import_pipe::Domain::Client045 ||
        m.code_begin < m.load_bias || m.code_begin > m.load_bias + kLock ||
        m.code_end <= m.load_bias + kExceptionUnlock || m.code_end > kMax)
        return false;
    for (const auto byte : m.code_identity) if (byte) return true;
    return false;
}
bool site(const Entry& entry, const Target& target, std::uintptr_t rva) noexcept {
    return entry.x[30] == target.module.load_bias + rva;
}
bool frame(const Entry& entry) noexcept {
    return address(entry.sp) && entry.x[29] == entry.sp && entry.sp <= kMax - 192;
}
bool same(const Entry& a, const Entry& b) noexcept {
    return a.sp == b.sp && a.x == b.x;
}
bool native_integer(std::uint64_t bits, int& result) noexcept {
    if (bits > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto word = static_cast<std::uint32_t>(bits);
    static_assert(sizeof(word) == sizeof(result), "Native integer ABI changed");
    std::memcpy(&result, &word, sizeof(result));
    return true;
}
Outcome receipt(Kind kind, Reason reason,
                const frame_import_forwarding::Notification& note) noexcept {
    Outcome out;
    out.kind = kind; out.reason = reason; out.entry = note.entry;
    out.result_bits = note.result_bits; out.preserved_errno = note.preserved_errno;
    return out;
}
} // namespace

Scope Scope::prepare_current() noexcept {
    Scope scope;
    scope.owner_ = reinterpret_cast<std::uintptr_t>(&owner_marker);
    return scope;
}
bool Scope::current() const noexcept {
    return owner_ && owner_ == reinterpret_cast<std::uintptr_t>(&owner_marker);
}
bool Scope::ownership_active() const noexcept { return current() && lock_owned_; }
bool Scope::expired() const noexcept { return current() && state_ == State::Expired; }
bool Scope::complete() const noexcept { return current() && state_ == State::Complete; }
Gate::Gate(Target target) noexcept : target_(target), target_valid_(target_valid(target)) {}
void Gate::expire(Scope& scope) const noexcept {
    if (!scope.current()) return;
    scope.lock_owned_ = false;
    scope.state_ = Scope::State::Expired;
    scope.pending_ = {}; scope.frame_ = 0; scope.argument_fd_ = -1;
}

Outcome Gate::observe(frame_import_forwarding::Notification note, Scope& scope) const noexcept {
    using State = Scope::State;
    if (!scope.owner_) return receipt(Kind::Rejected, Reason::UnpreparedScope, note);
    if (!scope.current()) return receipt(Kind::Rejected, Reason::WrongThread, note);
    // This samples one local invocation. Later ordinary RPC locks on the same
    // object are not reconnect/lifetime evidence and must not turn a completed
    // sample into a fictitious observation failure. Root retains responsibility
    // for actual destroy/shutdown/audit/callback loss outside this local sample.
    if (scope.state_ == State::Expired || scope.state_ == State::Complete) return {};
    const auto reject = [&](Reason reason) noexcept {
        expire(scope); return receipt(Kind::Rejected, reason, note);
    };
    if (!target_valid_) return reject(Reason::Target);
    if (scope.gate_owner_ && scope.gate_owner_ != this) return reject(Reason::Target);
    scope.gate_owner_ = this;
    const auto& e = note.entry;
    const bool target_argument = e.x[0] == target_.object.pipe;
    if (note.operation == Operation::Destroy && target_argument)
        return reject(Reason::Destroyed);
    if (note.phase != Phase::Before && note.phase != Phase::After)
        return reject(Reason::Order);

    // Native logging helpers can use other client mutexes before connect or
    // between its return and the outer unlock. They do not release this target
    // mutex. Never ignore the exact source sites or ANY changed/nested callback
    // while a supported Before is waiting for its matching After.
    const bool held_interval = scope.state_ == State::Owned ||
        scope.state_ == State::SuccessUnlock || scope.state_ == State::FailureUnlock;
    const bool mutex_call = note.operation == Operation::Init ||
        note.operation == Operation::Destroy || note.operation == Operation::Lock ||
        note.operation == Operation::Unlock;
    const bool source_mutex =
        (note.operation == Operation::Lock && site(e, target_, kLock)) ||
        (note.operation == Operation::Unlock &&
         (site(e, target_, kSuccessUnlock) || site(e, target_, kFailureUnlock) ||
          site(e, target_, kExceptionUnlock)));
    if (held_interval && mutex_call && !target_argument && !source_mutex) return {};

    // Any observed target unlock ends local ownership BEFORE the original
    // provider runs, including unsupported sites and exceptional cleanup.
    if (note.operation == Operation::Unlock && target_argument && note.phase == Phase::Before) {
        scope.lock_owned_ = false;
        if (site(e, target_, kExceptionUnlock)) return reject(Reason::Abandoned);
        const bool expected =
            (scope.state_ == State::SuccessUnlock && site(e, target_, kSuccessUnlock)) ||
            (scope.state_ == State::FailureUnlock && site(e, target_, kFailureUnlock));
        if (!expected) return reject(Reason::Unsupported);
        if (!frame(e) || e.sp != scope.frame_ || e.x[19] != target_.object.pipe)
            return reject(Reason::Invocation);
        scope.pending_ = e; scope.state_ = State::UnlockPending;
        return {};
    }

    if (scope.state_ == State::Idle) {
        if (note.operation != Operation::Lock || !target_argument) return {};
        if (note.phase != Phase::Before || !site(e, target_, kLock))
            return reject(Reason::Unsupported);
        if (!frame(e) || e.x[19] != target_.object.pipe) return reject(Reason::Entry);
        scope.pending_ = e; scope.frame_ = e.sp; scope.state_ = State::LockPending;
        return {};
    }
    if (scope.state_ == State::LockPending) {
        if (note.operation != Operation::Lock || note.phase != Phase::After ||
            !same(e, scope.pending_)) return reject(Reason::Invocation);
        int result = 0;
        if (!native_integer(note.result_bits, result)) return reject(Reason::ResultEncoding);
        if (result != 0) {
            auto out = reject(Reason::NativeLockFailure); out.native_result = result; return out;
        }
        scope.lock_owned_ = true; scope.state_ = State::Owned;
        return {};
    }
    if (scope.state_ == State::Owned) {
        // Recursive close/reconnect, another call, another frame, and unknown
        // nesting are explicitly outside this first supported connect window.
        if (note.operation != Operation::Connect || note.phase != Phase::Before ||
            !site(e, target_, kConnect)) return reject(Reason::Unsupported);
        if (!scope.lock_owned_ || !frame(e) || e.sp != scope.frame_ ||
            e.x[19] != target_.object.pipe) return reject(Reason::Invocation);
        if (e.x[0] > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
            e.x[1] != e.sp + 80 || e.x[20] != e.x[1] ||
            e.x[2] != e.x[22] || e.x[2] < kMinAddressLength || e.x[2] > kMaxAddressLength)
            return reject(Reason::Arguments);
        scope.pending_ = e; scope.argument_fd_ = static_cast<int>(e.x[0]);
        scope.state_ = State::ConnectPending;
        return {};
    }
    if (scope.state_ == State::ConnectPending) {
        if (note.operation != Operation::Connect || note.phase != Phase::After ||
            !same(e, scope.pending_)) return reject(Reason::Invocation);
        int result = 0;
        if (!native_integer(note.result_bits, result)) return reject(Reason::ResultEncoding);
        if (result != 0 && result != -1) return reject(Reason::ResultEncoding);
        scope.state_ = result == 0 ? State::SuccessUnlock : State::FailureUnlock;
        auto out = receipt(Kind::NativeConnectCall, Reason::None, note);
        out.argument_fd = scope.argument_fd_; out.native_result = result;
        return out;
    }
    if (scope.state_ == State::UnlockPending) {
        if (note.operation != Operation::Unlock || note.phase != Phase::After ||
            !same(e, scope.pending_)) return reject(Reason::Invocation);
        int result = 0;
        if (!native_integer(note.result_bits, result)) return reject(Reason::ResultEncoding);
        if (result != 0) {
            auto out = reject(Reason::NativeUnlockFailure); out.native_result = result; return out;
        }
        scope.pending_ = {}; scope.frame_ = 0; scope.argument_fd_ = -1;
        scope.state_ = State::Complete;
        return {};
    }
    // An observed connect result waits only for its matching outer unlock.
    // No other callback can silently keep the source-specific lock window live.
    return reject(Reason::Order);
}
} // namespace frame_client_connect
