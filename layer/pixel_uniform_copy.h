#pragma once

// Inert, one-shot buffer-copy foundation. Nothing in this helper establishes a
// draw/source/frame association or authorizes a native mask or passthrough.
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace frame_uniform_copy {

constexpr std::uint32_t MaximumSlices = 5;
constexpr VkDeviceSize MaximumCopyBytes = 32u * 1024u;
constexpr VkDeviceSize MaximumCapacity = 64u * 1024u * 1024u;

enum class State : std::uint8_t { Empty, Preparing, Ready, Recording, Recorded, Failed };
enum class Status : std::uint8_t { Ok, Refused };

struct Preparation {
    VkBuffer target = VK_NULL_HANDLE; // Borrowed, caller-owned staging buffer.
    VkDeviceSize capacity = 0;       // Actual buffer size, never an allocation guess.
    VkBufferUsageFlags usage = 0;   // Actual vkCreateBuffer usage; requires TRANSFER_DST.
};

struct Slice {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;          // Actual buffer size from its current live generation.
    VkBufferUsageFlags usage = 0;   // Requires TRANSFER_SRC and UNIFORM_BUFFER.
    VkDeviceSize effectiveOffset = 0; // Descriptor offset plus any proved dynamic offset.
    VkDeviceSize bytes = 0;
};

class CopyRecorder;
class Dispatch {
public:
    Dispatch() noexcept = default;
    std::uint32_t sliceCount() const noexcept { return count_; }
    VkBuffer target() const noexcept { return target_; }
    VkDeviceSize offset() const noexcept { return offset_; }
    VkDeviceSize bytes() const noexcept { return bytes_; }
    Slice source(std::uint32_t index) const noexcept {
        return index < count_ ? sources_[index] : Slice{};
    }
    bool region(std::uint32_t index, VkBufferCopy& out) const noexcept {
        out = {};
        if (index >= count_) return false;
        out = copies_[index];
        return true;
    }
private:
    friend class CopyRecorder;
    const CopyRecorder* owner_ = nullptr;
    VkBuffer target_ = VK_NULL_HANDLE;
    VkDeviceSize capacity_ = 0, offset_ = 0, bytes_ = 0;
    std::uint32_t count_ = 0;
    std::array<Slice, MaximumSlices> sources_{};
    std::array<VkBufferCopy, MaximumSlices> copies_{};
};

// prepare() has exclusive cold ownership and resolves ONLY the two original
// command functions. It creates/destroys no GPU objects and never maps memory.
// admit() is pure: its sealed dispatch copies only bounded supplied metadata.
// record() appends commands once, without allocations, I/O, waits, descriptor or
// pipeline changes. There is deliberately no reset/rearm or readback method.
//
// Caller obligations, NOT certified by this helper:
// - These are current complete buffers of the same device, ordinary/unprotected,
//   nonsparse and completely bound; distinct handles must not alias memory.
// - The caller owns the disjoint staging tail and prevents other GPU/CPU users
//   from accessing it. Source bytes/lifetimes and last-draw coherence are proved
//   separately; a handle, usage bit or successful copy is not that proof.
// - The exact command is recording, primary, outside all render scopes, on a
//   graphics/compute-capable queue with established ownership/synchronization.
// - Original queue submission and a completion fence, plus exact-generation
//   retirement for reusable commands, must finish BEFORE any CPU read. Recorded
//   native/staging handles remain alive through every possible command replay.
// - Cold setup/destruction must not overlap helper users. This object owns no
//   GPU resource to release; destroying it never frees the borrowed buffers.
class CopyRecorder {
public:
    CopyRecorder() noexcept = default;
    CopyRecorder(const CopyRecorder&) = delete;
    CopyRecorder& operator=(const CopyRecorder&) = delete;
    ~CopyRecorder() = default;
    Status prepare(VkDevice, PFN_vkGetDeviceProcAddr original, const Preparation&) noexcept;
    bool admit(const Slice*, std::uint32_t count, VkDeviceSize tailOffset, Dispatch&) const noexcept;
    Status record(VkCommandBuffer, const Dispatch&) noexcept;
    State state() const noexcept { return state_.load(std::memory_order_acquire); }
private:
    PFN_vkCmdPipelineBarrier barrier_ = nullptr;
    PFN_vkCmdCopyBuffer copy_ = nullptr;
    VkBuffer target_ = VK_NULL_HANDLE;
    VkDeviceSize capacity_ = 0;
    std::atomic<State> state_{State::Empty};
};

} // namespace frame_uniform_copy
