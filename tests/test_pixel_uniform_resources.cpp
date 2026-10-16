#include "layer/passive_binding_trace.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
using namespace frame_bindings;
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
constexpr std::array<std::uint32_t, 5> Numbers{{0, 2, 3, 4, 6}};
constexpr std::array<std::size_t, 5> Groups{{0, 1, 2, 0, 1}};
constexpr VkBufferUsageFlags Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
struct Options {
    VkSharingMode sharing = VK_SHARING_MODE_EXCLUSIVE;
    VkBufferUsageFlags usage = Usage;
    VkBufferCreateFlags flags = 0;
    const void* allocationChain = nullptr;
    const void* bufferChain = nullptr;
    bool dynamic = false;
};
struct Fixture {
    static constexpr std::uint64_t Device = 1;
    const VkDescriptorSetLayout layout = h<VkDescriptorSetLayout>(10);
    const VkPipelineLayout pipeline = h<VkPipelineLayout>(11);
    const VkDescriptorPool pool = h<VkDescriptorPool>(12);
    const VkDescriptorSet set = h<VkDescriptorSet>(13);
    const VkCommandPool commandPool = h<VkCommandPool>(14);
    const VkCommandBuffer command = h<VkCommandBuffer>(15);
    const std::array<VkBuffer, 3> buffers{{h<VkBuffer>(100), h<VkBuffer>(101), h<VkBuffer>(102)}};
    const std::array<VkDeviceMemory, 3> memories{{h<VkDeviceMemory>(200), h<VkDeviceMemory>(201), h<VkDeviceMemory>(202)}};
    std::array<VkDescriptorBufferInfo, 5> descriptors{};
    Options options;
    std::unique_ptr<Core> core = std::make_unique<Core>();

    explicit Fixture(Options requested = {}) : options(requested) {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = Numbers[i]; bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = options.dynamic && i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
        }
        auto layoutInfo = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layoutInfo.bindingCount = bindings.size(); layoutInfo.pBindings = bindings.data(); core->createSetLayout(Device, layout, layoutInfo);
        auto pipelineInfo = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipelineInfo.setLayoutCount = 1; pipelineInfo.pSetLayouts = &layout; core->createPipelineLayout(Device, pipeline, pipelineInfo);
        auto poolInfo = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.maxSets = 1; core->createPool(Device, pool, poolInfo);
        auto sets = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        sets.descriptorPool = pool; sets.descriptorSetCount = 1; sets.pSetLayouts = &layout; core->allocateSets(Device, sets, &set);
        for (std::size_t group = 0; group < buffers.size(); ++group) {
            createSourceBuffer(group);
            auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            memory.allocationSize = group == 0 ? 524288 : (group == 1 ? (options.dynamic ? 16384 : 2816) : 448);
            memory.memoryTypeIndex = 1; memory.pNext = group == 0 ? options.allocationChain : nullptr;
            core->allocateMemory(Device, memories[group], memory); core->bindBufferMemory(Device, buffers[group], memories[group], 0);
        }
        constexpr std::array<VkDeviceSize, 5> ranges{{272, VK_WHOLE_SIZE, VK_WHOLE_SIZE, 272, VK_WHOLE_SIZE}};
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            descriptors[i] = {buffers[Groups[i]], 0, ranges[i]};
            if (options.dynamic && Groups[i] == 1) descriptors[i] = {buffers[1], 256, 1024};
            write(i);
        }
        core->allocateCommands(Device, commandPool, 1, &command); core->beginCommand(Device, command); bind(256);
    }
    void createSourceBuffer(std::size_t group) {
        const std::uint32_t queues[] = {0, 1};
        auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        buffer.size = group == 0 ? 524288 : (group == 1 ? (options.dynamic ? 8192 : 2800) : 400);
        buffer.usage = group == 0 ? options.usage : Usage;
        buffer.flags = group == 0 ? options.flags : 0;
        buffer.pNext = group == 0 ? options.bufferChain : nullptr;
        buffer.sharingMode = group == 0 ? options.sharing : VK_SHARING_MODE_EXCLUSIVE;
        if (buffer.sharingMode == VK_SHARING_MODE_CONCURRENT) {
            buffer.queueFamilyIndexCount = 2; buffer.pQueueFamilyIndices = queues;
        }
        core->createBuffer(Device, buffers[group], buffer);
    }
    void write(std::size_t index) {
        auto writeInfo = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writeInfo.dstSet = set; writeInfo.dstBinding = Numbers[index]; writeInfo.descriptorCount = 1;
        writeInfo.descriptorType = options.dynamic && index == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeInfo.pBufferInfo = &descriptors[index]; core->updateSets(Device, 1, &writeInfo, 0, nullptr);
    }
    void bind(std::uint32_t dynamicOffset = 256) {
        core->bindSets(Device, command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, 0, 1, &set,
                       options.dynamic ? 1 : 0, options.dynamic ? &dynamicOffset : nullptr);
    }
    Snapshot snapshot() { return core->snapshot(Device, command, pipeline); }
    void aliasBuffer(VkBuffer buffer, VkDeviceSize size, VkDeviceMemory memory, VkDeviceSize offset,
                     bool supportedBind = true, const void* chain = nullptr) {
        auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = size; create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE; create.pNext = chain;
        core->createBuffer(Device, buffer, create); core->bindBufferMemory(Device, buffer, memory, offset, supportedBind);
    }
    void image(VkImage value, VkDeviceMemory memory, VkDeviceSize offset, const void* chain = nullptr) {
        auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        create.imageType = VK_IMAGE_TYPE_2D; create.format = VK_FORMAT_R8G8B8A8_UNORM;
        create.extent = {16, 16, 1}; create.mipLevels = create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT; create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; create.sharingMode = VK_SHARING_MODE_EXCLUSIVE; create.pNext = chain;
        core->createImage(Device, value, create);
        if (memory) core->bindImageMemory(Device, value, memory, offset);
    }
};
Binding& at(Snapshot& snapshot, std::uint32_t number) {
    for (auto& binding : snapshot.bindings) if (binding.binding == number) return binding;
    std::abort();
}
void expect(Core& core, const Snapshot& snapshot, UniformResourceReason reason, std::uint32_t binding = UINT32_MAX) {
    UniformResourceRefusal refusal{UniformResourceReason::ImageAlias, 987};
    const bool okay = core.validateUnaliasedUniformResources(snapshot, &refusal);
    require(okay == (reason == UniformResourceReason::None), "resource admission matches expected exact current graph result");
    require(refusal.reason == reason && refusal.binding == binding, "fixed refusal and offending binding are reported atomically with admission");
}

