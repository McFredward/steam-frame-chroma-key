#pragma once

// A one-shot diagnostic copy, never a source/frame association or permission to
// blend passthrough. Caller supplies already-established native command/queue
// ownership facts; this class does not infer them from handles or timestamps.
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace frame_pixels {
constexpr VkDeviceSize MaximumBytes = 64u * 1024u * 1024u;
constexpr VkFormat NativeP010 = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;

enum class Mode : std::uint8_t { NativeP010, OwnedRgbaFixture };
enum class State : std::uint8_t { Empty, Preparing, Ready, Recording, Recorded, Submitting, Pending, Polling, Complete, Failed, Released };
enum class Status : std::uint8_t { Ok, Refused, NotReady, Pending, Complete, VulkanFailure, UnsafeToRelease };
struct Config {
    VkFormat format = NativeP010;
    // Both zero reserve the fixed 64 MiB budget cold and admit the actual
    // bounded even source extent at record time. Otherwise exact extent pin.
    VkExtent2D extent{};
    Mode mode = Mode::NativeP010; // RGBA exists only for the owned offline GPU fixture.
};
struct Layout {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkDeviceSize bytes = 0;
    std::uint32_t planeCount = 0;
    std::array<VkBufferImageCopy, 2> planes{};
};
struct Source {
    VkImage image = VK_NULL_HANDLE;
    std::uint64_t generation = 0; // Object-lifetime generation, not a video-frame ID.
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
    VkImageType type = VK_IMAGE_TYPE_2D;
    std::uint32_t mipLevels = 0, arrayLayers = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
struct Admission {
    std::uint64_t commandGeneration = 0;
    // Must each be proved by the native command tracker immediately before
    // record(), not guessed from the currently bound descriptor or queue.
    bool complete = false, primary = false, outsideRenderPass = false;
    bool afterMatchingDistortDraw = false, queueOwnsImage = false;
    bool beforeExternalRelease = false, ordinaryUnprotected = false;
    // Without this recorded copy commands could be replayed later, rewriting
    // the mapped result or retaining a reference to already freed staging.
    bool oneTimeSubmit = false, noSimultaneousUse = false;
};
struct View {
    const std::uint8_t* data = nullptr;
    VkDeviceSize bytes = 0;
    Layout layout{};
    VkImage source = VK_NULL_HANDLE;
    std::uint64_t sourceGeneration = 0, commandGeneration = 0;
};

// No mutex, allocation, file I/O or blocking wait in record(),
// submitCompletion(), or poll(). Native queue external synchronization remains
// the caller's responsibility. prepare/release require exclusive cold ownership.
class Readback {
public:
    Readback() noexcept = default;
    Readback(const Readback&) = delete;
    Readback& operator=(const Readback&) = delete;
    // Explicit release is mandatory. A destructor cannot safely wait on, or
    // free, resources referenced by an unknown native in-flight submission.
    ~Readback() = default;
    static bool describe(const Config&, Layout&) noexcept;
    Status prepare(VkDevice, PFN_vkGetDeviceProcAddr next,
                   const VkPhysicalDeviceMemoryProperties&, const Config&) noexcept;
    Status record(VkCommandBuffer, const Source&, const Admission&) noexcept;
    // Call only after the original native submission has returned, on that
    // SAME externally synchronized queue, before releasing its synchronization.
    // The native result/fence are untouched. A mismatched CB is a no-op refusal.
    Status submitCompletion(VkQueue, VkCommandBuffer, std::uint64_t commandGeneration,
                            VkResult nativeSubmitResult) noexcept;
    Status poll(View&) noexcept;
    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    VkResult lastResult() const noexcept { return lastResult_.load(std::memory_order_relaxed); }
    // Safe without proof only before any commands were recorded or after our
    // successful completion fence. After ANY recording/failure the caller must
    // first join hook/writer users and establish native device quiescence
    // (downstream DeviceWaitIdle VK_SUCCESS) or terminal VK_ERROR_DEVICE_LOST
    // outside hooks. Never call a speculative wait here.
    Status release(bool deviceQuiescent = false) noexcept;
private:
    struct Procs {
        PFN_vkCreateBuffer createBuffer = nullptr;
        PFN_vkDestroyBuffer destroyBuffer = nullptr;
        PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
        PFN_vkAllocateMemory allocateMemory = nullptr;
        PFN_vkFreeMemory freeMemory = nullptr;
        PFN_vkBindBufferMemory bindBufferMemory = nullptr;
        PFN_vkMapMemory mapMemory = nullptr;
        PFN_vkUnmapMemory unmapMemory = nullptr;
        PFN_vkCreateFence createFence = nullptr;
        PFN_vkDestroyFence destroyFence = nullptr;
        PFN_vkCmdPipelineBarrier barrier = nullptr;
        PFN_vkCmdCopyImageToBuffer copy = nullptr;
        PFN_vkQueueSubmit submit = nullptr;
        PFN_vkGetFenceStatus fenceStatus = nullptr;
        PFN_vkInvalidateMappedMemoryRanges invalidate = nullptr;
    } p_{};
    void destroyOwned() noexcept;
    void fail(VkResult) noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    Layout layout_{};
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkImage source_ = VK_NULL_HANDLE;
    std::uint64_t commandGeneration_ = 0, sourceGeneration_ = 0;
    Config config_{}; // Immutable after prepare publishes Ready.
    bool coherent_ = false, dynamicExtent_ = false;
    // Set before issuing any copy command; failure never clears this marker.
    bool recorded_ = false;
    std::atomic<State> state_{State::Empty};
    std::atomic<VkResult> lastResult_{VK_SUCCESS};
};
}
