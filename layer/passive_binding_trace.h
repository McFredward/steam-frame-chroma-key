#pragma once

// Passive public-API metadata only. No mapped-memory reads, Vulkan calls,
// allocations, logging or I/O occur here. Snapshots describe observed CPU API
// state; neither a draw nor validation certifies GPU-consumed uniform values.
#include <vulkan/vulkan.h>
#include "passive_binding_extensions.h"
#include "passive_binding_table.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>

namespace frame_bindings {

constexpr std::size_t LayoutCapacity = 256;
constexpr std::size_t PoolCapacity = 256;
constexpr std::size_t SetCapacity = 512;
constexpr std::size_t BufferCapacity = 512;
constexpr std::size_t ImageCapacity = 512;
constexpr std::size_t ViewCapacity = 512;
constexpr std::size_t SamplerCapacity = 256;
constexpr std::size_t ConversionCapacity = 256;
constexpr std::size_t MemoryCapacity = 1024;
constexpr std::size_t CommandCapacity = 512;
constexpr std::size_t BindingCapacity = 32;
constexpr std::size_t SetIndexCapacity = 8;
constexpr std::array<std::uint32_t, 8> TargetBindings{{0, 2, 3, 4, 6, 9, 25, 31}};

template<class T> inline std::uint64_t handle(T value) noexcept {
    if constexpr (std::is_pointer_v<T>) return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
    else return static_cast<std::uint64_t>(value);
}

struct Identity {
    std::uint64_t handle = 0;
    std::uint64_t generation = 0;
};
inline bool same(Identity a, Identity b) noexcept {
    return a.handle == b.handle && a.generation == b.generation && a.generation != 0;
}

enum class Presence : std::uint8_t { Unknown, Absent, Present };
enum class BindingState : std::uint8_t { Unknown, Absent, Unwritten, Written };
enum class Reason : std::uint8_t { None, Capacity, Contention, Unknown, Unsupported, Stale, Bounds, LayoutMismatch };
enum class Table : std::uint8_t { SetLayout, PipelineLayout, Pool, Set, Buffer, Image, View, Sampler, Memory, Command, Conversion, Count };

struct MemoryMetadata {
    Identity identity{};
    VkDeviceSize allocationSize = 0;
    std::uint32_t memoryTypeIndex = 0;
    extensions::MemoryExtensions extensions{};
    Identity dedicatedImage{}, dedicatedBuffer{};
    bool complete = false;
};
struct BufferMetadata {
    Identity identity{};
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    Identity memory{};
    VkDeviceSize memoryOffset = 0;
    MemoryMetadata allocation{};
    bool complete = false;
};
struct ImageMetadata {
    Identity identity{};
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    std::uint32_t mipLevels = 0;
    std::uint32_t arrayLayers = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    extensions::ImageExtensions extensions{};
    Identity memory{};
    VkDeviceSize memoryOffset = 0;
    MemoryMetadata allocation{};
    bool complete = false;
};
struct ConversionMetadata {
    Identity identity{};
    extensions::ConversionExtensions extensions{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSamplerYcbcrModelConversion model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    VkSamplerYcbcrRange range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    VkComponentMapping components{};
    VkChromaLocation xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkChromaLocation yChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkFilter chromaFilter = VK_FILTER_NEAREST;
    VkBool32 forceExplicitReconstruction = VK_FALSE;
    bool complete = false;
};
struct ViewMetadata {
    Identity identity{};
    Identity image{};
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkComponentMapping components{};
    VkImageSubresourceRange subresource{};
    extensions::ViewExtensions extensions{};
    ConversionMetadata conversion{};
    bool complete = false;
};
struct SamplerMetadata {
    Identity identity{};
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkFilter minFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mipLodBias = 0;
    float minLod = 0;
    float maxLod = 0;
    float maxAnisotropy = 0;
    VkBool32 anisotropyEnable = VK_FALSE;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkBool32 unnormalizedCoordinates = VK_FALSE;
    extensions::SamplerExtensions extensions{};
    ConversionMetadata conversion{};
    bool complete = false;
};
struct Binding {
    std::uint32_t binding = 0;
    BindingState state = BindingState::Unknown;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t descriptorCount = 0;
    VkShaderStageFlags stages = 0;
    std::uint64_t revision = 0;
    VkDeviceSize bufferOffset = 0;
    VkDeviceSize bufferRange = 0;
    std::uint32_t dynamicOffset = 0;
    VkDeviceSize effectiveOffset = 0;
    VkDeviceSize effectiveRange = 0;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    BufferMetadata buffer{};
    ImageMetadata image{};
    ViewMetadata view{};
    SamplerMetadata sampler{};
    bool complete = false;
};
struct Snapshot {
    std::uint64_t device = 0;
    Identity command{};
    Identity pipelineLayout{};
    Identity set{};
    Identity setLayout{};
    std::uint64_t setRevision = 0;
    std::uint64_t commandBindingRevision = 0;
    Presence layout_has_binding31 = Presence::Unknown;
    Reason reason = Reason::Unknown;
    bool sourceComplete = false;
    // This remains false even after validate(): descriptor revisions cannot
    // prove memory contents, execution order or the associated video frame.
    bool gpuConsumedCoherent = false;
    std::array<Binding, TargetBindings.size()> bindings{};
};
static_assert(std::is_trivially_copyable_v<Snapshot>, "Snapshots must be safe bounded event payloads");
static_assert(sizeof(Snapshot) <= 16384, "Snapshot exceeds the bounded trace event budget");

// A receipt is an owned CPU lifecycle observation, not accepted native import,
// source video, cross-process namespace or GPU-consumption proof. The tag and
// cookie are supplied only after the adapter verifies the live native invocation.
constexpr std::size_t ImportTagCapacity = 32;
enum ImportReceiptError : std::uint32_t {
    ImportNone = 0, ImportUnknownTag = 1U << 0, ImportUntagged = 1U << 1,
    ImportTagMismatch = 1U << 2, ImportGenerationMismatch = 1U << 3,
    ImportMissingImage = 1U << 4, ImportMissingView = 1U << 5,
    ImportMissingMemory = 1U << 6, ImportMissingBinding = 1U << 7,
    ImportUnsupported = 1U << 8, ImportCapacity = 1U << 9,
    ImportJournalLoss = 1U << 10, ImportAmbiguousTag = 1U << 11,
    ImportRepeatedReceipt = 1U << 12
};
struct ImportResourceReceipt {
    std::uint64_t journalInstance = 0, localReplayOrder = 0;
    // The worker's existing resource-generation cursor. It is neither native
    // content generation nor a submit/source-frame counter.
    std::uint64_t resourceGenerationCursor = 0;
    std::uint64_t invocationTag = 0, cookie = 0, device = 0;
    std::uint64_t requestedImage = 0, requestedView = 0;
    std::uint64_t imageInvocationTag = 0, viewInvocationTag = 0;
    ImageMetadata image{};
    ViewMetadata view{};
    MemoryMetadata memory{};
    std::uint64_t lossCount = 0;
    std::uint32_t errors = ImportNone;
    Reason reason = Reason::Unknown;
    bool resourceComplete = false;
    bool nativeImportAccepted = false;
    bool sourceFrameAssociated = false;
    bool nativeCrossProcessNamespace = false;
    bool acceptedSubmit = false;
    bool gpuConsumedCoherent = false;
};
static_assert(std::is_trivially_copyable_v<ImportResourceReceipt>,
              "Import receipts must contain bounded owned metadata");
static_assert(sizeof(ImportResourceReceipt) < 4096, "Import receipt exceeds its bounded budget");

// Instantaneous owner-side samples, not an atomic lease on native resources.
enum class ImportReceiptValidationReason : std::uint8_t {
    Valid, Closed, Halted, JournalLoss, ActivePublication, PendingReplay,
    PublicationChanged, CoreGenerationMismatch
};
struct ImportReceiptValidation {
    bool valid = false, transient = false;
    ImportReceiptValidationReason reason = ImportReceiptValidationReason::CoreGenerationMismatch;
    std::uint64_t active_before = 0, active_after = 0;
    std::uint64_t expected_before = 0, expected_after = 0, tickets_before = 0, tickets_after = 0;
    std::uint64_t loss_before = 0, loss_after = 0;
    bool empty_before = false, empty_after = false, closed_before = false, closed_after = false;
    bool halted_before = false, halted_after = false;
};
class Core {
    friend struct TestAccess;
    struct Base { std::uint64_t device = 0; Identity id{}; bool complete = false; };
    struct LayoutBinding {
        std::uint32_t binding = 0;
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        std::uint32_t count = 0;
        VkShaderStageFlags stages = 0;
        Identity immutableSampler{};
    };
    struct SetLayout : Base {
        std::uint32_t count = 0;
        std::array<LayoutBinding, BindingCapacity> bindings{};
    };
    struct PipelineLayout : Base {
        std::uint32_t count = 0;
        std::array<Identity, SetIndexCapacity> sets{};
    };
    struct Pool : Base {};
    struct Value {
        Identity buffer{}, view{}, sampler{};
        VkDeviceSize offset = 0, range = 0;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::uint64_t revision = 0;
        bool written = false, complete = false;
    };
    struct Set : Base {
        Identity pool{}, layout{};
        std::uint64_t revision = 0;
        std::array<Value, BindingCapacity> values{};
    };
    struct Buffer : Base { BufferMetadata metadata{}; };
    struct Image : Base { ImageMetadata metadata{}; std::uint64_t invocationTag = 0; };
    struct View : Base { ViewMetadata metadata{}; std::uint64_t invocationTag = 0; };
    struct ImportTag {
        std::uint64_t tag = 0, device = 0;
        Identity image{}, view{};
        std::uint32_t errors = ImportNone;
        bool receiptRequested = false;
    };
    struct Sampler : Base { SamplerMetadata metadata{}; };
    struct Memory : Base { MemoryMetadata metadata{}; };
    struct Conversion : Base { ConversionMetadata metadata{}; };
    struct BoundSet {
        Identity set{};
        std::array<std::uint32_t, BindingCapacity> offsets{};
        bool complete = false;
    };
    struct Command : Base {
        std::uint64_t pool = 0;
        Identity layout{};
        std::uint64_t bindingRevision = 0;
        bool recording = false;
        std::array<BoundSet, SetIndexCapacity> sets{};
    };
public:
    explicit Core(std::uint64_t journalInstance = 0) noexcept : journalInstance_(journalInstance) {}
    // The caller owns capture admission/lifetime and invokes mutation methods
    // only after successful native API calls. Failed native calls must not
    // advance generations. Multi-device state is keyed by device AND handle.
    std::uint64_t lostState() const noexcept { return lost_.load(std::memory_order_relaxed); }
    std::uint64_t lossCount(Reason reason) const noexcept {
        const auto index = static_cast<std::size_t>(reason);
        return index < losses_.size() ? losses_[index].load(std::memory_order_relaxed) : 0;
    }
    std::uint64_t capacityLossCount(Table table) const noexcept {
        const auto index = static_cast<std::size_t>(table);
        return index < capacityLosses_.size() ? capacityLosses_[index].load(std::memory_order_relaxed) : 0;
    }
    void reportUnsupported(std::uint64_t /*device*/) noexcept { lose(Reason::Unsupported); }
    void reportLoss(Reason reason) noexcept {
        if (reason != Reason::None && static_cast<std::size_t>(reason) < losses_.size()) lose(reason);
    }

