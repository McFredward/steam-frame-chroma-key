#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace frame_import {

constexpr std::size_t kAttemptLimit = 32;
constexpr std::size_t kQueueCapacity = 32;
constexpr std::size_t kReplyBytes = 21;
// ELF-relative returns in the separately validated SteamOS 0.4.5 compositor.
constexpr std::uint64_t kBeginSite = 0x153fc0;
constexpr std::uint64_t kEarlyUnlockSite = 0x1540b0;
constexpr std::uint64_t kImageSite = 0x2c75ac;
constexpr std::uint64_t kBindSite = 0x2c7684;
constexpr std::uint64_t kViewSite = 0x2c7804;
constexpr std::uint64_t kWriteSite = 0x172c00;
constexpr std::uint64_t kFinishSite = 0x172fbc;
constexpr std::uint64_t kReplyParent = 0x1542b0;

enum Reason : std::uint64_t {
    None = 0, InvalidRoute = 1ULL << 0, ChangedRoute = 1ULL << 1,
    NestedImport = 1ULL << 2, InvalidImage = 1ULL << 3,
    MultipleImages = 1ULL << 4, MissingImage = 1ULL << 5,
    InvalidBinding = 1ULL << 6, MissingBinding = 1ULL << 7,
    InvalidView = 1ULL << 8, MissingView = 1ULL << 9,
    InvalidSpan = 1ULL << 10, ChangedSpan = 1ULL << 11,
    WriteFailed = 1ULL << 12, BadReply = 1ULL << 13,
    RepeatedReply = 1ULL << 14, NativeSendFailed = 1ULL << 15,
    IncompleteReply = 1ULL << 16, Abandoned = 1ULL << 17,
    SocketInvalidated = 1ULL << 18, ResourceRetired = 1ULL << 19,
    InvalidFinish = 1ULL << 20, DepthOverflow = 1ULL << 21,
    ObserverMismatch = 1ULL << 22, MissingClient = 1ULL << 23,
    DuplicateClientAttachment = 1ULL << 24, UnexpectedClient = 1ULL << 25,
    MissingPeer = 1ULL << 26, PayloadReadFailed = 1ULL << 27,
    MissingEarlyUnlock = 1ULL << 28
};

// These are adapter-established ownership facts, not guesses made by the core.
// The adapter must revalidate the same live native invocation at EVERY callback.
// Anchor equality alone does not establish a live stack frame or prevent reuse.
struct Route {
    std::uint64_t process_generation = 0;
    std::uintptr_t anchor_frame = 0;
    std::uint64_t pipe_identity = 0;
    std::uint64_t client_identity = 0;
    // Local ownership/capture label within this exact invocation. It is not a
    // native namespace, token lifetime or source-frame generation. Zero at begin.
    std::uint64_t client_generation = 0;
    std::uint64_t socket_generation = 0;
    std::uint32_t request_sequence = 0;
    std::uint32_t peer_pid = 0;
    int socket_fd = -1;
    bool native_route_verified = false;
    bool active_anchor_verified = false;
    bool client_identity_available = false;
    // The client scalar was copied under native ownership and is still bound
    // to this invocation. Never reread a released client object at reply time.
    bool client_ownership_verified = false;
    bool socket_lifetime_verified = false;
    bool peer_credentials_verified = false;
    bool same_native_connection_verified = false;
    bool foreign_namespace = true;
};

// Begin precedes the typed native handler: client fields must be absent. Only
// candidate() can attach their owned copies during nested Vulkan creation.
struct BeginProof { Route route{}; std::uint64_t site = 0; int lock_result = -1; };
struct EventProof { Route route{}; std::uint64_t site = 0; };

struct ImageCandidate {
    std::uint64_t device = 0, image = 0, image_generation = 0;
    std::uint64_t modifier = 0;
    std::uint32_t format = 0, width = 0, height = 0, layers = 0;
    int native_result = -1;
    bool owned_public_scalars_verified = false;
    bool modifier_available = false;
};
struct Binding {
    std::uint64_t device = 0, image = 0, image_generation = 0;
    std::uint64_t memory = 0, memory_generation = 0, memory_offset = 0;
    int native_result = -1;
    bool owned_public_scalars_verified = false;
};
struct View {
    std::uint64_t device = 0, image = 0, image_generation = 0;
    std::uint64_t view = 0, view_generation = 0;
    int native_result = -1;
    bool owned_public_scalars_verified = false;
};

