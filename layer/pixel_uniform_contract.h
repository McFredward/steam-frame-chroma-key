#pragma once

// INERT metadata-only foundation. No GPU command, memory read, Vulkan call,
// allocation, logging, pointer borrowing, or activation occurs here. A matching
// pair of caller-supplied snapshots is a CANDIDATE read plan, not a live lease.
// The caller must separately prove current last-draw/pipeline adjacency, primary
// recording/queue ownership, buffer sharing and memory requirements, no hostile
// alias or concurrent host/device writes, barriers, resource lifetime through
// completion, exact command retirement before CPU read, and frame/eye identity.
// Snapshot does not retain native buffer sharing mode; never infer EXCLUSIVE.
// Reusing one stale snapshot as both inputs cannot establish current state.
#include "passive_pipeline_trace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace frame_pixel_uniform {

constexpr std::uint64_t NativeVertexFingerprint = UINT64_C(0x9cc04d186b9c8fe5);
constexpr std::uint64_t NativeFragmentFingerprint = UINT64_C(0xf333a137b4e79ced);
constexpr std::uint32_t NativeVertexBytes = 9460, NativeFragmentBytes = 6620;
constexpr std::array<std::uint32_t, 5> UniformBindings{{0, 2, 3, 4, 6}};
// Candidate byte coverage only: b0 eye word at 256 and projection word at 264;
// b4 eye word at 256; b3 fixed prefix [0,396), ending after the word at 392;
// b2/b6 fixed first-record prefix including matrix span [880,944). No selector,
// selectedK or matrix is parsed.
// In particular, copying a complete b2 range does NOT prove the native K index.
constexpr VkDeviceSize CandidateEyeWordOffset = 256;
constexpr VkDeviceSize CandidateProjectionWordOffset = 264;
constexpr VkDeviceSize CandidateMatrixBegin = 880, CandidateMatrixEnd = 944;
constexpr std::array<VkDeviceSize, 5> MinimumRanges{{268, 944, 396, 260, 944}};
// Full effective ranges are copied, never truncated to a prefix. Four KiB per
// binding is a deliberately narrow diagnostic envelope, not a native ABI limit.
// This permits full observed small UBOs, excludes arbitrarily large b2 arrays,
// and bounds the eventual owned payload at 20 KiB rather than an unbounded read.
constexpr VkDeviceSize MaximumSliceBytes = 4096;
constexpr VkDeviceSize MaximumTotalBytes = MaximumSliceBytes * UniformBindings.size();
static_assert(MaximumTotalBytes <= 32 * 1024, "Uniform candidate exceeds the fixed diagnostic byte budget");
constexpr VkBufferUsageFlags RequiredUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
constexpr std::array<VkShaderStageFlags, 5> RequiredVisibility{{VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_VERTEX_BIT,
    VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT}};
constexpr VkShaderStageFlags KnownVisibilityBits = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT |
    VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_RAYGEN_BIT_KHR |
    VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
    VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR |
    VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI | VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI;
constexpr std::uint32_t NoBinding = UINT32_MAX;

