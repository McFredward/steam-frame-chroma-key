#pragma once

// COPY-only bridge, not a live native observer. The caller owns the verified
// true entry/window and copies public Vulkan results and actual positive write
// prefixes while they are live. This module never reads a native heap pointer,
// obtains permissions, invokes Vulkan, or assigns resource generations.
#include "native_import_route.h"
#include "import_pipe_epoch.h"
#include "import_observer_core.h"
#include "../layer/passive_binding_trace.h"

#include <array>
#include <type_traits>

namespace frame_compositor_attempt {

enum class Refusal : unsigned {
    None, Inactive, PendingOutput, Nested, NativeRoute, Lease, Ordering,
    NativeFailure, PublicMetadata, WritePrefix, Incomplete, ObserverLoss,
    Receipt, ResourceNotCurrent, ObserverCapacity, RepeatedReplay
};

// Ownership/mapping/pinned-provider obligations are external. StackBytes bounds
// alone do not authorize reading an address. No borrowed fields survive a call.
struct NativeInput {
    frame_import_native::Entry entry{};
    frame_import_native::StackBytes stack{};
    std::uintptr_t load_bias = 0;
    frame_import_pipe::Lease lease{};
};
struct ImageResult {
    // device is the existing Journal Device::id; native_device is the original
    // public VkDevice handle. They are different identity domains.
    std::uint64_t device = 0, image = 0, modifier = 0;
    std::uint32_t format = 0, width = 0, height = 0, depth = 0, layers = 0;
    int result = -1;
    bool modifier_available = false;
    std::uint64_t native_device = 0;
};
struct BindResult {
    std::uint64_t device = 0, image = 0, memory = 0, offset = 0;
    int result = -1;
    std::uint64_t native_device = 0;
};
struct ViewResult {
    std::uint64_t device = 0, image = 0, view = 0;
    std::uint32_t format = 0;
    int result = -1;
    std::uint64_t native_device = 0;
};

class Capture;
class Replay;
class OwnedAttempt {
public:
    std::uint64_t tag() const noexcept { return tag_; }
    std::uint64_t cookie() const noexcept { return cookie_; }
    std::uint64_t device() const noexcept { return image_.device; }
    std::uint64_t image() const noexcept { return image_.image; }
    std::uint64_t view() const noexcept { return view_.view; }
    Refusal refusal() const noexcept { return refusal_; }
    bool complete() const noexcept { return complete_ && refusal_ == Refusal::None; }
    bool native_send_success() const noexcept { return finish_.native_send_success; }
    int native_unlock_result() const noexcept { return finish_.unlock_result; }
private:
    friend class Capture;
    friend class Replay;
    struct Write {
        frame_import::WriteProof proof{};
        std::uint32_t count = 0;
        std::int64_t result = 0;
    };
    std::uint64_t tag_ = 0, cookie_ = 0;
    frame_import_pipe::Lease lease_{};
    frame_import::BeginProof begin_{};
    frame_import::EventProof early_{}, image_event_{}, bind_event_{}, view_event_{};
    ImageResult image_{};
    BindResult bind_{};
    ViewResult view_{};
    frame_import::FinishProof finish_{};
    std::array<Write, frame_import::kReplyBytes> writes_{};
    std::array<std::uint8_t, frame_import::kReplyBytes> bytes_{};
    std::uint32_t write_count_ = 0, byte_count_ = 0;
    int early_result_ = -1;
    bool payload_read_ = false, complete_ = false;
    Refusal refusal_ = Refusal::Inactive;
};
static_assert(std::is_trivially_copyable_v<OwnedAttempt>, "Queue must copy owned facts only");
static_assert(sizeof(OwnedAttempt) < 8192, "Owned attempt exceeds its bounded copy budget");

// One owner thread; caller queues take()'s value, never Capture/NativeInput.
// Invocation tags/cookies are unique labels reserved by the external observer,
// not call ordinals used as resource generations. Reuse is forbidden by that
// owner. Invalid/nested evidence remains refused until finish() or abandon().
class Capture {
public:
    explicit Capture(const frame_import_pipe::Registry& registry) noexcept : registry_(registry) {}
    bool begin(const NativeInput&, int native_lock_result,
               std::uint64_t invocation_tag, std::uint64_t cookie) noexcept;
    bool early_unlock(const NativeInput&, int native_unlock_result) noexcept;
    std::uint64_t prepare_vulkan(frame_import_native::Boundary, const NativeInput&) noexcept;
    bool image_result(const ImageResult&) noexcept;
    bool bind_result(const BindResult&) noexcept;
    bool view_result(const ViewResult&) noexcept;
    bool send_lock(const NativeInput&, int native_lock_result) noexcept;
    bool prepare_write(const NativeInput&) noexcept;
    // The prefix is exactly result positive bytes copied from this public write
    // argument by the true entry owner. No pre-send packet guess is accepted.
    bool write_result(const std::array<std::uint8_t, frame_import::kReplyBytes>& prefix,
                      std::size_t copied_count, std::int64_t native_result) noexcept;
    bool finish(const NativeInput&, int native_unlock_result) noexcept;
    void abandon() noexcept;
    void observer_loss() noexcept;
    bool take(OwnedAttempt&) noexcept;
    Refusal refusal() const noexcept { return record_.refusal_; }
private:
    enum class Stage { Idle, Begin, Early, ImagePending, Image, BindPending,
                       Bound, ViewPending, Viewed, Sending, WritePending, Done };
    const frame_import_pipe::Registry& registry_;
    OwnedAttempt record_{};
    frame_import_native::Expected expected_{};
    frame_import::WriteProof pending_write_{};
    std::array<std::uint64_t, 4> public_arguments_{};
    Stage stage_ = Stage::Idle;
    bool pending_output_ = false;
    bool lost_ = false;
    bool fail(Refusal) noexcept;
    bool live(const NativeInput&, frame_import_native::Boundary,
              frame_import_native::Scalars&) noexcept;
    frame_import::Route route(const frame_import_pipe::Lease&, bool client) const noexcept;
};

// This function is bound to the real owning Journal's background lifecycle
// validator. It is not a caller-supplied resource-complete boolean. It must not
// be called on a render thread or inside Journal::drain's receipt callback,
// and its owner must outlive this Replay. A false result can mean pending
// lifecycle publications; defer/retry after drain returns, never certify them.
struct ReceiptValidator {
    void* owner = nullptr;
    bool (*current)(void*, const frame_bindings::ImportResourceReceipt&) noexcept = nullptr;
};
// Exclusive background owner, bounded to the existing Observer's 32 attempts.
// validate at entry and publication; no stale receipt publishes accepted proof.
// Returned records still deny native cross-process namespace/source/GPU proof.
class Replay {
public:
    explicit Replay(ReceiptValidator validator) noexcept : validator_(validator) {}
    Refusal replay(const OwnedAttempt&, const frame_bindings::ImportResourceReceipt&,
                   const frame_import_pipe::Registry&, frame_import::Record&) noexcept;
private:
    ReceiptValidator validator_{};
    frame_import::Observer observer_{};
    std::array<std::uint64_t, frame_import::kAttemptLimit> replayed_tags_{}, replayed_cookies_{};
    std::size_t replayed_count_ = 0;
};

} // namespace frame_compositor_attempt
