#pragma once

#include "client_carrier_core.h"
#include "import_forwarding.h"
#include "native_client_route.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Bounded offline interpretation of owned source-specific metadata. The caller
// acquires and decodes sparse scalars with frame_client_native::decode. Nothing
// here reads native memory, establishes a lifetime lease, or enters the strict
// carrier Observer. Root adapter preconditions are not proof minted by this API.
namespace frame_client_raw {
using Boundary = frame_client_native::Boundary;
using Phase = frame_import_forwarding::Phase;
using Entry = frame_client_native::Entry;
using Scalars = frame_client_native::Scalars;
constexpr std::size_t kHeaderBytes = frame_client_carrier::kHeaderBytes;
constexpr std::size_t kPayloadBytes = frame_client_carrier::kPayloadBytes;
constexpr std::size_t kMaxRequestBytes = frame_client_carrier::kMaxRequestBytes;
constexpr unsigned kMaxWrites = 8, kMaxHeaderReads = 8, kMaxPayloadReads = 8;
constexpr unsigned kMaxTransfers = kMaxWrites + kMaxHeaderReads + kMaxPayloadReads;
constexpr std::size_t kMaxScalarBytes = 32 + 44 + 40 + 8 * 88 + 8 * 76 + 8 * 80;
constexpr std::size_t kMaxWireBytes = 2 * kHeaderBytes + kPayloadBytes;

enum Reason : std::uint64_t {
    None = 0, ThreadUnavailable = 1ULL << 0, ModuleUnavailable = 1ULL << 1,
    Order = 1ULL << 2, NestedImport = 1ULL << 3,
    UnmatchedCompletion = 1ULL << 4, ChangedEntry = 1ULL << 5,
    InvalidDecode = 1ULL << 6, ChangedInvocation = 1ULL << 7,
    NativeFailure = 1ULL << 8, UnsupportedTransfer = 1ULL << 9,
    InvalidPrefix = 1ULL << 10, ChangedSpan = 1ULL << 11,
    ChangedDescriptor = 1ULL << 12, TransferLimit = 1ULL << 13,
    BadRequest = 1ULL << 14, BadReply = 1ULL << 15,
    ParserContamination = 1ULL << 16, MissingRequest = 1ULL << 17,
    MissingReply = 1ULL << 18, MissingPending = 1ULL << 19,
    PendingFailed = 1ULL << 20, TokenMismatch = 1ULL << 21,
    InvalidReference = 1ULL << 22, AcquisitionBudget = 1ULL << 23,
    ChangedSnapshot = 1ULL << 24, Abandoned = 1ULL << 25,
    OwnerMismatch = 1ULL << 26, UnsupportedBoundary = 1ULL << 27
};

struct OwnedPrefix {
    // A successful prefix beginning at the original native buffer + offset.
    // Request payload is counted, never copied here, including on split writes.
    std::array<std::uint8_t, kHeaderBytes> bytes{};
    std::size_t count = 0;
    bool successful_prefix_owned = false;
};

struct Event {
    Boundary boundary = Boundary::Begin;
    Phase phase = Phase::Before;
    Entry entry{}; // Immutable genuine entry copy, identical in the paired After.
    Scalars scalars{};
    bool decoded_available = false;
    // Truthful acquisition ledger: 32 Begin After, 44 Pending Before, 40
    // Reference After; 88/76/80 successful Write/HeaderRead/PayloadRead After.
    // All other events acquire zero scalar bytes. Pending After reuses its copy.
    std::size_t scalar_bytes_copied = 0;
    OwnedPrefix prefix{};
    std::int64_t native_result = 0;
    int preserved_errno = 0;
    bool native_result_available = false;
    // Same-thread capability and pinned module checks supplied by the root.
    // Identity is only the existing prepared marker's identity, never a lease.
    std::uintptr_t thread_identity = 0;
    bool current_thread_owned = false, native_module_current = false;
    unsigned thread_reason = 0;
};

struct RawRecord {
    frame_client_native::Invocation invocation{};
    int fd = -1;
    std::uint32_t sequence = 0, request_bytes = 0;
    std::uint32_t reply_header_bytes = 0, reply_payload_bytes = 0;
    unsigned writes = 0, header_reads = 0, payload_reads = 0;
    std::size_t scalar_bytes_copied = 0, wire_bytes_copied = 0;
    std::array<std::uint8_t, kHeaderBytes> request_header{}, reply_header{};
    std::array<std::uint8_t, kPayloadBytes> reply_payload{};
    std::uint64_t reply_token = 0, parsed_token = 0, referenced_token = 0;
    std::uint64_t reason = None;
    frame_client_native::Refusal decoder_refusal = frame_client_native::Refusal::None;
    unsigned thread_reason = 0;
    Boundary last_boundary = Boundary::Begin;
    Phase last_phase = Phase::Before;
    std::int64_t last_native_result = 0;
    int last_preserved_errno = 0;
    bool last_native_result_available = false;
    bool raw_only = true, sequence_available = false;
    bool begin_lock_succeeded = false, request_complete = false, reply_complete = false;
    bool pending_unlock_succeeded = false, native_wait_success = false;
    bool matching_pending = false, reference_lock_succeeded = false;
    // A matching following reference is evidence after the native output store,
    // not proof the subsequent RefResource RPC succeeded or selected a frame.
    bool following_reference_observed = false, matched = false, terminal = false;
    bool reference_succeeded = false;
    std::uint64_t connection_generation = 0;
    bool continuous_connection_owned = false, immutable_lifetime_lease = false;
    bool native_cross_endpoint_associated = false, native_namespace_associated = false;
    bool source_frame_associated = false, uv_owned = false;
    bool accepted_submit = false, gpu_consumed = false;
};

enum class Kind : unsigned { None, Matched, Refused };
struct Outcome { Kind kind = Kind::None; RawRecord record{}; };

class Observer;
class Scope {
public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    bool active() const noexcept;
    bool finished() const noexcept;
    const RawRecord& record() const noexcept { return record_; }
    frame_client_native::Expected expected() const noexcept;
private:
    friend class Observer;
    enum class State : unsigned { Fresh, BeginAfter, Writing, TransferAfter,
        Header, Payload, PendingBefore, PendingAfter, ReferenceBefore,
        ReferenceAfter, Terminal };
    State state_ = State::Fresh;
    const Observer* owner_ = nullptr;
    std::uintptr_t thread_identity_ = 0;
    Entry paired_entry_{};
    Boundary paired_boundary_ = Boundary::Begin;
    Scalars pending_snapshot_{};
    std::uintptr_t request_base_ = 0, header_base_ = 0, payload_base_ = 0;
    std::uint32_t request_total_ = 0;
    std::uint32_t native_sequence_ = 0;
    bool native_sequence_seen_ = false;
    RawRecord record_{};
};

class Observer {
public:
    // Exactly one ImportDmabuf timeline per Scope. Terminal scopes never reset
    // and never emit a second record. Before events are undecoded except the
    // final Pending unlock, whose scalars are copied while its lock is owned.
    // Transfers are decoded/copied only After a successful original operation.
    Outcome observe(const Event&, Scope&) const noexcept;
    Outcome abandon(Scope&) const noexcept;
    Outcome expire(unsigned thread_reason, Scope&) const noexcept;
private:
    Outcome refuse(std::uint64_t, Scope&) const noexcept;
    Outcome finish(Scope&) const noexcept;
};
} // namespace frame_client_raw
