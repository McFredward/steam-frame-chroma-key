#include "import/import_pipe_epoch.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>

using namespace frame_import_pipe;
namespace {
unsigned checks = 0;
thread_local bool forbid_allocation = false;
std::atomic<unsigned> unexpected_allocations{0};
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
std::atomic<unsigned> requested_pause{0};
std::atomic<bool> publication_paused{false}, release_publication{false};
void wait_for_pause() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!publication_paused.load()) {
        if (std::chrono::steady_clock::now() >= deadline) std::abort();
        std::this_thread::yield();
    }
}
#endif
void check_at(bool condition, unsigned line) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "pipe epoch check %u failed at line %u\n", checks, line); std::abort(); }
}

#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
extern "C" void frame_import_pipe_epoch_fixture(unsigned phase) noexcept {
    if (requested_pause.load() != phase) return;
    publication_paused.store(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!release_publication.load()) {
        if (std::chrono::steady_clock::now() >= deadline) std::abort();
        std::this_thread::yield(); // Deterministic fixture only, absent from ordinary core.
    }
}
#endif
#define check(condition) check_at((condition), __LINE__)
struct NoAllocation {
    NoAllocation() { forbid_allocation = true; }
    ~NoAllocation() { forbid_allocation = false; }
};

BirthProof accepted(std::uintptr_t pipe = 0x10000, int fd = 19, Domain domain = Domain::Compositor045) {
    BirthProof proof{};
    proof.process_generation = 500;
    proof.pipe = proof.mutex_argument = pipe;
    proof.attributes_argument = pipe + 48;
    proof.domain = domain; proof.kind = BirthKind::Accepted; proof.pending_accepted_fd = fd;
    proof.entry_return = domain == Domain::Compositor045 ? 0x1722f4 : 0x244dbc;
    proof.constructor_parent_return = domain == Domain::Compositor045 ? 0x172544 : 0x24500c;
    proof.pinned_schema_verified = proof.active_constructor_verified = true;
    proof.constructor_initializes_peer_pid_zero = true;
    return proof;
}
BirthProof generic(std::uintptr_t pipe = 0x10000) {
    auto proof = accepted(pipe, -1, Domain::Client045);
    proof.kind = BirthKind::GenericClient;
    // An independently reviewed constructor caller context; never an accepted
    // FD claim and never guessed from an inherited native mutex/FD snapshot.
    proof.constructor_parent_return = 0x1234;
    return proof;
}
ObjectToken born(Registry& registry, const BirthProof& proof) {
    const auto reservation = registry.reserve_birth(proof);
    check(reservation.valid());
    const auto object = registry.created(reservation, 0);
    check(object.valid());
    return object;
}
BoundaryProof boundary(ObjectToken object, bool payload = false) {
    BoundaryProof proof{};
    proof.object = object;
    proof.entry_return = object.domain == Domain::Compositor045 ?
        (payload ? 0x1540b0 : 0x153fc0) : (payload ? 0x3624c4 : 0x3623c0);
    proof.pinned_schema_verified = proof.active_owner_verified = true;
    proof.copied_under_native_mutex = proof.native_lock_succeeded = true;
    proof.native_payload_read_succeeded = payload;
    return proof;
}
ConnectProof connect(ObjectToken object, int fd, int pid = 0) {
    ConnectProof proof{};
    proof.object = object; proof.entry_return = 0x245f9c;
    proof.argument_fd = proof.copied_native_fd = fd;
    proof.copied_native_peer_pid = pid;
    proof.pinned_schema_verified = proof.active_owner_verified = true;
    proof.copied_under_native_mutex = proof.native_lock_succeeded = true;
    return proof;
}
void denials(const Lease& lease) {
    check(!lease.peer_uid_gid_observed && !lease.kernel_endpoint_pair_verified);
    check(!lease.native_cross_process_namespace && !lease.source_frame_associated);
    check(!lease.accepted_submit && !lease.gpu_consumed_coherent);
}
}

