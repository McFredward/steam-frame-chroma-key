#include "layer/pixel_uniform_contract.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
using namespace frame_pixel_uniform;
using frame_bindings::Binding;
using frame_bindings::Snapshot;
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T out{}; out.sType = type; return out; }
NativeStages nativeStages() {
    NativeStages out;
    out.vertex.stage = VK_SHADER_STAGE_VERTEX_BIT; out.vertex.module = 100; out.vertex.generation = 200;
    out.vertex.hash = NativeVertexFingerprint; out.vertex.bytes = NativeVertexBytes; out.vertex.complete = true;
    std::memcpy(out.vertex.entry, "VS_DISTORT", sizeof("VS_DISTORT"));
    out.fragment.stage = VK_SHADER_STAGE_FRAGMENT_BIT; out.fragment.module = 101; out.fragment.generation = 201;
    out.fragment.hash = NativeFragmentFingerprint; out.fragment.bytes = NativeFragmentBytes; out.fragment.complete = true;
    std::memcpy(out.fragment.entry, "PS_DISTORT", sizeof("PS_DISTORT"));
    return out;
}
constexpr Limits limits{256, 65536};

struct Fixture {
    static constexpr std::uint64_t Device = 1;
    const VkDescriptorSetLayout layout = h<VkDescriptorSetLayout>(10);
    const VkPipelineLayout pipelineLayout = h<VkPipelineLayout>(11);
    const VkDescriptorPool pool = h<VkDescriptorPool>(12);
    const VkDescriptorSet set = h<VkDescriptorSet>(13);
    const VkCommandPool commandPool = h<VkCommandPool>(14);
    const VkCommandBuffer command = h<VkCommandBuffer>(15);
    std::array<VkBuffer, 5> buffers{};
    std::array<VkDeviceMemory, 5> memories{};
    std::array<VkDescriptorBufferInfo, 5> descriptors{};
    std::unique_ptr<frame_bindings::Core> core = std::make_unique<frame_bindings::Core>();

    Fixture() {
        std::array<VkDescriptorSetLayoutBinding, 5> definitions{};
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            definitions[i].binding = UniformBindings[i]; definitions[i].descriptorCount = 1;
            definitions[i].descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            definitions[i].stageFlags = VK_SHADER_STAGE_ALL;
        }
        auto sl = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        sl.bindingCount = definitions.size(); sl.pBindings = definitions.data(); core->createSetLayout(Device, layout, sl);
        auto pl = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pl.setLayoutCount = 1; pl.pSetLayouts = &layout; core->createPipelineLayout(Device, pipelineLayout, pl);
        auto dp = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        dp.maxSets = 1; core->createPool(Device, pool, dp);
        auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocate.descriptorPool = pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &layout;
        core->allocateSets(Device, allocate, &set);
        constexpr std::array<VkDeviceSize, 5> ranges{{272, 4096, 400, 272, 1024}};
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            buffers[i] = h<VkBuffer>(1000 + i); memories[i] = h<VkDeviceMemory>(2000 + i);
            createBuffer(i);
            auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            memory.allocationSize = 16384; memory.memoryTypeIndex = 1;
            core->allocateMemory(Device, memories[i], memory);
            core->bindBufferMemory(Device, buffers[i], memories[i], 1024);
            descriptors[i] = {buffers[i], 256, ranges[i]}; write(i);
        }
        core->allocateCommands(Device, commandPool, 1, &command); core->beginCommand(Device, command); bind(256);
    }
    void createBuffer(std::size_t i) {
        auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        buffer.size = 8192; buffer.usage = RequiredUsage; buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        core->createBuffer(Device, buffers[i], buffer);
    }
    void write(std::size_t i) {
        auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = set; write.dstBinding = UniformBindings[i]; write.descriptorCount = 1;
        write.descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &descriptors[i]; core->updateSets(Device, 1, &write, 0, nullptr);
    }
    void bind(std::uint32_t dynamicOffset) {
        core->bindSets(Device, command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 1, &dynamicOffset);
    }
    Snapshot snapshot() { return core->snapshot(Device, command, pipelineLayout); }
};
Binding& binding(Snapshot& snapshot, std::uint32_t number) {
    for (auto& b : snapshot.bindings) if (b.binding == number) return b;
    std::abort();
}
const Binding& binding(const Snapshot& snapshot, std::uint32_t number) {
    for (const auto& b : snapshot.bindings) if (b.binding == number) return b;
    std::abort();
}
void expectRefused(const CandidatePlan& plan, Refusal reason, Input input = Input::None, std::uint32_t number = NoBinding) {
    require(!plan.metadataCandidate && plan.refusal == reason, "exact refusal returns no available candidate");
    require(plan.offendingInput == input && plan.offendingBinding == number, "refusal identifies selected/current binding context");
    require(plan.totalBytes == 0 && plan.device == 0, "a refused candidate exposes no usable partial prefix or provenance");
    for (const auto& slice : plan.slices)
        require(slice.binding == NoBinding && slice.effectiveRange == 0 && slice.buffer.identity.handle == 0,
                "a refused plan clears every partial slice");
}

