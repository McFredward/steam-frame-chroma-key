#include "compositor_resource_raw.h"

#include <cstring>
#include <limits>

namespace frame_compositor_raw {
namespace {
constexpr std::uintptr_t kMaxAddress = (std::uintptr_t{1} << 48) - 1;
bool address(std::uintptr_t value, unsigned alignment = 16) noexcept {
    return value >= 4096 && value <= kMaxAddress && value % alignment == 0;
}
bool same_entry(const Entry& a, const Entry& b) noexcept { return a.sp == b.sp && a.x == b.x; }
bool same_invocation(const frame_import_native::Expected& a,
                     const frame_import_native::Expected& b) noexcept {
    return a.dispatch_frame == b.dispatch_frame && a.pipe == b.pipe && a.sequence == b.sequence;
}
bool same_scalars(const Scalars& a, const Scalars& b) noexcept {
    return same_invocation(a.invocation, b.invocation) && a.client == b.client &&
        a.client_available == b.client_available && a.native_payload_read == b.native_payload_read &&
        a.native_send_success == b.native_send_success && a.packet_base == b.packet_base &&
        a.packet_offset == b.packet_offset && a.remaining == b.remaining &&
        a.fd == b.fd && a.refusal == b.refusal;
}
bool no_prefix(const Event& e) noexcept { return e.prefix.count == 0 && !e.prefix.successful_prefix_owned; }
bool no_private(const Event& e) noexcept { return e.scalar_bytes_copied == 0 && no_prefix(e); }
bool undecoded(const Event& e) noexcept { return no_private(e) && !e.decoded_available; }
bool vulkan(Boundary b) noexcept { return b == Boundary::Image || b == Boundary::Bind || b == Boundary::View; }
std::size_t scalar_budget(Boundary b) noexcept {
    switch (b) {
    case Boundary::Begin: case Boundary::EarlyUnlock: return 12;
    case Boundary::Image: case Boundary::Bind: case Boundary::View: return 80;
    case Boundary::SendLock: case Boundary::Finish: return 28;
    case Boundary::Write: return 44;
    default: return 0;
    }
}
bool source_entry(const Event& e) noexcept {
    constexpr std::uint64_t sites[]{0x153fc0,0x1540b0,0x2c75ac,0x2c7684,0x2c7804,0x172ea0,0x172c00,0x172fbc};
    const auto index = static_cast<unsigned>(e.boundary);
    if (index >= 8 || !address(e.load_bias, 4096) || sites[index] > kMaxAddress - e.load_bias ||
        e.entry.x[30] != e.load_bias + sites[index] || !address(e.entry.sp) || !address(e.entry.x[29])) return false;
    if (vulkan(e.boundary))
        return e.entry.sp <= kMaxAddress - 1232 && e.entry.x[29] == e.entry.sp + 96 && e.entry.x[0] != 0;
    if (e.boundary == Boundary::Finish)
        return e.entry.sp <= kMaxAddress - 32 && e.entry.x[29] == e.entry.sp + 32;
    return e.entry.sp == e.entry.x[29];
}
bool entry_route(const Event& e, const RawRecord& r) noexcept {
    const auto& x = e.entry.x;
    const auto dispatch = r.invocation.dispatch_frame, pipe = r.invocation.pipe;
    if (!source_entry(e)) return false;
    switch (e.boundary) {
    case Boundary::Begin: return address(x[0]) && x[19] == x[0];
    case Boundary::EarlyUnlock: return e.entry.sp == dispatch && x[0] == pipe;
    case Boundary::Image: return e.entry.sp + 1232 == dispatch;
    case Boundary::Bind: return e.entry.sp + 1232 == dispatch && x[0] == r.image.native_device &&
        x[1] == r.image.image && x[2] != 0;
    case Boundary::View: return e.entry.sp + 1232 == dispatch && x[0] == r.image.native_device;
    case Boundary::SendLock:
        return dispatch >= 80 && e.entry.sp == dispatch - 80 && x[0] == pipe && x[19] == pipe &&
            x[20] == 9 && x[21] == 0x235 && x[22] == r.invocation.sequence;
    case Boundary::Write:
        return dispatch >= 192 && e.entry.sp == dispatch - 192 && x[0] <= std::numeric_limits<int>::max() &&
            (r.fd < 0 || x[0] == static_cast<unsigned>(r.fd)) && x[19] == r.reply_bytes && x[19] < kReplyBytes &&
            x[20] == kReplyBytes && x[22] == pipe && x[23] == dispatch - 112 &&
            (!r.packet_base || x[23] == r.packet_base) && x[1] == x[23] + x[19] && x[2] == kReplyBytes - x[19];
    case Boundary::Finish:
        return dispatch >= 112 && e.entry.sp == dispatch - 112 && x[0] == pipe && x[20] <= 1;
    default: return false;
    }
}
bool route(const Event& e, const RawRecord& r, bool beginning = false) noexcept {
    if (!e.decoded_available || !e.scalars.valid() || !address(e.scalars.invocation.dispatch_frame) ||
        !address(e.scalars.invocation.pipe)) return false;
    if (beginning)
        return e.scalars.invocation.dispatch_frame == e.entry.sp && e.scalars.invocation.pipe == e.entry.x[0] &&
            !e.scalars.client_available;
    if (!same_invocation(e.scalars.invocation, r.invocation)) return false;
    if (vulkan(e.boundary))
        return e.scalars.client_available;
    return !e.scalars.client_available;
}
void add_truthful(std::size_t value, std::size_t& total) noexcept {
    total = value > std::numeric_limits<std::size_t>::max() - total ?
        std::numeric_limits<std::size_t>::max() : total + value;
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{p[i]} << (8 * i);
    return value;
}
std::uint64_t le64(const std::uint8_t* p) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{p[i]} << (8 * i);
    return value;
}
}