struct NativeStages {
    frame_trace::Stage vertex{}, fragment{};
};
struct Limits {
    // Actual current physical-device limits, never guessed defaults. Their
    // provenance must be established by the future live owner outside this API.
    VkDeviceSize minUniformBufferOffsetAlignment = 0;
    std::uint32_t maxUniformBufferRange = 0;
};
enum class Input : std::uint8_t { None, Selected, Reobserved };
enum class Refusal : std::uint8_t {
    None, NativeStages, Limits, SnapshotIncomplete, SnapshotIdentity,
    SnapshotChanged, BindingMissing, BindingDuplicate, Descriptor,
    BufferIncomplete, AllocationIncomplete, UnsupportedMemory, BufferUsage,
    Range, Alignment, SliceLimit, BindingChanged,
};
struct Slice {
    std::uint32_t binding = NoBinding;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    VkShaderStageFlags descriptorStages = 0;
    std::uint64_t descriptorRevision = 0;
    frame_bindings::BufferMetadata buffer{};
    VkDeviceSize descriptorOffset = 0, descriptorRange = 0;
    std::uint32_t dynamicOffset = 0;
    VkDeviceSize effectiveOffset = 0, effectiveRange = 0;
    // Future owned-buffer layout only; this helper does not execute a copy.
    VkDeviceSize destinationOffset = 0;
};
struct CandidatePlan {
    bool metadataCandidate = false;
    Refusal refusal = Refusal::None;
    Input offendingInput = Input::None;
    std::uint32_t offendingBinding = NoBinding;
    std::uint64_t device = 0;
    frame_bindings::Identity command{}, pipelineLayout{}, set{}, setLayout{};
    std::uint64_t setRevision = 0, commandBindingRevision = 0;
    NativeStages stages{};
    std::array<Slice, UniformBindings.size()> slices{};
    VkDeviceSize totalBytes = 0;
};
static_assert(std::is_trivially_copyable_v<CandidatePlan> && sizeof(CandidatePlan) <= 4096,
              "Candidate plans contain only small owned scalar metadata");