void metadataPositiveAndLimits() {
    Fixture fixture; auto selected = fixture.snapshot(); auto reobserved = fixture.snapshot();
    require(selected.sourceComplete && fixture.core->validate(selected), "real public Core fixture produces a complete current metadata graph");
    const auto profile = nativeStages(); const auto plan = planCandidate(selected, reobserved, profile, limits);
    require(plan.metadataCandidate && plan.refusal == Refusal::None, "complete actual Core snapshots produce only a metadata candidate");
    require(plan.device == selected.device && frame_bindings::same(plan.command, selected.command) &&
            frame_bindings::same(plan.set, selected.set), "candidate retains exact native descriptor/command generations");
    require(plan.stages.vertex.hash == NativeVertexFingerprint && plan.stages.fragment.generation == profile.fragment.generation,
            "candidate retains stage identity without calling it authentication");
    VkDeviceSize sum = 0;
    for (std::size_t i = 0; i < UniformBindings.size(); ++i) {
        const auto& source = binding(selected, UniformBindings[i]); const auto& slice = plan.slices[i];
        require(slice.binding == UniformBindings[i] && slice.effectiveRange == source.effectiveRange &&
                slice.descriptorRange == source.bufferRange && slice.descriptorOffset == source.bufferOffset,
                "each complete descriptor range is retained rather than clipped to a convenient prefix");
        require(slice.destinationOffset == sum && frame_bindings::same(slice.buffer.identity, source.buffer.identity) &&
                frame_bindings::same(slice.buffer.allocation.identity, source.buffer.allocation.identity) &&
                slice.buffer.usage == source.buffer.usage, "destination layout is contiguous and exact buffer/allocation usage is retained");
        sum += source.effectiveRange;
    }
    require(plan.totalBytes == sum && sum == 6064 && sum <= MaximumTotalBytes, "all five complete ranges fit the fixed read budget");
    require(plan.slices[1].dynamicOffset == 256 && plan.slices[1].effectiveOffset == 512,
            "effective dynamic offset remains relative to the buffer and is not confused with memory allocation offset");
    require(fixture.core->validate(selected), "candidate parsing does not mutate Core API metadata");
    auto visibility = selected;
    for (std::size_t i = 0; i < UniformBindings.size(); ++i)
        binding(visibility, UniformBindings[i]).stages = RequiredVisibility[i];
    require(planCandidate(visibility, visibility, profile, limits).metadataCandidate,
            "only the appropriate vertex or fragment visibility is required for each native UBO");
    binding(visibility, 0).stages |= VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    require(planCandidate(visibility, visibility, profile, limits).metadataCandidate,
            "valid broader descriptor visibility is not confused with active shader stages");
    auto minimum = selected;
    for (std::size_t i = 0; i < UniformBindings.size(); ++i)
        binding(minimum, UniformBindings[i]).bufferRange = binding(minimum, UniformBindings[i]).effectiveRange = MinimumRanges[i];
    require(planCandidate(minimum, minimum, profile, limits).metadataCandidate && MinimumRanges[0] >= 268 &&
            MinimumRanges[1] >= CandidateMatrixEnd && MinimumRanges[4] >= CandidateMatrixEnd,
            "exact minimum ranges cover complete candidate words and first matrix spans without asserting selectedK");
    binding(selected, 0).buffer.identity.generation += 100; selected.setRevision += 100;
    require(plan.slices[0].buffer.identity.generation != binding(selected, 0).buffer.identity.generation &&
            plan.setRevision != selected.setRevision, "candidate owns copied metadata and does not borrow the caller's snapshots");

    selected = reobserved;
    for (const auto number : UniformBindings) {
        auto& b = binding(selected, number); b.bufferRange = b.effectiveRange = MaximumSliceBytes;
    }
    const auto maximum = planCandidate(selected, selected, profile, limits);
    require(maximum.metadataCandidate && maximum.totalBytes == MaximumTotalBytes && MaximumTotalBytes == 20480,
            "maximum complete slices fit exactly without expanding or allocating an unbounded payload");
    auto& full = binding(selected, 2);
    full.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; full.dynamicOffset = 0; full.bufferRange = VK_WHOLE_SIZE;
    full.effectiveOffset = full.bufferOffset; full.buffer.size = full.bufferOffset + MaximumSliceBytes;
    require(planCandidate(selected, selected, profile, limits).metadataCandidate,
            "WHOLE_SIZE is resolved from base offset with a zero dynamic offset and full bounded range");
    full.dynamicOffset = 256; full.effectiveOffset += 256;
    expectRefused(planCandidate(selected, selected, profile, limits), Refusal::Range, Input::Selected, 2);

    auto missingLimits = limits; missingLimits.minUniformBufferOffsetAlignment = 0;
    expectRefused(planCandidate(reobserved, reobserved, profile, missingLimits), Refusal::Limits);
    missingLimits.minUniformBufferOffsetAlignment = 3;
    expectRefused(planCandidate(reobserved, reobserved, profile, missingLimits), Refusal::Limits);
    missingLimits = limits; missingLimits.maxUniformBufferRange = 0;
    expectRefused(planCandidate(reobserved, reobserved, profile, missingLimits), Refusal::Limits);
    missingLimits.maxUniformBufferRange = 4000;
    expectRefused(planCandidate(reobserved, reobserved, profile, missingLimits), Refusal::Range, Input::Selected, 2);
}