void ordinarySourcesAndCurrentness() {
    Fixture f; const auto selected = f.snapshot();
    require(selected.sourceComplete && f.core->validate(selected), "observed native ALL visibility and full small ranges have an ordinary complete snapshot");
    expect(*f.core, selected, UniformResourceReason::None);
    require(f.core->validateUnaliasedUniformResources(selected), "legacy one-argument optional-companion call remains supported");
    auto current = f.snapshot();
    require(same(at(current, 0).buffer.identity, at(current, 4).buffer.identity) &&
            same(at(current, 2).buffer.identity, at(current, 6).buffer.identity), "read descriptors may alias the exact same native source buffer");
    require(f.core->validate(selected) && !selected.gpuConsumedCoherent && !current.gpuConsumedCoherent,
            "resource checks do not mutate ordinary snapshots or certify GPU consumption");
    f.write(0);
    expect(*f.core, selected, UniformResourceReason::SnapshotChanged);
    expect(*f.core, f.snapshot(), UniformResourceReason::None);
    auto malformed = f.snapshot(); at(malformed, 0).buffer.size -= 4;
    expect(*f.core, malformed, UniformResourceReason::BindingChanged, 0);
    malformed = f.snapshot(); at(malformed, 2).binding = 7;
    expect(*f.core, malformed, UniformResourceReason::BindingMissing, 2);
    malformed = f.snapshot(); malformed.bindings.back().binding = 2;
    expect(*f.core, malformed, UniformResourceReason::BindingDuplicate, 2);
    malformed = f.snapshot(); at(malformed, 4).state = BindingState::Unwritten;
    expect(*f.core, malformed, UniformResourceReason::Descriptor, 4);
    malformed = f.snapshot(); malformed.gpuConsumedCoherent = true;
    expect(*f.core, malformed, UniformResourceReason::SnapshotIncomplete);
    const auto beforeLoss = f.core->lostState(); f.core->reportLoss(Reason::Unsupported);
    expect(*f.core, f.snapshot(), UniformResourceReason::CoreUnavailable);
    require(f.core->lostState() == beforeLoss + 1, "refusal preserves separate existing Core loss counters");
}