inline bool validIdentity(frame_bindings::Identity identity) noexcept {
    return identity.handle && identity.generation;
}
inline bool emptyIdentity(frame_bindings::Identity identity) noexcept {
    return !identity.handle && !identity.generation;
}
inline bool entryIs(const char (&entry)[frame_trace::EntryCapacity], const char* expected) noexcept {
    for (std::size_t i = 0; i < frame_trace::EntryCapacity; ++i) {
        if (entry[i] != expected[i]) return false;
        if (!expected[i]) return true;
    }
    return false;
}
inline bool nativeStagesMatch(const NativeStages& stages) noexcept {
    const auto& v = stages.vertex; const auto& f = stages.fragment;
    // FNV+size+entry are diagnostic profile identity, never authentication.
    return v.complete && f.complete && v.module && v.generation && f.module && f.generation &&
        v.stage == VK_SHADER_STAGE_VERTEX_BIT && f.stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
        v.hash == NativeVertexFingerprint && f.hash == NativeFragmentFingerprint &&
        v.bytes == NativeVertexBytes && f.bytes == NativeFragmentBytes &&
        entryIs(v.entry, "VS_DISTORT") && entryIs(f.entry, "PS_DISTORT") &&
        !(v.module == f.module && v.generation == f.generation);
}
inline CandidatePlan refused(Refusal reason, Input input = Input::None,
                             std::uint32_t binding = NoBinding) noexcept {
    CandidatePlan out; out.refusal = reason; out.offendingInput = input; out.offendingBinding = binding;
    return out; // A partial plan is never returned as an available copy prefix.
}
inline Refusal snapshotRefusal(const frame_bindings::Snapshot& snapshot) noexcept {
    if (!snapshot.sourceComplete || snapshot.reason != frame_bindings::Reason::None || snapshot.gpuConsumedCoherent ||
        (snapshot.layout_has_binding31 != frame_bindings::Presence::Absent &&
         snapshot.layout_has_binding31 != frame_bindings::Presence::Present)) return Refusal::SnapshotIncomplete;
    if (!snapshot.device || !validIdentity(snapshot.command) || !validIdentity(snapshot.pipelineLayout) ||
        !validIdentity(snapshot.set) || !validIdentity(snapshot.setLayout) ||
        !snapshot.setRevision || !snapshot.commandBindingRevision) return Refusal::SnapshotIdentity;
    return Refusal::None;
}
inline bool sameSnapshotIdentity(const frame_bindings::Snapshot& a, const frame_bindings::Snapshot& b) noexcept {
    return a.device == b.device && frame_bindings::same(a.command, b.command) &&
        frame_bindings::same(a.pipelineLayout, b.pipelineLayout) && frame_bindings::same(a.set, b.set) &&
        frame_bindings::same(a.setLayout, b.setLayout) && a.setRevision == b.setRevision &&
        a.commandBindingRevision == b.commandBindingRevision && a.layout_has_binding31 == b.layout_has_binding31;
}
inline Refusal describeSlice(const frame_bindings::Binding& binding, std::uint64_t setRevision,
                             VkDeviceSize minimum, VkShaderStageFlags requiredVisibility,
                             const Limits& limits, Slice& out) noexcept {
    const auto alignment = limits.minUniformBufferOffsetAlignment;
    if (!alignment || (alignment & (alignment - 1)) || !limits.maxUniformBufferRange) return Refusal::Limits;
    if (!binding.complete || binding.state != frame_bindings::BindingState::Written ||
        (binding.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
        binding.descriptorCount != 1 || (binding.stages & requiredVisibility) != requiredVisibility ||
        (binding.stages != VK_SHADER_STAGE_ALL && (binding.stages & ~KnownVisibilityBits)) ||
        !binding.revision || binding.revision > setRevision) return Refusal::Descriptor;
    // Visibility is not active pipeline state. The native layout uses canonical
    // ALL; valid broader masks remain valid even though the pinned stages above
    // contain only the exact VS/PS. Only each UBO's relevant stage is required.
    const auto& b = binding.buffer; const auto& a = b.allocation; const auto& ext = a.extensions;
    if (!b.complete || !validIdentity(b.identity) || !b.size || !validIdentity(b.memory)) return Refusal::BufferIncomplete;
    if (!a.complete || !validIdentity(a.identity) || !frame_bindings::same(b.memory, a.identity) || !a.allocationSize ||
        b.memoryOffset > a.allocationSize || b.size > a.allocationSize - b.memoryOffset) return Refusal::AllocationIncomplete;
    if (!ext.chain.complete || ext.chain.count || ext.chain.error != frame_bindings::extensions::ChainError::None ||
        ext.chain.errorFlags || ext.hasImport || ext.hasExport || ext.hasDedicated || ext.importType || ext.exportTypes ||
        ext.inputFD != -1 || ext.dedicatedImageHandle || ext.dedicatedBufferHandle ||
        !emptyIdentity(a.dedicatedImage) || !emptyIdentity(a.dedicatedBuffer)) return Refusal::UnsupportedMemory;
    if ((b.usage & RequiredUsage) != RequiredUsage) return Refusal::BufferUsage;
    if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding.dynamicOffset) return Refusal::Descriptor;
    constexpr auto maximum = std::numeric_limits<VkDeviceSize>::max();
    if (binding.bufferOffset >= b.size || binding.bufferOffset > maximum - binding.dynamicOffset ||
        binding.effectiveOffset != binding.bufferOffset + binding.dynamicOffset) return Refusal::Range;
    VkDeviceSize range = binding.bufferRange;
    // Vulkan resolves WHOLE_SIZE before applying the dynamic offset, which must
    // then be zero. Do not silently shrink an overflowing dynamic descriptor.
    // https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBufferInfo.html
    // https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html
    if (range == VK_WHOLE_SIZE) {
        if (binding.dynamicOffset) return Refusal::Range;
        range = b.size - binding.bufferOffset;
    }
    if (!range || range != binding.effectiveRange || range < minimum || range > limits.maxUniformBufferRange ||
        binding.effectiveOffset >= b.size || range > b.size - binding.effectiveOffset) return Refusal::Range;
    if (binding.bufferOffset % limits.minUniformBufferOffsetAlignment ||
        binding.dynamicOffset % limits.minUniformBufferOffsetAlignment || binding.effectiveOffset % 4 || range % 4)
        return Refusal::Alignment;
    if (range > MaximumSliceBytes) return Refusal::SliceLimit;
    out.binding = binding.binding; out.descriptorType = binding.type; out.descriptorStages = binding.stages;
    out.descriptorRevision = binding.revision; out.buffer = b;
    out.descriptorOffset = binding.bufferOffset; out.descriptorRange = binding.bufferRange;
    out.dynamicOffset = binding.dynamicOffset; out.effectiveOffset = binding.effectiveOffset; out.effectiveRange = range;
    return Refusal::None;
}
inline bool sameSlice(const Slice& a, const Slice& b) noexcept {
    return a.binding == b.binding && a.descriptorType == b.descriptorType && a.descriptorStages == b.descriptorStages &&
        a.descriptorRevision == b.descriptorRevision && a.descriptorOffset == b.descriptorOffset &&
        a.descriptorRange == b.descriptorRange && a.dynamicOffset == b.dynamicOffset &&
        a.effectiveOffset == b.effectiveOffset && a.effectiveRange == b.effectiveRange &&
        frame_bindings::same(a.buffer.identity, b.buffer.identity) && a.buffer.size == b.buffer.size &&
        a.buffer.usage == b.buffer.usage && frame_bindings::same(a.buffer.memory, b.buffer.memory) &&
        a.buffer.memoryOffset == b.buffer.memoryOffset &&
        frame_bindings::same(a.buffer.allocation.identity, b.buffer.allocation.identity) &&
        a.buffer.allocation.allocationSize == b.buffer.allocation.allocationSize &&
        a.buffer.allocation.memoryTypeIndex == b.buffer.allocation.memoryTypeIndex;
}

