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
    VkImageCreateFlags flags = 0;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
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

// Optional uniform-companion admission only. These fixed scalar refusals do not
// alter ordinary snapshots, public buffer metadata or the existing trace schema.
enum class UniformResourceReason : std::uint8_t {
    None, CoreUnavailable, SnapshotIncomplete, SnapshotChanged, BindingMissing,
    BindingDuplicate, Descriptor, BindingChanged, BufferStale, Sharing,
    MemoryStale, UnsupportedMemory, SourceUsage, SourceBounds, BufferAlias, UnknownAllocationAliasSpan,
    AmbiguousBufferAlias, ImageAlias, AmbiguousImageAlias,
};
struct UniformResourceRefusal {
    UniformResourceReason reason = UniformResourceReason::None;
    std::uint32_t binding = UINT32_MAX;
};
inline const char* uniformResourceReasonName(UniformResourceReason reason) noexcept {
    switch (reason) {
    case UniformResourceReason::None: return "none";
    case UniformResourceReason::CoreUnavailable: return "core_unavailable";
    case UniformResourceReason::SnapshotIncomplete: return "snapshot_incomplete";
    case UniformResourceReason::SnapshotChanged: return "snapshot_changed";
    case UniformResourceReason::BindingMissing: return "binding_missing";
    case UniformResourceReason::BindingDuplicate: return "binding_duplicate";
    case UniformResourceReason::Descriptor: return "descriptor";
    case UniformResourceReason::BindingChanged: return "binding_changed";
    case UniformResourceReason::BufferStale: return "buffer_stale";
    case UniformResourceReason::Sharing: return "sharing";
    case UniformResourceReason::MemoryStale: return "memory_stale";
    case UniformResourceReason::UnsupportedMemory: return "unsupported_memory";
    case UniformResourceReason::SourceUsage: return "source_usage";
    case UniformResourceReason::SourceBounds: return "source_bounds";
    case UniformResourceReason::BufferAlias: return "buffer_alias";
    case UniformResourceReason::UnknownAllocationAliasSpan: return "unknown_allocation_alias_span";
    case UniformResourceReason::AmbiguousBufferAlias: return "ambiguous_buffer_alias";
    case UniformResourceReason::ImageAlias: return "image_alias";
    case UniformResourceReason::AmbiguousImageAlias: return "ambiguous_image_alias";
    }
    return "unknown_uniform_resource_reason";
}

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

