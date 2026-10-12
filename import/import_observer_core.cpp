#include "import_observer_core.h"

#include <cstring>
#include <limits>

namespace frame_import {
namespace {

bool valid_route(const Route& r) noexcept {
    return r.process_generation && r.anchor_frame && r.anchor_frame % 16 == 0 &&
        r.pipe_identity && r.socket_generation && r.socket_fd >= 0 && r.peer_pid &&
        r.native_route_verified && r.active_anchor_verified &&
        r.client_identity_available && r.client_generation &&
        r.client_ownership_verified && r.socket_lifetime_verified &&
        r.peer_credentials_verified && r.same_native_connection_verified &&
        !r.foreign_namespace;
}

bool equal_route(const Route& a, const Route& b) noexcept {
    return a.process_generation == b.process_generation &&
        a.anchor_frame == b.anchor_frame && a.pipe_identity == b.pipe_identity &&
        a.client_identity == b.client_identity &&
        a.client_generation == b.client_generation &&
        a.socket_generation == b.socket_generation &&
        a.request_sequence == b.request_sequence && a.peer_pid == b.peer_pid &&
        a.socket_fd == b.socket_fd &&
        a.client_identity_available == b.client_identity_available;
}

bool matches_image(const ImageCandidate& image, std::uint64_t device,
                   std::uint64_t handle, std::uint64_t generation) noexcept {
    return image.device == device && image.image == handle &&
        image.image_generation == generation;
}

std::uint32_t le32(const std::uint8_t* p) noexcept {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t{p[i]} << (8 * i);
    return result;
}

std::uint64_t le64(const std::uint8_t* p) noexcept {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t{p[i]} << (8 * i);
    return result;
}

bool checked_span(std::uintptr_t base, std::size_t size) noexcept {
    return base && size && size <= std::numeric_limits<std::uintptr_t>::max() - base;
}

} // namespace

bool Observer::begin(const BeginProof& proof, Scope& scope) noexcept {
    const auto identity = reinterpret_cast<std::uintptr_t>(this);
    if (proof.site != kBeginSite) {
        if (armed.load(std::memory_order_seq_cst) || scope.active)
            counters.unsupported.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (scope.active || scope.excluded) {
        if (scope.observer_identity != identity) {
            scope.record.reason |= ObserverMismatch;
            counters.unmatched.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (scope.depth != std::numeric_limits<std::uint32_t>::max()) ++scope.depth;
        else scope.record.reason |= DepthOverflow;
        if (scope.active) {
            scope.record.reason |= NestedImport;
            counters.nested.fetch_add(1, std::memory_order_relaxed);
        }
        return false;
    }
    if (!armed.load(std::memory_order_seq_cst)) return false;
    // At most 32 successful reservations can race. This bounded loop neither
    // waits for a producer nor increments/wraps an exhausted admission counter.
    auto ticket = counters.claimed.load(std::memory_order_relaxed);
    bool admitted = false;
    for (std::size_t attempt = 0; attempt <= kAttemptLimit; ++attempt) {
        if (ticket >= kAttemptLimit) break;
        if (counters.claimed.compare_exchange_strong(ticket, ticket + 1,
                                                     std::memory_order_relaxed)) {
            admitted = true;
            break;
        }
    }
    scope = Scope{};
    scope.observer_identity = identity;
    scope.depth = 1;
    if (!admitted) {
        scope.excluded = true;
        counters.cap_skipped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    scope.active = true;
    scope.record.observation = ticket + 1;
    scope.record.route = proof.route;
    scope.record.client_identity_available = proof.route.client_identity_available;
    if (proof.lock_result != 0 || !valid_route(proof.route))
        scope.record.reason |= InvalidRoute;
    if (!proof.route.client_identity_available) scope.record.reason |= MissingClient;
    counters.active.fetch_add(1, std::memory_order_seq_cst);
    return true;
}

bool Observer::route(const EventProof& proof, std::uint64_t site, Scope& scope) noexcept {
    if (!scope.active) return false;
    if (scope.observer_identity != reinterpret_cast<std::uintptr_t>(this)) {
        scope.record.reason |= ObserverMismatch;
        counters.unmatched.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (proof.site != site || !valid_route(proof.route)) {
        scope.record.reason |= InvalidRoute;
        return false;
    }
    if (!equal_route(scope.record.route, proof.route)) {
        scope.record.reason |= ChangedRoute;
        return false;
    }
    return scope.record.reason == None && scope.depth == 1;
}

void Observer::candidate(const EventProof& proof, const ImageCandidate& image,
                         Scope& scope) noexcept {
    if (!route(proof, kImageSite, scope)) return;
    if (++scope.image_count != 1) {
        scope.record.reason |= MultipleImages;
        return;
    }
    scope.record.image = image;
    if (image.native_result != 0 || !image.owned_public_scalars_verified ||
        !image.device || !image.image || !image.image_generation || !image.format ||
        !image.width || !image.height || !image.layers)
        scope.record.reason |= InvalidImage;
}

void Observer::bind(const EventProof& proof, const Binding& binding,
                    Scope& scope) noexcept {
    if (!route(proof, kBindSite, scope)) return;
    if (++scope.binding_count != 1 || scope.image_count != 1 ||
        !matches_image(scope.record.image, binding.device, binding.image,
                       binding.image_generation) || binding.native_result != 0 ||
        !binding.owned_public_scalars_verified || !binding.memory ||
        !binding.memory_generation) {
        scope.record.reason |= InvalidBinding;
        return;
    }
    scope.record.binding = binding;
}

void Observer::view(const EventProof& proof, const View& view, Scope& scope) noexcept {
    if (!route(proof, kViewSite, scope)) return;
    if (++scope.view_count != 1 || scope.image_count != 1 || scope.binding_count != 1 ||
        !matches_image(scope.record.image, view.device, view.image,
                       view.image_generation) || view.native_result != 0 ||
        !view.owned_public_scalars_verified || !view.view || !view.view_generation) {
        scope.record.reason |= InvalidView;
        return;
    }
    scope.record.view = view;
}

void Observer::early_unlock(const EventProof& proof, Scope& scope) noexcept {
    // Native 0x1540b0 releases the pipe before the actual handler and Vulkan
    // creation. Lifetime is established by active native frames, not that lock.
    (void)route(proof, kEarlyUnlockSite, scope);
}

void Observer::written(const WriteProof& proof,
                       const std::array<std::uint8_t, kReplyBytes>& bytes,
                       std::size_t copied_size, std::int64_t result,
                       Scope& scope) noexcept {
    if (!route(proof.event, kWriteSite, scope)) return;
    if (proof.parent_return != kReplyParent || !proof.immutable_native_span_verified ||
        !proof.successful_prefix_owned || !proof.packet_identity ||
        proof.total_size != kReplyBytes || !checked_span(proof.packet_base, kReplyBytes)) {
        scope.record.reason |= InvalidSpan;
        return;
    }
    if (scope.record.written_bytes == kReplyBytes) {
        scope.record.reason |= RepeatedReply;
        return;
    }
    if (result <= 0 || result > static_cast<std::int64_t>(kReplyBytes)) {
        scope.record.reason |= WriteFailed;
        return;
    }
    const auto count = static_cast<std::size_t>(result);
    const auto offset = static_cast<std::size_t>(scope.record.written_bytes);
    if (copied_size != count || proof.remaining_size != kReplyBytes - offset ||
        count > proof.remaining_size || proof.current_address != proof.packet_base + offset) {
        scope.record.reason |= InvalidSpan;
        return;
    }
    if (offset && (proof.packet_identity != scope.packet_identity ||
                   proof.packet_base != scope.packet_base)) {
        scope.record.reason |= ChangedSpan;
        return;
    }
    if (!offset) {
        scope.packet_identity = proof.packet_identity;
        scope.packet_base = proof.packet_base;
    }
    std::memcpy(scope.packet.data() + offset, bytes.data(), count);
    scope.record.written_bytes += static_cast<std::uint32_t>(count);
    ++scope.record.successful_writes;
    if (scope.record.written_bytes != kReplyBytes) return;
    // Little-endian envelope: native response opcode / request sequence /
    // payload length; exactly one present protobuf fixed64 field follows.
    const auto* p = scope.packet.data();
    if (le32(p) != 0x235 || le32(p + 4) != scope.record.route.request_sequence ||
        le32(p + 8) != 9 || p[12] != 0x09 || !le64(p + 13)) {
        scope.record.reason |= BadReply;
        return;
    }
    scope.record.token = le64(p + 13);
}

void Observer::finish(const FinishProof& proof, Scope& scope) noexcept {
    if (scope.excluded) {
        if (scope.observer_identity == reinterpret_cast<std::uintptr_t>(this)) {
            if (scope.depth > 1) --scope.depth;
            else scope = Scope{};
        }
        return;
    }
    if (!scope.active) {
        counters.unmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (scope.observer_identity != reinterpret_cast<std::uintptr_t>(this)) {
        scope.record.reason |= ObserverMismatch;
        counters.unmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (scope.depth > 1) {
        --scope.depth;
        return;
    }
    const bool verified = route(proof.event, kFinishSite, scope);
    if (!verified || proof.parent_return != kReplyParent || proof.unlock_result != 0)
        scope.record.reason |= InvalidFinish;
    if (!proof.native_send_success) scope.record.reason |= NativeSendFailed;
    if (scope.image_count != 1) scope.record.reason |= MissingImage;
    if (scope.binding_count != 1) scope.record.reason |= MissingBinding;
    if (scope.view_count != 1) scope.record.reason |= MissingView;
    if (scope.record.written_bytes != kReplyBytes || !scope.record.token)
        scope.record.reason |= IncompleteReply;
    scope.record.import_reply_complete = scope.record.reason == None;
    close(scope);
}

void Observer::abandon(std::uint64_t reason, Scope& scope) noexcept {
    if (scope.excluded) {
        if (scope.observer_identity == reinterpret_cast<std::uintptr_t>(this)) scope = Scope{};
        return;
    }
    if (!scope.active) return;
    if (scope.observer_identity != reinterpret_cast<std::uintptr_t>(this)) {
        scope.record.reason |= ObserverMismatch;
        counters.unmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    scope.record.reason |= Abandoned | reason;
    scope.record.import_reply_complete = false;
    close(scope);
}

void Observer::invalidate_socket(Scope& scope) noexcept {
    // The adapter owns its per-thread Scope. Other-thread closes must change the
    // independently verified socket epoch before its next callback/finish proof.
    if (scope.active && scope.observer_identity == reinterpret_cast<std::uintptr_t>(this))
        scope.record.reason |= SocketInvalidated;
}

void Observer::retire_image(std::uint64_t image, std::uint64_t generation,
                            Scope& scope) noexcept {
    if (scope.active && scope.observer_identity == reinterpret_cast<std::uintptr_t>(this) &&
        scope.image_count && scope.record.image.image == image &&
        scope.record.image.image_generation == generation)
        scope.record.reason |= ResourceRetired;
}

void Observer::close(Scope& scope) noexcept {
    if (!publish(scope.record)) scope.record.import_reply_complete = false;
    counters.completed.fetch_add(1, std::memory_order_release);
    (scope.record.import_reply_complete ? counters.complete : counters.rejected)
        .fetch_add(1, std::memory_order_relaxed);
    scope = Scope{};
    counters.active.fetch_sub(1, std::memory_order_seq_cst);
}

bool Observer::publish(const Record& record) noexcept {
    for (auto& slot : slots_) {
        unsigned free = 0;
        if (!slot.state.compare_exchange_strong(free, 1, std::memory_order_acquire,
                                                std::memory_order_relaxed)) continue;
        slot.value = record;
        slot.state.store(2, std::memory_order_release);
        return true;
    }
    counters.queue_loss.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool Observer::take(Record& record) noexcept {
    for (auto& slot : slots_) {
        unsigned ready = 2;
        if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire,
                                                std::memory_order_relaxed)) continue;
        record = slot.value;
        slot.state.store(0, std::memory_order_release);
        return true;
    }
    return false;
}

bool Observer::quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept {
    const auto before = callbacks.load(std::memory_order_seq_cst);
    const auto active = counters.active.load(std::memory_order_seq_cst);
    const auto after = callbacks.load(std::memory_order_seq_cst);
    return !before && !active && !after;
}

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Import callbacks require lock-free counters");
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "Import callbacks require lock-free publication");

} // namespace frame_import
