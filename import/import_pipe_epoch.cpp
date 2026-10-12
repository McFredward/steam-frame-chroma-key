#include "import_pipe_epoch.h"

#include <limits>

#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
// Deterministic fixture-only pauses. This symbol/call does not exist in an
// ordinary core object, and never enters a deployed/native callback path.
extern "C" void frame_import_pipe_epoch_fixture(unsigned phase) noexcept;
#endif

namespace frame_import_pipe {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "64-bit atomics must be lock-free");
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free, "pointer labels must be lock-free");
static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<int>::is_always_lock_free &&
              std::atomic<bool>::is_always_lock_free, "slot payload atomics must be lock-free");
bool pointer_label(std::uintptr_t value) noexcept {
    return value >= 4096 && value <= ((std::uint64_t{1} << 48) - 49) && !(value & 15);
}
bool payload_site(Domain domain, std::uint64_t site) noexcept {
    return (domain == Domain::Compositor045 && site == 0x1540b0) ||
           (domain == Domain::Client045 && site == 0x3624c4);
}
}

Registry::Registry(std::uint64_t process_generation, std::uint64_t first_generation,
                   std::uint64_t first_revision, std::uint64_t first_publication_revision) noexcept
    : process_generation_(process_generation), next_generation_(first_generation), revision_(first_revision),
      publication_revision_(first_publication_revision) {
    if (!process_generation || !first_generation) lost_.store(true);
}
bool Registry::healthy() const noexcept { return !lost_.load(); }
void Registry::observer_loss() noexcept { lost_.store(true); }

Registry::Mutation::Mutation(Registry& owner) noexcept : registry(owner) {
    if (!registry.healthy()) return;
    auto writers = registry.active_writes_.load();
    // This is an activity census, not mutual exclusion: overlapping writers are
    // never queued or waited for. Exhausting bounded admission loses evidence.
    for (std::size_t attempt = 0; attempt < kCapacity; ++attempt) {
        if (writers >= kCapacity) break;
        if (registry.active_writes_.compare_exchange_strong(writers, writers + 1)) {
            counted = true; break;
        }
    }
    if (!counted) { registry.observer_loss(); return; }
    if (writers) registry.counters.overlaps.fetch_add(1);
    auto revision = registry.revision_.load();
    bool incremented = false;
    for (std::size_t attempt = 0; attempt < kCapacity; ++attempt) {
        if (revision == std::numeric_limits<std::uint64_t>::max()) break;
        if (registry.revision_.compare_exchange_strong(revision, revision + 1)) {
            incremented = true; break;
        }
    }
    if (!incremented) {
        registry.counters.exhausted.fetch_add(1);
        registry.observer_loss(); return;
    }
    admitted = registry.healthy();
}
Registry::Mutation::~Mutation() {
    if (counted) registry.active_writes_.fetch_sub(1);
}

bool Registry::publication_step() noexcept {
    auto revision = publication_revision_.load();
    for (std::size_t attempt = 0; attempt < kCapacity; ++attempt) {
        if (revision == std::numeric_limits<std::uint64_t>::max()) break;
        if (publication_revision_.compare_exchange_strong(revision, revision + 1)) return true;
    }
    counters.exhausted.fetch_add(1); observer_loss(); return false;
}
Registry::BirthPublication::BirthPublication(Registry& owner) noexcept : registry(owner) {
    if (!registry.healthy()) return;
    auto publishers = registry.publishing_births_.load();
    for (std::size_t attempt = 0; attempt < kCapacity; ++attempt) {
        if (publishers >= kCapacity) break;
        if (registry.publishing_births_.compare_exchange_strong(publishers, publishers + 1)) {
            counted = true; break;
        }
    }
    if (!counted) { registry.observer_loss(); return; }
    admitted = registry.publication_step() && registry.healthy();
}
void Registry::BirthPublication::finish() noexcept {
    if (!counted) return;
    // Complete the revision before activity disappears: an invalidator that
    // scanned before publication must detect even a fully completed window.
    (void)registry.publication_step();
    registry.publishing_births_.fetch_sub(1); counted = false;
}
Registry::BirthPublication::~BirthPublication() { finish(); }
void Registry::reject_unknown_during_publication(std::uint64_t prior_revision) noexcept {
    if (publishing_births_.load() || publication_revision_.load() != prior_revision) observer_loss();
}