enum class TargetProbeOutcome : std::uint8_t { ExactMatch, Filtered, Unknown, QuotaOmitted };
// Refusal reasons describe only the existing CPU metadata gates. In particular,
// DifferentViewSameImage is diagnostic evidence, never an exact association.
enum class TargetProbeReason : std::uint8_t {
    None, ExactMatch, JournalAborted, JournalRestoreFailed, JournalLoss,
    JournalTargetMissing, JournalTargetTagMismatch, JournalTargetCookieMismatch,
    JournalTargetDeviceMismatch, CoreOwnerUnavailable, CoreLoss, TargetIncomplete,
    TargetDeviceMismatch, TargetJournalMismatch, TargetCurrentIncomplete,
    TargetImageStale, TargetViewStale, TargetMemoryStale, TargetBindingStale,
    TargetOffsetMismatch, CommandAbsent, LayoutAbsent, CommandIncomplete,
    LayoutIncomplete, LayoutEmpty, CommandLayoutMismatch, BoundSetIncomplete,
    SetAbsentOrStale, SetIncomplete, SetLayoutAbsentOrStale, SetLayoutIncomplete,
    SetLayoutMismatch, PoolAbsentOrStale, DescriptorUnwritten, DescriptorIncomplete,
    ViewAbsentOrStale, ImageAbsentOrStale, ViewIncomplete, ImageIncomplete,
    DifferentViewSameImage, DifferentImage, MemoryMismatch, ExactBindingIncomplete,
    NoSupportedImageBinding, NoMatch, SnapshotQuota, Count
};
constexpr std::uint64_t targetProbeReasonBit(TargetProbeReason reason) noexcept {
    return UINT64_C(1) << static_cast<unsigned>(reason);
}
inline const char* targetProbeReasonName(TargetProbeReason reason) noexcept {
    static constexpr std::array<const char*, static_cast<std::size_t>(TargetProbeReason::Count)> names{{
        "none", "exact_match", "journal_aborted", "journal_restore_failed", "journal_loss",
        "journal_target_missing", "journal_target_tag_mismatch", "journal_target_cookie_mismatch",
        "journal_target_device_mismatch", "core_owner_unavailable", "core_loss", "target_incomplete",
        "target_device_mismatch", "target_journal_mismatch", "target_current_incomplete",
        "target_image_stale", "target_view_stale", "target_memory_stale", "target_binding_stale",
        "target_offset_mismatch", "command_absent", "layout_absent", "command_incomplete",
        "layout_incomplete", "layout_empty", "command_layout_mismatch", "bound_set_incomplete",
        "set_absent_or_stale", "set_incomplete", "set_layout_absent_or_stale", "set_layout_incomplete",
        "set_layout_mismatch", "pool_absent_or_stale", "descriptor_unwritten", "descriptor_incomplete",
        "view_absent_or_stale", "image_absent_or_stale", "view_incomplete", "image_incomplete",
        "different_view_same_image", "different_image", "memory_mismatch", "exact_binding_incomplete",
        "no_supported_image_binding", "no_match", "snapshot_quota"
    }};
    const auto index = static_cast<std::size_t>(reason);
    return index < names.size() ? names[index] : "invalid";
}
enum TargetProbeObservation : std::uint32_t {
    ProbeJournalTargetObserved = 1U << 0, ProbeCurrentTargetObserved = 1U << 1,
    ProbeCommandObserved = 1U << 2, ProbeLayoutObserved = 1U << 3,
    ProbeBoundSetObserved = 1U << 4, ProbeSetObserved = 1U << 5,
    ProbeSetLayoutObserved = 1U << 6, ProbeDescriptorObserved = 1U << 7,
    ProbeViewObserved = 1U << 8, ProbeImageObserved = 1U << 9
};
enum DescriptorStateObservation : std::uint32_t {
    DescriptorWriteComplete = 1U << 0, DescriptorCurrentViewComplete = 1U << 1,
    DescriptorCurrentImageComplete = 1U << 2, DescriptorConversionRequested = 1U << 3,
    DescriptorConversionObserved = 1U << 4, DescriptorConversionComplete = 1U << 5,
    DescriptorViewChainComplete = 1U << 6, DescriptorImageChainComplete = 1U << 7,
    DescriptorImageHasDrmList = 1U << 8
};
enum class TrackedResourceKind : std::uint8_t { Image, View, Memory };
struct TargetProbeDiagnostics {
    TargetProbeOutcome outcome = TargetProbeOutcome::Unknown;
    TargetProbeReason reason = TargetProbeReason::None;
    // Contains all encountered binding refusals, not just the decisive reason.
    std::uint64_t reasonMask = 0;
    std::uint64_t requestedCommand = 0, requestedLayout = 0;
    std::uint64_t observedTargetTag = 0, observedTargetCookie = 0, observedTargetDevice = 0;
    Identity command{}, layout{}, commandLayout{}, boundSet{}, set{}, setLayout{}, setDeclaredLayout{}, pool{};
    Identity currentTargetImage{}, currentTargetView{}, currentTargetMemory{};
    Identity descriptorView{}, candidateView{}, candidateImage{}, candidateMemory{};
    std::uint32_t binding = std::numeric_limits<std::uint32_t>::max(), examinedImageBindings = 0;
    std::uint32_t currentTargetErrors = 0;
    std::uint32_t observations = 0;
    // Public write-time/current metadata only. Observing a current candidate
    // never revives an incomplete descriptor or accepts a different generation.
    std::uint32_t descriptorStateFlags = 0;
    Identity descriptorConversion{};
    extensions::ChainSummary descriptorViewChain{}, descriptorImageChain{};
};
static_assert(static_cast<unsigned>(TargetProbeReason::Count) <= 64, "Probe reasons exceed bounded bitset");
static_assert(std::is_trivially_copyable_v<TargetProbeDiagnostics> && sizeof(TargetProbeDiagnostics) <= 512,
              "Probe diagnostics must remain small owned public metadata");
