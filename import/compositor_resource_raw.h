#pragma once

#include "compositor_attempt.h"
#include "import_forwarding.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Copy-only interpretation of a single native compositor import. This module
// never reads native memory or creates a lifetime lease, namespace, frame or GPU
// proof. The entry owner independently authenticates source/provider/module and
// same-thread ownership before supplying decoder scalars and public results.
namespace frame_compositor_raw {
using Boundary = frame_import_native::Boundary;
using Entry = frame_import_native::Entry;
using Scalars = frame_import_native::Scalars;
using Phase = frame_import_forwarding::Phase;
using ImageResult = frame_compositor_attempt::ImageResult;
using BindResult = frame_compositor_attempt::BindResult;
using ViewResult = frame_compositor_attempt::ViewResult;
constexpr std::size_t kReplyBytes = 21;
constexpr unsigned kMaxWrites = 8;
constexpr std::size_t kMaxScalarBytes = 12 + 12 + 3 * 80 + 28 + kMaxWrites * 44 + 28;
constexpr std::size_t kMaxWireBytes = kReplyBytes;

enum Reason : std::uint64_t {
    None = 0, ThreadUnavailable = 1ULL << 0, ModuleUnavailable = 1ULL << 1,
    ProviderUnavailable = 1ULL << 2, SourceUnavailable = 1ULL << 3,
    Order = 1ULL << 4, NestedImport = 1ULL << 5, ChangedEntry = 1ULL << 6,
    InvalidEntry = 1ULL << 7, InvalidDecode = 1ULL << 8,
    ChangedInvocation = 1ULL << 9, ChangedClient = 1ULL << 10,
    NativeFailure = 1ULL << 11, MissingPayload = 1ULL << 12,
    PublicMetadata = 1ULL << 13, InvalidPrefix = 1ULL << 14,
    ChangedDescriptor = 1ULL << 15, ChangedPacket = 1ULL << 16,
    TransferLimit = 1ULL << 17, BadReply = 1ULL << 18,
    AcquisitionBudget = 1ULL << 19, ChangedSnapshot = 1ULL << 20,
    Incomplete = 1ULL << 21, Abandoned = 1ULL << 22,
    ObserverLoss = 1ULL << 23, OwnerMismatch = 1ULL << 24,
    UnsupportedBoundary = 1ULL << 25
};
struct OwnedPrefix {
    // Actual positive native write prefix at the original argument's offset.
    std::array<std::uint8_t, kReplyBytes> bytes{};
    std::size_t count = 0;
    bool successful_prefix_owned = false;
};
struct Event {
    Boundary boundary = Boundary::Begin;
    Phase phase = Phase::Before;
    Entry entry{};
    Scalars scalars{};
    bool decoded_available = false;
    std::size_t scalar_bytes_copied = 0;
    OwnedPrefix prefix{};
    std::int64_t native_result = 0;
    int preserved_errno = 0;
    bool native_result_available = false;
    std::uintptr_t thread_identity = 0, load_bias = 0;
    bool current_thread_owned = false, native_module_current = false;
    bool native_provider_current = false, source_current = false;
    unsigned thread_reason = 0;
    // Native VkDevice and Journal device ID stay in separate fields/domains.
    // These are copied genuine public results, never Vulkan invocations.
    bool public_result_available = false;
    ImageResult image{};
    BindResult bind{};
    ViewResult view{};
};
struct RawRecord {
    frame_import_native::Expected invocation{};
    std::uint32_t client = 0;
    bool client_available = false;
    std::uintptr_t packet_base = 0;
    int fd = -1;
    unsigned writes = 0;
    std::uint32_t reply_bytes = 0;
    std::array<std::uint8_t, kReplyBytes> reply{};
    std::uint64_t reply_token = 0;
    std::size_t scalar_bytes_copied = 0, wire_bytes_copied = 0;
    ImageResult image{};
    BindResult bind{};
    ViewResult view{};
    bool begin_lock_succeeded = false, early_unlock_succeeded = false;
    bool native_payload_read = false, image_succeeded = false;
    bool bind_succeeded = false, view_succeeded = false;
    bool send_lock_succeeded = false, reply_complete = false;
    bool finish_unlock_succeeded = false, native_send_success = false;
    bool raw_only = true, matched = false, terminal = false;
    std::uint64_t reason = None;
    frame_import_native::Refusal decoder_refusal = frame_import_native::Refusal::None;
    unsigned thread_reason = 0;
    Boundary last_boundary = Boundary::Begin;
    Phase last_phase = Phase::Before;
    std::int64_t last_native_result = 0;
    int last_preserved_errno = 0;
    bool last_native_result_available = false;
    // A complete local reply is deliberately weaker than every proof below.
    bool request_header_observed = false, continuous_connection_owned = false;
    bool immutable_lifetime_lease = false, native_cross_endpoint_associated = false;
    bool native_namespace_associated = false, source_frame_associated = false;
    bool uv_owned = false, accepted_submit = false, gpu_consumed = false;
};
static_assert(std::is_trivially_copyable_v<RawRecord>, "Queue owned facts only");
static_assert(sizeof(RawRecord) < 2048, "Raw observation must remain bounded");
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
    frame_import_native::Expected expected() const noexcept { return record_.invocation; }
private:
    friend class Observer;
    enum class State : unsigned { Fresh, BeginAfter, EarlyBefore, EarlyAfter,
        ImageBefore, ImageAfter, BindBefore, BindAfter, ViewBefore, ViewAfter,
        SendBefore, SendAfter, Writing, WriteAfter, FinishAfter, Terminal };
    State state_ = State::Fresh;
    const Observer* owner_ = nullptr;
    std::uintptr_t thread_identity_ = 0, load_bias_ = 0;
    Entry paired_entry_{};
    Boundary paired_boundary_ = Boundary::Begin;
    Scalars paired_snapshot_{};
    RawRecord record_{};
};
class Observer {
public:
    // Pure zero-read admission before private scalar/wire acquisition. Public
    // result After and reused unlock After are never admitted for private reads.
    // False must be submitted unchanged to observe() to terminate safely.
    bool can_acquire(const Event&, const Scope&) const noexcept;
    Outcome observe(const Event&, Scope&) const noexcept;
    Outcome abandon(Scope&) const noexcept;
    Outcome observer_loss(Scope&) const noexcept;
    Outcome expire(unsigned thread_reason, Scope&) const noexcept;
private:
    Outcome refuse(std::uint64_t reason, Scope&) const noexcept;
};
} // namespace frame_compositor_raw