inline CandidatePlan planCandidate(const frame_bindings::Snapshot& selected,
                                   const frame_bindings::Snapshot& reobserved,
                                   const NativeStages& stages, const Limits& limits) noexcept {
    if (!nativeStagesMatch(stages)) return refused(Refusal::NativeStages);
    const auto alignment = limits.minUniformBufferOffsetAlignment;
    if (!alignment || (alignment & (alignment - 1)) || !limits.maxUniformBufferRange) return refused(Refusal::Limits);
    const auto oldError = snapshotRefusal(selected), newError = snapshotRefusal(reobserved);
    if (oldError != Refusal::None) return refused(oldError, Input::Selected);
    if (newError != Refusal::None) return refused(newError, Input::Reobserved);
    if (!sameSnapshotIdentity(selected, reobserved)) return refused(Refusal::SnapshotChanged);
    CandidatePlan out;
    for (std::size_t i = 0; i < UniformBindings.size(); ++i) {
        const auto number = UniformBindings[i];
        const frame_bindings::Binding* oldBinding = nullptr; const frame_bindings::Binding* newBinding = nullptr;
        for (const auto& binding : selected.bindings) if (binding.binding == number) {
            if (oldBinding) return refused(Refusal::BindingDuplicate, Input::Selected, number);
            oldBinding = &binding;
        }
        for (const auto& binding : reobserved.bindings) if (binding.binding == number) {
            if (newBinding) return refused(Refusal::BindingDuplicate, Input::Reobserved, number);
            newBinding = &binding;
        }
        if (!oldBinding) return refused(Refusal::BindingMissing, Input::Selected, number);
        if (!newBinding) return refused(Refusal::BindingMissing, Input::Reobserved, number);
        Slice newer;
        const auto oldBindingError = describeSlice(*oldBinding, selected.setRevision, MinimumRanges[i], RequiredVisibility[i], limits, out.slices[i]);
        if (oldBindingError != Refusal::None) return refused(oldBindingError, Input::Selected, number);
        const auto newBindingError = describeSlice(*newBinding, reobserved.setRevision, MinimumRanges[i], RequiredVisibility[i], limits, newer);
        if (newBindingError != Refusal::None) return refused(newBindingError, Input::Reobserved, number);
        if (!sameSlice(out.slices[i], newer)) return refused(Refusal::BindingChanged, Input::Reobserved, number);
        if (out.totalBytes > MaximumTotalBytes - out.slices[i].effectiveRange) return refused(Refusal::SliceLimit, Input::Selected, number);
        out.slices[i].destinationOffset = out.totalBytes;
        out.totalBytes += out.slices[i].effectiveRange;
    }
    out.device = selected.device; out.command = selected.command; out.pipelineLayout = selected.pipelineLayout;
    out.set = selected.set; out.setLayout = selected.setLayout; out.setRevision = selected.setRevision;
    out.commandBindingRevision = selected.commandBindingRevision; out.stages = stages;
    out.metadataCandidate = true;
    return out;
}

} // namespace frame_pixel_uniform