void historicalObservedScalarEnvelope() {
    // Offline compatibility control derived from the already supplied
    // 2026-10-11 bindings_recorded metadata. Fake identities remain local; this
    // is neither a new Frame run nor a proof of live provider/resource state.
    Fixture fixture; auto historical = fixture.snapshot();
    constexpr std::array<VkDeviceSize, 5> ranges{{272, 2800, 400, 272, 2800}};
    for (std::size_t i = 0; i < UniformBindings.size(); ++i) {
        auto& b = binding(historical, UniformBindings[i]);
        b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; b.stages = VK_SHADER_STAGE_ALL;
        b.dynamicOffset = 0; b.bufferOffset = b.effectiveOffset = 0; b.effectiveRange = ranges[i];
        b.bufferRange = (i == 0 || i == 3) ? ranges[i] : VK_WHOLE_SIZE;
        b.buffer.size = (i == 0 || i == 3) ? 524288 : ranges[i];
        b.buffer.usage = (i == 0 || i == 3) ? 17 : 19;
        b.buffer.memoryOffset = 0;
        b.buffer.allocation.allocationSize = (i == 0 || i == 3) ? 524288 : (i == 2 ? 448 : 2816);
    }
    binding(historical, 4).buffer = binding(historical, 0).buffer;
    binding(historical, 6).buffer = binding(historical, 2).buffer;
    const auto plan = planCandidate(historical, historical, nativeStages(), limits);
    require(plan.metadataCandidate && plan.totalBytes == 6544,
            "observed ALL visibility and272/2800/400/272/2800 ranges fit the strict full-range envelope");
    for (std::size_t i = 0; i < UniformBindings.size(); ++i)
        require(plan.slices[i].descriptorStages == VK_SHADER_STAGE_ALL && plan.slices[i].effectiveRange == ranges[i],
                "every historical native scalar range remains complete rather than truncated or guessed");
    require(frame_bindings::same(plan.slices[0].buffer.identity, plan.slices[3].buffer.identity) &&
            frame_bindings::same(plan.slices[1].buffer.identity, plan.slices[4].buffer.identity),
            "candidate retains observed same-buffer bindings without declaring alias freedom");
}