std::uint64_t Registry::generation() noexcept {
    auto next = next_generation_.load();
    for (std::size_t attempt = 0; attempt < kCapacity; ++attempt) {
        if (!next || next == std::numeric_limits<std::uint64_t>::max()) break;
        if (next_generation_.compare_exchange_strong(next, next + 1)) return next;
    }
    counters.exhausted.fetch_add(1); observer_loss(); return 0;
}
bool Registry::owned(const ObjectToken& token) const noexcept {
    return token.valid() && token.registry_identity == reinterpret_cast<std::uintptr_t>(this) &&
           token.process_generation == process_generation_ && token.pipe && token.object_generation;
}
Registry::Slot* Registry::find(std::uintptr_t pipe) noexcept {
    for (auto& slot : slots_) if (slot.pipe.load() == pipe && slot.state.load() != Empty) return &slot;
    return nullptr;
}
const Registry::Slot* Registry::find(std::uintptr_t pipe) const noexcept {
    for (const auto& slot : slots_) if (slot.pipe.load() == pipe && slot.state.load() != Empty) return &slot;
    return nullptr;
}
bool Registry::matching(const Slot& slot, const ObjectToken& token) const noexcept {
    return slot.birth_valid.load() && slot.pipe.load() == token.pipe &&
           slot.object_generation.load() == token.object_generation &&
           slot.domain.load() == static_cast<unsigned>(token.domain);
}
bool Registry::claim(Slot& slot, unsigned& previous) noexcept {
    previous = slot.state.load();
    if (previous == Writing || !slot.state.compare_exchange_strong(previous, Writing)) {
        counters.overlaps.fetch_add(1); observer_loss(); return false;
    }
    return true;
}
bool Registry::fd_conflict(const Slot* ignore, int fd) const noexcept {
    if (fd < 0) return true;
    for (const auto& slot : slots_) {
        if (&slot == ignore) continue;
        const auto state = slot.state.load();
        if ((state == Accepted || state == Connected) && slot.fd.load() == fd) return true;
        if ((state == Pending || state == Reserved) && slot.pending_fd.load() == fd) return true;
    }
    return false;
}
bool Registry::census() noexcept {
    const auto revision = revision_.load();
    if (active_writes_.load() != 1) return healthy();
    bool conflict = false;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto a = slots_[i].state.load();
        if (a == Empty || a == Destroyed || a == Invalid) continue;
        if (a == Writing) continue;
        for (std::size_t j = i + 1; j < slots_.size(); ++j) {
            const auto b = slots_[j].state.load();
            if (b == Empty || b == Destroyed || b == Invalid) continue;
            if (b == Writing) continue;
            if (slots_[i].pipe.load() == slots_[j].pipe.load()) {
                conflict = true;
            }
            const auto fa = a == Pending || a == Reserved ? slots_[i].pending_fd.load() : slots_[i].fd.load();
            const auto fb = b == Pending || b == Reserved ? slots_[j].pending_fd.load() : slots_[j].fd.load();
            if (fa >= 0 && fa == fb) conflict = true;
        }
    }
    if (active_writes_.load() == 1 && revision_.load() == revision && conflict) observer_loss();
    return healthy();
}
bool Registry::boundary(const BoundaryProof& proof) const noexcept {
    if (!owned(proof.object) || !proof.pinned_schema_verified || !proof.active_owner_verified ||
        !proof.copied_under_native_mutex || !proof.native_lock_succeeded || proof.nested) return false;
    return payload_site(proof.object.domain, proof.entry_return) ||
        (proof.object.domain == Domain::Compositor045 && proof.entry_return == 0x153fc0) ||
        (proof.object.domain == Domain::Client045 && proof.entry_return == 0x3623c0);
}
ObjectToken Registry::object(const Slot& slot, Reason reason) const noexcept {
    return {reinterpret_cast<std::uintptr_t>(this), slot.pipe.load(), process_generation_,
            slot.object_generation.load(), static_cast<Domain>(slot.domain.load()), reason};
}
Lease Registry::lease(const Slot& slot, Reason reason) const noexcept {
    Lease out{};
    out.object = object(slot);
    out.socket_generation = slot.socket_generation.load();
    out.registry_revision = revision_.load();
    out.slot = static_cast<std::uint32_t>(&slot - slots_.data());
    out.fd = slot.fd.load();
    out.copied_native_peer_pid = slot.native_pid.load();
    out.peer_pid = slot.fresh_pid.load();
    out.fresh_native_peer_pid = out.peer_pid > 0;
    out.local_socket_lifetime_observed = out.socket_generation && out.fd >= 0;
    out.reason = reason;
    return out;
}
ObjectToken Registry::reject_object(Reason reason) noexcept {
    counters.rejected.fetch_add(1); ObjectToken out{}; out.reason = reason; return out;
}
Reservation Registry::reject_reservation(Reason reason) noexcept {
    counters.rejected.fetch_add(1); Reservation out{}; out.reason = reason; return out;
}
Lease Registry::reject_lease(Reason reason) noexcept {
    counters.rejected.fetch_add(1); Lease out{}; out.reason = reason; return out;
}
void Registry::invalidate(Slot& slot, unsigned next_state, bool birth_valid) noexcept {
    if (slot.reservation_pending.exchange(false)) {
        counters.pending_reservations.fetch_sub(1); counters.cancelled.fetch_add(1);
    }
    slot.socket_generation.store(0); slot.pending_fd.store(-1); slot.fd.store(-1);
    slot.native_pid.store(0); slot.fresh_pid.store(0); slot.birth_valid.store(birth_valid);
    slot.state.store(next_state);
}