void privateSharingAndOrdinaryMemory() {
    {
        Options options; options.sharing = VK_SHARING_MODE_CONCURRENT; Fixture f(options);
        const auto snapshot = f.snapshot();
        require(snapshot.sourceComplete && f.core->validate(snapshot), "ordinary concurrent sharing still has unchanged public snapshot acceptance");
        expect(*f.core, snapshot, UniformResourceReason::Sharing, 0);
    }
    {
        Options options; options.sharing = VK_SHARING_MODE_MAX_ENUM; Fixture f(options);
        const auto snapshot = f.snapshot();
        require(snapshot.sourceComplete, "unknown private sharing does not silently alter legacy snapshot behavior");
        expect(*f.core, snapshot, UniformResourceReason::Sharing, 0);
    }
    for (const auto flags : {VK_BUFFER_CREATE_SPARSE_BINDING_BIT, VK_BUFFER_CREATE_PROTECTED_BIT}) {
        Options options; options.flags = flags; Fixture f(options);
        require(!f.snapshot().sourceComplete, "existing Core buffer flags exclude sparse or protected source metadata");
        expect(*f.core, f.snapshot(), UniformResourceReason::SnapshotIncomplete);
    }
    {
        VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr}; Options options; options.bufferChain = &unknown; Fixture f(options);
        expect(*f.core, f.snapshot(), UniformResourceReason::SnapshotIncomplete);
    }
    {
        Options options; options.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; Fixture f(options);
        const auto snapshot = f.snapshot(); require(snapshot.sourceComplete, "missing transfer usage remains separate from existing descriptor validity");
        expect(*f.core, snapshot, UniformResourceReason::SourceUsage, 0);
    }
    {
        auto imports = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
        imports.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT; imports.fd = 9;
        Options options; options.allocationChain = &imports; Fixture f(options);
        require(f.snapshot().sourceComplete, "recognized imported-memory scalars remain a complete ordinary snapshot");
        expect(*f.core, f.snapshot(), UniformResourceReason::UnsupportedMemory, 0);
    }
    {
        auto exports = info<VkExportMemoryAllocateInfo>(VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
        exports.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        Options options; options.allocationChain = &exports; Fixture f(options);
        expect(*f.core, f.snapshot(), UniformResourceReason::UnsupportedMemory, 0);
    }
    {
        auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
        dedicated.buffer = h<VkBuffer>(100); Options options; options.allocationChain = &dedicated; Fixture f(options);
        require(f.snapshot().sourceComplete, "legitimate dedicated buffer metadata remains accepted by the ordinary Core");
        expect(*f.core, f.snapshot(), UniformResourceReason::UnsupportedMemory, 0);
    }
    {
        VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr}; Options options; options.allocationChain = &unknown; Fixture f(options);
        expect(*f.core, f.snapshot(), UniformResourceReason::SnapshotIncomplete);
    }
}

void otherBufferAliasing() {
    const auto other = h<VkBuffer>(300);
    {
        Fixture f; const auto selected = f.snapshot();
        f.aliasBuffer(other, 64, f.memories[0], 8192);
        expect(*f.core, selected, UniformResourceReason::UnknownAllocationAliasSpan, 0);
        f.core->destroyBuffer(Fixture::Device, other); f.aliasBuffer(other, 64, f.memories[0], 272);
        expect(*f.core, selected, UniformResourceReason::UnknownAllocationAliasSpan, 0);
        f.core->destroyBuffer(Fixture::Device, other); f.aliasBuffer(other, 64, f.memories[0], 256);
        expect(*f.core, selected, UniformResourceReason::BufferAlias, 0);
        f.core->destroyBuffer(Fixture::Device, other);
        expect(*f.core, selected, UniformResourceReason::None);
    }
    {
        Options options; options.dynamic = true; Fixture f(options); const auto selected = f.snapshot();
        f.aliasBuffer(other, 256, f.memories[1], 1280);
        expect(*f.core, selected, UniformResourceReason::BufferAlias, 2);
        f.core->destroyBuffer(Fixture::Device, other); f.aliasBuffer(other, 256, f.memories[1], 1536);
        expect(*f.core, selected, UniformResourceReason::UnknownAllocationAliasSpan, 2);
        f.bind(512);
        expect(*f.core, selected, UniformResourceReason::SnapshotChanged);
        expect(*f.core, f.snapshot(), UniformResourceReason::BufferAlias, 2);
    }
    {
        Fixture f; const auto selected = f.snapshot(); f.aliasBuffer(other, 64, f.memories[0], 8192, false);
        expect(*f.core, selected, UniformResourceReason::AmbiguousBufferAlias, 0);
    }
    {
        Fixture f; const auto selected = f.snapshot(); f.aliasBuffer(other, 64, f.memories[0], UINT64_MAX - 32);
        expect(*f.core, selected, UniformResourceReason::AmbiguousBufferAlias, 0);
    }
    {
        Fixture f; const auto selected = f.snapshot(); VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
        f.aliasBuffer(other, 64, f.memories[0], 8192, true, &unknown);
        expect(*f.core, selected, UniformResourceReason::AmbiguousBufferAlias, 0);
    }
    {
        Fixture f; const auto selected = f.snapshot();
        const auto differentMemory = h<VkDeviceMemory>(400); auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        memory.allocationSize = 1024; f.core->allocateMemory(Fixture::Device, differentMemory, memory);
        f.aliasBuffer(other, 256, differentMemory, 0);
        expect(*f.core, selected, UniformResourceReason::None);
    }
}