void nativeProfileAndSnapshotFailures() {
    Fixture fixture; const auto original = fixture.snapshot(); const auto profile = nativeStages();
    auto badProfile = profile; badProfile.vertex.hash ^= 1;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; --badProfile.vertex.bytes;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; std::memcpy(badProfile.vertex.entry, "main", sizeof("main"));
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; std::memset(badProfile.fragment.entry, 'A', sizeof(badProfile.fragment.entry));
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; badProfile.vertex.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; badProfile.fragment.hash ^= 1;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; badProfile.vertex.generation = 0;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    badProfile = profile; badProfile.fragment.complete = false;
    expectRefused(planCandidate(original, original, badProfile, limits), Refusal::NativeStages);
    require(planCandidate(original, original, profile, limits).metadataCandidate,
            "restoring the exact native diagnostic stage profile repairs each independent negative control");

    auto bad = original; bad.sourceComplete = false;
    expectRefused(planCandidate(bad, original, profile, limits), Refusal::SnapshotIncomplete, Input::Selected);
    bad = original; bad.reason = frame_bindings::Reason::Stale;
    expectRefused(planCandidate(original, bad, profile, limits), Refusal::SnapshotIncomplete, Input::Reobserved);
    bad = original; bad.gpuConsumedCoherent = true;
    expectRefused(planCandidate(bad, original, profile, limits), Refusal::SnapshotIncomplete, Input::Selected);
    bad = original; bad.layout_has_binding31 = static_cast<frame_bindings::Presence>(99);
    expectRefused(planCandidate(bad, original, profile, limits), Refusal::SnapshotIncomplete, Input::Selected);
    bad = original; bad.command.generation = 0;
    expectRefused(planCandidate(original, bad, profile, limits), Refusal::SnapshotIdentity, Input::Reobserved);
    bad = original; bad.set.generation += 1;
    expectRefused(planCandidate(original, bad, profile, limits), Refusal::SnapshotChanged);
    bad = original; bad.commandBindingRevision += 1;
    expectRefused(planCandidate(original, bad, profile, limits), Refusal::SnapshotChanged);
    bad = original; binding(bad, 2).binding = 7;
    expectRefused(planCandidate(bad, original, profile, limits), Refusal::BindingMissing, Input::Selected, 2);
    bad = original; bad.bindings.back().binding = 2;
    expectRefused(planCandidate(original, bad, profile, limits), Refusal::BindingDuplicate, Input::Reobserved, 2);
}

template<class Mutation> void alteredBinding(const Snapshot& original, std::uint32_t number, Mutation mutate, Refusal reason) {
    auto bad = original; mutate(binding(bad, number));
    expectRefused(planCandidate(bad, bad, nativeStages(), limits), reason, Input::Selected, number);
}
void descriptorRangeAndMemoryFailures() {
    Fixture fixture; const auto original = fixture.snapshot();
    for (std::size_t i = 0; i < UniformBindings.size(); ++i) {
        const auto number = UniformBindings[i];
        alteredBinding(original, number, [](Binding& b) { b.complete = false; }, Refusal::Descriptor);
        alteredBinding(original, number, [](Binding& b) { b.state = frame_bindings::BindingState::Unwritten; }, Refusal::Descriptor);
        alteredBinding(original, number, [](Binding& b) { b.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }, Refusal::Descriptor);
        alteredBinding(original, number, [](Binding& b) { b.descriptorCount = 2; }, Refusal::Descriptor);
        alteredBinding(original, number, [i](Binding& b) {
            b.stages = RequiredVisibility[i] == VK_SHADER_STAGE_VERTEX_BIT ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        }, Refusal::Descriptor);
        alteredBinding(original, number, [i](Binding& b) { b.stages = RequiredVisibility[i] | UINT32_C(0x80000000); }, Refusal::Descriptor);
        alteredBinding(original, number, [](Binding& b) { b.buffer.complete = false; }, Refusal::BufferIncomplete);
        alteredBinding(original, number, [](Binding& b) { b.buffer.identity.generation = 0; }, Refusal::BufferIncomplete);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.complete = false; }, Refusal::AllocationIncomplete);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.identity.generation += 1; }, Refusal::AllocationIncomplete);
        alteredBinding(original, number, [](Binding& b) { b.buffer.memoryOffset = UINT64_MAX; }, Refusal::AllocationIncomplete);
        alteredBinding(original, number, [](Binding& b) { b.buffer.usage &= ~VK_BUFFER_USAGE_TRANSFER_SRC_BIT; }, Refusal::BufferUsage);
        alteredBinding(original, number, [](Binding& b) { b.buffer.usage &= ~VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; }, Refusal::BufferUsage);
        alteredBinding(original, number, [](Binding& b) { b.bufferRange = b.effectiveRange = 0; }, Refusal::Range);
        alteredBinding(original, number, [i](Binding& b) { b.bufferRange = b.effectiveRange = MinimumRanges[i] - 4; }, Refusal::Range);
        alteredBinding(original, number, [](Binding& b) { b.effectiveOffset += 4; }, Refusal::Range);
        alteredBinding(original, number, [](Binding& b) { b.bufferOffset += 4; b.effectiveOffset += 4; }, Refusal::Alignment);
        alteredBinding(original, number, [](Binding& b) { b.bufferRange = b.effectiveRange = MaximumSliceBytes + 4; }, Refusal::SliceLimit);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.extensions.chain.complete = false; }, Refusal::UnsupportedMemory);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.extensions.hasImport = true; }, Refusal::UnsupportedMemory);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.extensions.hasExport = true; }, Refusal::UnsupportedMemory);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.extensions.hasDedicated = true; }, Refusal::UnsupportedMemory);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.dedicatedBuffer = b.buffer.identity; }, Refusal::UnsupportedMemory);
        alteredBinding(original, number, [](Binding& b) { b.buffer.allocation.extensions.chain.count = 1; }, Refusal::UnsupportedMemory);
    }
    alteredBinding(original, 2, [](Binding& b) { b.dynamicOffset += 4; b.effectiveOffset += 4; }, Refusal::Alignment);
    alteredBinding(original, 0, [](Binding& b) { b.dynamicOffset = 256; b.effectiveOffset += 256; }, Refusal::Descriptor);
    alteredBinding(original, 2, [](Binding& b) {
        b.buffer.memoryOffset = 0; b.buffer.size = b.buffer.allocation.allocationSize = UINT64_MAX;
        b.bufferOffset = UINT64_MAX - 128; b.dynamicOffset = 256;
    }, Refusal::Range);
    alteredBinding(original, 6, [](Binding& b) { b.effectiveOffset = b.bufferOffset = 7936; }, Refusal::Range);
    auto different = original; binding(different, 2).buffer.identity.generation += 1;
    expectRefused(planCandidate(original, different, nativeStages(), limits), Refusal::BindingChanged, Input::Reobserved, 2);
    different = original; binding(different, 4).buffer.memoryOffset += 256;
    expectRefused(planCandidate(original, different, nativeStages(), limits), Refusal::BindingChanged, Input::Reobserved, 4);
    require(planCandidate(original, original, nativeStages(), limits).metadataCandidate,
            "independent malformed records never poison an unchanged complete candidate");
}