Reservation Registry::reserve_birth(const BirthProof& proof) noexcept {
    const bool compositor = proof.domain == Domain::Compositor045;
    const bool client = proof.domain == Domain::Client045;
    const bool accepted = proof.kind == BirthKind::Accepted;
    if (proof.process_generation != process_generation_ || !pointer_label(proof.pipe) ||
        proof.mutex_argument != proof.pipe || proof.attributes_argument != proof.pipe + 48 ||
        !proof.pinned_schema_verified || !proof.active_constructor_verified ||
        !proof.constructor_initializes_peer_pid_zero || proof.nested || (!compositor && !client) ||
        proof.entry_return != (compositor ? 0x1722f4u : 0x244dbcu) ||
        (accepted && (proof.pending_accepted_fd < 0 || proof.constructor_parent_return !=
                      (compositor ? 0x172544u : 0x24500cu))) ||
        (!accepted && (proof.kind != BirthKind::GenericClient || !client ||
                       proof.pending_accepted_fd != -1 || !proof.constructor_parent_return)))
        return reject_reservation(Reason::InvalidProof);
    BirthPublication publication(*this);
    if (!publication.admitted) return reject_reservation(Reason::ObserverLoss);
    Mutation mutation(*this);
    if (!mutation.admitted) return reject_reservation(Reason::ObserverLoss);
    Slot* slot = find(proof.pipe);
    if (slot && slot->state.load() != Destroyed) {
        unsigned previous;
        if (claim(*slot, previous)) invalidate(*slot, Invalid, false);
        return reject_reservation(Reason::DuplicateBirth);
    }
    if (accepted && fd_conflict(slot, proof.pending_accepted_fd)) {
        observer_loss(); return reject_reservation(Reason::ConflictingFD);
    }
    bool already_claimed = false;
    if (!slot) for (auto& candidate : slots_) {
        const auto state = candidate.state.load();
        if (state != Empty && state != Destroyed) continue;
        auto expected = state;
        if (candidate.state.compare_exchange_strong(expected, Writing)) {
            slot = &candidate; already_claimed = true; break;
        }
    }
    if (!slot) {
        counters.capacity.fetch_add(1); observer_loss(); return reject_reservation(Reason::Capacity);
    }
    unsigned previous;
    if (!already_claimed && !claim(*slot, previous)) return reject_reservation(Reason::Busy);
    if (!already_claimed && previous != Destroyed) {
        invalidate(*slot, Invalid, false); observer_loss(); return reject_reservation(Reason::DuplicateBirth);
    }
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
    frame_import_pipe_epoch_fixture(1); // Writing, neither key has been published.
#endif
    const auto gen = generation();
    if (!gen) { invalidate(*slot, Invalid, false); return reject_reservation(Reason::GenerationExhausted); }
    slot->pipe.store(proof.pipe); slot->domain.store(static_cast<unsigned>(proof.domain));
    slot->kind.store(static_cast<unsigned>(proof.kind)); slot->object_generation.store(gen);
    slot->socket_generation.store(0); slot->pending_fd.store(proof.pending_accepted_fd);
    slot->fd.store(-1); slot->native_pid.store(0); slot->fresh_pid.store(0);
    slot->birth_valid.store(false); slot->zero_initialized.store(false);
    slot->reservation_pending.store(true); counters.reserved.fetch_add(1);
    counters.pending_reservations.fetch_add(1); slot->state.store(Reserved);
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
    frame_import_pipe_epoch_fixture(2); // Both keys are visible; invalidators can match.
#endif
    publication.finish();
    const Reservation reservation{reinterpret_cast<std::uintptr_t>(this), proof.pipe,
                                  process_generation_, gen, Reason::None};
    if (!census()) { cancel_birth(reservation); return reject_reservation(Reason::ObserverLoss); }
    return reservation;
}