    void createSetLayout(std::uint64_t device, VkDescriptorSetLayout value, const VkDescriptorSetLayoutCreateInfo& info) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(layouts_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO && !info.pNext && info.flags == 0 &&
            info.bindingCount <= BindingCapacity && (!info.bindingCount || info.pBindings);
        if (!out->complete) return;
        out->count = info.bindingCount;
        for (std::uint32_t i = 0; i < info.bindingCount; ++i) {
            const auto& in = info.pBindings[i];
            auto& binding = out->bindings[i];
            binding.binding = in.binding; binding.type = in.descriptorType; binding.count = in.descriptorCount; binding.stages = in.stageFlags;
            if (in.descriptorCount != 1 || !supported(in.descriptorType)) out->complete = false;
            if (in.pImmutableSamplers && in.descriptorCount == 1) {
                if (in.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER && in.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) out->complete = false;
                const auto* sampler = find(samplers_, device, handle(in.pImmutableSamplers[0]));
                if (sampler && sampler->complete) binding.immutableSampler = sampler->id; else out->complete = false;
            }
        }
        // Small fixed-size insertion sort: Vulkan specifies ascending binding
        // order for dynamic offsets, independent of pBindings input order.
        for (std::uint32_t i = 1; i < out->count; ++i) {
            auto item = out->bindings[i]; auto j = i;
            while (j && out->bindings[j - 1].binding > item.binding) { out->bindings[j] = out->bindings[j - 1]; --j; }
            out->bindings[j] = item;
        }
        for (std::uint32_t i = 1; i < out->count; ++i) if (out->bindings[i - 1].binding == out->bindings[i].binding) out->complete = false;
    }
    void destroySetLayout(std::uint64_t device, VkDescriptorSetLayout value) noexcept { eraseGuarded(layouts_, device, handle(value)); }
    void createPipelineLayout(std::uint64_t device, VkPipelineLayout value, const VkPipelineLayoutCreateInfo& info) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(pipelineLayouts_, device, handle(value)); if (!out) return;
        // Exact pipeline-layout identity is required at bind/snapshot; no
        // structural equivalence is inferred from set layouts or push ranges.
        out->complete = info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO && !info.pNext && info.flags == 0 &&
            info.setLayoutCount <= SetIndexCapacity && (!info.setLayoutCount || info.pSetLayouts) &&
            (!info.pushConstantRangeCount || info.pPushConstantRanges);
        if (!out->complete) return;
        out->count = info.setLayoutCount;
        for (std::uint32_t i = 0; i < out->count; ++i) {
            const auto* layout = find(layouts_, device, handle(info.pSetLayouts[i]));
            if (layout && layout->complete) out->sets[i] = layout->id; else out->complete = false;
        }
    }
    void destroyPipelineLayout(std::uint64_t device, VkPipelineLayout value) noexcept { eraseGuarded(pipelineLayouts_, device, handle(value)); }
    void createPool(std::uint64_t device, VkDescriptorPool value, const VkDescriptorPoolCreateInfo& info) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(pools_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO && !info.pNext &&
            (info.flags & ~VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0;
    }
    void resetPool(std::uint64_t device, VkDescriptorPool value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        const auto* pool = find(pools_, device, handle(value));
        if (!pool) { lose(Reason::Unknown); return; }
        sets_.eraseIf([&](const Set& set) { return set.device == device && same(set.pool, pool->id); });
    }
    void destroyPool(std::uint64_t device, VkDescriptorPool value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        const auto* pool = find(pools_, device, handle(value));
        if (!pool) { if (handle(value)) lose(Reason::Unknown); return; }
        sets_.eraseIf([&](const Set& set) { return set.device == device && same(set.pool, pool->id); });
        erase(pools_, device, handle(value));
    }
    void allocateSets(std::uint64_t device, const VkDescriptorSetAllocateInfo& info, const VkDescriptorSet* values) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        if (info.descriptorSetCount > SetCapacity || (info.descriptorSetCount && (!values || !info.pSetLayouts))) { lose(Reason::Bounds); return; }
        const auto* pool = find(pools_, device, handle(info.descriptorPool));
        for (std::uint32_t i = 0; i < info.descriptorSetCount; ++i) {
            auto* out = insert(sets_, device, handle(values[i])); if (!out) continue;
            const auto* layout = find(layouts_, device, handle(info.pSetLayouts[i]));
            out->complete = info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO && !info.pNext && pool && pool->complete && layout && layout->complete;
            if (pool) out->pool = pool->id;
            if (layout) out->layout = layout->id;
            out->revision = next();
        }
    }
    void freeSets(std::uint64_t device, VkDescriptorPool value, std::uint32_t count, const VkDescriptorSet* values) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        if (count > SetCapacity || (count && !values)) { lose(Reason::Bounds); return; }
        const auto* pool = find(pools_, device, handle(value));
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* set = find(sets_, device, handle(values[i]));
            if (!pool || !set || !same(set->pool, pool->id)) { lose(Reason::Unknown); continue; }
            sets_.eraseEntry(*set);
        }
    }
    void updateSets(std::uint64_t device, std::uint32_t writeCount, const VkWriteDescriptorSet* writes,
                    std::uint32_t copyCount, const VkCopyDescriptorSet* copies) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        if (writeCount > SetCapacity || copyCount > SetCapacity || (writeCount && !writes) || (copyCount && !copies)) { lose(Reason::Bounds); return; }
        for (std::uint32_t i = 0; i < writeCount; ++i) write(device, writes[i]);
        // Vulkan processes all writes before all copies, in input order.
        for (std::uint32_t i = 0; i < copyCount; ++i) copy(device, copies[i]);
    }

    void allocateMemory(std::uint64_t device, VkDeviceMemory value, const VkMemoryAllocateInfo& info) noexcept {
        allocateMemory(device, value, info, extensions::parseMemory(info.pNext));
    }
    void allocateMemory(std::uint64_t device, VkDeviceMemory value, const VkMemoryAllocateInfo& info,
                        const extensions::MemoryExtensions& extension) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(memories_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO && extension.chain.complete;
        auto& m = out->metadata;
        m.identity = out->id; m.allocationSize = info.allocationSize; m.memoryTypeIndex = info.memoryTypeIndex; m.extensions = extension;
        if (extension.dedicatedImageHandle) {
            const auto* image = find(images_, device, extension.dedicatedImageHandle);
            if (image) m.dedicatedImage = image->id;
            if (!image || !image->complete) out->complete = false;
        }
        if (extension.dedicatedBufferHandle) {
            const auto* buffer = find(buffers_, device, extension.dedicatedBufferHandle);
            if (buffer) m.dedicatedBuffer = buffer->id;
            if (!buffer || !buffer->complete) out->complete = false;
        }
        m.complete = out->complete;
    }
    void freeMemory(std::uint64_t device, VkDeviceMemory value) noexcept { eraseGuarded(memories_, device, handle(value)); }
    void createBuffer(std::uint64_t device, VkBuffer value, const VkBufferCreateInfo& info) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(buffers_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && !info.pNext && info.flags == 0;
        out->metadata.identity = out->id; out->metadata.size = info.size; out->metadata.usage = info.usage;
    }
    void destroyBuffer(std::uint64_t device, VkBuffer value) noexcept { eraseGuarded(buffers_, device, handle(value)); }
    void createImage(std::uint64_t device, VkImage value, const VkImageCreateInfo& info) noexcept {
        createImage(device, value, info, extensions::parseImage(info.pNext));
    }
    void createImage(std::uint64_t device, VkImage value, const VkImageCreateInfo& info,
                     const extensions::ImageExtensions& extension, std::uint64_t invocationTag = 0) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(images_, device, handle(value)); if (!out) return;
        // A modifier list describes candidates, not the implementation's selected
        // modifier. Retain the input but reject full resource proof until queried.
        out->complete = info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO && extension.chain.complete && !extension.hasDrmList &&
            (info.flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_DISJOINT_BIT)) == 0;
        auto& m = out->metadata;
        m.identity = out->id; m.type = info.imageType; m.format = info.format; m.extent = info.extent; m.mipLevels = info.mipLevels;
        m.arrayLayers = info.arrayLayers; m.samples = info.samples; m.tiling = info.tiling; m.usage = info.usage; m.extensions = extension;
        out->invocationTag = invocationTag;
        if (invocationTag) {
            if (auto* tag = importTag(invocationTag, device, true)) {
                if (tag->image.generation || tag->view.generation) tag->errors |= ImportAmbiguousTag;
                else tag->image = out->id;
            }
        }
    }
    void destroyImage(std::uint64_t device, VkImage value) noexcept { eraseGuarded(images_, device, handle(value)); }
    void createView(std::uint64_t device, VkImageView value, const VkImageViewCreateInfo& info) noexcept {
        createView(device, value, info, extensions::parseView(info.pNext));
    }
    void createView(std::uint64_t device, VkImageView value, const VkImageViewCreateInfo& info,
                    const extensions::ViewExtensions& extension, std::uint64_t invocationTag = 0) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(views_, device, handle(value)); if (!out) return;
        const auto* image = find(images_, device, handle(info.image));
        out->complete = info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && extension.chain.complete && info.flags == 0 && image && image->complete;
        auto& m = out->metadata;
        m.identity = out->id; if (image) m.image = image->id;
        m.type = info.viewType; m.format = info.format; m.components = info.components; m.subresource = info.subresourceRange; m.extensions = extension;
        out->invocationTag = invocationTag;
        if (invocationTag) {
            if (auto* tag = importTag(invocationTag, device, true)) {
                if (tag->view.generation) tag->errors |= ImportAmbiguousTag;
                else tag->view = out->id;
                if (!image || !same(tag->image, image->id) || image->invocationTag != invocationTag)
                    tag->errors |= ImportTagMismatch;
            }
        }
        if (extension.hasConversion) {
            const auto* conversion = find(conversions_, device, extension.conversionHandle);
            if (conversion) m.conversion = conversion->metadata;
            if (!conversion || !conversion->complete) out->complete = false;
        }
    }
    void destroyView(std::uint64_t device, VkImageView value) noexcept { eraseGuarded(views_, device, handle(value)); }
    void createSampler(std::uint64_t device, VkSampler value, const VkSamplerCreateInfo& info) noexcept {
        createSampler(device, value, info, extensions::parseSampler(info.pNext));
    }
    void createSampler(std::uint64_t device, VkSampler value, const VkSamplerCreateInfo& info,
                       const extensions::SamplerExtensions& extension) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(samplers_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && extension.chain.complete && info.flags == 0;
        if ((info.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT || info.borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT) &&
            !extension.hasCustomBorder) out->complete = false;
        auto& m = out->metadata;
        m.identity = out->id; m.magFilter = info.magFilter; m.minFilter = info.minFilter; m.mipmapMode = info.mipmapMode;
        m.addressModeU = info.addressModeU; m.addressModeV = info.addressModeV; m.addressModeW = info.addressModeW;
        m.mipLodBias = info.mipLodBias; m.minLod = info.minLod; m.maxLod = info.maxLod; m.maxAnisotropy = info.maxAnisotropy;
        m.anisotropyEnable = info.anisotropyEnable; m.compareEnable = info.compareEnable; m.compareOp = info.compareOp;
        m.borderColor = info.borderColor; m.unnormalizedCoordinates = info.unnormalizedCoordinates; m.extensions = extension;
        if (extension.hasConversion) {
            const auto* conversion = find(conversions_, device, extension.conversionHandle);
            if (conversion) m.conversion = conversion->metadata;
            if (!conversion || !conversion->complete) out->complete = false;
        }
        m.complete = out->complete;
    }
    void destroySampler(std::uint64_t device, VkSampler value) noexcept { eraseGuarded(samplers_, device, handle(value)); }
    void createConversion(std::uint64_t device, VkSamplerYcbcrConversion value, const VkSamplerYcbcrConversionCreateInfo& info) noexcept {
        createConversion(device, value, info, extensions::parseConversion(info.pNext));
    }
    void createConversion(std::uint64_t device, VkSamplerYcbcrConversion value, const VkSamplerYcbcrConversionCreateInfo& info,
                          const extensions::ConversionExtensions& extension) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = insert(conversions_, device, handle(value)); if (!out) return;
        out->complete = info.sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO && extension.chain.complete;
        auto& m = out->metadata;
        m.identity = out->id; m.extensions = extension; m.format = info.format; m.model = info.ycbcrModel; m.range = info.ycbcrRange;
        m.components = info.components; m.xChromaOffset = info.xChromaOffset; m.yChromaOffset = info.yChromaOffset;
        m.chromaFilter = info.chromaFilter; m.forceExplicitReconstruction = info.forceExplicitReconstruction; m.complete = out->complete;
    }
    void destroyConversion(std::uint64_t device, VkSamplerYcbcrConversion value) noexcept { eraseGuarded(conversions_, device, handle(value)); }
    void bindBufferMemory(std::uint64_t device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset, bool supportedInfo = true) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(buffers_, device, handle(buffer)); const auto* allocation = find(memories_, device, handle(memory));
        if (!out) { lose(Reason::Unknown); return; }
        if (!allocation || !supportedInfo || out->metadata.memory.generation) out->complete = false;
        if (allocation && (allocation->metadata.dedicatedImage.generation ||
                           (allocation->metadata.dedicatedBuffer.generation && !same(allocation->metadata.dedicatedBuffer, out->id)))) out->complete = false;
        if (allocation) out->metadata.memory = allocation->id;
        out->metadata.memoryOffset = offset;
    }
    void bindImageMemory(std::uint64_t device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset, bool supportedInfo = true) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(images_, device, handle(image)); const auto* allocation = find(memories_, device, handle(memory));
        if (!out) { lose(Reason::Unknown); return; }
        if (!allocation || !supportedInfo || out->metadata.memory.generation) out->complete = false;
        if (allocation && (allocation->metadata.dedicatedBuffer.generation ||
                           (allocation->metadata.dedicatedImage.generation && !same(allocation->metadata.dedicatedImage, out->id)))) out->complete = false;
        if (allocation) out->metadata.memory = allocation->id;
        out->metadata.memoryOffset = offset;
    }

    void allocateCommands(std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        if (count > CommandCapacity || (count && !values)) { lose(Reason::Bounds); return; }
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* out = insert(commands_, device, handle(values[i]));
            if (out) { out->pool = handle(pool); out->complete = true; }
        }
    }
    void freeCommands(std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        if (count > CommandCapacity || (count && !values)) { lose(Reason::Bounds); return; }
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* out = find(commands_, device, handle(values[i]));
            if (!out || out->pool != handle(pool)) { lose(Reason::Unknown); continue; }
            commands_.eraseEntry(*out);
        }
    }
    void beginCommand(std::uint64_t device, VkCommandBuffer value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(commands_, device, handle(value)); if (!out) { lose(Reason::Unknown); return; }
        const auto pool = out->pool; const auto command = out->id.handle;
        *out = {}; out->device = device; out->id = {command, next()}; out->pool = pool; out->complete = true; out->recording = true;
    }
    void endCommand(std::uint64_t device, VkCommandBuffer value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(commands_, device, handle(value));
        if (!out || !out->recording) { lose(Reason::Unknown); return; }
        out->recording = false;
    }
    void resetCommand(std::uint64_t device, VkCommandBuffer value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(commands_, device, handle(value)); if (!out) { lose(Reason::Unknown); return; }
        clearCommand(*out);
    }
    void resetCommandPool(std::uint64_t device, VkCommandPool pool) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        for (auto& command : commands_) if (command.device == device && command.id.generation && command.pool == handle(pool)) clearCommand(command);
    }
    void destroyCommandPool(std::uint64_t device, VkCommandPool pool) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        commands_.eraseIf([&](const Command& command) { return command.device == device && command.pool == handle(pool); });
    }
    void executeCommands(std::uint64_t device, VkCommandBuffer primary) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* out = find(commands_, device, handle(primary));
        if (!out || !out->recording) { lose(Reason::Unknown); return; }
        // Ordinary secondary execution leaves primary graphics state undefined.
        // Secondary CB metadata remains independent; a primary must rebind.
        out->layout = {}; out->sets = {}; out->bindingRevision = next();
    }
    void bindSets(std::uint64_t device, VkCommandBuffer value, VkPipelineBindPoint bindPoint, VkPipelineLayout layoutValue,
                  std::uint32_t firstSet, std::uint32_t count, const VkDescriptorSet* values,
                  std::uint32_t dynamicCount, const std::uint32_t* offsets) noexcept {
        // Compute state cannot disturb graphics descriptor bindings.
        if (bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
        auto lock = acquire(); if (!lock.owns_lock()) return;
        auto* command = find(commands_, device, handle(value));
        if (!command || !command->recording) { lose(Reason::Unknown); return; }
        command->bindingRevision = next();
        const auto* layout = find(pipelineLayouts_, device, handle(layoutValue));
        if (!layout || !layout->complete || firstSet >= SetIndexCapacity || count > SetIndexCapacity - firstSet ||
            count > layout->count || firstSet > layout->count - count || (count && !values) ||
            dynamicCount > SetIndexCapacity * BindingCapacity || (dynamicCount && !offsets)) {
            command->complete = false; command->sets = {}; return;
        }
        if (!same(command->layout, layout->id)) {
            // Layout compatibility includes push constants/independent sets;
            // this intentionally requires the exact same layout generation.
            command->sets = {};
        }
        command->layout = layout->id;
        std::uint32_t dynamicIndex = 0;
        bool valid = true;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& bound = command->sets[firstSet + i]; bound = {};
            const auto* set = find(sets_, device, handle(values[i]));
            const auto* setLayout = set ? lookup(layouts_, device, set->layout) : nullptr;
            if (!set || !set->complete || !setLayout || !setLayout->complete || !same(set->layout, layout->sets[firstSet + i])) { valid = false; continue; }
            bound.set = set->id; bound.complete = true;
            for (std::uint32_t j = 0; j < setLayout->count; ++j) if (dynamic(setLayout->bindings[j].type)) {
                if (dynamicIndex < dynamicCount) bound.offsets[j] = offsets[dynamicIndex++];
                else { valid = false; bound.complete = false; }
            }
        }
        if (!valid || dynamicIndex != dynamicCount) { command->complete = false; command->sets = {}; }
    }
    Snapshot snapshot(std::uint64_t device, VkCommandBuffer commandValue, VkPipelineLayout layoutValue) noexcept {
        auto lock = acquire();
        if (!lock.owns_lock()) { Snapshot out; out.device = device; out.reason = Reason::Contention; return out; }
        return snapshotLocked(device, handle(commandValue), handle(layoutValue));
    }
    bool validate(const Snapshot& previous) noexcept {
        auto lock = acquire(); if (!lock.owns_lock() || !previous.sourceComplete || previous.gpuConsumedCoherent) return false;
        const auto current = snapshotLocked(previous.device, previous.command.handle, previous.pipelineLayout.handle);
        // Whole-set revisions deliberately reject even unrelated later writes.
        // Resource generations are also rechecked by snapshotLocked.
        return current.sourceComplete && same(previous.command, current.command) && same(previous.pipelineLayout, current.pipelineLayout) &&
            same(previous.set, current.set) && same(previous.setLayout, current.setLayout) && previous.setRevision == current.setRevision &&
            previous.commandBindingRevision == current.commandBindingRevision;
    }
    // Only the journal's background owner calls this lookup. Numeric handles
    // select candidate entries, but original tagged creation generations are
    // independently required; no latest-handle/fallback association is accepted.
    ImportResourceReceipt importResourceReceipt(std::uint64_t device, VkImage image,
                                                 VkImageView view, std::uint64_t invocationTag) noexcept {
        auto lock = acquire();
        ImportResourceReceipt out;
        out.device = device; out.requestedImage = handle(image); out.requestedView = handle(view);
        out.invocationTag = invocationTag;
        if (!lock.owns_lock()) { out.errors = ImportJournalLoss; out.reason = Reason::Contention; return out; }
        out = importResourceReceiptLocked(device, handle(image), handle(view), invocationTag);
        if (auto* tag = importTag(invocationTag, device, false)) {
            if (tag->receiptRequested) {
                out.errors |= ImportRepeatedReceipt; out.resourceComplete = false; out.reason = Reason::Stale;
            }
            tag->receiptRequested = true;
        }
        return out;
    }
    bool validate(const ImportResourceReceipt& previous) noexcept {
        auto lock = acquire();
        if (!lock.owns_lock() || previous.journalInstance != journalInstance_ ||
            previous.resourceGenerationCursor > serial_ ||
            previous.resourceGenerationCursor < previous.image.identity.generation ||
            previous.resourceGenerationCursor < previous.view.identity.generation ||
            previous.resourceGenerationCursor < previous.memory.identity.generation ||
            !previous.resourceComplete || previous.nativeImportAccepted ||
            previous.sourceFrameAssociated || previous.nativeCrossProcessNamespace ||
            previous.acceptedSubmit || previous.gpuConsumedCoherent) return false;
        const auto current = importResourceReceiptLocked(previous.device, previous.requestedImage,
                                                        previous.requestedView, previous.invocationTag);
        return current.resourceComplete && same(previous.image.identity, current.image.identity) &&
            same(previous.view.identity, current.view.identity) && same(previous.memory.identity, current.memory.identity) &&
            same(previous.image.memory, current.image.memory) && previous.image.memoryOffset == current.image.memoryOffset;
    }
    void deviceDestroyed(std::uint64_t device) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        eraseDevice(layouts_, device); eraseDevice(pipelineLayouts_, device); eraseDevice(pools_, device); eraseDevice(sets_, device);
        eraseDevice(buffers_, device); eraseDevice(images_, device); eraseDevice(views_, device); eraseDevice(samplers_, device);
        eraseDevice(memories_, device); eraseDevice(commands_, device); eraseDevice(conversions_, device);
    }
