#include "client_resource_raw.h"

#include <cstring>
#include <limits>

namespace frame_client_raw {
namespace {
constexpr std::uintptr_t kMaxAddress = (std::uintptr_t{1} << 48) - 1;
bool address(std::uintptr_t p, unsigned alignment = 8) noexcept {
    return p >= 4096 && p <= kMaxAddress && p % alignment == 0;
}
bool invocation_valid(const frame_client_native::Invocation& v) noexcept {
    return address(v.import_frame, 16) && v.import_frame <= kMaxAddress - 352 &&
        v.receiver_frame == v.import_frame + 352 && address(v.pipe, 16) && address(v.record);
}
bool same_invocation(const frame_client_native::Invocation& a,
                     const frame_client_native::Invocation& b) noexcept {
    return a.import_frame == b.import_frame && a.receiver_frame == b.receiver_frame &&
        a.pipe == b.pipe && a.record == b.record;
}
bool same_entry(const Entry& a, const Entry& b) noexcept { return a.sp == b.sp && a.x == b.x; }
bool same_scalars(const Scalars& a, const Scalars& b) noexcept {
    return same_invocation(a.invocation, b.invocation) &&
        a.native_sequence == b.native_sequence && a.sequence_available == b.sequence_available &&
        a.buffer_base == b.buffer_base && a.offset == b.offset && a.total == b.total &&
        a.remaining == b.remaining && a.fd == b.fd && a.token == b.token &&
        a.token_present == b.token_present && a.native_wait_success == b.native_wait_success &&
        a.accepted_fd_pending == b.accepted_fd_pending && a.refusal == b.refusal;
}
bool no_acquisition(const Event& e) noexcept {
    return !e.decoded_available && e.scalar_bytes_copied == 0 && e.prefix.count == 0 &&
        !e.prefix.successful_prefix_owned;
}
bool no_prefix(const Event& e) noexcept { return e.prefix.count == 0 && !e.prefix.successful_prefix_owned; }
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
bool decoded(const Event& e) noexcept {
    return e.decoded_available && e.scalars.valid() && invocation_valid(e.scalars.invocation) &&
        !e.scalars.accepted_fd_pending;
}
bool transfer(Boundary b) noexcept {
    return b == Boundary::Write || b == Boundary::HeaderRead || b == Boundary::PayloadRead;
}
std::size_t scalar_budget(Boundary b) noexcept {
    switch (b) {
    case Boundary::Begin: return 32;
    case Boundary::Write: return 88;
    case Boundary::HeaderRead: return 76;
    case Boundary::PayloadRead: return 80;
    case Boundary::Pending: return 44;
    case Boundary::Reference: return 40;
    default: return 0;
    }
}
bool add_scalar_budget(const Event& e, RawRecord& r, std::size_t expected) noexcept {
    if (e.scalar_bytes_copied != expected ||
        e.scalar_bytes_copied > kMaxScalarBytes - r.scalar_bytes_copied) return false;
    r.scalar_bytes_copied += e.scalar_bytes_copied;
    return true;
}
bool scalar_route(const Event& e, const RawRecord& r) noexcept {
    return decoded(e) && same_invocation(e.scalars.invocation, r.invocation);
}
bool registered_transfer(const Event& e) noexcept {
    // The After decoder authenticates source links and argument relationships;
    // these checks also bind successful geometry to its unchanged original call.
    const auto& s = e.scalars;
    return s.sequence_available && s.fd >= 0 &&
        e.entry.x[0] == static_cast<std::uint64_t>(s.fd) &&
        s.total && s.offset < s.total && s.remaining == s.total - s.offset &&
        s.buffer_base >= 4096 && s.buffer_base <= kMaxAddress - s.total &&
        e.entry.x[1] == s.buffer_base + s.offset && e.entry.x[2] == s.remaining;
}
}

bool Scope::active() const noexcept { return state_ != State::Fresh && state_ != State::Terminal; }
bool Scope::finished() const noexcept { return state_ == State::Terminal; }
frame_client_native::Expected Scope::expected() const noexcept {
    return {record_.invocation, record_.sequence, record_.sequence_available};
}

Outcome Observer::refuse(std::uint64_t reason, Scope& s) const noexcept {
    if (s.state_ == Scope::State::Terminal) return {};
    s.record_.reason |= reason;
    s.record_.matched = false;
    s.record_.terminal = true;
    s.state_ = Scope::State::Terminal;
    return {Kind::Refused, s.record_};
}
Outcome Observer::finish(Scope& s) const noexcept {
    s.record_.matched = true;
    s.record_.terminal = true;
    s.state_ = Scope::State::Terminal;
    return {Kind::Matched, s.record_};
}
Outcome Observer::abandon(Scope& s) const noexcept {
    if (s.finished()) return {};
    if (s.owner_ && s.owner_ != this) return refuse(OwnerMismatch | Abandoned, s);
    return refuse(Abandoned, s);
}
Outcome Observer::expire(unsigned reason, Scope& s) const noexcept {
    if (s.finished()) return {};
    s.record_.thread_reason = reason;
    return refuse(ThreadUnavailable | (s.owner_ && s.owner_ != this ? OwnerMismatch : None), s);
}

bool Observer::can_acquire(const Event& e, const Scope& s) const noexcept {
    if (s.finished() || s.owner_ != this || !e.current_thread_owned || !e.native_module_current ||
        e.thread_reason || !e.thread_identity || e.thread_identity != s.thread_identity_ ||
        !no_acquisition(e)) return false;
    const auto& r = s.record_;
    const auto budget = scalar_budget(e.boundary);
    if (!budget || r.scalar_bytes_copied > kMaxScalarBytes ||
        budget > kMaxScalarBytes - r.scalar_bytes_copied) return false;
    if (e.boundary == Boundary::Pending) {
        return e.phase == Phase::Before && !e.native_result_available &&
            s.state_ == Scope::State::PendingBefore && r.request_complete && r.reply_complete &&
            e.entry.x[0] == r.invocation.pipe && e.entry.sp == r.invocation.import_frame &&
            e.entry.x[29] == e.entry.sp;
    }
    if (e.phase != Phase::After || !e.native_result_available ||
        e.boundary != s.paired_boundary_ || !same_entry(e.entry, s.paired_entry_)) return false;
    if (e.boundary == Boundary::Begin)
        return s.state_ == Scope::State::BeginAfter && e.native_result == 0;
    if (e.boundary == Boundary::Reference)
        return s.state_ == Scope::State::ReferenceAfter && r.matching_pending &&
            e.native_result == 0;
    if (!transfer(e.boundary) || s.state_ != Scope::State::TransferAfter || e.native_result <= 0 ||
        e.entry.x[2] > kMaxRequestBytes || static_cast<std::uint64_t>(e.native_result) > e.entry.x[2] ||
        r.writes > kMaxWrites || r.header_reads > kMaxHeaderReads || r.payload_reads > kMaxPayloadReads ||
        r.writes + r.header_reads + r.payload_reads > kMaxTransfers) return false;
    const auto offset = e.boundary == Boundary::Write ? r.request_bytes :
        e.boundary == Boundary::HeaderRead ? r.reply_header_bytes : r.reply_payload_bytes;
    if (e.entry.x[19] != offset) return false;
    if (e.boundary == Boundary::Write) {
        return r.writes && e.entry.x[20] > kHeaderBytes && e.entry.x[20] <= kMaxRequestBytes &&
            (!s.request_total_ || s.request_total_ == e.entry.x[20]);
    }
    return e.boundary == Boundary::HeaderRead ?
        r.header_reads && r.request_complete && offset < kHeaderBytes &&
            e.entry.x[2] == kHeaderBytes - offset :
        r.payload_reads && r.reply_header_bytes == kHeaderBytes && offset < kPayloadBytes &&
            e.entry.x[2] == kPayloadBytes - offset;
}

Outcome Observer::observe(const Event& e, Scope& s) const noexcept {
    if (s.finished()) return {};
    auto& r = s.record_;
    r.last_boundary = e.boundary;
    r.last_phase = e.phase;
    r.last_native_result = e.native_result;
    r.last_native_result_available = e.native_result_available;
    r.last_preserved_errno = e.preserved_errno;
    r.thread_reason = e.thread_reason;
    if (s.owner_ && s.owner_ != this) return refuse(OwnerMismatch, s);
    if (!e.current_thread_owned || !e.thread_identity || e.thread_reason ||
        (s.thread_identity_ && s.thread_identity_ != e.thread_identity)) return refuse(ThreadUnavailable, s);
    if (!e.native_module_current) return refuse(ModuleUnavailable, s);
    s.owner_ = this;
    s.thread_identity_ = e.thread_identity;
    if (static_cast<unsigned>(e.boundary) > static_cast<unsigned>(Boundary::Reference))
        return refuse(UnsupportedBoundary, s);
    if (e.phase != Phase::Before && e.phase != Phase::After) return refuse(Order, s);
    if ((e.phase == Phase::Before && e.native_result_available) ||
        (e.phase == Phase::After && !e.native_result_available)) return refuse(UnmatchedCompletion, s);

    if (s.state_ == Scope::State::Fresh) {
        if (e.boundary != Boundary::Begin || e.phase != Phase::Before)
            return refuse(UnmatchedCompletion | MissingRequest, s);
        if (!no_acquisition(e)) return refuse(AcquisitionBudget, s);
        s.paired_entry_ = e.entry;
        s.paired_boundary_ = e.boundary;
        s.state_ = Scope::State::BeginAfter;
        return {};
    }

    const bool pair_pending = s.state_ == Scope::State::BeginAfter ||
        s.state_ == Scope::State::TransferAfter || s.state_ == Scope::State::PendingAfter ||
        s.state_ == Scope::State::ReferenceAfter;
    if (e.boundary == Boundary::Begin &&
        !(s.state_ == Scope::State::BeginAfter && e.phase == Phase::After))
        return refuse(NestedImport, s);
    if (pair_pending) {
        if (e.phase != Phase::After || e.boundary != s.paired_boundary_)
            return refuse(UnmatchedCompletion | Order, s);
        if (!same_entry(e.entry, s.paired_entry_)) return refuse(ChangedEntry, s);
    } else if (e.phase != Phase::Before) {
        return refuse(UnmatchedCompletion, s);
    }

    if (s.state_ == Scope::State::BeginAfter) {
        if (e.native_result != 0)
            return refuse(NativeFailure | (!no_acquisition(e) ? AcquisitionBudget : None), s);
        r.begin_lock_succeeded = true;
        if (!add_scalar_budget(e, r, 32) || !no_prefix(e)) return refuse(AcquisitionBudget, s);
        if (!decoded(e) || e.scalars.sequence_available || e.scalars.token_present) {
            r.decoder_refusal = e.scalars.refusal;
            return refuse(InvalidDecode, s);
        }
        r.invocation = e.scalars.invocation;
        s.state_ = Scope::State::Writing;
        return {};
    }

    if (s.state_ == Scope::State::TransferAfter) {
        if (e.native_result <= 0 || e.entry.x[2] > kMaxRequestBytes ||
            static_cast<std::uint64_t>(e.native_result) > e.entry.x[2])
            return refuse(NativeFailure | UnsupportedTransfer |
                (!no_acquisition(e) ? AcquisitionBudget : None), s);
        if (!add_scalar_budget(e, r, scalar_budget(e.boundary))) return refuse(AcquisitionBudget, s);
        if (!decoded(e)) {
            r.decoder_refusal = e.scalars.refusal;
            return refuse(InvalidDecode, s);
        }
        if (!same_invocation(e.scalars.invocation, r.invocation)) return refuse(ChangedInvocation, s);
        if (!registered_transfer(e)) return refuse(ChangedSpan, s);
        const auto& v = e.scalars;
        if (r.fd < 0) r.fd = v.fd;
        else if (r.fd != v.fd) return refuse(ChangedDescriptor, s);
        if (r.sequence_available && r.sequence != v.native_sequence) return refuse(ChangedInvocation, s);
        if (s.native_sequence_seen_ && s.native_sequence_ != v.native_sequence)
            return refuse(ChangedInvocation, s);
        s.native_sequence_ = v.native_sequence;
        s.native_sequence_seen_ = true;
        const auto count = static_cast<std::uint32_t>(e.native_result);
        std::size_t copy_count = count;
        std::uintptr_t* base = nullptr;
        std::uint32_t offset = 0;
        if (e.boundary == Boundary::Write) {
            if (v.total <= kHeaderBytes || v.total > kMaxRequestBytes) return refuse(BadRequest | UnsupportedTransfer, s);
            if (!s.request_total_) s.request_total_ = v.total;
            if (s.request_total_ != v.total) return refuse(ChangedSpan, s);
            base = &s.request_base_;
            offset = r.request_bytes;
            copy_count = offset < kHeaderBytes ?
                (count < kHeaderBytes - offset ? count : kHeaderBytes - offset) : 0;
        } else if (e.boundary == Boundary::HeaderRead) {
            if (v.total != kHeaderBytes) return refuse(ChangedSpan | ParserContamination, s);
            base = &s.header_base_;
            offset = r.reply_header_bytes;
        } else {
            if (v.total != kPayloadBytes) return refuse(ChangedSpan | ParserContamination, s);
            base = &s.payload_base_;
            offset = r.reply_payload_bytes;
        }
        if (v.offset != offset || (*base && *base != v.buffer_base))
            return refuse(ChangedSpan | (e.boundary != Boundary::Write ? ParserContamination : None), s);
        if (e.prefix.count != copy_count || e.prefix.count > e.prefix.bytes.size() ||
            e.prefix.successful_prefix_owned != (copy_count != 0)) return refuse(InvalidPrefix, s);
        if (e.prefix.count > kMaxWireBytes - r.wire_bytes_copied) return refuse(AcquisitionBudget, s);
        *base = v.buffer_base;
        r.wire_bytes_copied += e.prefix.count;
        if (e.boundary == Boundary::Write) {
            if (copy_count) std::memcpy(r.request_header.data() + offset, e.prefix.bytes.data(), copy_count);
            r.request_bytes += count;
            if (r.request_bytes >= kHeaderBytes && !r.sequence_available) {
                if (le32(r.request_header.data()) != 0x234 ||
                    le32(r.request_header.data() + 8) != s.request_total_ - kHeaderBytes ||
                    le32(r.request_header.data() + 4) != v.native_sequence) return refuse(BadRequest, s);
                r.sequence = le32(r.request_header.data() + 4);
                r.sequence_available = true;
            }
            r.request_complete = r.request_bytes == s.request_total_;
            s.state_ = r.request_complete ? Scope::State::Header : Scope::State::Writing;
        } else if (e.boundary == Boundary::HeaderRead) {
            std::memcpy(r.reply_header.data() + offset, e.prefix.bytes.data(), copy_count);
            r.reply_header_bytes += count;
            if (r.reply_header_bytes == kHeaderBytes) {
                if (le32(r.reply_header.data()) != 0x235 ||
                    le32(r.reply_header.data() + 4) != r.sequence ||
                    le32(r.reply_header.data() + 8) != kPayloadBytes)
                    return refuse(BadReply | ParserContamination, s);
                s.state_ = Scope::State::Payload;
            } else s.state_ = Scope::State::Header;
        } else {
            std::memcpy(r.reply_payload.data() + offset, e.prefix.bytes.data(), copy_count);
            r.reply_payload_bytes += count;
            if (r.reply_payload_bytes == kPayloadBytes) {
                if (r.reply_payload[0] != 0x09 || !le64(r.reply_payload.data() + 1))
                    return refuse(BadReply | ParserContamination, s);
                r.reply_token = le64(r.reply_payload.data() + 1);
                r.reply_complete = true;
                s.state_ = Scope::State::PendingBefore;
            } else s.state_ = Scope::State::Payload;
        }
        return {};
    }

    if (s.state_ == Scope::State::PendingAfter) {
        if (!e.decoded_available || !same_scalars(e.scalars, s.pending_snapshot_))
            return refuse(ChangedSnapshot, s);
        if (e.scalar_bytes_copied || !no_prefix(e)) return refuse(AcquisitionBudget, s);
        if (e.native_result != 0) return refuse(NativeFailure | PendingFailed, s);
        r.pending_unlock_succeeded = true;
        r.native_wait_success = e.scalars.native_wait_success;
        r.parsed_token = e.scalars.token;
        if (!r.request_complete || !r.sequence_available) return refuse(MissingRequest, s);
        if (!r.reply_complete) return refuse(MissingReply, s);
        if (!r.native_wait_success) return refuse(PendingFailed, s);
        if (!e.scalars.token_present || !r.parsed_token || r.parsed_token != r.reply_token)
            return refuse(TokenMismatch | PendingFailed, s);
        r.matching_pending = true;
        s.state_ = Scope::State::ReferenceBefore;
        return {};
    }

    if (s.state_ == Scope::State::ReferenceAfter) {
        if (e.native_result != 0)
            return refuse(NativeFailure | InvalidReference | (!no_acquisition(e) ? AcquisitionBudget : None), s);
        r.reference_lock_succeeded = true;
        if (!add_scalar_budget(e, r, 40) || !no_prefix(e)) return refuse(AcquisitionBudget, s);
        if (!scalar_route(e, r) || e.scalars.sequence_available) {
            r.decoder_refusal = e.scalars.refusal;
            return refuse(InvalidDecode | InvalidReference, s);
        }
        r.referenced_token = e.scalars.token;
        if (!r.matching_pending) return refuse(MissingPending, s);
        if (!e.scalars.token_present || !r.referenced_token || r.referenced_token != r.parsed_token ||
            r.referenced_token != r.reply_token) return refuse(TokenMismatch | InvalidReference, s);
        r.following_reference_observed = true;
        return finish(s);
    }

    if (transfer(e.boundary)) {
        const bool expected = (s.state_ == Scope::State::Writing && e.boundary == Boundary::Write) ||
            (s.state_ == Scope::State::Header && e.boundary == Boundary::HeaderRead) ||
            (s.state_ == Scope::State::Payload && e.boundary == Boundary::PayloadRead);
        if (!expected) return refuse(Order | (e.boundary != Boundary::Write ? ParserContamination : None), s);
        if (!no_acquisition(e)) return refuse(AcquisitionBudget, s);
        unsigned* count = e.boundary == Boundary::Write ? &r.writes :
            e.boundary == Boundary::HeaderRead ? &r.header_reads : &r.payload_reads;
        const unsigned limit = e.boundary == Boundary::Write ? kMaxWrites :
            e.boundary == Boundary::HeaderRead ? kMaxHeaderReads : kMaxPayloadReads;
        if (*count >= limit || r.writes + r.header_reads + r.payload_reads >= kMaxTransfers)
            return refuse(TransferLimit, s);
        ++*count;
        s.paired_entry_ = e.entry;
        s.paired_boundary_ = e.boundary;
        s.state_ = Scope::State::TransferAfter;
        return {};
    }

    if (e.boundary == Boundary::Pending) {
        if (s.state_ != Scope::State::PendingBefore)
            return refuse(Order | (!r.request_complete ? MissingRequest : MissingReply), s);
        if (!add_scalar_budget(e, r, 44) || !no_prefix(e)) return refuse(AcquisitionBudget, s);
        if (!scalar_route(e, r) || e.scalars.sequence_available) {
            r.decoder_refusal = e.scalars.refusal;
            return refuse(InvalidDecode | PendingFailed, s);
        }
        s.pending_snapshot_ = e.scalars;
        s.paired_entry_ = e.entry;
        s.paired_boundary_ = e.boundary;
        s.state_ = Scope::State::PendingAfter;
        return {};
    }
    if (e.boundary == Boundary::Reference) {
        if (s.state_ != Scope::State::ReferenceBefore) return refuse(MissingPending | Order, s);
        if (!no_acquisition(e)) return refuse(AcquisitionBudget, s);
        s.paired_entry_ = e.entry;
        s.paired_boundary_ = e.boundary;
        s.state_ = Scope::State::ReferenceAfter;
        return {};
    }
    return refuse(Order | UnsupportedBoundary, s);
}
static_assert(kMaxScalarBytes == 2068 && kMaxWireBytes == 33 && kMaxTransfers == 24,
              "Raw acquisition budgets must remain bounded");
} // namespace frame_client_raw