ObjectToken Registry::created(const Reservation& reservation, int result) noexcept {
    if (!reservation.valid() || reservation.registry_identity != reinterpret_cast<std::uintptr_t>(this) ||
        reservation.process_generation != process_generation_ || !reservation.generation)
        return reject_object(Reason::InvalidProof);
    const auto* candidate = find(reservation.pipe);
    if (!candidate || candidate->object_generation.load() != reservation.generation || candidate->state.load() != Reserved)
        return reject_object(Reason::Invalidated);
    Mutation mutation(*this);
    if (!mutation.admitted) {
        cancel_birth(reservation); return reject_object(Reason::ObserverLoss);
    }
    auto* slot = find(reservation.pipe);
    if (!slot || slot->object_generation.load() != reservation.generation || slot->state.load() != Reserved)
        return reject_object(Reason::Invalidated);
    unsigned previous;
    if (!claim(*slot, previous)) return reject_object(Reason::Busy);
    if (previous != Reserved || slot->object_generation.load() != reservation.generation) {
        slot->state.store(previous); observer_loss(); return reject_object(Reason::Invalidated);
    }
    if (result) { invalidate(*slot, Invalid, false); return reject_object(Reason::NativeFailure); }
    if (!slot->reservation_pending.exchange(false)) {
        invalidate(*slot, Invalid, false); observer_loss(); return reject_object(Reason::Invalidated);
    }
    counters.pending_reservations.fetch_sub(1);
    slot->birth_valid.store(true); slot->zero_initialized.store(true);
    slot->state.store(slot->kind.load() == static_cast<unsigned>(BirthKind::Accepted) ? Pending : ObjectOnly);
    if (!census()) return reject_object(Reason::ObserverLoss);
    counters.births.fetch_add(1);
    return object(*slot);
}
void Registry::cancel_birth(const Reservation& reservation) noexcept {
    if (!reservation.valid() || reservation.registry_identity != reinterpret_cast<std::uintptr_t>(this) ||
        reservation.process_generation != process_generation_) return;
    auto* slot = find(reservation.pipe);
    if (!slot || slot->object_generation.load() != reservation.generation || slot->state.load() != Reserved) return;
    if (!healthy()) {
        // Loss forbids all leases/new births, but a matching completed callback
        // may still release its own reservation without resurrecting anything.
        unsigned expected = Reserved;
        if (slot->state.compare_exchange_strong(expected, Writing)) {
            if (slot->object_generation.load() == reservation.generation) invalidate(*slot, Invalid, false);
            else slot->state.store(Reserved);
        }
        return;
    }
    Mutation mutation(*this);
    if (!mutation.admitted) return;
    unsigned previous;
    if (claim(*slot, previous)) {
        if (previous == Reserved && slot->object_generation.load() == reservation.generation)
            invalidate(*slot, Invalid, false);
        else { slot->state.store(previous); observer_loss(); }
    }
}