private:
    ImportTag* importTag(std::uint64_t value, std::uint64_t device, bool create) noexcept {
        if (!value) return nullptr;
        ImportTag* available = nullptr;
        for (auto& tag : importTags_) {
            if (tag.tag == value) {
                if (tag.device != device) tag.errors |= ImportTagMismatch | ImportAmbiguousTag;
                return &tag;
            }
            if (!tag.tag && !available) available = &tag;
        }
        if (!create) return nullptr;
        if (!available) { lose(Reason::Capacity); return nullptr; }
        available->tag = value; available->device = device;
        return available;
    }
    ImportResourceReceipt importResourceReceiptLocked(std::uint64_t device, std::uint64_t imageHandle,
                                                       std::uint64_t viewHandle, std::uint64_t invocationTag) noexcept {
        ImportResourceReceipt out;
        out.device = device; out.requestedImage = imageHandle; out.requestedView = viewHandle;
        out.invocationTag = invocationTag; out.resourceGenerationCursor = serial_;
        out.journalInstance = journalInstance_;
        const auto* tag = importTag(invocationTag, device, false);
        if (!tag) out.errors |= ImportUnknownTag;
        if (!invocationTag) out.errors |= ImportUntagged;
        if (tag) out.errors |= tag->errors;
        const auto* image = find(images_, device, imageHandle);
        const auto* view = find(views_, device, viewHandle);
        if (!image) out.errors |= ImportMissingImage;
        if (!view) out.errors |= ImportMissingView;
        if (image) {
            out.image = image->metadata; out.imageInvocationTag = image->invocationTag;
            if (!image->invocationTag) out.errors |= ImportUntagged;
            if (image->invocationTag != invocationTag) out.errors |= ImportTagMismatch;
            if (!tag || !same(tag->image, image->id)) out.errors |= ImportGenerationMismatch;
            if (!image->complete) out.errors |= ImportUnsupported;
            if (!image->metadata.memory.generation) out.errors |= ImportMissingBinding;
            out.memory = allocation(device, image->metadata.memory);
            if (!out.memory.identity.generation) out.errors |= ImportMissingMemory;
            if (!out.memory.complete || image->metadata.memoryOffset >= out.memory.allocationSize)
                out.errors |= ImportUnsupported;
            out.image.allocation = out.memory;
            out.image.complete = image->complete && out.memory.complete &&
                image->metadata.memoryOffset < out.memory.allocationSize;
        }
        if (view) {
            out.view = view->metadata; out.viewInvocationTag = view->invocationTag;
            if (!view->invocationTag) out.errors |= ImportUntagged;
            if (view->invocationTag != invocationTag) out.errors |= ImportTagMismatch;
            if (!tag || !same(tag->view, view->id) || !image || !same(view->metadata.image, image->id))
                out.errors |= ImportGenerationMismatch;
            out.view.complete = view->complete;
            if (out.view.extensions.hasConversion) {
                const auto* conversion = lookup(conversions_, device, out.view.conversion.identity);
                out.view.conversion.complete = conversion && conversion->complete;
                out.view.complete = out.view.complete && out.view.conversion.complete;
            }
            if (!out.view.complete) out.errors |= ImportUnsupported;
        }
        out.lossCount = lostState();
        if (out.lossCount) {
            out.errors |= ImportJournalLoss;
            if (lastLoss_.load(std::memory_order_relaxed) == Reason::Capacity) out.errors |= ImportCapacity;
        }
        out.resourceComplete = out.errors == ImportNone;
        out.reason = out.resourceComplete ? Reason::None :
            out.lossCount ? lastLoss_.load(std::memory_order_relaxed) :
            out.errors & ImportUnsupported ? Reason::Unsupported : Reason::Stale;
        return out;
    }
    static bool dynamic(VkDescriptorType type) noexcept { return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC; }
    static bool bufferType(VkDescriptorType type) noexcept {
        return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || dynamic(type);
    }
    static bool imageType(VkDescriptorType type) noexcept {
        return type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    static bool samplerType(VkDescriptorType type) noexcept { return type == VK_DESCRIPTOR_TYPE_SAMPLER || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; }
    static bool supported(VkDescriptorType type) noexcept { return bufferType(type) || imageType(type) || samplerType(type); }
    void lose(Reason reason) noexcept {
        lost_.fetch_add(1, std::memory_order_relaxed); lastLoss_.store(reason, std::memory_order_relaxed);
        losses_[static_cast<std::size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
    }
    std::unique_lock<std::mutex> acquire() noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) lose(Reason::Contention);
        return lock;
    }
    std::uint64_t next() noexcept {
        if (serial_ == std::numeric_limits<std::uint64_t>::max()) { lose(Reason::Capacity); return 0; }
        return ++serial_;
    }
    template<class T, std::size_t N> static T* find(ObjectTable<T, N>& table, std::uint64_t device, std::uint64_t value) noexcept {
        return table.find(device, value);
    }
    template<class T, std::size_t N> static const T* lookup(ObjectTable<T, N>& table, std::uint64_t device, Identity id) noexcept {
        const auto* entry = find(table, device, id.handle); return entry && same(entry->id, id) ? entry : nullptr;
    }
    template<class T, std::size_t N> T* insert(ObjectTable<T, N>& table, std::uint64_t device, std::uint64_t value) noexcept {
        if (!value) { lose(Reason::Unknown); return nullptr; }
        if (find(table, device, value)) { lose(Reason::Stale); return nullptr; }
        if (table.hasRoom()) {
            const auto generation = next();
            if (!generation) return nullptr;
            auto* entry = table.insert(device, value, generation);
            if (!entry) lose(Reason::Unknown);
            return entry;
        }
        constexpr auto kind = std::is_same_v<T, SetLayout> ? Table::SetLayout :
            std::is_same_v<T, PipelineLayout> ? Table::PipelineLayout : std::is_same_v<T, Pool> ? Table::Pool :
            std::is_same_v<T, Set> ? Table::Set : std::is_same_v<T, Buffer> ? Table::Buffer :
            std::is_same_v<T, Image> ? Table::Image : std::is_same_v<T, View> ? Table::View :
            std::is_same_v<T, Sampler> ? Table::Sampler : std::is_same_v<T, Memory> ? Table::Memory :
            std::is_same_v<T, Conversion> ? Table::Conversion : Table::Command;
        capacityLosses_[static_cast<std::size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
        lose(Reason::Capacity); return nullptr;
    }
    template<class T, std::size_t N> void erase(ObjectTable<T, N>& table, std::uint64_t device, std::uint64_t value) noexcept {
        auto* entry = find(table, device, value);
        if (entry) table.eraseEntry(*entry); else if (value) lose(Reason::Unknown);
    }
    template<class T, std::size_t N> void eraseGuarded(ObjectTable<T, N>& table, std::uint64_t device, std::uint64_t value) noexcept {
        auto lock = acquire(); if (lock.owns_lock()) erase(table, device, value);
    }
    template<class T, std::size_t N> static void eraseDevice(ObjectTable<T, N>& table, std::uint64_t device) noexcept {
        table.eraseIf([&](const T& entry) { return entry.device == device; });
    }
    void clearCommand(Command& out) noexcept {
        const auto device = out.device; const auto pool = out.pool; const auto value = out.id.handle;
        out = {}; out.device = device; out.pool = pool; out.id = {value, next()}; out.complete = true;
    }
    static std::uint32_t index(const SetLayout& layout, std::uint32_t binding) noexcept {
        for (std::uint32_t i = 0; i < layout.count; ++i) if (layout.bindings[i].binding == binding) return i;
        return static_cast<std::uint32_t>(BindingCapacity);
    }
    void write(std::uint64_t device, const VkWriteDescriptorSet& info) noexcept {
        auto* set = find(sets_, device, handle(info.dstSet)); if (!set) { lose(Reason::Unknown); return; }
        set->revision = next();
        const auto* layout = lookup(layouts_, device, set->layout);
        if (!layout || !layout->complete || info.sType != VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET || info.pNext ||
            info.descriptorCount != 1 || info.dstArrayElement != 0) { set->complete = false; return; }
        const auto pos = index(*layout, info.dstBinding);
        if (pos == BindingCapacity || layout->bindings[pos].type != info.descriptorType) { set->complete = false; return; }
        auto& out = set->values[pos]; out = {}; out.written = true; out.revision = set->revision; out.complete = true;
        const auto& definition = layout->bindings[pos];
        if (bufferType(info.descriptorType)) {
            if (!info.pBufferInfo) { out.complete = false; return; }
            const auto* buffer = find(buffers_, device, handle(info.pBufferInfo[0].buffer));
            if (buffer) out.buffer = buffer->id;
            if (!buffer || !buffer->complete) out.complete = false;
            out.offset = info.pBufferInfo[0].offset; out.range = info.pBufferInfo[0].range;
        }
        if (imageType(info.descriptorType)) {
            if (!info.pImageInfo) { out.complete = false; return; }
            const auto* view = find(views_, device, handle(info.pImageInfo[0].imageView));
            if (view) out.view = view->id;
            if (!view || !view->complete) out.complete = false;
            out.imageLayout = info.pImageInfo[0].imageLayout;
        }
        if (samplerType(info.descriptorType)) {
            if (definition.immutableSampler.generation) out.sampler = definition.immutableSampler;
            else if (info.pImageInfo) {
                const auto* sampler = find(samplers_, device, handle(info.pImageInfo[0].sampler));
                if (sampler) out.sampler = sampler->id;
                if (!sampler || !sampler->complete) out.complete = false;
            } else out.complete = false;
        }
    }
    void copy(std::uint64_t device, const VkCopyDescriptorSet& info) noexcept {
        auto* destination = find(sets_, device, handle(info.dstSet)); if (!destination) { lose(Reason::Unknown); return; }
        destination->revision = next();
        const auto* source = find(sets_, device, handle(info.srcSet));
        const auto* sourceLayout = source ? lookup(layouts_, device, source->layout) : nullptr;
        const auto* destinationLayout = lookup(layouts_, device, destination->layout);
        if (info.sType != VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET || info.pNext || info.descriptorCount != 1 ||
            info.srcArrayElement || info.dstArrayElement || !source || !source->complete || !sourceLayout || !sourceLayout->complete ||
            !destinationLayout || !destinationLayout->complete) { destination->complete = false; return; }
        const auto src = index(*sourceLayout, info.srcBinding), dst = index(*destinationLayout, info.dstBinding);
        if (src == BindingCapacity || dst == BindingCapacity || sourceLayout->bindings[src].type != destinationLayout->bindings[dst].type ||
            !source->values[src].written || !source->values[src].complete) { destination->complete = false; return; }
        auto replacement = source->values[src]; replacement.revision = destination->revision;
        if (destinationLayout->bindings[dst].immutableSampler.generation) replacement.sampler = destinationLayout->bindings[dst].immutableSampler;
        destination->values[dst] = replacement;
    }
    MemoryMetadata allocation(std::uint64_t device, Identity id) noexcept {
        const auto* memory = lookup(memories_, device, id);
        if (!memory) return {};
        auto out = memory->metadata;
        if (out.dedicatedImage.generation && !lookup(images_, device, out.dedicatedImage)) out.complete = false;
        if (out.dedicatedBuffer.generation && !lookup(buffers_, device, out.dedicatedBuffer)) out.complete = false;
        return out;
    }
    Binding binding(std::uint64_t device, const Set& set, const SetLayout& layout, const BoundSet& bound, std::uint32_t value) noexcept {
        Binding out; out.binding = value;
        const auto pos = index(layout, value);
        if (pos == BindingCapacity) { out.state = BindingState::Absent; out.complete = true; return out; }
        const auto& definition = layout.bindings[pos]; const auto& source = set.values[pos];
        out.type = definition.type; out.descriptorCount = definition.count; out.stages = definition.stages; out.revision = source.revision;
        if (!source.written && !(definition.type == VK_DESCRIPTOR_TYPE_SAMPLER && definition.immutableSampler.generation)) { out.state = BindingState::Unwritten; return out; }
        out.state = BindingState::Written;
        out.complete = source.written ? source.complete : true;
        out.bufferOffset = source.offset; out.bufferRange = source.range; out.dynamicOffset = bound.offsets[pos]; out.imageLayout = source.imageLayout;
        if (bufferType(definition.type)) {
            const auto* buffer = lookup(buffers_, device, source.buffer);
            if (buffer) {
                out.buffer = buffer->metadata; out.buffer.allocation = allocation(device, out.buffer.memory);
                out.buffer.complete = buffer->complete && out.buffer.allocation.complete && out.buffer.memoryOffset <= out.buffer.allocation.allocationSize &&
                    out.buffer.size <= out.buffer.allocation.allocationSize - out.buffer.memoryOffset;
                out.effectiveOffset = source.offset;
                const auto max = std::numeric_limits<VkDeviceSize>::max();
                if (source.offset > max - out.dynamicOffset) out.complete = false;
                else out.effectiveOffset += out.dynamicOffset;
                if (source.range == VK_WHOLE_SIZE) {
                    if (source.offset <= out.buffer.size) out.effectiveRange = out.buffer.size - source.offset; else out.complete = false;
                    if (out.dynamicOffset) out.complete = false;
                } else out.effectiveRange = source.range;
                out.complete = out.complete && out.buffer.complete && out.effectiveRange != 0 && out.effectiveOffset <= out.buffer.size &&
                    out.effectiveRange <= out.buffer.size - out.effectiveOffset;
            } else out.complete = false;
        }
        if (imageType(definition.type)) {
            const auto* view = lookup(views_, device, source.view);
            const auto* image = view ? lookup(images_, device, view->metadata.image) : nullptr;
            if (view) {
                out.view = view->metadata; out.view.complete = view->complete;
                if (out.view.extensions.hasConversion) {
                    const auto* conversion = lookup(conversions_, device, out.view.conversion.identity);
                    out.view.conversion.complete = conversion && conversion->complete;
                    out.view.complete = out.view.complete && out.view.conversion.complete;
                }
            }
            if (image) {
                out.image = image->metadata;
                out.image.allocation = allocation(device, out.image.memory); out.image.complete = out.image.allocation.complete &&
                    image->complete && out.image.memoryOffset < out.image.allocation.allocationSize;
                out.complete = out.complete && out.view.complete && out.image.complete;
            } else out.complete = false;
        }
        if (samplerType(definition.type)) {
            const auto identity = definition.immutableSampler.generation ? definition.immutableSampler : source.sampler;
            const auto* sampler = lookup(samplers_, device, identity);
            if (sampler) {
                out.sampler = sampler->metadata;
                if (out.sampler.extensions.hasConversion) {
                    const auto* conversion = lookup(conversions_, device, out.sampler.conversion.identity);
                    out.sampler.conversion.complete = conversion && conversion->complete;
                    out.sampler.complete = out.sampler.complete && out.sampler.conversion.complete;
                }
            }
            if (!sampler || !sampler->complete || !out.sampler.complete) out.complete = false;
        }
        if (definition.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            (out.view.extensions.hasConversion || out.sampler.extensions.hasConversion) &&
            !same(out.view.conversion.identity, out.sampler.conversion.identity)) out.complete = false;
        return out;
    }
    Snapshot snapshotLocked(std::uint64_t device, std::uint64_t commandValue, std::uint64_t layoutValue) noexcept {
        Snapshot out; out.device = device;
        for (std::size_t i = 0; i < TargetBindings.size(); ++i) out.bindings[i].binding = TargetBindings[i];
        const auto* command = find(commands_, device, commandValue);
        const auto* layout = find(pipelineLayouts_, device, layoutValue);
        if (command) out.command = command->id;
        if (layout) out.pipelineLayout = layout->id;
        const auto* knownSetLayout = layout && layout->count ? lookup(layouts_, device, layout->sets[0]) : nullptr;
        if (layout && layout->complete && knownSetLayout && knownSetLayout->complete && !lostState()) {
            out.layout_has_binding31 = index(*knownSetLayout, 31) == BindingCapacity ? Presence::Absent : Presence::Present;
        }
        if (!command || !layout || !command->complete || !layout->complete || !layout->count) return out;
        if (!same(command->layout, layout->id)) { out.reason = Reason::LayoutMismatch; return out; }
        const auto& bound = command->sets[0];
        const auto* set = lookup(sets_, device, bound.set);
        const auto* setLayout = lookup(layouts_, device, layout->sets[0]);
        if (!set || !set->complete || !setLayout || !setLayout->complete || !bound.complete || !same(set->layout, setLayout->id) ||
            !lookup(pools_, device, set->pool)) return out;
        out.set = set->id; out.setLayout = setLayout->id; out.setRevision = set->revision;
        out.commandBindingRevision = command->bindingRevision;
        out.sourceComplete = true; out.reason = Reason::None;
        for (std::size_t i = 0; i < TargetBindings.size(); ++i) {
            out.bindings[i] = binding(device, *set, *setLayout, bound, TargetBindings[i]);
            if (!out.bindings[i].complete) { out.sourceComplete = false; out.reason = Reason::Stale; }
        }
        if (lostState()) { out.sourceComplete = false; out.reason = lastLoss_.load(std::memory_order_relaxed); }
        return out;
    }
    const std::uint64_t journalInstance_;
    std::mutex mutex_;
    std::atomic<std::uint64_t> lost_{0};
    std::array<std::atomic<std::uint64_t>, 8> losses_{};
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Table::Count)> capacityLosses_{};
    std::atomic<Reason> lastLoss_{Reason::None};
    std::uint64_t serial_ = 0;
    ObjectTable<SetLayout, LayoutCapacity> layouts_{};
    ObjectTable<PipelineLayout, LayoutCapacity> pipelineLayouts_{};
    ObjectTable<Pool, PoolCapacity> pools_{};
    ObjectTable<Set, SetCapacity> sets_{};
    ObjectTable<Buffer, BufferCapacity> buffers_{};
    ObjectTable<Image, ImageCapacity> images_{};
    ObjectTable<View, ViewCapacity> views_{};
    ObjectTable<Sampler, SamplerCapacity> samplers_{};
    ObjectTable<Memory, MemoryCapacity> memories_{};
    ObjectTable<Command, CommandCapacity> commands_{};
    ObjectTable<Conversion, ConversionCapacity> conversions_{};
    std::array<ImportTag, ImportTagCapacity> importTags_{};
};

} // namespace frame_bindings