void* operator new(std::size_t size) {
    if (forbid_allocation) unexpected_allocations.fetch_add(1);
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
#ifdef FRAME_IMPORT_PIPE_EPOCH_TEST_FIXTURE
    for (unsigned phase : {1u, 2u}) for (bool destroying : {false, true}) {
        Registry registry(500);
        requested_pause = phase; publication_paused = false; release_publication = false;
        Reservation reservation{};
        std::thread writer([&] { reservation = registry.reserve_birth(accepted()); });
        wait_for_pause();
        if (destroying) registry.destroy(0x10000, 0);
        else registry.close_fd(19);
        if (phase == 1) check(!registry.healthy()); // Neither key existed: proof loss required.
        else check(registry.healthy()); // Known key is explicitly retired without global loss.
        release_publication = true; writer.join(); requested_pause = 0;
        const auto object = registry.created(reservation, 0);
        check(!object.valid());
        check(!registry.activate(boundary(object), 19, 0).valid());
        check(registry.counters.pending_reservations == 0 && registry.counters.activations == 0);
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted());
        const auto first = registry.activate(boundary(object), 19, 0);
        check(first.valid());
        requested_pause = 3; publication_paused = false; release_publication = false;
        Lease refreshed{};
        std::thread writer([&] { refreshed = registry.activate(boundary(object, true), 19, 123); });
        wait_for_pause();
        registry.close_fd(900); registry.destroy(0x99000, 0);
        check(registry.healthy()); // Ordinary row Writing does not imply unknown birth ownership.
        release_publication = true; writer.join(); requested_pause = 0;
        check(refreshed.valid() && registry.check_lease(refreshed));
    }
    for (bool destroying : {false, true}) {
        Registry registry(500);
        requested_pause = destroying ? 5 : 4;
        publication_paused = false; release_publication = false;
        std::thread invalidator([&] {
            if (destroying) registry.destroy(0x10000, 0);
            else registry.close_fd(19);
        });
        wait_for_pause();
        const auto object = born(registry, accepted());
        const auto current = registry.activate(boundary(object), 19, 0);
        check(current.valid());
        release_publication = true; invalidator.join(); requested_pause = 0;
        // Activity has already returned to zero. Revision must still expose
        // the complete publication that occurred during the earlier scan.
        check(!registry.healthy() && !registry.check_lease(current));
    }