Lease Registry::activate(const BoundaryProof& proof, int fd, int pid) noexcept {
    const auto* candidate = find(proof.object.pipe);
    if (!owned(proof.object) || !candidate || !matching(*candidate, proof.object))
        return reject_lease(Reason::StaleObject);
    if (!boundary(proof) || fd < 0 || pid < 0) {
        if (owned(proof.object)) observer_loss();
        return reject_lease(Reason::InvalidProof);
    }
    const auto current = lookup(proof.object);
    const bool fresh_payload = pid > 0 && payload_site(proof.object.domain, proof.entry_return) &&
                               proof.native_payload_read_succeeded;
    // The first observed import can follow other RPC payloads on this same
    // accepted connection. Begin retains their cached PID only diagnostically;
    // the current successful payload can establish it even if its value did
    // not change. A connected/reconnected pipe never gains that claim here.
    if (current.valid() && current.fd == fd && current.copied_native_peer_pid == pid &&
        (current.fresh_native_peer_pid || !pid ||
         !payload_site(proof.object.domain, proof.entry_return) || candidate->state.load() == Connected))
        return current;
    Mutation mutation(*this);
    if (!mutation.admitted) return reject_lease(Reason::ObserverLoss);
    auto* slot = find(proof.object.pipe);
    if (!slot || !matching(*slot, proof.object)) return reject_lease(Reason::StaleObject);
    unsigned previous;
    if (!claim(*slot, previous)) return reject_lease(Reason::Busy);
    if (!matching(*slot, proof.object)) {
        slot->state.store(previous); observer_loss(); return reject_lease(Reason::StaleObject);
    }
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
    frame_import_pipe_epoch_fixture(3); // Ordinary mutation, no birth publication.
#endif
    if ((previous != Pending && previous != Accepted && previous != Connected) ||
        (previous == Pending ? slot->pending_fd.load() : slot->fd.load()) != fd) {
        invalidate(*slot, Invalid, false); return reject_lease(Reason::ChangedNativeFields);
    }
    if (fd_conflict(slot, fd)) {
        invalidate(*slot, Invalid, false); observer_loss(); return reject_lease(Reason::ConflictingFD);
    }
    if (previous != Connected && pid > 0 && !slot->fresh_pid.load() &&
        payload_site(proof.object.domain, proof.entry_return) &&
        (!slot->zero_initialized.load() || !proof.native_payload_read_succeeded)) {
        invalidate(*slot, Invalid, false); return reject_lease(Reason::InvalidProof);
    }
    if (previous == Accepted && slot->fresh_pid.load() && pid != slot->fresh_pid.load()) {
        invalidate(*slot, Invalid, false); return reject_lease(Reason::ChangedNativeFields);
    }
    if (previous == Pending) {
        const auto gen = generation();
        if (!gen) { invalidate(*slot, Invalid, false); return reject_lease(Reason::GenerationExhausted); }
        slot->socket_generation.store(gen); slot->fd.store(fd); slot->pending_fd.store(-1);
        counters.activations.fetch_add(1);
    }
    slot->native_pid.store(pid);
    // Reconnect can retain an old positive native PID. Its scalar is retained
    // for diagnostics only; this narrow core never upgrades it to fresh facts.
    const auto established_pid = slot->fresh_pid.load();
    slot->fresh_pid.store(previous == Connected ? 0 :
        (established_pid > 0 ? established_pid : (fresh_payload ? pid : 0)));
    slot->state.store(previous == Connected ? Connected : Accepted);
    if (!census()) return reject_lease(Reason::ObserverLoss);
    return lease(*slot, active_writes_.load() == 1 ? Reason::None : Reason::Busy);
}

