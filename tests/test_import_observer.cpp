#include "import/import_observer_core.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <thread>
#include <vector>

using namespace frame_import;
namespace {
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "import check %u failed\n", checks);
        std::abort();
    }
}

// Independent serialized success fixture, not produced by the tested parser.
constexpr std::array<std::uint8_t, kReplyBytes> kSuccess{
    0x35, 0x02, 0x00, 0x00, 0x40, 0x30, 0x20, 0x10, 0x09, 0x00, 0x00, 0x00,
    0x09, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
};
constexpr std::uint64_t kToken = 0x8877665544332211ULL;

struct Fixture {
    Observer observer{};
    Scope scope{};
    Route route{};
    ImageCandidate image{};
    Binding binding{};
    View image_view{};
    std::array<std::uint8_t, kReplyBytes> packet = kSuccess;
    std::uint64_t packet_identity = 500;
    std::uintptr_t packet_base = 0x9000;
    Fixture() {
        route.process_generation = 11;
        route.anchor_frame = 0x1000;
        route.pipe_identity = 0x2000;
        route.client_identity = 51;
        route.client_generation = 3;
        route.socket_generation = 7;
        route.request_sequence = 0x10203040;
        route.peer_pid = 6000;
        route.socket_fd = 9;
        route.native_route_verified = route.active_anchor_verified = true;
        route.client_identity_available = route.client_ownership_verified = true;
        route.socket_lifetime_verified = route.peer_credentials_verified = true;
        route.same_native_connection_verified = true;
        route.foreign_namespace = false;
        image.device = 0x20;
        image.image = 0x123;
        image.image_generation = 2;
        image.modifier = 0;
        image.format = 37;
        image.width = 1920;
        image.height = 1080;
        image.layers = 1;
        image.native_result = 0;
        image.owned_public_scalars_verified = image.modifier_available = true;
        binding.device = image.device;
        binding.image = image.image;
        binding.image_generation = image.image_generation;
        binding.memory = 0x987;
        binding.memory_generation = 2;
        binding.memory_offset = 1024;
        binding.native_result = 0;
        binding.owned_public_scalars_verified = true;
        image_view.device = image.device;
        image_view.image = image.image;
        image_view.image_generation = image.image_generation;
        image_view.view = 0x999;
        image_view.view_generation = 11;
        image_view.native_result = 0;
        image_view.owned_public_scalars_verified = true;
        observer.armed = true;
    }
    Route provisional_route() const {
        Route provisional = route;
        provisional.client_identity = provisional.client_generation = 0;
        provisional.client_identity_available = provisional.client_ownership_verified = false;
        return provisional;
    }
    EventProof event(std::uint64_t site) const {
        return {site == kEarlyUnlockSite ? provisional_route() : route, site};
    }
    bool begin() { return observer.begin({provisional_route(), kBeginSite, 0}, scope); }
    void gpu() {
        observer.candidate(event(kImageSite), image, scope);
        observer.bind(event(kBindSite), binding, scope);
        observer.view(event(kViewSite), image_view, scope);
    }
    WriteProof write_proof() const {
        WriteProof proof{};
        proof.event = event(kWriteSite);
        proof.parent_return = kReplyParent;
        proof.packet_identity = packet_identity;
        proof.packet_base = packet_base;
        proof.current_address = packet_base + scope.record.written_bytes;
        proof.total_size = kReplyBytes;
        proof.remaining_size = kReplyBytes - scope.record.written_bytes;
        proof.immutable_native_span_verified = proof.successful_prefix_owned = true;
        return proof;
    }
    void write(std::size_t count) {
        std::array<std::uint8_t, kReplyBytes> owned{};
        const auto offset = scope.record.written_bytes;
        for (std::size_t i = 0; i < count && i + offset < packet.size(); ++i)
            owned[i] = packet[i + offset];
        observer.written(write_proof(), owned, count, static_cast<std::int64_t>(count), scope);
    }
    FinishProof finish_proof() const { return {event(kFinishSite), kReplyParent, true, 0}; }
    void finish() { observer.finish(finish_proof(), scope); }
    Record take() {
        Record record{};
        check(observer.take(record));
        check(!observer.take(record));
        return record;
    }
    void full() { check(begin()); gpu(); write(kReplyBytes); finish(); }
};