#endif
    {
        Registry registry(500);
        const auto proof = accepted();
        const auto reservation = registry.reserve_birth(proof);
        check(reservation.valid() && registry.counters.pending_reservations == 1);
        ObjectToken forged{reservation.registry_identity, reservation.pipe, 500, reservation.generation,
                           Domain::Compositor045, Reason::None};
        check(!registry.lookup(forged).valid()); // Reservation has no native birth/connection.
        const auto object = registry.created(reservation, 0);
        check(object.valid() && registry.counters.pending_reservations == 0 && registry.counters.births == 1);
        check(registry.lookup(object).reason == Reason::PendingAssignment);
        auto provisional = registry.activate(boundary(object), 19, 0);
        check(provisional.valid() && registry.check_lease(provisional));
        registry.cancel_birth(reservation); // A completed handle cannot cancel a committed birth.
        check(registry.check_lease(provisional));
        check(provisional.local_socket_lifetime_observed && !provisional.fresh_native_peer_pid);
        check(!provisional.peer_pid && !provisional.copied_native_peer_pid);
        denials(provisional);
        auto fresh = registry.activate(boundary(object, true), 19, 7123);
        check(fresh.valid() && registry.check_lease(fresh) && !registry.check_lease(provisional));
        check(fresh.socket_generation == provisional.socket_generation && fresh.object.object_generation == object.object_generation);
        check(fresh.fresh_native_peer_pid && fresh.peer_pid == 7123 && fresh.copied_native_peer_pid == 7123);
        denials(fresh);
        const auto counters = registry.counters.activations.load();
        for (unsigned i = 0; i < 40; ++i) {
            const auto repeat = registry.activate(boundary(object, true), 19, 7123);
            const auto snapshot = registry.lookup(object);
            check(repeat.valid() && repeat.registry_revision == fresh.registry_revision);
            check(snapshot.socket_generation == fresh.socket_generation && registry.check_lease(fresh));
        }
        check(registry.counters.activations == counters && counters == 1);
        registry.close_fd(900); // Unrelated file/DMA-BUF descriptor.
        registry.destroy(0x99000, 0); // Unrelated native mutex.
        check(registry.check_lease(fresh));
        auto forged_lease = fresh;
        forged_lease.kernel_endpoint_pair_verified = true;
        check(!registry.check_lease(forged_lease));
        forged_lease = fresh; ++forged_lease.socket_generation;
        check(!registry.check_lease(forged_lease));
        registry.close_fd(19);
        check(registry.healthy() && !registry.check_lease(fresh) && !registry.lookup(object).valid());
        check(!registry.activate(boundary(object), 19, 0).valid()); // A later snapshot cannot invent a reconnect.
    }
    {
        Registry registry(500);
        auto reservation = registry.reserve_birth(accepted());
        check(reservation.valid());
        registry.close_fd(19); // Between entry reservation and original init return.
        check(registry.counters.pending_reservations == 0 && registry.counters.cancelled == 1);
        check(!registry.created(reservation, 0).valid());
        const auto replacement = born(registry, accepted(0x20000, 19));
        const auto active = registry.activate(boundary(replacement, true), 19, 99);
        check(active.valid() && registry.check_lease(active));
        check(!registry.created(reservation, 0).valid()); // FD reuse cannot revive the previous birth.
        check(registry.lookup(replacement).valid());
    }
    for (bool failed : {false, true}) {
        Registry registry(500);
        const auto reservation = registry.reserve_birth(accepted());
        check(reservation.valid());
        if (failed) check(!registry.created(reservation, 5).valid());
        else registry.cancel_birth(reservation);
        check(registry.counters.pending_reservations == 0 && registry.counters.cancelled == 1);
        check(registry.counters.births == 0 && registry.counters.activations == 0);
        check(!registry.created(reservation, 0).valid());
        check(!registry.reserve_birth(accepted()).valid());
        registry.destroy(0x10000, 0);
        const auto object = born(registry, accepted());
        check(object.object_generation != reservation.generation);
    }
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
        Registry registry(500);
        auto proof = accepted();
        switch (mutation) {
        case 0: proof.process_generation = 0; break;
        case 1: proof.pipe = 0; break;
        case 2: proof.mutex_argument += 16; break;
        case 3: proof.attributes_argument += 16; break;
        case 4: proof.entry_return = 0x187f30; break; // Tail helper PC is not actual entry LR.
        case 5: proof.constructor_parent_return = 0x172528; break; // Accept return, not constructor caller.
        case 6: proof.domain = Domain::Unknown; break;
        case 7: proof.kind = BirthKind::GenericClient; break;
        case 8: proof.pending_accepted_fd = -1; break;
        case 9: proof.pinned_schema_verified = false; break;
        case 10: proof.active_constructor_verified = false; break;
        case 11: proof.constructor_initializes_peer_pid_zero = false; break;
        case 12: proof.nested = true; break;
        }
        check(!registry.reserve_birth(proof).valid() && registry.counters.reserved == 0);
    }
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
        Registry registry(500);
        const auto object = born(registry, accepted());
        auto proof = boundary(object, true);
        switch (mutation) {
        case 0: proof.entry_return = 0x153fbc; break;
        case 1: proof.pinned_schema_verified = false; break;
        case 2: proof.active_owner_verified = false; break;
        case 3: proof.copied_under_native_mutex = false; break;
        case 4: proof.native_lock_succeeded = false; break;
        case 5: proof.nested = true; break;
        case 6: proof.native_payload_read_succeeded = false; break;
        }
        check(!registry.activate(proof, 19, 99).valid());
        check(registry.counters.activations == 0 && !registry.lookup(object).valid());
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted(0x10000, 0));
        const auto fresh = registry.activate(boundary(object, true), 0, 77);
        check(fresh.valid() && fresh.fd == 0 && registry.check_lease(fresh));
        check(!registry.activate(boundary(object, true), 0, 78).valid());
        check(!registry.check_lease(fresh));
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted());
        check(!registry.activate(boundary(object), 20, 0).valid());
        check(!registry.lookup(object).valid() && registry.counters.activations == 0);
    }
    {
        Registry registry(500);
        const auto object = born(registry, generic());
        check(registry.lookup(object).reason == Reason::PendingAssignment);
        check(!registry.connected(connect(object, 25, 700), 1).valid());
        auto first = registry.connected(connect(object, 26, 700), 0);
        check(first.valid() && registry.check_lease(first) && !first.fresh_native_peer_pid && !first.peer_pid);
        check(first.copied_native_peer_pid == 700);
        auto changed_pid = registry.activate(boundary(object, true), 26, 701);
        check(changed_pid.valid() && !changed_pid.fresh_native_peer_pid && !changed_pid.peer_pid);
        denials(changed_pid);
        registry.close_fd(26);
        check(!registry.check_lease(first) && !registry.check_lease(changed_pid));
        auto second = registry.connected(connect(object, 26, 701), 0);
        check(second.valid() && registry.check_lease(second));
        check(second.socket_generation != first.socket_generation && second.object.object_generation == first.object.object_generation);
        check(second.copied_native_peer_pid == 701 && !second.fresh_native_peer_pid && !second.peer_pid);
        check(!registry.connected(connect(object, 26, 701), 0).valid()); // No observed close/new connection.
        check(!registry.check_lease(second));
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted(0x10000, 19, Domain::Client045));
        const auto first = registry.activate(boundary(object, true), 19, 7123);
        check(first.valid() && first.fresh_native_peer_pid);
        registry.close_fd(19);
        const auto rebound = registry.connected(connect(object, 19, 7123), 0);
        check(rebound.valid() && rebound.socket_generation != first.socket_generation);
        check(!rebound.fresh_native_peer_pid && !rebound.peer_pid && rebound.copied_native_peer_pid == 7123);
        check(registry.activate(boundary(object, true), 19, 7123).valid());
        check(!registry.lookup(object).fresh_native_peer_pid);
    }
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
        Registry registry(500);
        const auto object = born(registry, generic());
        auto proof = connect(object, 26);
        switch (mutation) {
        case 0: proof.entry_return = 0x245f20; break;
        case 1: proof.argument_fd = 27; break;
        case 2: proof.pinned_schema_verified = false; break;
        case 3: proof.active_owner_verified = false; break;
        case 4: proof.copied_under_native_mutex = false; break;
        case 5: proof.native_lock_succeeded = false; break;
        case 6: proof.nested = true; break;
        }
        check(!registry.connected(proof, 0).valid() && registry.counters.connects == 0);
    }
    {
        Registry registry(500);
        ObjectToken inherited{reinterpret_cast<std::uintptr_t>(&registry), 0x80000, 500, 1,
                              Domain::Client045, Reason::None};
        check(!registry.lookup(inherited).valid());
        check(!registry.connected(connect(inherited, 50, 1234), 0).valid());
        check(!registry.activate(boundary(inherited, true), 50, 1234).valid());
        check(registry.counters.activations == 0 && registry.counters.connects == 0);
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted());
        const auto old = registry.activate(boundary(object), 19, 0);
        check(old.valid());
        check(!registry.reserve_birth(accepted()).valid()); // Pointer reuse without destruction.
        check(!registry.check_lease(old));
        registry.destroy(0x10000, 0);
        const auto replacement = born(registry, accepted());
        const auto current = registry.activate(boundary(replacement), 19, 0);
        check(current.valid() && current.object.object_generation != old.object.object_generation);
        check(current.socket_generation != old.socket_generation && !registry.lookup(object).valid());
        registry.protected_retire(boundary(replacement));
        check(!registry.check_lease(current));
    }
    {
        Registry registry(500);
        const auto reservation = registry.reserve_birth(accepted());
        const auto original = registry.created(reservation, 0);
        const auto first = registry.activate(boundary(original), 19, 0);
        check(first.valid());
        registry.destroy(0x10000, 0);
        const auto replacement = born(registry, accepted(0x20000, 19));
        const auto current = registry.activate(boundary(replacement), 19, 0);
        check(current.valid() && current.slot == first.slot); // A destroyed slot can be recycled.
        check(!registry.lookup(original).valid() && !registry.created(reservation, 0).valid());
        check(registry.check_lease(current));
    }
    for (unsigned operation = 0; operation < 3; ++operation) {
        Registry registry(500);
        const auto a = born(registry, accepted(0x10000, 19));
        const auto b = born(registry, accepted(0x20000, 20));
        const auto first = registry.activate(boundary(a), 19, 0);
        const auto second = registry.activate(boundary(b), 20, 0);
        check(first.valid() && second.valid());
        // Adapter contract for dup2/dup3/fcntl rebinding: both original and
        // replaced descriptor lifetimes become unavailable before forwarding.
        registry.close_fd(19);
        registry.close_fd(20);
        check(!registry.lookup(a).valid() && !registry.lookup(b).valid());
        check(!registry.check_lease(first) && !registry.check_lease(second));
        check(!registry.activate(boundary(a), 19, 0).valid());
    }
    for (bool reserved : {false, true}) {
        Registry registry(500);
        Lease current{};
        if (reserved) check(registry.reserve_birth(accepted()).valid());
        else {
            const auto object = born(registry, accepted());
            current = registry.activate(boundary(object), 19, 0);
            check(current.valid());
        }
        check(!registry.reserve_birth(accepted(0x20000, 19)).valid());
        check(!registry.healthy() && !registry.check_lease(current));
    }
    {
        Registry registry(500);
        const auto a = born(registry, generic(0x10000));
        const auto b = born(registry, generic(0x20000));
        const auto current = registry.connected(connect(a, 20), 0);
        check(current.valid());
        check(!registry.connected(connect(b, 20), 0).valid());
        check(!registry.healthy() && !registry.check_lease(current));
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted());
        const auto current = registry.activate(boundary(object), 19, 0);
        check(current.valid());
        registry.destroy(0x10000, 16);
        check(!registry.healthy() && !registry.check_lease(current));
        check(!registry.reserve_birth(accepted()).valid());
    }
    {
        Registry registry(500);
        ObjectToken first{};
        for (std::size_t i = 0; i < kCapacity; ++i) {
            const auto current = born(registry, accepted(0x10000 + i * 0x100, static_cast<int>(100 + i)));
            if (!i) first = current;
        }
        const auto current = registry.activate(boundary(first), 100, 0);
        check(current.valid());
        check(!registry.reserve_birth(accepted(0x90000, 900)).valid());
        check(registry.counters.capacity == 1 && !registry.check_lease(current));
    }
    {
        Registry registry(500);
        auto object = born(registry, accepted());
        for (unsigned i = 0; i < 100; ++i) {
            const auto current = registry.activate(boundary(object), 19, 0);
            check(current.valid());
            registry.destroy(0x10000, 0);
            auto next = born(registry, accepted());
            check(next.object_generation > object.object_generation);
            check(!registry.check_lease(current));
            object = next;
        }
        check(registry.healthy());
    }
    for (unsigned overflow = 0; overflow < 3; ++overflow) {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        Registry registry(500, overflow == 0 ? maximum : (overflow == 1 ? maximum - 1 : 1),
                          overflow == 2 ? maximum : 0);
        const auto reservation = registry.reserve_birth(accepted());
        if (overflow == 1) {
            check(reservation.valid());
            const auto object = registry.created(reservation, 0);
            check(object.valid());
            check(!registry.activate(boundary(object), 19, 0).valid());
        } else check(!reservation.valid());
        check(!registry.healthy() && registry.counters.exhausted > 0);
    }
    for (const auto seed : {std::numeric_limits<std::uint64_t>::max(),
                            std::numeric_limits<std::uint64_t>::max() - 1}) {
        Registry registry(500, 1, 0, seed);
        check(!registry.reserve_birth(accepted()).valid());
        check(!registry.healthy() && registry.counters.exhausted > 0);
    }
    {
        Registry registry(0);
        check(!registry.healthy());
        check(!registry.reserve_birth(accepted()).valid());
    }
    {
        Registry registry(500);
        const auto object = born(registry, accepted());
        const auto current = registry.activate(boundary(object), 19, 0);
        check(current.valid());
        Registry foreign(500);
        check(!foreign.lookup(object).valid() && !foreign.check_lease(current));
        registry.observer_loss();
        check(!registry.lookup(object).valid() && !registry.check_lease(current));
    }
    {
        Registry registry(500);
        const auto reservation = registry.reserve_birth(accepted());
        check(reservation.valid() && registry.counters.pending_reservations == 1);
        registry.observer_loss();
        check(!registry.created(reservation, 0).valid());
        check(registry.counters.pending_reservations == 0 && registry.counters.cancelled == 1);
        check(registry.counters.births == 0 && registry.counters.activations == 0);
    }
    {
        Registry registry(500);
        NoAllocation no_allocation;
        const auto reservation = registry.reserve_birth(accepted());
        const auto object = registry.created(reservation, 0);
        auto current = registry.activate(boundary(object), 19, 0);
        check(current.valid());
        current = registry.activate(boundary(object, true), 19, 5000);
        check(current.valid() && registry.check_lease(current));
        registry.close_fd(19);
        registry.destroy(0x10000, 0);
    }

    // Actual concurrent calls, not a prefilled table/mocked lookup: three
    // constructors/activators own disjoint rows; a fourth thread repeatedly
    // snapshots all known objects. Busy/lost outcomes never grant a lease.
    for (unsigned round = 0; round < 64; ++round) {
        Registry registry(500);
        std::atomic<unsigned> ready{0};
        std::atomic<bool> start{false}, done{false}, bad{false};
        std::array<ObjectToken, 3> tokens{};
        std::array<std::atomic<bool>, 3> published{};
        std::array<std::thread, 3> writers;
        for (unsigned thread = 0; thread < 3; ++thread) writers[thread] = std::thread([&, thread] {
            ready.fetch_add(1);
            while (!start.load()) std::this_thread::yield(); // Fixture only; not in callbacks.
            NoAllocation no_allocation;
            const auto pipe = 0x10000 + thread * 0x100;
            const auto fd = 20 + static_cast<int>(thread);
            const auto reservation = registry.reserve_birth(accepted(pipe, fd));
            if (!reservation.valid()) return;
            const auto object = registry.created(reservation, 0);
            if (!object.valid()) return;
            tokens[thread] = object; published[thread].store(true);
            const auto first = registry.activate(boundary(object), fd, 0);
            if (first.valid() && first.fd != fd) bad.store(true);
            registry.close_fd(fd);
            if (registry.check_lease(first)) bad.store(true);
            if (registry.lookup(object).valid()) bad.store(true);
        });
        std::thread reader([&] {
            ready.fetch_add(1);
            while (!start.load()) std::this_thread::yield();
            NoAllocation no_allocation;
            while (!done.load()) for (unsigned i = 0; i < 3; ++i) {
                if (!published[i].load()) continue;
                const auto current = registry.lookup(tokens[i]);
                if (current.valid() && (current.fd != 20 + static_cast<int>(i) ||
                    current.object.pipe != 0x10000 + i * 0x100 || !current.socket_generation)) bad.store(true);
            }
        });
        while (ready.load() != 4) std::this_thread::yield();
        start.store(true);
        for (auto& writer : writers) writer.join();
        done.store(true); reader.join();
        check(!bad.load());
        check(registry.healthy()); // Different-row overlap is not observer loss.
        for (unsigned i = 0; i < 3; ++i) if (published[i].load()) check(!registry.lookup(tokens[i]).valid());
    }
    // Same accepted FD across concurrently born objects is a real conflict,
    // unlike different-row overlap. No final stable lookup may accept it.
    for (unsigned round = 0; round < 32; ++round) {
        Registry registry(500);
        std::atomic<unsigned> ready{0}; std::atomic<bool> start{false};
        std::array<ObjectToken, 3> objects{};
        std::array<std::thread, 3> writers;
        for (unsigned i = 0; i < 3; ++i) writers[i] = std::thread([&, i] {
            ready.fetch_add(1); while (!start.load()) std::this_thread::yield();
            const auto reservation = registry.reserve_birth(accepted(0x10000 + i * 0x100, 55));
            if (reservation.valid()) objects[i] = registry.created(reservation, 0);
        });
        while (ready.load() != 3) std::this_thread::yield();
        start.store(true);
        for (auto& writer : writers) writer.join();
        unsigned valid = 0;
        for (const auto& object : objects) if (object.valid()) {
            const auto current = registry.activate(boundary(object), 55, 0);
            valid += current.valid() && registry.check_lease(current) ? 1u : 0u;
        }
        check(valid <= 1);
        check(!registry.healthy());
        if (registry.healthy()) for (const auto& object : objects)
            if (object.valid()) check(!registry.lookup(object).valid());
    }
    check(unexpected_allocations == 0);
    std::printf("pipe epoch core: %u focused checks passed; scalar/model evidence only, no runtime kernel/native association\n", checks);
}
