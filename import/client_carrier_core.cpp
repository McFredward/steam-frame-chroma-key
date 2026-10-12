#include "client_carrier_core.h"
#include <cstring>
#include <limits>

namespace frame_client_carrier {
namespace {
bool valid(const Route& r) noexcept {
    return r.process_generation && r.object_generation && r.connection_generation &&
        r.import_frame >= 4096 && r.import_frame % 16 == 0 &&
        r.import_frame <= std::numeric_limits<std::uintptr_t>::max() - 352 &&
        r.receiver_frame == r.import_frame + 352 && r.pipe >= 4096 &&
        r.record >= 4096 && r.record % 8 == 0 && r.fd >= 0 &&
        r.exact_native_route && r.active_invocation_owned &&
        r.continuous_connection_owned && r.immutable_lifetime_lease;
}
bool equal(const Route& a, const Route& b) noexcept {
    return a.process_generation == b.process_generation && a.object_generation == b.object_generation &&
        a.connection_generation == b.connection_generation && a.import_frame == b.import_frame &&
        a.receiver_frame == b.receiver_frame && a.pipe == b.pipe && a.record == b.record && a.fd == b.fd;
}
std::uint32_t le32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= std::uint32_t{p[i]} << (8 * i); return v;
}
std::uint64_t le64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v |= std::uint64_t{p[i]} << (8 * i); return v;
}
}
bool Observer::begin(const BeginProof& p, Scope& s) noexcept {
    if (p.site != kBegin) { if (armed.load() || s.active) counters.unsupported.fetch_add(1); return false; }
    if (s.active || s.excluded) {
        if (s.owner != reinterpret_cast<std::uintptr_t>(this)) {
            s.record.reason |= OwnerMismatch; counters.unmatched.fetch_add(1); return false;
        }
        if (s.active) { s.record.reason |= NestedImport; counters.nested.fetch_add(1); }
        return false;
    }
    if (!armed.load(std::memory_order_seq_cst)) return false;
    auto ticket = counters.claimed.load(std::memory_order_relaxed); bool admitted = false;
    for (std::size_t i = 0; i <= kLimit; ++i) {
        if (ticket >= kLimit) break;
        if (counters.claimed.compare_exchange_strong(ticket, ticket + 1, std::memory_order_relaxed)) { admitted = true; break; }
    }
    s = Scope{}; s.owner = reinterpret_cast<std::uintptr_t>(this);
    if (!admitted) { s.excluded = true; counters.cap_skipped.fetch_add(1); return false; }
    s.active = true; s.record.observation = ticket + 1; s.record.route = p.route;
    if (p.native_lock_result != 0 || !valid(p.route)) s.record.reason |= InvalidRoute;
    counters.active.fetch_add(1, std::memory_order_seq_cst);
    // The sequence is assigned by native code AFTER this entry. It stays absent.
    return true;
}
bool Observer::route(const Event& e, std::uint64_t site, Scope& s) noexcept {
    if (!s.active) return false;
    if (s.owner != reinterpret_cast<std::uintptr_t>(this)) { s.record.reason |= OwnerMismatch; return false; }
    if (e.site != site || !valid(e.route)) { s.record.reason |= InvalidRoute; return false; }
    if (!equal(e.route, s.record.route)) { s.record.reason |= ChangedRoute; return false; }
    return s.record.reason == None;
}
bool Observer::span(const Span& p, std::size_t total, std::size_t offset,
                    std::uint64_t& identity, std::uintptr_t& base, Scope& s) noexcept {
    if (!p.buffer_identity || !p.base || !p.owned_successful_prefix || !p.immutable_native_span ||
        !total || p.total != total || offset >= total || p.remaining != total - offset ||
        p.base > std::numeric_limits<std::uintptr_t>::max() - total || p.current != p.base + offset) {
        s.record.reason |= InvalidSpan; return false;
    }
    if (offset && (identity != p.buffer_identity || base != p.base)) { s.record.reason |= ChangedSpan; return false; }
    if (!offset) { identity = p.buffer_identity; base = p.base; }
    return true;
}
void Observer::request_written(const Span& p, const std::array<std::uint8_t, kHeaderBytes>& bytes,
                               std::size_t copied, std::int64_t result, Scope& s) noexcept {
    if (!route(p.event, kWrite, s)) return;
    if (s.record.request_complete || s.record.native_pending) { s.record.reason |= RepeatedTransfer; return; }
    if (result <= 0 || result > static_cast<std::int64_t>(kMaxRequestBytes)) { s.record.reason |= WriteFailed; return; }
    if (p.total <= kHeaderBytes || p.total > kMaxRequestBytes) { s.record.reason |= BadRequest; return; }
    if (!s.record.request_bytes) s.request_total = p.total;
    if (s.request_total != p.total) { s.record.reason |= ChangedSpan; return; }
    const auto offset = s.record.request_bytes;
    if (!span(p, s.request_total, offset, s.request_buffer, s.request_base, s)) return;
    const auto count = static_cast<std::size_t>(result);
    if (count > p.remaining) { s.record.reason |= InvalidSpan; return; }
    const auto header = offset < kHeaderBytes ?
        (count < kHeaderBytes - offset ? count : kHeaderBytes - offset) : 0;
    if (copied != header) { s.record.reason |= InvalidSpan; return; }
    if (header) std::memcpy(s.request_header.data() + offset, bytes.data(), header);
    s.record.request_bytes += static_cast<std::uint32_t>(count);
    if (s.record.request_bytes >= kHeaderBytes && !s.record.sequence_available) {
        if (le32(s.request_header.data()) != 0x234 || le32(s.request_header.data() + 8) != s.request_total - kHeaderBytes) {
            s.record.reason |= BadRequest; return;
        }
        s.record.sequence = le32(s.request_header.data() + 4);
        s.record.sequence_available = true;
    }
    s.record.request_complete = s.record.request_bytes == s.request_total;
}
void Observer::reply_header_read(const Span& p, const std::array<std::uint8_t, kHeaderBytes>& bytes,
                                 std::size_t copied, std::int64_t result, Scope& s) noexcept {
    if (!route(p.event, kHeaderRead, s)) return;
    if (!s.record.request_complete) { s.record.reason |= MissingRequest | ParserContamination; return; }
    if (s.record.reply_header_bytes == kHeaderBytes || s.record.native_pending) {
        s.record.reason |= RepeatedTransfer | ParserContamination; return;
    }
    if (result <= 0 || result > static_cast<std::int64_t>(kHeaderBytes) || copied != static_cast<std::size_t>(result)) {
        s.record.reason |= ReadFailed | ParserContamination; return;
    }
    if (!span(p, kHeaderBytes, s.record.reply_header_bytes, s.header_buffer, s.header_base, s) ||
        static_cast<std::size_t>(result) > p.remaining) { s.record.reason |= InvalidSpan | ParserContamination; return; }
    std::memcpy(s.reply_header.data() + s.record.reply_header_bytes, bytes.data(), copied);
    s.record.reply_header_bytes += static_cast<std::uint32_t>(copied);
    if (s.record.reply_header_bytes == kHeaderBytes &&
        (le32(s.reply_header.data()) != 0x235 || le32(s.reply_header.data() + 4) != s.record.sequence ||
         le32(s.reply_header.data() + 8) != kPayloadBytes)) s.record.reason |= BadReply | ParserContamination;
}
void Observer::reply_payload_read(const Span& p, const std::array<std::uint8_t, kPayloadBytes>& bytes,
                                  std::size_t copied, std::int64_t result, Scope& s) noexcept {
    if (!route(p.event, kPayloadRead, s)) return;
    if (s.record.reply_header_bytes != kHeaderBytes) { s.record.reason |= MissingReply | ParserContamination; return; }
    if (s.record.reply_payload_bytes == kPayloadBytes || s.record.native_pending) {
        s.record.reason |= RepeatedTransfer | ParserContamination; return;
    }
    if (result <= 0 || result > static_cast<std::int64_t>(kPayloadBytes) || copied != static_cast<std::size_t>(result)) {
        s.record.reason |= ReadFailed | ParserContamination; return;
    }
    if (!span(p, kPayloadBytes, s.record.reply_payload_bytes, s.payload_buffer, s.payload_base, s) ||
        static_cast<std::size_t>(result) > p.remaining) { s.record.reason |= InvalidSpan | ParserContamination; return; }
    std::memcpy(s.reply_payload.data() + s.record.reply_payload_bytes, bytes.data(), copied);
    s.record.reply_payload_bytes += static_cast<std::uint32_t>(copied);
    if (s.record.reply_payload_bytes != kPayloadBytes) return;
    if (s.reply_payload[0] != 0x09 || !le64(s.reply_payload.data() + 1)) {
        s.record.reason |= BadReply | ParserContamination; return;
    }
    s.record.reply_token = le64(s.reply_payload.data() + 1); s.record.reply_complete = true;
}
void Observer::pending(const PendingProof& p, Scope& s) noexcept {
    if (s.excluded) { if (s.owner == reinterpret_cast<std::uintptr_t>(this)) s = Scope{}; return; }
    if (!s.active) { counters.unmatched.fetch_add(1); return; }
    const bool verified = route(p.event, kPending, s);
    if (s.owner != reinterpret_cast<std::uintptr_t>(this)) return;
    if (!verified || p.native_unlock_result != 0 || !p.copied_while_native_lock_owned) s.record.reason |= InvalidRoute;
    if (s.record.native_pending) s.record.reason |= RepeatedTransfer;
    if (!s.record.request_complete || !s.record.sequence_available) s.record.reason |= MissingRequest;
    if (!s.record.reply_complete) s.record.reason |= MissingReply;
    if (!p.native_wait_success) s.record.reason |= NativeWaitFailed;
    s.record.parsed_token = p.parsed_token;
    if (!p.parsed_token_present || !p.parsed_token || p.parsed_token != s.record.reply_token)
        s.record.reason |= ParsedTokenMismatch;
    s.record.native_pending = s.record.reason == None;
    // This public unlock precedes the native output store. Keep a valid pending
    // scope for the following receiver reference, never certify publication here.
    if (!s.record.native_pending) close(s);
}
void Observer::reference(const ReferenceProof& p, Scope& s) noexcept {
    if (!s.active) { counters.unmatched.fetch_add(1); return; }
    const bool verified = route(p.event, kReference, s);
    if (s.owner != reinterpret_cast<std::uintptr_t>(this)) return;
    if (!s.record.native_pending) s.record.reason |= MissingPending;
    if (!verified || p.native_lock_result != 0 ||
        p.ref_frame > std::numeric_limits<std::uintptr_t>::max() - 240 ||
        p.ref_frame + 240 != s.record.route.receiver_frame || !p.live_token ||
        p.live_token != p.saved_token || p.live_token != s.record.reply_token ||
        p.live_token != s.record.parsed_token) s.record.reason |= InvalidReference;
    s.record.referenced_token = p.live_token;
    s.record.output_published = s.record.reference_invoked = s.record.reason == None;
    close(s);
}
void Observer::invalidate(Scope& s) noexcept { if (s.active && s.owner == reinterpret_cast<std::uintptr_t>(this)) s.record.reason |= LifetimeInvalidated; }
void Observer::abandon(std::uint64_t reason, Scope& s) noexcept {
    if (s.excluded) { if (s.owner == reinterpret_cast<std::uintptr_t>(this)) s = Scope{}; return; }
    if (!s.active) return;
    if (s.owner != reinterpret_cast<std::uintptr_t>(this)) { s.record.reason |= OwnerMismatch; return; }
    s.record.reason |= Abandoned | reason; close(s);
}
void Observer::close(Scope& s) noexcept {
    if (!publish(s.record)) s.record.output_published = false;
    (s.record.output_published ? counters.published : counters.rejected).fetch_add(1);
    counters.completed.fetch_add(1, std::memory_order_release);
    s = Scope{}; counters.active.fetch_sub(1, std::memory_order_seq_cst);
}
bool Observer::publish(const Record& r) noexcept {
    for (auto& slot : slots_) {
        unsigned empty = 0; if (!slot.state.compare_exchange_strong(empty, 1, std::memory_order_acquire)) continue;
        slot.value = r; slot.state.store(2, std::memory_order_release); return true;
    }
    counters.queue_loss.fetch_add(1); return false;
}
bool Observer::take(Record& r) noexcept {
    for (auto& slot : slots_) {
        unsigned ready = 2; if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) continue;
        r = slot.value; slot.state.store(0, std::memory_order_release); return true;
    }
    return false;
}
bool Observer::quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept {
    const auto before = callbacks.load(std::memory_order_seq_cst), active = counters.active.load(std::memory_order_seq_cst);
    return !before && !active && !callbacks.load(std::memory_order_seq_cst);
}
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Carrier counters must be lock free");
static_assert(std::atomic<unsigned>::is_always_lock_free, "Carrier publication must be lock free");
} // namespace frame_client_carrier
