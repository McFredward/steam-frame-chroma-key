#pragma once

// Offline scalar-only lifecycle core for the source-reviewed SteamOS 0.4.5
// pipes. This neither reads native memory nor authorizes a callback adapter.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace frame_import_pipe {

constexpr std::size_t kCapacity = 32;
enum class Domain : unsigned { Unknown, Compositor045, Client045 };
enum class BirthKind : unsigned { Unknown, Accepted, GenericClient };
enum class Reason : unsigned {
    None, InvalidProof, NativeFailure, UnknownBirth, PendingAssignment,
    StaleObject, Invalidated, DuplicateBirth, ConflictingFD, Capacity,
    GenerationExhausted, Busy, ObserverLoss, ChangedNativeFields
};

// Every *_verified flag is an independently established adapter obligation,
// not a permission, source-frame identity or shortcut for arbitrary heap reads.
struct BirthProof {
    std::uint64_t process_generation = 0;
    std::uintptr_t pipe = 0, mutex_argument = 0, attributes_argument = 0;
    std::uint64_t entry_return = 0, constructor_parent_return = 0;
    Domain domain = Domain::Unknown;
    BirthKind kind = BirthKind::Unknown;
    int pending_accepted_fd = -1;
    bool pinned_schema_verified = false, active_constructor_verified = false;
    bool constructor_initializes_peer_pid_zero = false, nested = false;
};

struct ObjectToken {
    std::uintptr_t registry_identity = 0, pipe = 0;
    std::uint64_t process_generation = 0, object_generation = 0;
    Domain domain = Domain::Unknown;
    Reason reason = Reason::UnknownBirth;
    bool valid() const noexcept { return reason == Reason::None; }
};

// Reserve BEFORE forwarding mutex_init. The accepted parent's exclusive FD/
// object ownership until reservation begins remains a native-source obligation;
// this is not generally observed coverage since accept return. Publication and
// the forwarding interval are guarded here. A reservation is not object birth.
struct Reservation {
    std::uintptr_t registry_identity = 0, pipe = 0;
    std::uint64_t process_generation = 0, generation = 0;
    Reason reason = Reason::UnknownBirth;
    bool valid() const noexcept { return reason == Reason::None; }
};

struct BoundaryProof {
    ObjectToken object{};
    std::uint64_t entry_return = 0;
    bool pinned_schema_verified = false, active_owner_verified = false;
    bool copied_under_native_mutex = false, native_lock_succeeded = false;
    bool native_payload_read_succeeded = false, nested = false;
};

struct ConnectProof {
    ObjectToken object{};
    std::uint64_t entry_return = 0;
    int argument_fd = -1, copied_native_fd = -1, copied_native_peer_pid = -1;
    bool pinned_schema_verified = false, active_owner_verified = false;
    bool copied_under_native_mutex = false, native_lock_succeeded = false;
    bool nested = false;
};

// A snapshot is not a lifetime pin. Call check_lease() again at use/publication.
// Tracked lifecycle revision changes conservatively invalidate older leases,
// including other rows. Untracked FD closes are ignored. Lookup never creates
// object/socket generations. Distinct-row writers can overlap without waiting;
// readers refuse the unstable window, and actual conflicts lose the evidence.
struct Lease {
    ObjectToken object{};
    std::uint64_t socket_generation = 0, registry_revision = 0;
    std::uint32_t slot = 0;
    int fd = -1, copied_native_peer_pid = 0, peer_pid = 0;
    bool fresh_native_peer_pid = false;
    bool local_socket_lifetime_observed = false;
    // A PID-only native copy does not supply UID/GID or a kernel endpoint pair.
    bool peer_uid_gid_observed = false, kernel_endpoint_pair_verified = false;
    bool native_cross_process_namespace = false, source_frame_associated = false;
    bool accepted_submit = false, gpu_consumed_coherent = false;
    Reason reason = Reason::UnknownBirth;
    bool valid() const noexcept { return reason == Reason::None; }
};

