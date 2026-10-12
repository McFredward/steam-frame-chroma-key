#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace frame_client_carrier {
constexpr std::size_t kLimit = 32;
constexpr std::size_t kHeaderBytes = 12, kPayloadBytes = 9;
// Observation bound, not a claim about every possible native request size.
constexpr std::size_t kMaxRequestBytes = 512;
constexpr std::uint64_t kBegin = 0x3623c0, kWrite = 0x2456c8;
constexpr std::uint64_t kHeaderRead = 0x245334, kPayloadRead = 0x245460;
constexpr std::uint64_t kPending = 0x3624c4, kReference = 0x362d58;

enum Reason : std::uint64_t {
    None = 0, InvalidRoute = 1ULL << 0, ChangedRoute = 1ULL << 1,
    InvalidSpan = 1ULL << 2, ChangedSpan = 1ULL << 3,
    WriteFailed = 1ULL << 4, ReadFailed = 1ULL << 5,
    BadRequest = 1ULL << 6, BadReply = 1ULL << 7,
    ParserContamination = 1ULL << 8, MissingRequest = 1ULL << 9,
    MissingReply = 1ULL << 10, NativeWaitFailed = 1ULL << 11,
    ParsedTokenMismatch = 1ULL << 12, InvalidReference = 1ULL << 13,
    MissingPending = 1ULL << 14, RepeatedTransfer = 1ULL << 15,
    NestedImport = 1ULL << 16, Abandoned = 1ULL << 17,
    LifetimeInvalidated = 1ULL << 18, OwnerMismatch = 1ULL << 19
};
struct Route {
    std::uint64_t process_generation = 0, object_generation = 0, connection_generation = 0;
    std::uintptr_t import_frame = 0, receiver_frame = 0, pipe = 0, record = 0;
    int fd = -1;
    bool exact_native_route = false, active_invocation_owned = false;
    // Supplied by independently proved lifecycle tracking, never minted here.
    bool continuous_connection_owned = false, immutable_lifetime_lease = false;
};
struct BeginProof { Route route{}; std::uint64_t site = 0; int native_lock_result = -1; };
struct Event { Route route{}; std::uint64_t site = 0; };
struct Span {
    Event event{};
    std::uint64_t buffer_identity = 0;
    std::uintptr_t base = 0, current = 0;
    std::size_t total = 0, remaining = 0;
    bool owned_successful_prefix = false, immutable_native_span = false;
};
struct PendingProof {
    Event event{};
    int native_unlock_result = -1;
    bool copied_while_native_lock_owned = false;
    bool native_wait_success = false, parsed_token_present = false;
    std::uint64_t parsed_token = 0;
};
struct ReferenceProof {
    Event event{};
    std::uintptr_t ref_frame = 0;
    std::uint64_t live_token = 0, saved_token = 0;
    int native_lock_result = -1;
};
// Local output publications can truthfully reuse numeric sequences. Before any
// cross-endpoint match, the closed-trace interpreter must reject duplicate exact
// process/object/connection/pipe/fd/sequence keys. Observation ordinals are never
// a substitute for native transaction uniqueness or a cross-process namespace.
struct Record {
    std::uint64_t observation = 0; // Diagnostic identity, never a video frame.
    Route route{};
    std::uint32_t sequence = 0, request_bytes = 0, reply_header_bytes = 0, reply_payload_bytes = 0;
    std::uint64_t reply_token = 0, parsed_token = 0, referenced_token = 0, reason = None;
    bool sequence_available = false, request_complete = false, reply_complete = false;
    bool native_pending = false, output_published = false, reference_invoked = false;
    // Reference entry is after output publication, before reference success.
    bool reference_succeeded = false;
    bool native_cross_endpoint_associated = false, native_namespace_associated = false;
    bool source_frame_associated = false, uv_owned = false, accepted_submit = false, gpu_consumed = false;
};
struct Scope {
    Record record{};
    std::array<std::uint8_t, kHeaderBytes> request_header{}, reply_header{};
    std::array<std::uint8_t, kPayloadBytes> reply_payload{};
    std::uint64_t request_buffer = 0, header_buffer = 0, payload_buffer = 0;
    std::uintptr_t request_base = 0, header_base = 0, payload_base = 0, owner = 0;
    std::size_t request_total = 0;
    bool active = false, excluded = false;
};
struct Counters {
    std::atomic<std::uint64_t> claimed{0}, completed{0}, published{0}, rejected{0}, active{0};
    std::atomic<std::uint64_t> unsupported{0}, cap_skipped{0}, nested{0}, unmatched{0}, queue_loss{0};
};
class Observer {
public:
    std::atomic<bool> armed{false};
    Counters counters{};
    bool begin(const BeginProof&, Scope&) noexcept;
    // Only the successfully written header bytes are copied. Native request
    // payload bytes are counted under owned-span proof, never read or retained.
    void request_written(const Span&, const std::array<std::uint8_t, kHeaderBytes>& header_prefix,
                         std::size_t copied_header_bytes, std::int64_t native_result, Scope&) noexcept;
    void reply_header_read(const Span&, const std::array<std::uint8_t, kHeaderBytes>& prefix,
                           std::size_t copied_bytes, std::int64_t native_result, Scope&) noexcept;
    void reply_payload_read(const Span&, const std::array<std::uint8_t, kPayloadBytes>& prefix,
                            std::size_t copied_bytes, std::int64_t native_result, Scope&) noexcept;
    void pending(const PendingProof&, Scope&) noexcept;
    void reference(const ReferenceProof&, Scope&) noexcept;
    void invalidate(Scope&) noexcept;
    void abandon(std::uint64_t reason, Scope&) noexcept;
    bool take(Record&) noexcept;
    bool publish(const Record&) noexcept;
    bool quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept;
private:
    struct Slot { std::atomic<unsigned> state{0}; Record value{}; };
    std::array<Slot, kLimit> slots_{};
    bool route(const Event&, std::uint64_t, Scope&) noexcept;
    bool span(const Span&, std::size_t total, std::size_t offset,
              std::uint64_t& identity, std::uintptr_t& base, Scope&) noexcept;
    void close(Scope&) noexcept;
};
} // namespace frame_client_carrier