struct WriteProof {
    EventProof event{};
    std::uint64_t parent_return = 0;
    std::uint64_t packet_identity = 0;
    std::uintptr_t packet_base = 0, current_address = 0;
    std::size_t total_size = 0, remaining_size = 0;
    bool immutable_native_span_verified = false;
    bool successful_prefix_owned = false;
};
struct FinishProof {
    EventProof event{};
    std::uint64_t parent_return = 0;
    bool native_send_success = false;
    int unlock_result = -1;
};

struct Record {
    std::uint64_t observation = 0; // Observer ticket only; never a source frame.
    Route route{};
    ImageCandidate image{};
    Binding binding{};
    View view{};
    std::uint64_t token = 0, reason = None;
    std::uint32_t written_bytes = 0, successful_writes = 0;
    bool import_reply_complete = false;
    bool client_identity_available = false;
    // A local import observation cannot establish any of these requirements.
    bool native_cross_process_namespace = false;
    bool source_frame_associated = false;
    bool accepted_submit = false;
    bool gpu_consumed_coherent = false;
};

// Owned by one adapter thread. Never share or read an active Scope on a worker.
// Only copied scalars/bytes are retained; numeric addresses are not dereferenced.
struct Scope {
    Record record{};
    std::array<std::uint8_t, kReplyBytes> packet{};
    std::uint64_t packet_identity = 0;
    std::uintptr_t packet_base = 0, observer_identity = 0;
    std::uint32_t depth = 0, image_count = 0, binding_count = 0, view_count = 0;
    bool active = false, excluded = false, client_attached = false, peer_attached = false;
    bool early_unlock_seen = false;
};

struct Counters {
    std::atomic<std::uint64_t> claimed{0}, completed{0}, complete{0}, rejected{0};
    std::atomic<std::uint64_t> active{0}, cap_skipped{0}, unsupported{0};
    std::atomic<std::uint64_t> nested{0}, unmatched{0}, queue_loss{0};
};

class Observer {
public:
    std::atomic<bool> armed{false};
    Counters counters{};
    bool begin(const BeginProof&, Scope&) noexcept;
    // Sole client-attachment boundary: require the live retained typed context.
    // Later events carry this exact copy without rereading a released object.
    void candidate(const EventProof&, const ImageCandidate&, Scope&) noexcept;
    void bind(const EventProof&, const Binding&, Scope&) noexcept;
    void view(const EventProof&, const View&, Scope&) noexcept;
    // bytes[0..copied_size) is an adapter-owned copy of exactly the positively
    // written prefix of the current remaining native span. No native reads here.
    void written(const WriteProof&, const std::array<std::uint8_t, kReplyBytes>& bytes,
                 std::size_t copied_size, std::int64_t native_result, Scope&) noexcept;
    void finish(const FinishProof&, Scope&) noexcept;
    void abandon(std::uint64_t reason, Scope&) noexcept;
    void invalidate_socket(Scope&) noexcept;
    void retire_image(std::uint64_t image, std::uint64_t generation, Scope&) noexcept;
    // The first pipe unlock precedes image creation. It is NOT completion.
    void early_unlock(const EventProof&, int native_unlock_result,
                      bool native_payload_read, Scope&) noexcept;
    bool take(Record&) noexcept;
    bool publish(const Record&) noexcept;
    bool quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept;
private:
    struct Slot {
        std::atomic<unsigned> state{0}; // free, writing, ready, reading
        Record value{};
    };
    std::array<Slot, kQueueCapacity> slots_{};
    bool route(const EventProof&, std::uint64_t expected_site, Scope&) noexcept;
    void close(Scope&) noexcept;
};

} // namespace frame_import
