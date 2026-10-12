#include "compositor_attempt.h"

#include <cstring>

namespace frame_compositor_attempt {
namespace {
using Boundary = frame_import_native::Boundary;
bool same_connection(const frame_import_pipe::Lease& a, const frame_import_pipe::Lease& b) noexcept {
    return a.object.registry_identity == b.object.registry_identity &&
        a.object.process_generation == b.object.process_generation &&
        a.object.object_generation == b.object.object_generation && a.object.pipe == b.object.pipe &&
        a.object.domain == b.object.domain && a.socket_generation == b.socket_generation && a.fd == b.fd;
}
bool local_lease(const frame_import_pipe::Registry& registry, const frame_import_pipe::Lease& lease) noexcept {
    return lease.valid() && lease.object.domain == frame_import_pipe::Domain::Compositor045 &&
        lease.local_socket_lifetime_observed && registry.check_lease(lease) &&
        !lease.native_cross_process_namespace && !lease.source_frame_associated &&
        !lease.accepted_submit && !lease.gpu_consumed_coherent;
}
bool peer_owned(const frame_import_pipe::Lease& lease) noexcept {
    return lease.fresh_native_peer_pid && lease.peer_pid > 0 &&
        lease.copied_native_peer_pid == lease.peer_pid;
}
bool same_identity(const frame_bindings::Identity& a, const frame_bindings::Identity& b) noexcept {
    return a.handle == b.handle && a.generation == b.generation;
}
bool matches_receipt(const OwnedAttempt& a, const frame_bindings::ImportResourceReceipt& r) noexcept {
    return a.complete() && r.resourceComplete && r.reason == frame_bindings::Reason::None &&
        !r.errors && !r.lossCount && r.journalInstance && r.localReplayOrder && r.resourceGenerationCursor &&
        r.invocationTag == a.tag() && r.imageInvocationTag == a.tag() && r.viewInvocationTag == a.tag() &&
        r.cookie == a.cookie() && r.device == a.device() && r.requestedImage == a.image() &&
        r.requestedView == a.view() && r.image.identity.handle == a.image() &&
        r.view.identity.handle == a.view() && r.image.identity.generation &&
        r.view.identity.generation && r.memory.identity.generation && r.image.complete &&
        r.view.complete && r.memory.complete && same_identity(r.image.memory, r.memory.identity) &&
        same_identity(r.view.image, r.image.identity) &&
        !r.nativeImportAccepted && !r.sourceFrameAssociated && !r.nativeCrossProcessNamespace &&
        !r.acceptedSubmit && !r.gpuConsumedCoherent;
}
} // namespace

bool Capture::fail(Refusal why) noexcept {
    if (record_.refusal_ == Refusal::None) record_.refusal_ = why;
    return false;
}
frame_import::Route Capture::route(const frame_import_pipe::Lease& lease, bool client) const noexcept {
    frame_import::Route r{};
    r.process_generation = lease.object.process_generation;
    r.anchor_frame = expected_.dispatch_frame;
    r.pipe_identity = expected_.pipe;
    r.socket_generation = lease.socket_generation;
    r.request_sequence = expected_.sequence;
    r.socket_fd = lease.fd;
    r.native_route_verified = r.active_anchor_verified = true;
    r.socket_lifetime_verified = r.same_native_connection_verified = true;
    // same_native_connection refers ONLY to this observed local object/socket
    // lifetime. It proves neither the other endpoint nor a token namespace.
    if (peer_owned(lease)) {
        r.peer_pid = static_cast<std::uint32_t>(lease.peer_pid);
        r.peer_credentials_verified = true; r.foreign_namespace = false;
    }
    if (client) {
        r.client_identity = record_.image_event_.route.client_identity;
        r.client_generation = record_.tag_;
        r.client_identity_available = r.client_ownership_verified = true;
    }
    return r;
}
bool Capture::live(const NativeInput& in, Boundary boundary, frame_import_native::Scalars& scalar) noexcept {
    if (stage_ == Stage::Idle || stage_ == Stage::Done || record_.refusal_ != Refusal::None) return false;
    scalar = frame_import_native::decode(boundary, in.entry, in.stack, in.load_bias, expected_);
    if (!scalar.valid()) return fail(Refusal::NativeRoute);
    if (!local_lease(registry_, in.lease) || !same_connection(record_.lease_, in.lease) ||
        in.lease.object.pipe != scalar.invocation.pipe) return fail(Refusal::Lease);
    if (record_.lease_.fresh_native_peer_pid &&
        (!peer_owned(in.lease) || record_.lease_.peer_pid != in.lease.peer_pid)) return fail(Refusal::Lease);
    if (boundary != Boundary::EarlyUnlock && boundary != Boundary::Begin && !peer_owned(in.lease))
        return fail(Refusal::Lease);
    if (scalar.client_available && boundary != Boundary::Image &&
        scalar.client != record_.image_event_.route.client_identity) return fail(Refusal::NativeRoute);
    record_.lease_ = in.lease;
    return true;
}
bool Capture::begin(const NativeInput& in, int result, std::uint64_t tag, std::uint64_t cookie) noexcept {
    if (pending_output_) return false;
    if (stage_ != Stage::Idle) return fail(Refusal::Nested);
    if (lost_) { record_.refusal_ = Refusal::ObserverLoss; return false; }
    record_ = OwnedAttempt{}; record_.refusal_ = Refusal::None;
    record_.tag_ = tag; record_.cookie_ = cookie; stage_ = Stage::Begin;
    const auto scalar = frame_import_native::decode(Boundary::Begin, in.entry, in.stack, in.load_bias);
    if (!scalar.valid()) return fail(Refusal::NativeRoute);
    expected_ = scalar.invocation;
    if (!tag || !cookie || result != 0) return fail(Refusal::NativeFailure);
    if (!local_lease(registry_, in.lease) || in.lease.object.pipe != expected_.pipe) return fail(Refusal::Lease);
    record_.lease_ = in.lease;
    record_.begin_ = {route(in.lease, false), frame_import::kBeginSite, result};
    return true;
}
bool Capture::early_unlock(const NativeInput& in, int result) noexcept {
    if (stage_ != Stage::Begin) return fail(Refusal::Ordering);
    frame_import_native::Scalars scalar;
    if (!live(in, Boundary::EarlyUnlock, scalar)) return false;
    record_.early_ = {route(in.lease, false), frame_import::kEarlyUnlockSite};
    record_.early_result_ = result; record_.payload_read_ = scalar.native_payload_read;
    if (result != 0 || !scalar.native_payload_read || !peer_owned(in.lease)) return fail(Refusal::NativeFailure);
    stage_ = Stage::Early; return true;
}
std::uint64_t Capture::prepare_vulkan(Boundary boundary, const NativeInput& in) noexcept {
    Stage required, pending;
    if (boundary == Boundary::Image) { required = Stage::Early; pending = Stage::ImagePending; }
    else if (boundary == Boundary::Bind) { required = Stage::Image; pending = Stage::BindPending; }
    else if (boundary == Boundary::View) { required = Stage::Bound; pending = Stage::ViewPending; }
    else { fail(Refusal::Ordering); return 0; }
    if (stage_ != required) { fail(Refusal::Ordering); return 0; }
    frame_import_native::Scalars scalar;
    if (!live(in, boundary, scalar) || !scalar.client_available) return 0;
    if (boundary == Boundary::Image) {
        record_.image_event_.route.client_identity = scalar.client;
        record_.image_event_ = {route(in.lease, true), frame_import::kImageSite};
    } else if (boundary == Boundary::Bind) record_.bind_event_ = {route(in.lease, true), frame_import::kBindSite};
    else record_.view_event_ = {route(in.lease, true), frame_import::kViewSite};
    for (std::size_t i = 0; i < public_arguments_.size(); ++i) public_arguments_[i] = in.entry.x[i];
    stage_ = pending; return record_.tag_;
}
bool Capture::image_result(const ImageResult& image) noexcept {
    if (stage_ != Stage::ImagePending) return fail(Refusal::Ordering);
    record_.image_ = image;
    if (image.result != 0 || !image.device || image.device != public_arguments_[0] ||
        !image.image || !image.format || !image.width ||
        !image.height || !image.depth || !image.layers) return fail(Refusal::PublicMetadata);
    stage_ = Stage::Image; return true;
}
bool Capture::bind_result(const BindResult& binding) noexcept {
    if (stage_ != Stage::BindPending) return fail(Refusal::Ordering);
    record_.bind_ = binding;
    if (binding.result != 0 || binding.device != record_.image_.device ||
        binding.image != record_.image_.image || !binding.memory || binding.device != public_arguments_[0] ||
        binding.image != public_arguments_[1] || binding.memory != public_arguments_[2] ||
        binding.offset != public_arguments_[3]) return fail(Refusal::PublicMetadata);
    stage_ = Stage::Bound; return true;
}
bool Capture::view_result(const ViewResult& view) noexcept {
    if (stage_ != Stage::ViewPending) return fail(Refusal::Ordering);
    record_.view_ = view;
    if (view.result != 0 || view.device != record_.image_.device || view.image != record_.image_.image ||
        !view.view || !view.format || view.device != public_arguments_[0]) return fail(Refusal::PublicMetadata);
    stage_ = Stage::Viewed; return true;
}
bool Capture::send_lock(const NativeInput& in, int result) noexcept {
    if (stage_ != Stage::Viewed) return fail(Refusal::Ordering);
    frame_import_native::Scalars scalar;
    if (!live(in, Boundary::SendLock, scalar)) return false;
    if (result != 0) return fail(Refusal::NativeFailure);
    stage_ = Stage::Sending; return true;
}
bool Capture::prepare_write(const NativeInput& in) noexcept {
    if (stage_ != Stage::Sending) return fail(Refusal::Ordering);
    frame_import_native::Scalars scalar;
    if (!live(in, Boundary::Write, scalar)) return false;
    if (scalar.fd != in.lease.fd || scalar.packet_offset != record_.byte_count_ ||
        scalar.remaining != frame_import::kReplyBytes - record_.byte_count_) return fail(Refusal::WritePrefix);
    pending_write_ = {};
    pending_write_.event = {route(in.lease, true), frame_import::kWriteSite};
    pending_write_.parent_return = frame_import::kReplyParent;
    pending_write_.packet_identity = record_.tag_;
    pending_write_.packet_base = scalar.packet_base;
    pending_write_.current_address = scalar.packet_base + scalar.packet_offset;
    pending_write_.total_size = frame_import::kReplyBytes;
    pending_write_.remaining_size = scalar.remaining;
    pending_write_.immutable_native_span_verified = pending_write_.successful_prefix_owned = true;
    stage_ = Stage::WritePending; return true;
}
bool Capture::write_result(const std::array<std::uint8_t, frame_import::kReplyBytes>& prefix,
                          std::size_t count, std::int64_t result) noexcept {
    if (stage_ != Stage::WritePending) return fail(Refusal::Ordering);
    if (result <= 0 || result > static_cast<std::int64_t>(frame_import::kReplyBytes) ||
        count != static_cast<std::size_t>(result) || count > pending_write_.remaining_size ||
        record_.write_count_ >= record_.writes_.size()) return fail(Refusal::WritePrefix);
    if (record_.write_count_ && pending_write_.packet_base != record_.writes_[0].proof.packet_base)
        return fail(Refusal::WritePrefix);
    record_.writes_[record_.write_count_++] = {pending_write_, static_cast<std::uint32_t>(count), result};
    std::memcpy(record_.bytes_.data() + record_.byte_count_, prefix.data(), count);
    record_.byte_count_ += static_cast<std::uint32_t>(count);
    stage_ = Stage::Sending; return true;
}
bool Capture::finish(const NativeInput& in, int result) noexcept {
    if (stage_ == Stage::Idle || stage_ == Stage::Done || pending_output_) return false;
    if (stage_ != Stage::Sending) fail(Refusal::Ordering);
    // Even a previously refused attempt retains the actual final scalar result
    // when this is still its exact native invocation. It never revives refusal.
    const auto scalar = frame_import_native::decode(Boundary::Finish, in.entry, in.stack, in.load_bias, expected_);
    if (scalar.valid()) {
        record_.finish_ = {{route(in.lease, true), frame_import::kFinishSite},
                          frame_import::kReplyParent, scalar.native_send_success, result};
        if (!local_lease(registry_, in.lease) || !same_connection(record_.lease_, in.lease) ||
            !peer_owned(in.lease) || (record_.lease_.fresh_native_peer_pid &&
            record_.lease_.peer_pid != in.lease.peer_pid)) fail(Refusal::Lease);
        else record_.lease_ = in.lease;
        if (!scalar.native_send_success || result != 0) fail(Refusal::NativeFailure);
        if (record_.byte_count_ != frame_import::kReplyBytes) fail(Refusal::Incomplete);
        record_.complete_ = record_.refusal_ == Refusal::None;
    } else fail(Refusal::NativeRoute);
    stage_ = Stage::Done; pending_output_ = true; return record_.complete();
}
void Capture::abandon() noexcept {
    if (stage_ == Stage::Idle || stage_ == Stage::Done) return;
    fail(Refusal::Incomplete); stage_ = Stage::Done; pending_output_ = true;
}
void Capture::observer_loss() noexcept {
    lost_ = true;
    if (stage_ != Stage::Idle) fail(Refusal::ObserverLoss);
}
bool Capture::take(OwnedAttempt& out) noexcept {
    if (!pending_output_) return false;
    out = record_; record_ = OwnedAttempt{}; expected_ = {}; pending_write_ = {};
    public_arguments_ = {}; pending_output_ = false; stage_ = Stage::Idle; return true;
}

Refusal Replay::replay(const OwnedAttempt& a, const frame_bindings::ImportResourceReceipt& r,
                       const frame_import_pipe::Registry& registry, frame_import::Record& out) noexcept {
    out = {};
    if (!a.complete()) return a.refusal();
    if (!local_lease(registry, a.lease_)) return Refusal::Lease;
    if (!matches_receipt(a, r) || !validator_.owner || !validator_.current) return Refusal::Receipt;
    if (!validator_.current(validator_.owner, r)) return Refusal::ResourceNotCurrent;
    if (r.image.format != a.image_.format || r.image.extent.width != a.image_.width ||
        r.image.extent.height != a.image_.height || r.image.extent.depth != a.image_.depth ||
        r.image.arrayLayers != a.image_.layers || r.memory.identity.handle != a.bind_.memory ||
        r.image.memoryOffset != a.bind_.offset || r.view.format != a.view_.format)
        return Refusal::Receipt;
    if (a.image_.modifier_available && (!r.image.extensions.hasDrmExplicit ||
        r.image.extensions.drmModifier != a.image_.modifier)) return Refusal::Receipt;
    for (std::size_t i = 0; i < replayed_count_; ++i)
        if (replayed_tags_[i] == a.tag_ || replayed_cookies_[i] == a.cookie_) return Refusal::RepeatedReplay;
    if (replayed_count_ == replayed_tags_.size()) return Refusal::ObserverCapacity;
    replayed_tags_[replayed_count_] = a.tag_; replayed_cookies_[replayed_count_++] = a.cookie_;
    observer_.armed.store(true);
    frame_import::Scope scope;
    if (!observer_.begin(a.begin_, scope)) return Refusal::ObserverCapacity;
    observer_.early_unlock(a.early_, a.early_result_, a.payload_read_, scope);
    frame_import::ImageCandidate image{};
    image.device = a.image_.device; image.image = a.image_.image;
    image.image_generation = r.image.identity.generation;
    image.modifier = a.image_.modifier; image.modifier_available = a.image_.modifier_available;
    image.format = a.image_.format; image.width = a.image_.width; image.height = a.image_.height;
    image.layers = a.image_.layers; image.native_result = a.image_.result; image.owned_public_scalars_verified = true;
    observer_.candidate(a.image_event_, image, scope);
    frame_import::Binding binding{};
    binding.device = a.bind_.device; binding.image = a.bind_.image;
    binding.image_generation = r.image.identity.generation;
    binding.memory = a.bind_.memory; binding.memory_generation = r.memory.identity.generation;
    binding.memory_offset = a.bind_.offset; binding.native_result = a.bind_.result; binding.owned_public_scalars_verified = true;
    observer_.bind(a.bind_event_, binding, scope);
    frame_import::View view{};
    view.device = a.view_.device; view.image = a.view_.image; view.view = a.view_.view;
    view.image_generation = r.image.identity.generation; view.view_generation = r.view.identity.generation;
    view.native_result = a.view_.result; view.owned_public_scalars_verified = true;
    observer_.view(a.view_event_, view, scope);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < a.write_count_; ++i) {
        std::array<std::uint8_t, frame_import::kReplyBytes> prefix{};
        std::memcpy(prefix.data(), a.bytes_.data() + offset, a.writes_[i].count);
        observer_.written(a.writes_[i].proof, prefix, a.writes_[i].count, a.writes_[i].result, scope);
        offset += a.writes_[i].count;
    }
    // Publication is background-owned. A lifecycle loss/close or resource
    // retirement occurring during replay cannot leave an accepted record.
    if (!local_lease(registry, a.lease_)) observer_.invalidate_socket(scope);
    if (!validator_.current(validator_.owner, r))
        observer_.retire_image(image.image, image.image_generation, scope);
    observer_.finish(a.finish_, scope);
    if (!observer_.take(out)) return Refusal::ObserverCapacity;
    // Recheck immediately before the caller receives the owned publication.
    if (!local_lease(registry, a.lease_)) { out.import_reply_complete = false; out.reason |= frame_import::SocketInvalidated; return Refusal::Lease; }
    if (!validator_.current(validator_.owner, r)) { out.import_reply_complete = false; out.reason |= frame_import::ResourceRetired; return Refusal::ResourceNotCurrent; }
    return out.import_reply_complete ? Refusal::None : Refusal::Incomplete;
}
} // namespace frame_compositor_attempt