// The optional pointer is owned by the replay callback's stack. Consumers must
// copy a selected result before returning. An explicitly admitted public draw
// diagnostic may carry a snapshot without matching the imported target.
enum class SnapshotProvenance : std::uint8_t { None, ExactTarget, UnassociatedPublicDraw };
struct TargetSnapshotProbeResult {
    TargetProbeOutcome outcome = TargetProbeOutcome::Unknown;
    std::uint64_t replayOrder = 0, invocationTag = 0;
    const Snapshot* snapshot = nullptr;
    SnapshotProvenance snapshotProvenance = SnapshotProvenance::None;
    bool publicSnapshotQuotaOmitted = false;
    TargetProbeDiagnostics diagnostics{};
};

// Instantaneous owner-side samples, not an atomic lease on native resources.
enum class ImportReceiptValidationReason : std::uint8_t {
    Valid, Closed, Halted, JournalLoss, ActivePublication, PendingReplay,
    PublicationChanged, CoreGenerationMismatch
};
struct ImportReceiptValidation {
    bool valid = false, transient = false, before_observed = false, after_observed = false, core_checked = false;
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
    struct Buffer : Base {
        BufferMetadata metadata{};
        // Only the original observed create input supplies this private fact.
        // Public BufferMetadata/serialization deliberately remains unchanged.
        VkSharingMode observedSharing = VK_SHARING_MODE_MAX_ENUM;
    };
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
        if (out->complete && (info.sharingMode == VK_SHARING_MODE_EXCLUSIVE || info.sharingMode == VK_SHARING_MODE_CONCURRENT))
            out->observedSharing = info.sharingMode;
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
        m.arrayLayers = info.arrayLayers; m.samples = info.samples; m.tiling = info.tiling; m.usage = info.usage;
        m.flags = info.flags; m.sharingMode = info.sharingMode; m.extensions = extension;
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
    // Live adapters reuse the same per-object bind validation as journal replay.
    // Every input is public successful Vulkan API metadata, never a copied
    // asynchronous pointer. Keep the batch finite before touching its members.
    void bindBufferMemory2(std::uint64_t device,std::uint32_t count,const VkBindBufferMemoryInfo* info) noexcept {
        if(count>32 || (count && !info)){reportLoss(Reason::Bounds);return;}
        for(std::uint32_t i=0;i<count;++i)
            bindBufferMemory(device,info[i].buffer,info[i].memory,info[i].memoryOffset,
                info[i].sType==VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO && !info[i].pNext);
    }
    void bindImageMemory2(std::uint64_t device,std::uint32_t count,const VkBindImageMemoryInfo* info) noexcept {
        if(count>32 || (count && !info)){reportLoss(Reason::Bounds);return;}
        for(std::uint32_t i=0;i<count;++i)
            bindImageMemory(device,info[i].image,info[i].memory,info[i].memoryOffset,
                info[i].sType==VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO && !info[i].pNext);
    }
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
    // One bounded CPU graph check for the OPTIONAL read-only uniform companion.
    // It is not a GPU barrier, content-coherence claim or resource lifetime lease.
    // The live owner must separately prove the same primary's final native draw,
    // same-queue ownership, synchronization, valid native lifetime and retirement.
    bool validateUnaliasedUniformResources(const Snapshot& previous, UniformResourceRefusal* refusal = nullptr) noexcept {
        if (refusal) *refusal = {};
        const auto reject = [&](UniformResourceReason reason, std::uint32_t binding = UINT32_MAX) noexcept {
            if (refusal) *refusal = {reason, binding};
            return false;
        };
        auto lock = acquire();
        if (!lock.owns_lock() || lostState()) return reject(UniformResourceReason::CoreUnavailable);
        if (!previous.device || !previous.sourceComplete || previous.reason != Reason::None || previous.gpuConsumedCoherent)
            return reject(UniformResourceReason::SnapshotIncomplete);
        const auto current = snapshotLocked(previous.device, previous.command.handle, previous.pipelineLayout.handle);
        if (!same(previous.command, current.command) || !same(previous.pipelineLayout, current.pipelineLayout) ||
            !same(previous.set, current.set) || !same(previous.setLayout, current.setLayout) ||
            previous.setRevision != current.setRevision || previous.commandBindingRevision != current.commandBindingRevision ||
            previous.layout_has_binding31 != current.layout_has_binding31)
            return reject(UniformResourceReason::SnapshotChanged);
        constexpr std::array<std::uint32_t, 5> numbers{{0, 2, 3, 4, 6}};
        for (const auto number : numbers) {
            const Binding* old = nullptr; const Binding* now = nullptr;
            for (const auto& binding : previous.bindings) if (binding.binding == number) {
                if (old) return reject(UniformResourceReason::BindingDuplicate, number);
                old = &binding;
            }
            for (const auto& binding : current.bindings) if (binding.binding == number) now = &binding;
            if (!old || !now || old->state == BindingState::Absent || now->state == BindingState::Absent)
                return reject(UniformResourceReason::BindingMissing, number);
            const auto visibility = number <= 3 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            if (!old->complete || old->state != BindingState::Written || old->descriptorCount != 1 ||
                (old->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && old->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
                !(old->stages & visibility) || !old->revision || old->revision > previous.setRevision)
                return reject(UniformResourceReason::Descriptor, number);
            const auto* source = lookup(buffers_, previous.device, old->buffer.identity);
            if (!source || !source->complete || !old->buffer.complete)
                return reject(UniformResourceReason::BufferStale, number);
            if (source->observedSharing != VK_SHARING_MODE_EXCLUSIVE)
                return reject(UniformResourceReason::Sharing, number);
            const auto* memory = lookup(memories_, previous.device, source->metadata.memory);
            if (!memory || !memory->complete || !same(old->buffer.memory, memory->id) ||
                !same(old->buffer.allocation.identity, memory->id))
                return reject(UniformResourceReason::MemoryStale, number);
            const auto allocationNow = allocation(previous.device, memory->id);
            if (!ordinaryUniformAllocation(allocationNow) || !ordinaryUniformAllocation(old->buffer.allocation))
                return reject(UniformResourceReason::UnsupportedMemory, number);
            if (!now->complete || now->state != BindingState::Written || now->descriptorCount != old->descriptorCount ||
                now->type != old->type || now->stages != old->stages || now->revision != old->revision ||
                now->bufferOffset != old->bufferOffset || now->bufferRange != old->bufferRange ||
                now->dynamicOffset != old->dynamicOffset || now->effectiveOffset != old->effectiveOffset ||
                now->effectiveRange != old->effectiveRange || !same(now->buffer.identity, old->buffer.identity) ||
                now->buffer.size != old->buffer.size || now->buffer.usage != old->buffer.usage ||
                !same(now->buffer.memory, old->buffer.memory) || now->buffer.memoryOffset != old->buffer.memoryOffset ||
                now->buffer.allocation.allocationSize != old->buffer.allocation.allocationSize ||
                now->buffer.allocation.memoryTypeIndex != old->buffer.allocation.memoryTypeIndex)
                return reject(UniformResourceReason::BindingChanged, number);
            const auto& buffer = source->metadata;
            constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            constexpr auto maximum = std::numeric_limits<VkDeviceSize>::max();
            if ((buffer.usage & usage) != usage) return reject(UniformResourceReason::SourceUsage, number);
            if (!buffer.size || !old->effectiveRange ||
                buffer.memoryOffset > allocationNow.allocationSize || buffer.size > allocationNow.allocationSize - buffer.memoryOffset ||
                old->bufferOffset > maximum - old->dynamicOffset ||
                old->effectiveOffset != old->bufferOffset + old->dynamicOffset ||
                (old->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && old->dynamicOffset) ||
                old->effectiveOffset >= buffer.size || old->effectiveRange > buffer.size - old->effectiveOffset)
                return reject(UniformResourceReason::SourceBounds, number);
            const auto begin = buffer.memoryOffset + old->effectiveOffset;
            const auto end = begin + old->effectiveRange; // Both sums are bounded by the live allocation above.
            for (const auto& image : images_) {
                if (!image.id.generation || image.device != previous.device || image.metadata.memory.handle != memory->id.handle) continue;
                if (!image.complete || !same(image.metadata.memory, memory->id))
                    return reject(UniformResourceReason::AmbiguousImageAlias, number);
                // No complete image memory-requirements envelope is retained.
                // Even an apparently distant offset cannot prove disjointness.
                return reject(UniformResourceReason::ImageAlias, number);
            }
            for (const auto& other : buffers_) {
                if (!other.id.generation || other.device != previous.device || same(other.id, source->id) ||
                    other.metadata.memory.handle != memory->id.handle) continue;
                const auto& alias = other.metadata;
                if (!other.complete || !same(alias.memory, memory->id) || !alias.size ||
                    (other.observedSharing != VK_SHARING_MODE_EXCLUSIVE && other.observedSharing != VK_SHARING_MODE_CONCURRENT) ||
                    alias.memoryOffset > allocationNow.allocationSize || alias.size > allocationNow.allocationSize - alias.memoryOffset)
                    return reject(UniformResourceReason::AmbiguousBufferAlias, number);
                const auto aliasEnd = alias.memoryOffset + alias.size;
                if (begin < aliasEnd && alias.memoryOffset < end) return reject(UniformResourceReason::BufferAlias, number);
                // VkBufferCreateInfo.size is only the logical byte extent.
                // The full bound span comes from VkMemoryRequirements.size,
                // which this Core never observes. Logical disjointness cannot
                // therefore establish a safe distinct-buffer alias envelope.
                return reject(UniformResourceReason::UnknownAllocationAliasSpan, number);
            }
        }
        if (!current.sourceComplete || current.reason != Reason::None) return reject(UniformResourceReason::SnapshotIncomplete);
        if (lostState()) return reject(UniformResourceReason::CoreUnavailable);
        return true;
    }
    // Live-owner resource check for an immutable previously observed image
    // binding. Descriptor rebinding is deliberately irrelevant: a later UI
    // draw must not replace the original sampled source. This is an instant
    // public-API graph check, not a native/GPU resource lifetime lease.
    bool validateBindingResources(const Binding& previous) noexcept {
        auto lock = acquire();
        if (!lock.owns_lock() || lostState() || !previous.complete ||
            previous.state != BindingState::Written || !imageType(previous.type) ||
            !previous.view.complete || !previous.image.complete || !previous.image.allocation.complete)
            return false;
        // Core generations are unique across devices. Recover the device from
        // the exact image generation, never from its reusable numeric handle.
        const Image* image = nullptr;
        for (const auto& entry : images_)
            if (same(entry.id, previous.image.identity)) { image = &entry; break; }
        if (!image || !image->complete) return false;
        const auto device = image->device;
        const auto* view = lookup(views_, device, previous.view.identity);
        const auto* memory = lookup(memories_, device, previous.image.memory);
        if (!view || !view->complete || !memory || !memory->complete ||
            !same(view->metadata.image, image->id) || !same(previous.view.image, image->id) ||
            !same(image->metadata.memory, memory->id) ||
            !same(previous.image.allocation.identity, memory->id) ||
            previous.image.memoryOffset != image->metadata.memoryOffset)
            return false;
        const auto currentAllocation = allocation(device, memory->id);
        if (!currentAllocation.complete || image->metadata.memoryOffset >= currentAllocation.allocationSize)
            return false;
        if (view->metadata.extensions.hasConversion) {
            const auto* conversion = lookup(conversions_, device, view->metadata.conversion.identity);
            if (!conversion || !conversion->complete ||
                !same(previous.view.conversion.identity, conversion->id)) return false;
        }
        return true;
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
    // Single owning-Journal replay only. Selection precedes full snapshot
    // construction and compares generations, not recycled native handles.
    TargetProbeDiagnostics targetSnapshotMatchDetailed(std::uint64_t device, VkCommandBuffer commandValue,
                                         VkPipelineLayout layoutValue, const ImportResourceReceipt& target) noexcept {
        TargetProbeDiagnostics out;
        out.requestedCommand = handle(commandValue); out.requestedLayout = handle(layoutValue);
        out.observedTargetTag = target.invocationTag; out.observedTargetCookie = target.cookie;
        out.observedTargetDevice = target.device;
        const auto reject = [&](TargetProbeReason reason) {
            out.reason = reason; out.reasonMask |= targetProbeReasonBit(reason); return out;
        };
        auto lock = acquire();
        if (!lock.owns_lock()) return reject(TargetProbeReason::CoreOwnerUnavailable);
        if (lostState()) return reject(TargetProbeReason::CoreLoss);
        if (!target.resourceComplete) return reject(TargetProbeReason::TargetIncomplete);
        if (target.device != device) return reject(TargetProbeReason::TargetDeviceMismatch);
        if (target.journalInstance != journalInstance_) return reject(TargetProbeReason::TargetJournalMismatch);
        const auto current = importResourceReceiptLocked(device, target.requestedImage, target.requestedView, target.invocationTag);
        out.currentTargetImage = current.image.identity; out.currentTargetView = current.view.identity;
        out.currentTargetMemory = current.memory.identity; out.currentTargetErrors = current.errors;
        out.observations |= ProbeCurrentTargetObserved;
        if (!current.resourceComplete) return reject(TargetProbeReason::TargetCurrentIncomplete);
        if (!same(target.image.identity, current.image.identity)) return reject(TargetProbeReason::TargetImageStale);
        if (!same(target.view.identity, current.view.identity)) return reject(TargetProbeReason::TargetViewStale);
        if (!same(target.memory.identity, current.memory.identity)) return reject(TargetProbeReason::TargetMemoryStale);
        if (!same(target.image.memory, current.image.memory)) return reject(TargetProbeReason::TargetBindingStale);
        if (target.image.memoryOffset != current.image.memoryOffset) return reject(TargetProbeReason::TargetOffsetMismatch);
        const auto* command = find(commands_, device, handle(commandValue));
        const auto* layout = find(pipelineLayouts_, device, handle(layoutValue));
        if (command) { out.command = command->id; out.commandLayout = command->layout; out.observations |= ProbeCommandObserved; }
        if (layout) { out.layout = layout->id; out.observations |= ProbeLayoutObserved; }
        if (!command) return reject(TargetProbeReason::CommandAbsent);
        if (!layout) return reject(TargetProbeReason::LayoutAbsent);
        if (!command->complete) return reject(TargetProbeReason::CommandIncomplete);
        if (!layout->complete) return reject(TargetProbeReason::LayoutIncomplete);
        if (!layout->count) return reject(TargetProbeReason::LayoutEmpty);
        if (!same(command->layout, layout->id)) return reject(TargetProbeReason::CommandLayoutMismatch);
        const auto& bound = command->sets[0]; out.boundSet = bound.set; out.observations |= ProbeBoundSetObserved;
        const auto* set = lookup(sets_, device, bound.set);
        const auto* setLayout = lookup(layouts_, device, layout->sets[0]);
        // The requested generation is retained even when lookup rejects it.
        out.setLayout = layout->sets[0];
        if (set) { out.set = set->id; out.setDeclaredLayout = set->layout; out.pool = set->pool; out.observations |= ProbeSetObserved; }
        if (setLayout) out.observations |= ProbeSetLayoutObserved;
        if (!bound.complete) return reject(TargetProbeReason::BoundSetIncomplete);
        if (!set) return reject(TargetProbeReason::SetAbsentOrStale);
        if (!set->complete) return reject(TargetProbeReason::SetIncomplete);
        if (!setLayout) return reject(TargetProbeReason::SetLayoutAbsentOrStale);
        if (!setLayout->complete) return reject(TargetProbeReason::SetLayoutIncomplete);
        if (!same(set->layout, setLayout->id)) return reject(TargetProbeReason::SetLayoutMismatch);
        if (!lookup(pools_, device, set->pool)) return reject(TargetProbeReason::PoolAbsentOrStale);
        bool unknown = false;
        TargetProbeDiagnostics firstUnknown{}, firstDifferentView{};
        bool hasUnknown = false, hasDifferentView = false;
        const auto note = [&](TargetProbeReason reason, bool isUnknown) {
            out.reason = reason; out.reasonMask |= targetProbeReasonBit(reason);
            if (isUnknown) { unknown = true; if (!hasUnknown) { firstUnknown = out; hasUnknown = true; } }
            if (reason == TargetProbeReason::DifferentViewSameImage && !hasDifferentView) {
                firstDifferentView = out; hasDifferentView = true;
            }
        };
        for (const auto bindingNumber : TargetBindings) {
            const auto pos = index(*setLayout, bindingNumber);
            if (pos == BindingCapacity || !imageType(setLayout->bindings[pos].type)) continue;
            ++out.examinedImageBindings; out.binding = bindingNumber;
            out.observations &= ~(ProbeDescriptorObserved | ProbeViewObserved | ProbeImageObserved);
            out.observations |= ProbeDescriptorObserved;
            out.descriptorView = {}; out.candidateView = {}; out.candidateImage = {}; out.candidateMemory = {};
            out.descriptorStateFlags = 0; out.descriptorConversion = {};
            out.descriptorViewChain = {}; out.descriptorImageChain = {};
            const auto& value = set->values[pos]; out.descriptorView = value.view;
            if (!value.written) { note(TargetProbeReason::DescriptorUnwritten, true); continue; }
            if (value.complete) out.descriptorStateFlags |= DescriptorWriteComplete;
            const auto* view = lookup(views_, device, value.view);
            const auto* image = view ? lookup(images_, device, view->metadata.image) : nullptr;
            if (view) {
                out.candidateView = view->id; out.observations |= ProbeViewObserved;
                out.descriptorViewChain = view->metadata.extensions.chain;
                if (view->complete) out.descriptorStateFlags |= DescriptorCurrentViewComplete;
                if (out.descriptorViewChain.complete) out.descriptorStateFlags |= DescriptorViewChainComplete;
                if (view->metadata.extensions.hasConversion) {
                    out.descriptorStateFlags |= DescriptorConversionRequested;
                    out.descriptorConversion = view->metadata.conversion.identity;
                    const auto* conversion = lookup(conversions_, device, out.descriptorConversion);
                    if (conversion) out.descriptorStateFlags |= DescriptorConversionObserved;
                    if (conversion && conversion->complete) out.descriptorStateFlags |= DescriptorConversionComplete;
                }
            }
            // candidateMemory is the observed image's generation-bearing link,
            // not a separately validated allocation or GPU-consumption claim.
            if (image) {
                out.candidateImage = image->id; out.candidateMemory = image->metadata.memory; out.observations |= ProbeImageObserved;
                out.descriptorImageChain = image->metadata.extensions.chain;
                if (image->complete) out.descriptorStateFlags |= DescriptorCurrentImageComplete;
                if (out.descriptorImageChain.complete) out.descriptorStateFlags |= DescriptorImageChainComplete;
                if (image->metadata.extensions.hasDrmList) out.descriptorStateFlags |= DescriptorImageHasDrmList;
            }
            // These additional observations explain an existing refusal. They
            // never override write-time completeness or generation lookup.
            if (!value.complete) { note(TargetProbeReason::DescriptorIncomplete, true); continue; }
            if (!view) { note(TargetProbeReason::ViewAbsentOrStale, true); continue; }
            if (!image) { note(TargetProbeReason::ImageAbsentOrStale, true); continue; }
            if (!view->complete) { note(TargetProbeReason::ViewIncomplete, true); continue; }
            if (!image->complete) { note(TargetProbeReason::ImageIncomplete, true); continue; }
            if (!same(value.view, target.view.identity)) {
                const bool sameImage = same(view->metadata.image, target.image.identity) && same(image->id, target.image.identity);
                note(sameImage ? TargetProbeReason::DifferentViewSameImage : TargetProbeReason::DifferentImage, false);
                continue;
            }
            if (!same(view->metadata.image, target.image.identity) || !same(image->id, target.image.identity)) {
                note(TargetProbeReason::DifferentImage, false); continue;
            }
            if (!same(image->metadata.memory, target.memory.identity)) { note(TargetProbeReason::MemoryMismatch, false); continue; }
            const auto selected = binding(device, *set, *setLayout, bound, bindingNumber);
            if (selected.complete && selected.image.complete && selected.view.complete && selected.image.allocation.complete &&
                same(selected.image.allocation.identity, target.memory.identity)) {
                out.outcome = TargetProbeOutcome::ExactMatch; return reject(TargetProbeReason::ExactMatch);
            }
            note(TargetProbeReason::ExactBindingIncomplete, true);
        }
        // A single representative retains its actual identities; the mask and
        // count still describe every supported binding examined by this probe.
        const auto mask = out.reasonMask; const auto examined = out.examinedImageBindings;
        if (hasUnknown) out = firstUnknown;
        else if (hasDifferentView) out = firstDifferentView;
        else if (!examined) out.reason = TargetProbeReason::NoSupportedImageBinding;
        else out.reason = TargetProbeReason::NoMatch;
        out.reasonMask = mask | targetProbeReasonBit(out.reason); out.examinedImageBindings = examined;
        out.outcome = unknown ? TargetProbeOutcome::Unknown : TargetProbeOutcome::Filtered;
        return out;
    }
    TargetProbeOutcome targetSnapshotMatch(std::uint64_t device, VkCommandBuffer commandValue,
                                         VkPipelineLayout layoutValue, const ImportResourceReceipt& target) noexcept {
        return targetSnapshotMatchDetailed(device, commandValue, layoutValue, target).outcome;
    }
    // Background replay owner only. A public destroy/free invocation can retire
    // the currently tracked generation; this lookup provides no GPU completion.
    Identity trackedResourceIdentity(TrackedResourceKind kind, std::uint64_t device, std::uint64_t value) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return {};
        if (kind == TrackedResourceKind::Image) { const auto* p = find(images_, device, value); return p ? p->id : Identity{}; }
        if (kind == TrackedResourceKind::View) { const auto* p = find(views_, device, value); return p ? p->id : Identity{}; }
        if (kind == TrackedResourceKind::Memory) { const auto* p = find(memories_, device, value); return p ? p->id : Identity{}; }
        return {};
    }
#ifdef FRAME_PIXEL_SSBO_TRANSACTION
    bool descriptorPoolContainsSet(std::uint64_t device, VkDescriptorPool value,
                                   VkDescriptorSet selected, bool& contains) noexcept {
        contains = false;
        auto lock = acquire(); if (!lock.owns_lock()) return false;
        const auto* pool = find(pools_, device, handle(value));
        const auto* set = find(sets_, device, handle(selected));
        if (!pool || !pool->complete || !set || !set->complete) return false;
        contains = same(pool->id, set->pool); return true;
    }
    // Original creation-time prefix identity for the optional SSBO observer.
    // A later equal raw layout handle is not the prefix baked into a pipeline.
    bool pipelinePrefixIdentity(std::uint64_t device, VkPipelineLayout value,
                                Identity& layout, Identity& set0) noexcept {
        layout = {}; set0 = {};
        auto lock = acquire(); if (!lock.owns_lock()) return false;
        const auto* entry = find(pipelineLayouts_, device, handle(value));
        if (!entry || !entry->complete || entry->count != 1) return false;
        layout = entry->id; set0 = entry->sets[0];
        return layout.handle && layout.generation && set0.handle && set0.generation;
    }
#endif
    void deviceDestroyed(std::uint64_t device) noexcept {
        auto lock = acquire(); if (!lock.owns_lock()) return;
        eraseDevice(layouts_, device); eraseDevice(pipelineLayouts_, device); eraseDevice(pools_, device); eraseDevice(sets_, device);
        eraseDevice(buffers_, device); eraseDevice(images_, device); eraseDevice(views_, device); eraseDevice(samplers_, device);
        eraseDevice(memories_, device); eraseDevice(commands_, device); eraseDevice(conversions_, device);
    }
private:
    static bool ordinaryUniformAllocation(const MemoryMetadata& memory) noexcept {
        const auto& ext = memory.extensions;
        return memory.complete && memory.identity.handle && memory.identity.generation && memory.allocationSize &&
            ext.chain.complete && !ext.chain.count && ext.chain.error == extensions::ChainError::None && !ext.chain.errorFlags &&
            !ext.hasImport && !ext.hasExport && !ext.hasDedicated && !ext.importType && !ext.exportTypes && ext.inputFD == -1 &&
            !ext.dedicatedImageHandle && !ext.dedicatedBufferHandle &&
            !memory.dedicatedImage.handle && !memory.dedicatedImage.generation &&
            !memory.dedicatedBuffer.handle && !memory.dedicatedBuffer.generation;
    }
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