void realGenerationAndRebindControls() {
    const auto profile = nativeStages();
    {
        Fixture fixture; const auto selected = fixture.snapshot();
        fixture.bind(512); const auto moved = fixture.snapshot();
        require(moved.sourceComplete && binding(moved, 2).effectiveOffset == 768,
                "actual public rebinding updates the native dynamic offset in Core");
        expectRefused(planCandidate(selected, moved, profile, limits), Refusal::SnapshotChanged);
        const auto fresh = planCandidate(moved, moved, profile, limits);
        require(fresh.metadataCandidate && fresh.slices[1].effectiveOffset == 768,
                "a newly selected current recording retains the new dynamic source range");
    }
    {
        Fixture fixture; const auto selected = fixture.snapshot();
        fixture.core->destroyBuffer(Fixture::Device, fixture.buffers[0]); fixture.createBuffer(0);
        fixture.core->bindBufferMemory(Fixture::Device, fixture.buffers[0], fixture.memories[0], 1024);
        const auto stale = fixture.snapshot();
        expectRefused(planCandidate(selected, stale, profile, limits), Refusal::SnapshotIncomplete, Input::Reobserved);
        fixture.write(0); const auto replacement = fixture.snapshot();
        require(replacement.sourceComplete && frame_bindings::handle(fixture.buffers[0]) == selected.bindings[0].buffer.identity.handle,
                "same numeric buffer handle can be observed with a new complete generation");
        expectRefused(planCandidate(selected, replacement, profile, limits), Refusal::SnapshotChanged);
        require(planCandidate(replacement, replacement, profile, limits).metadataCandidate,
                "new explicit selection admits the repaired generation without accepting the stale selection");
    }
    {
        Fixture fixture; const auto selected = fixture.snapshot();
        fixture.core->freeMemory(Fixture::Device, fixture.memories[3]);
        auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        memory.allocationSize = 16384; memory.memoryTypeIndex = 1;
        fixture.core->allocateMemory(Fixture::Device, fixture.memories[3], memory);
        expectRefused(planCandidate(selected, fixture.snapshot(), profile, limits), Refusal::SnapshotIncomplete, Input::Reobserved);
    }
    {
        Fixture fixture; const auto selected = fixture.snapshot();
        fixture.core->resetCommand(Fixture::Device, fixture.command); fixture.core->beginCommand(Fixture::Device, fixture.command); fixture.bind(256);
        const auto rerecorded = fixture.snapshot();
        require(rerecorded.sourceComplete && rerecorded.command.generation != selected.command.generation,
                "successful native re-recording has a distinct Core command generation");
        expectRefused(planCandidate(selected, rerecorded, profile, limits), Refusal::SnapshotChanged);
    }
}
} // namespace

int main() {
    metadataPositiveAndLimits(); historicalObservedScalarEnvelope(); nativeProfileAndSnapshotFailures();
    descriptorRangeAndMemoryFailures(); realGenerationAndRebindControls();
    std::cout << "pixel uniform contract: " << checks << " checks passed; inert metadata candidates only, no GPU/frame/eye proof\n";
}