void imageAliasingAndRetirement() {
    const auto image = h<VkImage>(500);
    {
        Fixture f; const auto selected = f.snapshot(); f.image(image, VK_NULL_HANDLE, 0);
        expect(*f.core, selected, UniformResourceReason::None);
        f.core->bindImageMemory(Fixture::Device, image, f.memories[0], 8192);
        expect(*f.core, selected, UniformResourceReason::ImageAlias, 0);
        f.core->destroyImage(Fixture::Device, image);
        expect(*f.core, selected, UniformResourceReason::None);
    }
    {
        Fixture f; const auto selected = f.snapshot(); VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
        f.image(image, f.memories[0], 8192, &unknown);
        expect(*f.core, selected, UniformResourceReason::AmbiguousImageAlias, 0);
    }
    {
        Fixture f; const auto selected = f.snapshot();
        const auto differentMemory = h<VkDeviceMemory>(401); auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        memory.allocationSize = 8192; f.core->allocateMemory(Fixture::Device, differentMemory, memory);
        f.image(image, differentMemory, 0);
        expect(*f.core, selected, UniformResourceReason::None);
    }
    {
        Fixture f; const auto selected = f.snapshot(); f.core->destroyBuffer(Fixture::Device, f.buffers[0]);
        expect(*f.core, selected, UniformResourceReason::BufferStale, 0);
        f.createSourceBuffer(0); f.core->bindBufferMemory(Fixture::Device, f.buffers[0], f.memories[0], 0);
        expect(*f.core, selected, UniformResourceReason::BufferStale, 0);
        f.write(0); f.write(3);
        expect(*f.core, selected, UniformResourceReason::SnapshotChanged);
        expect(*f.core, f.snapshot(), UniformResourceReason::None);
    }
    {
        Fixture f; const auto selected = f.snapshot(); f.core->freeMemory(Fixture::Device, f.memories[1]);
        expect(*f.core, selected, UniformResourceReason::MemoryStale, 2);
    }
    {
        Fixture f; const auto selected = f.snapshot();
        f.core->resetCommand(Fixture::Device, f.command); f.core->beginCommand(Fixture::Device, f.command); f.bind();
        expect(*f.core, selected, UniformResourceReason::SnapshotChanged);
    }
}

void boundedNamesAndOutputReset() {
    for (unsigned i = 0; i <= static_cast<unsigned>(UniformResourceReason::AmbiguousImageAlias); ++i)
        require(std::strcmp(uniformResourceReasonName(static_cast<UniformResourceReason>(i)), "unknown_uniform_resource_reason") != 0,
                "each finite refusal has a fixed bounded report name");
    require(std::strcmp(uniformResourceReasonName(static_cast<UniformResourceReason>(255)), "unknown_uniform_resource_reason") == 0,
            "unknown reason names do not read outside a finite table");
    Fixture f; const auto selected = f.snapshot(); const auto other = h<VkBuffer>(300);
    f.aliasBuffer(other, 64, f.memories[0], 0);
    UniformResourceRefusal result;
    require(!f.core->validateUnaliasedUniformResources(selected, &result) && result.reason == UniformResourceReason::BufferAlias,
            "caller receives precise alias failure rather than a later best-effort query");
    f.core->destroyBuffer(Fixture::Device, other);
    require(f.core->validateUnaliasedUniformResources(selected, &result) && result.reason == UniformResourceReason::None && result.binding == UINT32_MAX,
            "a later successful graph check clears previous output refusal without claiming GPU coherence");
}
} // namespace

int main() {
    ordinarySourcesAndCurrentness(); privateSharingAndOrdinaryMemory(); otherBufferAliasing();
    imageAliasingAndRetirement(); boundedNamesAndOutputReset();
    std::cout << "pixel uniform resources: " << checks << " checks passed; CPU graph only, no lifetime or GPU-coherence lease\n";
}