bool Scope::active() const noexcept { return state_ != State::Fresh && state_ != State::Terminal; }
bool Scope::finished() const noexcept { return state_ == State::Terminal; }
Outcome Observer::refuse(std::uint64_t why, Scope& s) const noexcept {
    if (s.finished()) return {};
    s.record_.reason |= why;
    s.record_.matched = false;
    s.record_.terminal = true;
    s.state_ = Scope::State::Terminal;
    return {Kind::Refused, s.record_};
}
Outcome Observer::abandon(Scope& s) const noexcept {
    return refuse(Abandoned | (s.owner_ && s.owner_ != this ? OwnerMismatch : None), s);
}
Outcome Observer::observer_loss(Scope& s) const noexcept {
    return refuse(ObserverLoss | (s.owner_ && s.owner_ != this ? OwnerMismatch : None), s);
}
Outcome Observer::expire(unsigned reason, Scope& s) const noexcept {
    if (s.finished()) return {};
    s.record_.thread_reason = reason;
    return refuse(ThreadUnavailable | (s.owner_ && s.owner_ != this ? OwnerMismatch : None), s);
}

bool Observer::can_acquire(const Event& e, const Scope& s) const noexcept {
    if (s.finished() || s.owner_ != this || !e.current_thread_owned || !e.native_module_current ||
        !e.native_provider_current || !e.source_current || e.thread_reason || !e.thread_identity ||
        e.thread_identity != s.thread_identity_ || e.load_bias != s.load_bias_ ||
        !undecoded(e) || e.public_result_available || !entry_route(e, s.record_)) return false;
    const auto count = scalar_budget(e.boundary);
    if (!count || s.record_.scalar_bytes_copied > kMaxScalarBytes ||
        count > kMaxScalarBytes - s.record_.scalar_bytes_copied) return false;
    if (e.phase == Phase::Before) {
        if (e.native_result_available) return false;
        switch (e.boundary) {
        case Boundary::EarlyUnlock: return s.state_ == Scope::State::EarlyBefore;
        case Boundary::Image: return s.state_ == Scope::State::ImageBefore;
        case Boundary::Bind: return s.state_ == Scope::State::BindBefore;
        case Boundary::View: return s.state_ == Scope::State::ViewBefore;
        case Boundary::Finish: return s.state_ == Scope::State::Writing && s.record_.reply_complete;
        default: return false;
        }
    }
    if (e.phase != Phase::After || !e.native_result_available ||
        e.boundary != s.paired_boundary_ || !same_entry(e.entry, s.paired_entry_)) return false;
    if (e.boundary == Boundary::Begin)
        return s.state_ == Scope::State::BeginAfter && e.native_result == 0;
    if (e.boundary == Boundary::SendLock)
        return s.state_ == Scope::State::SendAfter && e.native_result == 0;
    if (e.boundary != Boundary::Write || s.state_ != Scope::State::WriteAfter || e.native_result <= 0 ||
        static_cast<std::uint64_t>(e.native_result) > e.entry.x[2] || s.record_.writes > kMaxWrites ||
        s.record_.wire_bytes_copied > kMaxWireBytes) return false;
    return static_cast<std::uint64_t>(e.native_result) <= kMaxWireBytes - s.record_.wire_bytes_copied;
}