Lease Registry::connected(const ConnectProof& proof, int result) noexcept {
    const auto* candidate = find(proof.object.pipe);
    if (!owned(proof.object) || !candidate || !matching(*candidate, proof.object))
        return reject_lease(Reason::StaleObject);
    if (!owned(proof.object) || proof.object.domain != Domain::Client045 || proof.entry_return != 0x245f9c ||
        !proof.pinned_schema_verified || !proof.active_owner_verified || !proof.copied_under_native_mutex ||
        !proof.native_lock_succeeded || proof.nested || proof.argument_fd < 0 ||
        proof.argument_fd != proof.copied_native_fd || proof.copied_native_peer_pid < 0) {
        if (owned(proof.object)) observer_loss();
        return reject_lease(Reason::InvalidProof);
    }
    Mutation mutation(*this);
    if (!mutation.admitted) return reject_lease(Reason::ObserverLoss);
    auto* slot = find(proof.object.pipe);
    if (!slot || !matching(*slot, proof.object)) return reject_lease(Reason::StaleObject);
    unsigned previous;
    if (!claim(*slot, previous)) return reject_lease(Reason::Busy);
    if (!matching(*slot, proof.object)) {
        slot->state.store(previous); observer_loss(); return reject_lease(Reason::StaleObject);
    }
    if (result) { invalidate(*slot, Closed, true); return reject_lease(Reason::NativeFailure); }
    if ((previous != ObjectOnly && previous != Closed) || fd_conflict(slot, proof.argument_fd)) {
        invalidate(*slot, Invalid, false); observer_loss(); return reject_lease(Reason::ConflictingFD);
    }
    const auto gen = generation();
    if (!gen) { invalidate(*slot, Invalid, false); return reject_lease(Reason::GenerationExhausted); }
    slot->socket_generation.store(gen); slot->fd.store(proof.argument_fd); slot->pending_fd.store(-1);
    slot->native_pid.store(proof.copied_native_peer_pid); slot->fresh_pid.store(0);
    slot->state.store(Connected);
    if (!census()) return reject_lease(Reason::ObserverLoss);
    counters.connects.fetch_add(1);
    return lease(*slot, active_writes_.load() == 1 ? Reason::None : Reason::Busy);
}