struct Counters {
    std::atomic<std::uint64_t> reserved{0}, pending_reservations{0}, cancelled{0};
    std::atomic<std::uint64_t> births{0}, activations{0}, connects{0};
    std::atomic<std::uint64_t> closes{0}, destroyed{0}, rejected{0};
    std::atomic<std::uint64_t> overlaps{0}, unstable{0}, capacity{0}, exhausted{0};
};

class Registry {
public:
    // Seeds permit focused overflow fixtures. Production starts with generation
    // 1 and revision 0; labels denote observed lifecycle events, never video.
    explicit Registry(std::uint64_t process_generation,
                      std::uint64_t first_generation = 1,
                      std::uint64_t first_revision = 0,
                      std::uint64_t first_publication_revision = 0) noexcept;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Counters counters{};
    Reservation reserve_birth(const BirthProof&) noexcept;
    ObjectToken created(const Reservation&, int original_init_result) noexcept;
    void cancel_birth(const Reservation&) noexcept;
    Lease activate(const BoundaryProof&, int copied_native_fd,
                   int copied_native_peer_pid) noexcept;
    Lease connected(const ConnectProof&, int original_connect_result) noexcept;
    void close_fd(int fd) noexcept;
    void protected_retire(const BoundaryProof&) noexcept;
    // A failed/unknown native destroy invalidates everything and cannot provide
    // the observed destruction required to reuse a pipe pointer.
    void destroy(std::uintptr_t pipe, int original_destroy_result) noexcept;
    Lease lookup(const ObjectToken&) const noexcept;
    bool check_lease(const Lease&) const noexcept;
    void observer_loss() noexcept;
    bool healthy() const noexcept;
private:
    enum State : unsigned { Empty, Writing, Reserved, ObjectOnly, Pending, Accepted,
                            Connected, Closed, Destroyed, Invalid };
    struct Slot {
        std::atomic<unsigned> state{Empty}, domain{0}, kind{0};
        std::atomic<std::uintptr_t> pipe{0};
        std::atomic<std::uint64_t> object_generation{0}, socket_generation{0};
        std::atomic<int> pending_fd{-1}, fd{-1}, native_pid{0}, fresh_pid{0};
        std::atomic<bool> birth_valid{false}, zero_initialized{false}, reservation_pending{false};
    };
    struct Mutation {
        Registry& registry;
        bool counted = false, admitted = false;
        explicit Mutation(Registry&) noexcept;
        ~Mutation();
        Mutation(const Mutation&) = delete;
    };
    struct BirthPublication {
        Registry& registry;
        bool counted = false, admitted = false;
        explicit BirthPublication(Registry&) noexcept;
        ~BirthPublication();
        void finish() noexcept;
        BirthPublication(const BirthPublication&) = delete;
    };
    const std::uint64_t process_generation_;
    std::array<Slot, kCapacity> slots_{};
    mutable std::atomic<bool> lost_{false};
    std::atomic<std::uint64_t> next_generation_, revision_, active_writes_{0};
    // This activity/revision pair closes the unknown-key publication window;
    // it is not a lock, source frame or native socket/namespace generation.
    std::atomic<std::uint64_t> publishing_births_{0}, publication_revision_;
    bool publication_step() noexcept;
    void reject_unknown_during_publication(std::uint64_t prior_revision) noexcept;
    std::uint64_t generation() noexcept;
    bool owned(const ObjectToken&) const noexcept;
    Slot* find(std::uintptr_t pipe) noexcept;
    const Slot* find(std::uintptr_t pipe) const noexcept;
    bool matching(const Slot&, const ObjectToken&) const noexcept;
    bool claim(Slot&, unsigned& previous) noexcept;
    bool fd_conflict(const Slot*, int fd) const noexcept;
    bool census() noexcept;
    bool boundary(const BoundaryProof&) const noexcept;
    ObjectToken object(const Slot&, Reason = Reason::None) const noexcept;
    Lease lease(const Slot&, Reason = Reason::None) const noexcept;
    ObjectToken reject_object(Reason) noexcept;
    Reservation reject_reservation(Reason) noexcept;
    Lease reject_lease(Reason) noexcept;
    void invalidate(Slot&, unsigned next_state, bool birth_valid) noexcept;
};

} // namespace frame_import_pipe