void verify_denials(const Record& r) {
    check(!r.native_cross_process_namespace);
    check(!r.source_frame_associated);
    check(!r.accepted_submit);
    check(!r.gpu_consumed_coherent);
}
void rejected(Fixture& f, std::uint64_t reason) {
    f.finish();
    const auto record = f.take();
    check(!record.import_reply_complete && (record.reason & reason));
    check(f.observer.counters.rejected == 1 && !f.observer.counters.complete);
    check(!f.scope.active && !f.observer.counters.active);
    verify_denials(record);
}

} // namespace

int main() {
    {
        Fixture f;
        f.observer.armed = false;
        check(!f.begin());
        check(!f.scope.active && !f.observer.counters.claimed);
        f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        f.observer.armed = true;
        for (const auto old : {0x153418ULL, 0x137dd0ULL, 0x70ad0ULL, 0ULL})
            check(!f.observer.begin({f.route, old, 0}, f.scope));
        check(!f.scope.active && !f.observer.counters.claimed);
        check(f.observer.counters.unsupported == 4);
        check(f.begin());
        check(!f.observer.begin({f.route, 0x153418, 0}, f.scope));
        check(f.scope.depth == 1 && !f.scope.record.reason);
        check(!f.scope.client_attached && !f.scope.record.client_identity_available);
        check(!f.scope.record.route.client_identity && !f.scope.record.route.client_generation);
        f.observer.early_unlock(f.event(kEarlyUnlockSite), f.scope);
        check(f.scope.active && !f.observer.counters.completed);
        f.gpu();
        check(f.scope.client_attached && f.scope.record.route.client_identity == 51);
        check(f.scope.record.route.client_generation == 3 && f.scope.record.client_identity_available);
        f.write(kReplyBytes);
        check(!f.scope.record.import_reply_complete && f.scope.record.token == kToken);
        f.finish();
        const auto r = f.take();
        check(r.import_reply_complete && !r.reason && r.token == kToken);
        check(r.image.image == f.image.image && r.image.image_generation == 2);
        check(r.binding.memory == f.binding.memory && r.view.view_generation == 11);
        check(r.written_bytes == kReplyBytes && r.successful_writes == 1);
        check(r.observation == 1 && r.route.request_sequence == 0x10203040);
        check(r.client_identity_available && f.observer.counters.complete == 1);
        verify_denials(r);
    }
    // The typed client does not exist at the pipe-lock boundary. The old
    // fixture invented that future fact and therefore never modeled this route.
    {
        Fixture f;
        check(f.begin());
        f.observer.early_unlock(f.event(kEarlyUnlockSite), f.scope);
        check(!f.scope.client_attached && !f.scope.record.reason);
        f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        check(f.scope.client_attached && f.scope.record.route.client_identity == f.route.client_identity);
        // The native object is now allowed to be released. Later event proofs
        // carry its owned scalar copy; no native pointer exists in this fixture.
        f.observer.bind(f.event(kBindSite), f.binding, f.scope);
        f.observer.view(f.event(kViewSite), f.image_view, f.scope);
        f.write(21); f.finish(); check(f.take().import_reply_complete);
    }
    for (unsigned missing = 0; missing < 3; ++missing) {
        Fixture f; check(f.begin());
        if (missing == 0) f.route.client_identity_available = false;
        if (missing == 1) f.route.client_ownership_verified = false;
        if (missing == 2) f.route.client_generation = 0;
        f.gpu();
        check(!f.scope.client_attached && !f.scope.record.client_identity_available);
        check(!f.scope.record.route.client_identity && !f.scope.record.route.client_generation);
        f.write(21); rejected(f, MissingClient);
    }
    {
        Fixture f; check(f.begin());
        // Client ID zero is a known scalar, not an absent scalar. Availability
        // and protected ownership, rather than a guessed numeric range, decide.
        f.route.client_identity = 0;
        f.gpu(); f.write(21); f.finish(); check(f.take().import_reply_complete);
    }
    for (unsigned duplicate = 0; duplicate < 2; ++duplicate) {
        Fixture f; check(f.begin());
        f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        if (duplicate) ++f.route.client_identity;
        f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        check(f.scope.record.route.client_identity == 51);
        rejected(f, DuplicateClientAttachment);
    }
    {
        Fixture f; check(f.begin());
        f.write(21); rejected(f, MissingClient);
    }
    {
        Fixture f;
        check(f.observer.begin({f.route, kBeginSite, 0}, f.scope));
        check(!f.scope.client_attached && !f.scope.record.route.client_identity);
        f.gpu(); f.write(21); rejected(f, UnexpectedClient);
    }
    {
        Fixture f; check(f.begin());
        f.observer.early_unlock({f.route, kEarlyUnlockSite}, f.scope);
        rejected(f, UnexpectedClient);
    }
    {
        Fixture f; check(f.begin()); f.gpu();
        f.observer.early_unlock(f.event(kEarlyUnlockSite), f.scope);
        rejected(f, UnexpectedClient);
    }
    // Each post-creation phase must carry the same protected client copy.
    for (unsigned phase = 0; phase < 4; ++phase) {
        for (unsigned changed = 0; changed < 5; ++changed) {
            Fixture f; check(f.begin());
            f.observer.candidate(f.event(kImageSite), f.image, f.scope);
            if (phase > 0) f.observer.bind(f.event(kBindSite), f.binding, f.scope);
            if (phase > 1) f.observer.view(f.event(kViewSite), f.image_view, f.scope);
            if (phase > 2) f.write(21);
            if (changed == 0) f.route.client_identity_available = false;
            if (changed == 1) f.route.client_ownership_verified = false;
            if (changed == 2) f.route.client_generation = 0;
            if (changed == 3) ++f.route.client_identity;
            if (changed == 4) ++f.route.client_generation;
            if (phase == 0) f.observer.bind(f.event(kBindSite), f.binding, f.scope);
            if (phase == 1) f.observer.view(f.event(kViewSite), f.image_view, f.scope);
            if (phase == 2) f.write(21);
            rejected(f, changed < 3 ? MissingClient : ChangedRoute);
        }
    }
    // Every possible two-part boundary, including splits inside the token.
    for (std::size_t split = 1; split < kReplyBytes; ++split) {
        Fixture f;
        check(f.begin()); f.gpu(); f.write(split);
        check(f.scope.record.written_bytes == split && !f.scope.record.token);
        f.write(kReplyBytes - split); f.finish();
        const auto r = f.take();
        check(r.import_reply_complete && r.token == kToken && r.successful_writes == 2);
    }
    {
        Fixture f; check(f.begin()); f.gpu();
        for (std::size_t i = 0; i < kReplyBytes; ++i) f.write(1);
        f.finish();
        const auto r = f.take();
        check(r.import_reply_complete && r.successful_writes == kReplyBytes);
    }
    // One-bit/schema mutations in the independent envelope and field tag.
    for (std::size_t byte = 0; byte < 13; ++byte) {
        Fixture f; check(f.begin()); f.gpu(); f.packet[byte] ^= 1; f.write(kReplyBytes);
        rejected(f, BadReply);
    }
    {
        Fixture f; check(f.begin()); f.gpu();
        for (std::size_t i = 13; i < f.packet.size(); ++i) f.packet[i] = 0;
        f.write(kReplyBytes); rejected(f, BadReply);
    }
    for (unsigned which = 0; which < 17; ++which) {
        Fixture f;
        auto provisional = f.provisional_route();
        switch (which) {
        case 0: provisional.process_generation = 0; break;
        case 1: provisional.anchor_frame = 0; break;
        case 2: provisional.anchor_frame = 0x1008; break;
        case 3: provisional.pipe_identity = 0; break;
        case 4: provisional.client_generation = 1; break;
        case 5: provisional.socket_generation = 0; break;
        case 6: provisional.socket_fd = -1; break;
        case 7: provisional.peer_pid = 0; break;
        case 8: provisional.native_route_verified = false; break;
        case 9: provisional.active_anchor_verified = false; break;
        case 10: provisional.client_identity_available = true; break;
        case 11: provisional.client_ownership_verified = true; break;
        case 12: provisional.socket_lifetime_verified = false; break;
        case 13: provisional.peer_credentials_verified = false; break;
        case 14: provisional.same_native_connection_verified = false; break;
        case 15: provisional.foreign_namespace = true; break;
        default: break;
        }
        check(f.observer.begin({provisional, kBeginSite, which == 16 ? 1 : 0}, f.scope));
        f.gpu(); f.write(kReplyBytes); rejected(f, which == 4 || which == 10 || which == 11 ? UnexpectedClient : InvalidRoute);
    }
    // Reused frame address alone never permits a changed native invocation.
    for (unsigned which = 0; which < 9; ++which) {
        Fixture f; check(f.begin()); f.gpu();
        switch (which) {
        case 0: ++f.route.process_generation; break;
        case 1: f.route.anchor_frame += 16; break;
        case 2: ++f.route.pipe_identity; break;
        case 3: ++f.route.client_identity; break;
        case 4: ++f.route.client_generation; break;
        case 5: ++f.route.socket_generation; break; // Same FD, different lifetime.
        case 6: ++f.route.request_sequence; break;
        case 7: ++f.route.peer_pid; break;
        default: ++f.route.socket_fd; break;
        }
        f.write(kReplyBytes); rejected(f, ChangedRoute);
    }
    for (unsigned which = 0; which < 10; ++which) {
        Fixture f; check(f.begin());
        switch (which) {
        case 0: f.image.native_result = -1; break;
        case 1: f.image.owned_public_scalars_verified = false; break;
        case 2: f.image.device = 0; break;
        case 3: f.image.image = 0; break;
        case 4: f.image.image_generation = 0; break;
        case 5: f.image.format = 0; break;
        case 6: f.image.width = 0; break;
        case 7: f.image.height = 0; break;
        case 8: f.image.layers = 0; break;
        default: break;
        }
        f.gpu();
        if (which == 9) f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        f.write(kReplyBytes); rejected(f, which == 9 ? MultipleImages : InvalidImage);
    }
    for (unsigned which = 0; which < 8; ++which) {
        Fixture f; check(f.begin());
        switch (which) {
        case 0: ++f.binding.device; break;
        case 1: ++f.binding.image; break;
        case 2: ++f.binding.image_generation; break;
        case 3: f.binding.memory = 0; break;
        case 4: f.binding.memory_generation = 0; break;
        case 5: f.binding.native_result = -1; break;
        case 6: f.binding.owned_public_scalars_verified = false; break;
        default: break;
        }
        f.gpu();
        if (which == 7) f.observer.bind(f.event(kBindSite), f.binding, f.scope);
        f.write(kReplyBytes); rejected(f, InvalidBinding);
    }
    for (unsigned which = 0; which < 8; ++which) {
        Fixture f; check(f.begin());
        switch (which) {
        case 0: ++f.image_view.device; break;
        case 1: ++f.image_view.image; break;
        case 2: ++f.image_view.image_generation; break;
        case 3: f.image_view.view = 0; break;
        case 4: f.image_view.view_generation = 0; break;
        case 5: f.image_view.native_result = -1; break;
        case 6: f.image_view.owned_public_scalars_verified = false; break;
        default: break;
        }
        f.gpu();
        if (which == 7) f.observer.view(f.event(kViewSite), f.image_view, f.scope);
        f.write(kReplyBytes); rejected(f, InvalidView);
    }
    for (unsigned missing = 0; missing < 3; ++missing) {
        Fixture f; check(f.begin());
        if (missing != 0) f.observer.candidate(f.event(kImageSite), f.image, f.scope);
        if (missing != 0 && missing != 1) f.observer.bind(f.event(kBindSite), f.binding, f.scope);
        f.write(kReplyBytes);
        rejected(f, missing == 0 ? MissingImage : missing == 1 ? MissingBinding : MissingView);
    }
    for (unsigned which = 0; which < 14; ++which) {
        Fixture f; check(f.begin()); f.gpu();
        auto proof = f.write_proof();
        std::int64_t result = kReplyBytes;
        std::size_t copied = kReplyBytes;
        switch (which) {
        case 0: proof.parent_return = 0; break;
        case 1: proof.packet_identity = 0; break;
        case 2: proof.packet_base = 0; break;
        case 3: proof.packet_base = std::numeric_limits<std::uintptr_t>::max() - 20; break;
        case 4: --proof.total_size; break;
        case 5: ++proof.total_size; break;
        case 6: --proof.remaining_size; break;
        case 7: ++proof.current_address; break;
        case 8: proof.immutable_native_span_verified = false; break;
        case 9: proof.successful_prefix_owned = false; break;
        case 10: result = 0; copied = 0; break;
        case 11: result = -1; copied = 0; break;
        case 12: result = 22; copied = 21; break;
        default: copied = 20; break;
        }
        f.observer.written(proof, kSuccess, copied, result, f.scope);
        rejected(f, which >= 10 && which <= 12 ? WriteFailed : InvalidSpan);
    }
    for (unsigned which = 0; which < 6; ++which) {
        Fixture f; check(f.begin()); f.gpu(); f.write(12);
        auto proof = f.write_proof();
        switch (which) {
        case 0: ++proof.packet_identity; break;
        case 1: ++proof.packet_base; ++proof.current_address; break;
        case 2: --proof.current_address; break; // Duplicate/overlapping prefix.
        case 3: ++proof.current_address; break; // Gap.
        case 4: ++proof.remaining_size; break;
        default: proof.event.site = 0x172c04; break;
        }
        std::array<std::uint8_t, kReplyBytes> tail{};
        for (std::size_t i = 0; i < 9; ++i) tail[i] = kSuccess[i + 12];
        f.observer.written(proof, tail, 9, 9, f.scope);
        rejected(f, which < 2 ? ChangedSpan : which == 5 ? InvalidRoute : InvalidSpan);
    }
    {
        Fixture f; check(f.begin()); f.gpu(); f.write(kReplyBytes);
        f.observer.written(f.write_proof(), kSuccess, 21, 21, f.scope);
        rejected(f, RepeatedReply);
    }
    for (unsigned which = 0; which < 5; ++which) {
        Fixture f; check(f.begin()); f.gpu(); f.write(kReplyBytes);
        auto proof = f.finish_proof();
        switch (which) {
        case 0: proof.native_send_success = false; break;
        case 1: proof.unlock_result = 1; break;
        case 2: proof.parent_return = 0; break;
        case 3: proof.event.site = kEarlyUnlockSite; break;
        default: proof.event.route.active_anchor_verified = false; break;
        }
        f.observer.finish(proof, f.scope);
        const auto r = f.take();
        check(!r.import_reply_complete && (r.reason & (which == 0 ? NativeSendFailed : InvalidFinish)));
    }
    {
        Fixture f; check(f.begin()); f.gpu(); f.write(20);
        rejected(f, IncompleteReply);
    }
    {
        Fixture f; check(f.begin()); f.gpu(); f.write(kReplyBytes);
        f.observer.invalidate_socket(f.scope); rejected(f, SocketInvalidated);
    }
    {
        Fixture f; check(f.begin()); f.gpu(); f.write(kReplyBytes);
        f.observer.retire_image(f.image.image, f.image.image_generation, f.scope);
        rejected(f, ResourceRetired);
    }
    {
        Fixture f; check(f.begin()); f.gpu();
        f.observer.retire_image(f.image.image, f.image.image_generation + 1, f.scope);
        f.write(kReplyBytes); f.finish(); check(f.take().import_reply_complete);
    }
    {
        Fixture f; check(f.begin());
        check(!f.begin());
        check(f.scope.depth == 2 && f.observer.counters.nested == 1);
        f.finish(); check(f.scope.active && f.scope.depth == 1);
        rejected(f, NestedImport);
    }
    {
        Fixture f; check(f.begin());
        f.scope.depth = std::numeric_limits<std::uint32_t>::max();
        check(!f.begin()); check(f.scope.record.reason & DepthOverflow);
        f.observer.abandon(DepthOverflow, f.scope);
        check(f.take().reason & Abandoned);
    }
    {
        Fixture f; check(f.begin());
        Observer other; other.armed = true;
        other.candidate(f.event(kImageSite), f.image, f.scope);
        check(f.scope.active && other.counters.unmatched == 1 && !other.counters.active);
        f.observer.abandon(None, f.scope);
        check(f.take().reason & ObserverMismatch);
    }
    {
        Fixture f; check(f.begin()); f.gpu();
        f.observer.armed = false;
        std::atomic<std::uint64_t> callbacks{0};
        check(!f.observer.quiescent(callbacks));
        f.write(kReplyBytes); f.finish(); check(f.take().import_reply_complete);
        check(f.observer.quiescent(callbacks));
        callbacks = 1; check(!f.observer.quiescent(callbacks));
        callbacks = 0; check(f.observer.quiescent(callbacks));
        check(!f.begin());
    }
    {
        Fixture f; check(f.begin()); f.gpu(); f.write(1);
        f.observer.armed = false;
        f.observer.abandon(IncompleteReply, f.scope);
        const auto r = f.take();
        check(!r.import_reply_complete && (r.reason & Abandoned));
        std::atomic<std::uint64_t> callbacks{0}; check(f.observer.quiescent(callbacks));
        f.observer.abandon(None, f.scope); check(f.observer.counters.completed == 1);
    }
    {
        Fixture f;
        for (unsigned i = 0; i < 32; ++i) { check(f.begin()); f.gpu(); f.write(21); f.finish(); }
        for (unsigned i = 0; i < 8; ++i) {
            check(!f.begin()); f.finish();
            check(!f.scope.active && !f.scope.excluded);
        }
        check(f.observer.counters.claimed == 32 && f.observer.counters.cap_skipped == 8);
        check(f.observer.counters.completed == 32 && f.observer.counters.complete == 32);
        std::set<std::uint64_t> observations;
        Record r{};
        while (f.observer.take(r)) { check(r.import_reply_complete); observations.insert(r.observation); }
        check(observations.size() == 32 && !f.observer.counters.queue_loss);
    }
    {
        Fixture f;
        f.observer.counters.claimed = std::numeric_limits<std::uint64_t>::max();
        check(!f.begin()); check(f.observer.counters.claimed == std::numeric_limits<std::uint64_t>::max());
        f.observer.abandon(None, f.scope); check(!f.scope.excluded);
    }
    {
        Observer o; Record r{};
        for (unsigned i = 0; i < 32; ++i) { r.observation = i + 1; check(o.publish(r)); }
        check(!o.publish(r) && o.counters.queue_loss == 1);
        std::set<std::uint64_t> observations;
        while (o.take(r)) observations.insert(r.observation);
        check(observations.size() == 32);
        check(o.publish(r) && o.take(r));
    }
    {
        Fixture f;
        Record filler{};
        for (unsigned i = 0; i < 32; ++i) check(f.observer.publish(filler));
        check(f.begin()); f.gpu(); f.write(21); f.finish();
        check(f.observer.counters.queue_loss == 1 && !f.observer.counters.complete);
        check(f.observer.counters.completed == 1 && f.observer.counters.rejected == 1);
    }
    // Interleaved scopes on different threads retain their native sequence and
    // image generation, despite equal numeric token values and a shared queue.
    {
        Observer o; o.armed = true;
        std::array<Record, 32> expected{};
        std::vector<std::thread> threads;
        for (unsigned producer = 0; producer < 4; ++producer) threads.emplace_back([&, producer] {
            for (unsigned j = 0; j < 8; ++j) {
                Fixture f;
                f.route.anchor_frame += producer * 0x100;
                f.route.request_sequence += producer * 8 + j;
                f.route.socket_generation += producer;
                f.image.image += producer * 8 + j;
                f.image.image_generation += j;
                f.binding.image = f.image.image;
                f.binding.image_generation = f.image.image_generation;
                f.image_view.image = f.image.image;
                f.image_view.image_generation = f.image.image_generation;
                for (unsigned byte = 0; byte < 4; ++byte)
                    f.packet[4 + byte] = static_cast<std::uint8_t>(f.route.request_sequence >> (8 * byte));
                Scope scope{};
                if (!o.begin({f.provisional_route(), kBeginSite, 0}, scope)) std::abort();
                o.early_unlock(f.event(kEarlyUnlockSite), scope);
                o.candidate(f.event(kImageSite), f.image, scope);
                o.bind(f.event(kBindSite), f.binding, scope);
                o.view(f.event(kViewSite), f.image_view, scope);
                const auto observation = scope.record.observation;
                WriteProof proof{};
                proof.event = f.event(kWriteSite);
                proof.parent_return = kReplyParent;
                proof.packet_identity = f.packet_identity + producer * 8 + j;
                proof.packet_base = proof.current_address = f.packet_base;
                proof.total_size = proof.remaining_size = kReplyBytes;
                proof.immutable_native_span_verified = proof.successful_prefix_owned = true;
                o.written(proof, f.packet, kReplyBytes, kReplyBytes, scope);
                expected[observation - 1] = scope.record;
                o.finish(f.finish_proof(), scope);
            }
        });
        for (auto& thread : threads) thread.join();
        Record r{};
        std::set<std::uint64_t> observations;
        while (o.take(r)) {
            check(r.import_reply_complete && r.token == kToken);
            check(r.route.request_sequence == expected[r.observation - 1].route.request_sequence);
            check(r.image.image_generation == expected[r.observation - 1].image.image_generation);
            check(r.image.image == expected[r.observation - 1].image.image);
            observations.insert(r.observation);
        }
        check(observations.size() == 32 && o.counters.claimed == 32 && o.counters.complete == 32);
        check(!o.counters.queue_loss && !o.counters.active);
    }
    std::printf("import observer: %u checks passed; offline scalar/reply evidence only\n", checks);
}