Outcome Observer::observe(const Event& e, Scope& s) const noexcept {
    if (s.finished()) return {};
    auto& r = s.record_;
    // Retain the actual acquisition amount even if validation subsequently
    // fails. No malformed event can trigger a new read or an out-of-bounds copy.
    add_truthful(e.scalar_bytes_copied, r.scalar_bytes_copied);
    add_truthful(e.prefix.count, r.wire_bytes_copied);
    r.last_boundary = e.boundary; r.last_phase = e.phase;
    r.last_native_result = e.native_result; r.last_preserved_errno = e.preserved_errno;
    r.last_native_result_available = e.native_result_available;
    if (r.scalar_bytes_copied > kMaxScalarBytes || r.wire_bytes_copied > kMaxWireBytes)
        return refuse(AcquisitionBudget, s);
    if (s.owner_ && s.owner_ != this) return refuse(OwnerMismatch, s);
    if (!e.current_thread_owned || !e.thread_identity || e.thread_reason ||
        (s.thread_identity_ && s.thread_identity_ != e.thread_identity)) {
        r.thread_reason = e.thread_reason;
        return refuse(ThreadUnavailable, s);
    }
    if (!e.native_module_current || (s.owner_ && e.load_bias != s.load_bias_)) return refuse(ModuleUnavailable, s);
    if (!e.native_provider_current) return refuse(ProviderUnavailable, s);
    if (!e.source_current) return refuse(SourceUnavailable, s);
    if (static_cast<unsigned>(e.boundary) >= 8) return refuse(UnsupportedBoundary, s);
    if (e.phase != Phase::Before && e.phase != Phase::After) return refuse(Order, s);
    if (!entry_route(e, r)) return refuse(InvalidEntry, s);
    if (e.phase == Phase::Before && e.native_result_available) return refuse(NativeFailure, s);
    if (e.phase == Phase::After && !e.native_result_available) return refuse(NativeFailure, s);
    if (e.public_result_available && !(e.phase == Phase::After && vulkan(e.boundary))) return refuse(PublicMetadata, s);
    if (e.boundary == Boundary::Begin && s.state_ != Scope::State::Fresh && s.state_ != Scope::State::BeginAfter)
        return refuse(NestedImport, s);
    if (e.phase == Phase::After) {
        if (e.boundary != s.paired_boundary_) return refuse(Order, s);
        if (!same_entry(e.entry, s.paired_entry_)) return refuse(ChangedEntry, s);
    }
    auto decode_checked = [&](std::size_t count, bool beginning = false) noexcept {
        if (e.scalar_bytes_copied != count || !route(e, r, beginning)) {
            r.decoder_refusal = e.scalars.refusal;
            return false;
        }
        return true;
    };
    auto pair = [&](Scope::State state) noexcept {
        s.paired_entry_ = e.entry; s.paired_boundary_ = e.boundary;
        s.state_ = state;
    };
    if (s.state_ == Scope::State::Fresh) {
        if (e.boundary != Boundary::Begin || e.phase != Phase::Before) return refuse(Order, s);
        if (!undecoded(e) || e.public_result_available) return refuse(AcquisitionBudget, s);
        s.owner_ = this; s.thread_identity_ = e.thread_identity; s.load_bias_ = e.load_bias;
        pair(Scope::State::BeginAfter); return {};
    }
    if (s.state_ == Scope::State::BeginAfter) {
        if (e.phase != Phase::After || e.boundary != Boundary::Begin) return refuse(Order, s);
        if (e.native_result != 0) return refuse(NativeFailure, s);
        if (!decode_checked(12, true) || !no_prefix(e)) return refuse(InvalidDecode | AcquisitionBudget, s);
        r.invocation = e.scalars.invocation; r.begin_lock_succeeded = true;
        s.state_ = Scope::State::EarlyBefore; return {};
    }
    if (e.phase == Phase::Before) {
        Scope::State next;
        if (e.boundary == Boundary::EarlyUnlock && s.state_ == Scope::State::EarlyBefore) next = Scope::State::EarlyAfter;
        else if (e.boundary == Boundary::Image && s.state_ == Scope::State::ImageBefore) next = Scope::State::ImageAfter;
        else if (e.boundary == Boundary::Bind && s.state_ == Scope::State::BindBefore) next = Scope::State::BindAfter;
        else if (e.boundary == Boundary::View && s.state_ == Scope::State::ViewBefore) next = Scope::State::ViewAfter;
        else if (e.boundary == Boundary::SendLock && s.state_ == Scope::State::SendBefore) next = Scope::State::SendAfter;
        else if (e.boundary == Boundary::Write && s.state_ == Scope::State::Writing) {
            if (r.reply_complete) return refuse(Order, s);
            if (r.writes >= kMaxWrites) return refuse(TransferLimit, s);
            if (!undecoded(e)) return refuse(AcquisitionBudget, s);
            ++r.writes; pair(Scope::State::WriteAfter); return {};
        } else if (e.boundary == Boundary::Finish && s.state_ == Scope::State::Writing) {
            if (!r.reply_complete) return refuse(Incomplete, s);
            next = Scope::State::FinishAfter;
        } else return refuse(Order, s);
        if (e.boundary == Boundary::SendLock) {
            if (!undecoded(e)) return refuse(AcquisitionBudget, s);
        } else {
            if (!decode_checked(scalar_budget(e.boundary)) || !no_prefix(e)) return refuse(InvalidDecode | AcquisitionBudget, s);
            if (vulkan(e.boundary)) {
                if (r.client_available && e.scalars.client != r.client) return refuse(ChangedClient, s);
                r.client = e.scalars.client; r.client_available = true;
            }
            s.paired_snapshot_ = e.scalars;
        }
        pair(next); return {};
    }
    if (s.state_ == Scope::State::EarlyAfter || s.state_ == Scope::State::FinishAfter ||
        s.state_ == Scope::State::ImageAfter || s.state_ == Scope::State::BindAfter || s.state_ == Scope::State::ViewAfter) {
        if (!no_private(e) || !e.decoded_available || !same_scalars(e.scalars, s.paired_snapshot_))
            return refuse(ChangedSnapshot | AcquisitionBudget, s);
        if (e.native_result != 0) return refuse(NativeFailure, s);
        if (s.state_ == Scope::State::EarlyAfter) {
            r.early_unlock_succeeded = true; r.native_payload_read = e.scalars.native_payload_read;
            if (!r.native_payload_read) return refuse(MissingPayload, s);
            s.state_ = Scope::State::ImageBefore; return {};
        }
        if (s.state_ == Scope::State::FinishAfter) {
            r.finish_unlock_succeeded = true; r.native_send_success = e.scalars.native_send_success;
            if (!r.native_send_success || !r.reply_complete) return refuse(NativeFailure | Incomplete, s);
            r.matched = r.terminal = true; s.state_ = Scope::State::Terminal;
            return {Kind::Matched, r};
        }
        if (!e.public_result_available) return refuse(PublicMetadata, s);
        if (s.state_ == Scope::State::ImageAfter) {
            r.image = e.image;
            if (e.image.result != e.native_result || !e.image.device || !e.image.native_device ||
                e.image.native_device != e.entry.x[0] || !e.image.image || !e.image.format ||
                !e.image.width || !e.image.height || !e.image.depth || !e.image.layers) return refuse(PublicMetadata, s);
            r.image_succeeded = true; s.state_ = Scope::State::BindBefore;
        } else if (s.state_ == Scope::State::BindAfter) {
            r.bind = e.bind;
            if (e.bind.result != e.native_result || e.bind.device != r.image.device ||
                e.bind.native_device != e.entry.x[0] || e.bind.native_device != r.image.native_device ||
                e.bind.image != r.image.image || e.bind.image != e.entry.x[1] ||
                !e.bind.memory || e.bind.memory != e.entry.x[2] || e.bind.offset != e.entry.x[3]) return refuse(PublicMetadata, s);
            r.bind_succeeded = true; s.state_ = Scope::State::ViewBefore;
        } else {
            r.view = e.view;
            if (e.view.result != e.native_result || e.view.device != r.image.device ||
                e.view.native_device != e.entry.x[0] || e.view.native_device != r.image.native_device ||
                e.view.image != r.image.image || !e.view.view || e.view.format != r.image.format) return refuse(PublicMetadata, s);
            r.view_succeeded = true; s.state_ = Scope::State::SendBefore;
        }
        return {};
    }
    if (s.state_ == Scope::State::SendAfter) {
        if (e.boundary != Boundary::SendLock || e.native_result != 0) return refuse(NativeFailure, s);
        if (!decode_checked(28) || !no_prefix(e)) return refuse(InvalidDecode | AcquisitionBudget, s);
        r.send_lock_succeeded = true; s.state_ = Scope::State::Writing; return {};
    }
    if (s.state_ == Scope::State::WriteAfter) {
        if (e.boundary != Boundary::Write || e.native_result <= 0 ||
            static_cast<std::uint64_t>(e.native_result) > e.entry.x[2]) return refuse(NativeFailure, s);
        if (!decode_checked(44)) return refuse(InvalidDecode | AcquisitionBudget, s);
        if (e.scalars.fd < 0 || e.entry.x[0] != static_cast<unsigned>(e.scalars.fd) ||
            (r.fd >= 0 && e.scalars.fd != r.fd)) return refuse(ChangedDescriptor, s);
        if (e.scalars.packet_base != e.entry.x[23] || (r.packet_base && e.scalars.packet_base != r.packet_base) ||
            e.scalars.packet_offset != r.reply_bytes || e.scalars.remaining != kReplyBytes - r.reply_bytes)
            return refuse(ChangedPacket, s);
        if (!e.prefix.successful_prefix_owned || e.prefix.count != static_cast<std::size_t>(e.native_result) ||
            e.prefix.count > kReplyBytes - r.reply_bytes) return refuse(InvalidPrefix, s);
        r.fd = e.scalars.fd; r.packet_base = e.scalars.packet_base;
        std::memcpy(r.reply.data() + r.reply_bytes, e.prefix.bytes.data(), e.prefix.count);
        r.reply_bytes += static_cast<std::uint32_t>(e.prefix.count);
        if (r.reply_bytes == kReplyBytes) {
            if (le32(r.reply.data()) != 0x235 || le32(r.reply.data() + 4) != r.invocation.sequence ||
                le32(r.reply.data() + 8) != 9 || r.reply[12] != 9 || !le64(r.reply.data() + 13)) return refuse(BadReply, s);
            r.reply_token = le64(r.reply.data() + 13); r.reply_complete = true;
        }
        s.state_ = Scope::State::Writing; return {};
    }
    return refuse(Order, s);
}
} // namespace frame_compositor_raw