void Registry::close_fd(int fd) noexcept {
    if (fd < 0) return;
    const auto publication_revision = publication_revision_.load();
    bool tracked = false;
    for (const auto& slot : slots_) {
        if (slot.fd.load() == fd || slot.pending_fd.load() == fd) { tracked = true; break; }
    }
    // Plane descriptors, log files and unrelated sockets are common on the
    // compositor. They must not invalidate a proven pipe or its current lease.
    if (!tracked) {
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
        frame_import_pipe_epoch_fixture(4); // Unknown scan completed, before publication recheck.
#endif
        reject_unknown_during_publication(publication_revision); return;
    }
    Mutation mutation(*this);
    if (!mutation.admitted) return;
    // Apply also at entry to shutdown, dup/dup2/dup3 and rebind-capable fcntl:
    // invalidate both known source and replacement target FDs conservatively.
    for (auto& slot : slots_) {
        if (slot.fd.load() != fd && slot.pending_fd.load() != fd) continue;
        unsigned previous;
        if (!claim(slot, previous)) return;
        if (slot.fd.load() != fd && slot.pending_fd.load() != fd) { slot.state.store(previous); continue; }
        invalidate(slot, slot.birth_valid.load() ? Closed : Invalid, slot.birth_valid.load());
    }
    counters.closes.fetch_add(1);
    (void)census();
}
void Registry::protected_retire(const BoundaryProof& proof) noexcept {
    const auto* candidate = find(proof.object.pipe);
    if (!owned(proof.object) || !candidate || !matching(*candidate, proof.object)) return;
    if (!boundary(proof)) { if (owned(proof.object)) observer_loss(); return; }
    Mutation mutation(*this);
    if (!mutation.admitted) return;
    auto* slot = find(proof.object.pipe);
    if (!slot || !matching(*slot, proof.object)) return;
    unsigned previous;
    if (claim(*slot, previous)) {
        if (matching(*slot, proof.object)) invalidate(*slot, Closed, true);
        else { slot->state.store(previous); observer_loss(); }
    }
    (void)census();
}
void Registry::destroy(std::uintptr_t pipe, int result) noexcept {
    const auto publication_revision = publication_revision_.load();
    if (!find(pipe)) {
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
        frame_import_pipe_epoch_fixture(5); // Unknown scan completed, before publication recheck.
#endif
        reject_unknown_during_publication(publication_revision);
        return; // A quiescent unrelated mutex is not our lifecycle.
    }
    Mutation mutation(*this);
    if (!mutation.admitted) return;
    auto* slot = find(pipe);
    if (!slot) return;
    unsigned previous;
    if (!claim(*slot, previous)) return;
    invalidate(*slot, result == 0 ? Destroyed : Invalid, false);
    counters.destroyed.fetch_add(1);
    if (result) observer_loss();
    (void)census();
}

Lease Registry::lookup(const ObjectToken& token) const noexcept {
    Lease denied{};
    if (!owned(token)) { denied.reason = Reason::StaleObject; return denied; }
    const auto revision = revision_.load();
    if (!healthy()) { denied.reason = Reason::ObserverLoss; return denied; }
    if (active_writes_.load()) { denied.reason = Reason::Busy; return denied; }
    const auto* slot = find(token.pipe);
    if (!slot || !matching(*slot, token)) { denied.reason = Reason::StaleObject; return denied; }
    const auto state = slot->state.load();
    if (state != Accepted && state != Connected) {
        denied.reason = state == Pending || state == ObjectOnly ? Reason::PendingAssignment : Reason::Invalidated;
        return denied;
    }
    auto out = lease(*slot);
    bool conflict = false;
    for (const auto& other : slots_) {
        if (&other == slot) continue;
        const auto other_state = other.state.load();
        if (other_state == Empty || other_state == Destroyed || other_state == Invalid || other_state == Writing)
            continue;
        if (other.pipe.load() == token.pipe) conflict = true;
        const auto other_fd = other_state == Pending || other_state == Reserved ? other.pending_fd.load() : other.fd.load();
        if (other_fd == out.fd) conflict = true;
    }
    if (!out.socket_generation || out.fd < 0 || !healthy() || active_writes_.load() ||
        revision_.load() != revision || out.registry_revision != revision ||
        slot->state.load() != state || !matching(*slot, token)) {
        denied.reason = Reason::Busy; return denied;
    }
    if (conflict) { lost_.store(true); denied.reason = Reason::ConflictingFD; return denied; }
    return out;
}
bool Registry::check_lease(const Lease& candidate) const noexcept {
    if (!candidate.valid() || candidate.slot >= kCapacity || !candidate.local_socket_lifetime_observed ||
        candidate.peer_uid_gid_observed || candidate.kernel_endpoint_pair_verified ||
        candidate.native_cross_process_namespace || candidate.source_frame_associated ||
        candidate.accepted_submit || candidate.gpu_consumed_coherent) return false;
    const auto current = lookup(candidate.object);
    return current.valid() && current.slot == candidate.slot &&
        current.registry_revision == candidate.registry_revision &&
        current.socket_generation == candidate.socket_generation && current.fd == candidate.fd &&
        current.copied_native_peer_pid == candidate.copied_native_peer_pid &&
        current.peer_pid == candidate.peer_pid && current.fresh_native_peer_pid == candidate.fresh_native_peer_pid;
}

} // namespace frame_import_pipe
