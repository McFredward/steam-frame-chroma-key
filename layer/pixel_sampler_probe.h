#pragma once

// One-shot comparison with the native P010 conversion at pixel centers. These
// sampled RGB_IDENTITY components are not reconstructed scene RGB, a coverage
// mask, a source-frame identity, or permission to enable passthrough.
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>

namespace frame_pixel_sampler {
constexpr VkDeviceSize MaximumBytes = 64u * 1024u * 1024u;
constexpr VkFormat P010 = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;

enum class State : std::uint8_t { Empty, Preparing, Ready, Recording, Recorded, Failed, Released };
enum class Status : std::uint8_t { Ok, Refused, VulkanFailure, UnsafeToRelease };

struct Preparation {
    VkBuffer targetBuffer = VK_NULL_HANDLE; // Borrowed, STORAGE_BUFFER|TRANSFER_DST.
    VkDeviceSize capacity = 0;
    bool samplerYcbcrConversionEnabled = false; // Enabled, not merely supported.
    // Exact cold vkGetPhysicalDeviceImageFormatProperties2 result for P010 and
    // this conversion route; immutable YCbCr samplers can consume >1 slot.
    std::uint32_t combinedImageSamplerDescriptorCount = 0;
};

struct Source {
    bool complete = false;
    VkImageView view = VK_NULL_HANDLE;
    std::uint64_t viewGeneration = 0;
    VkExtent2D extent{};
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkComponentMapping components{};
    VkImageSubresourceRange subresource{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Required on the IMAGE VIEW itself. An unrelated sampler's conversion
    // cannot authorize binding a view into our immutable combined descriptor.
    bool hasViewConversion = false, conversionComplete = false;
    VkSamplerYcbcrConversionCreateInfo conversion{};
};

class SamplerProbe;
class Dispatch {
public:
    Dispatch() noexcept = default;
    VkExtent2D extent() const noexcept { return extent_; }
    VkDeviceSize offset() const noexcept { return offset_; }
    VkDeviceSize sampledBytes() const noexcept { return sampledBytes_; }
    VkDeviceSize totalBytes() const noexcept { return totalBytes_; }
    VkImageView sourceView() const noexcept { return view_; }
    std::uint64_t sourceViewGeneration() const noexcept { return generation_; }
private:
    friend class SamplerProbe;
    const SamplerProbe* owner_ = nullptr;
    VkImageView view_ = VK_NULL_HANDLE;
    std::uint64_t generation_ = 0;
    VkExtent2D extent_{};
    VkDeviceSize offset_ = 0, sampledBytes_ = 0, totalBytes_ = 0;
};

// All creation and compilation are cold. admit() is pure and can run before
// another one-shot owner wins selection. Only AFTER Readback::record wins may
// record() update this probe's OWN descriptor set and append compute commands
// in the same valid primary, outside native render passes. The caller proves
// original view/image lifetime, queue compute support, enabled feature/profile,
// and absence of prior or unknown compute state; this class invents none.
class SamplerProbe {
public:
    SamplerProbe() noexcept = default;
    SamplerProbe(const SamplerProbe&) = delete;
    SamplerProbe& operator=(const SamplerProbe&) = delete;
    ~SamplerProbe() = default; // Explicit release after joined users is required.
    static VkSamplerYcbcrConversionCreateInfo profile() noexcept;
    static bool matchesProfile(const VkSamplerYcbcrConversionCreateInfo&) noexcept;
    Status prepare(VkDevice, PFN_vkGetDeviceProcAddr original, const Preparation&) noexcept;
    bool admit(const Source&, Dispatch&) const noexcept;
    Status record(VkCommandBuffer, const Dispatch&) noexcept;
    // No native waits here. Once recorded, the caller must join all users and
    // prove actual device quiescence/loss before destroying referenced objects.
    Status release(bool deviceQuiescent = false) noexcept;
    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    VkResult lastResult() const noexcept { return lastResult_.load(std::memory_order_relaxed); }
private:
    struct Procs {
        PFN_vkCreateSamplerYcbcrConversion createConversion = nullptr;
        PFN_vkDestroySamplerYcbcrConversion destroyConversion = nullptr;
        PFN_vkCreateSampler createSampler = nullptr;
        PFN_vkDestroySampler destroySampler = nullptr;
        PFN_vkCreateDescriptorSetLayout createLayout = nullptr;
        PFN_vkDestroyDescriptorSetLayout destroyLayout = nullptr;
        PFN_vkCreateDescriptorPool createPool = nullptr;
        PFN_vkDestroyDescriptorPool destroyPool = nullptr;
        PFN_vkAllocateDescriptorSets allocateSets = nullptr;
        PFN_vkUpdateDescriptorSets updateSets = nullptr;
        PFN_vkCreateShaderModule createShader = nullptr;
        PFN_vkDestroyShaderModule destroyShader = nullptr;
        PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
        PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
        PFN_vkCreateComputePipelines createPipelines = nullptr;
        PFN_vkDestroyPipeline destroyPipeline = nullptr;
        PFN_vkCmdPipelineBarrier barrier = nullptr;
        PFN_vkCmdBindPipeline bindPipeline = nullptr;
        PFN_vkCmdBindDescriptorSets bindSets = nullptr;
        PFN_vkCmdDispatch dispatch = nullptr;
    } p_{};
    Status fail(VkResult) noexcept;
    void destroyOwned() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE; // Never destroyed by this object.
    VkDeviceSize capacity_ = 0;
    VkSamplerYcbcrConversion conversion_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkShaderModule shader_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::atomic<VkResult> lastResult_{VK_SUCCESS};
    std::atomic<State> state_{State::Empty};
};
}
