#include "layer/passive_binding_trace.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace frame_bindings {
struct TestAccess {
    static std::unique_lock<std::mutex> hold(Core& core) { return std::unique_lock<std::mutex>(core.mutex_); }
    static void exhaustGeneration(Core& core) { core.serial_ = UINT64_MAX; }
};
}

namespace {
using namespace frame_bindings;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
const auto sl = h<VkDescriptorSetLayout>(10);
const auto pl = h<VkPipelineLayout>(20);
const auto dp = h<VkDescriptorPool>(30);
const auto ds = h<VkDescriptorSet>(40);
const auto cp = h<VkCommandPool>(50);
const auto cb = h<VkCommandBuffer>(60);
const auto buffer = h<VkBuffer>(70);
const auto memory = h<VkDeviceMemory>(80);

template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

void makeBuffer(Core& core, std::uint64_t device = 1, VkBuffer value = buffer, VkDeviceMemory mem = memory) {
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = 16384; allocation.memoryTypeIndex = 2;
    core.allocateMemory(device, mem, allocation);
    auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    create.size = 8192; create.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    core.createBuffer(device, value, create);
    core.bindBufferMemory(device, value, mem, 256);
}
void makeLayout(Core& core, const VkDescriptorSetLayoutBinding* bindings, std::uint32_t count,
                std::uint64_t device = 1, VkDescriptorSetLayout setLayout = sl, VkPipelineLayout pipelineLayout = pl) {
    auto create = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    create.bindingCount = count; create.pBindings = bindings;
    core.createSetLayout(device, setLayout, create);
    auto pipeline = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &setLayout;
    core.createPipelineLayout(device, pipelineLayout, pipeline);
}
void makeSets(Core& core, std::uint64_t device = 1, VkDescriptorSetLayout setLayout = sl, VkDescriptorPool pool = dp, VkDescriptorSet set = ds) {
    auto create = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; create.maxSets = 16;
    core.createPool(device, pool, create);
    auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &setLayout;
    core.allocateSets(device, allocate, &set);
}
void writeBuffer(Core& core, std::uint32_t binding = 0, VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 std::uint64_t device = 1, VkDescriptorSet set = ds, VkBuffer value = buffer, VkDeviceSize offset = 64, VkDeviceSize range = 128) {
    VkDescriptorBufferInfo bi{value, offset, range};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1; write.descriptorType = type; write.pBufferInfo = &bi;
    core.updateSets(device, 1, &write, 0, nullptr);
}
void record(Core& core, std::uint64_t device = 1, const std::uint32_t* offsets = nullptr, std::uint32_t count = 0) {
    core.allocateCommands(device, cp, 1, &cb);
    core.beginCommand(device, cb);
    core.bindSets(device, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, count, offsets);
}
void seed(Core& core, std::uint64_t device = 1) {
    makeBuffer(core, device);
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    makeLayout(core, &binding, 1, device);
    makeSets(core, device);
    writeBuffer(core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, device);
    record(core, device);
}
const Binding& at(const Snapshot& snapshot, std::uint32_t value) {
    for (const auto& entry : snapshot.bindings) if (entry.binding == value) return entry;
    std::abort();
}

void indexedCollisionsAndChurn() {
    struct Entry { std::uint64_t device = 0; Identity id{}; std::uint64_t payload = 0; };
    ObjectTable<Entry, 8> table;
    std::array<std::uint64_t, 8> keys{};
    std::array<Entry*, 8> pointers{};
    // A cluster starting at the final index bucket must wrap around; remove its
    // first/middle/last entries without severing the search path of survivors.
    for (std::uint64_t key = 1, count = 0; count < keys.size(); ++key) {
        if (ObjectTable<Entry, 8>::bucketFor(1, key) != 15) continue;
        keys[count] = key; pointers[count] = table.insert(1, key, count + 1);
        require(pointers[count] != nullptr, "collision cluster could not fill fixed capacity");
        pointers[count]->payload = 100 + count; ++count;
    }
    require(!table.hasRoom() && !table.insert(1, 999, 100), "full index accepted an extra object");
    require(!table.insert(1, keys[0], 100), "duplicate key replaced its live identity");
    require(!table.insert(1, 0, 100) && !table.insert(1, 999, 0), "invalid key/generation entered index");
    for (const auto removed : {0u, 4u, 7u}) {
        require(table.erase(1, keys[removed]), "collision erase lost its exact keyed object");
        pointers[removed] = nullptr;
        require(!table.find(1, keys[removed]), "deleted collision key remained reachable");
        for (unsigned i = 0; i < keys.size(); ++i) if (pointers[i]) {
            require(table.find(1, keys[i]) == pointers[i] && pointers[i]->payload == 100 + i,
                    "backward shifting moved payload or hid collision survivor");
        }
    }
    auto* reused = table.insert(1, keys[4], 1000);
    require(reused && reused->id.generation == 1000 && reused->payload == 0, "freed slot revived stale metadata");
    auto* otherDevice = table.insert(2, keys[4], 1001);
    require(otherDevice && otherDevice != reused && table.find(1, keys[4]) == reused && table.find(2, keys[4]) == otherDevice,
            "same handle on another device aliased the indexed object");
    table.eraseIf([](const Entry& entry) { return entry.device == 1; });
    require(table.find(2, keys[4]) == otherDevice && !table.find(1, keys[4]), "device erase removed another device or retained stale keys");
    table.eraseEntry(*otherDevice);
    require(table.hasRoom() && !table.find(2, keys[4]), "entry erasure left a hidden occupied key");

    // Deterministic model comparison deliberately churns both full and sparse
    // collision chains. No tombstone or historical occupancy can accumulate.
    ObjectTable<Entry, 32> churn;
    struct Expected { std::uint64_t device = 0, key = 0, generation = 0; Entry* entry = nullptr; };
    std::array<Expected, 32> expected{};
    std::uint32_t state = 0x51b9u;
    std::uint64_t generation = 1;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        auto& slot = expected[state % expected.size()];
        if (slot.entry) {
            require(churn.erase(slot.device, slot.key), "churn failed to erase an observed key");
            require(!churn.find(slot.device, slot.key), "churn retained an erased key");
            slot = {};
        } else {
            slot.device = (state >> 16) % 4 + 1;
            slot.key = (iteration + 1) * UINT64_C(4096);
            slot.generation = generation++;
            slot.entry = churn.insert(slot.device, slot.key, slot.generation);
            require(slot.entry != nullptr, "churn exhausted capacity before live-object limit");
            slot.entry->payload = iteration;
        }
        for (const auto& current : expected) if (current.entry) {
            require(churn.find(current.device, current.key) == current.entry && current.entry->id.generation == current.generation,
                    "churn lookup moved object or revived another generation");
            require(!churn.find(current.device + 10, current.key), "missing device key matched existing handle");
        }
    }
}
void indexedLifecycleCapacityRecovery() {
    auto core = std::make_unique<Core>();
    const auto pool = h<VkDescriptorPool>(0x8100);
    auto poolInfo = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; poolInfo.maxSets = SetCapacity;
    core->createPool(1, pool, poolInfo);
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    makeLayout(*core, &binding, 1);
    auto allocation = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocation.descriptorPool = pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &sl;
    std::array<VkDescriptorSet, SetCapacity> sets{};
    std::array<VkCommandBuffer, CommandCapacity> commands{};
    for (unsigned i = 0; i < SetCapacity; ++i) {
        sets[i] = h<VkDescriptorSet>(0x100000 + i * 4096);
        commands[i] = h<VkCommandBuffer>(0x200000 + i * 4096);
        core->allocateSets(1, allocation, &sets[i]); core->allocateCommands(1, cp, 1, &commands[i]);
    }
    core->resetPool(1, pool);
    for (auto& set : sets) core->allocateSets(1, allocation, &set);
    core->freeSets(1, pool, sets.size(), sets.data());
    for (auto& set : sets) core->allocateSets(1, allocation, &set);
    core->destroyPool(1, pool); core->createPool(1, pool, poolInfo);
    for (auto& set : sets) core->allocateSets(1, allocation, &set);
    core->freeCommands(1, cp, commands.size(), commands.data());
    core->allocateCommands(1, cp, commands.size(), commands.data());
    core->destroyCommandPool(1, cp); core->allocateCommands(1, cp, commands.size(), commands.data());
    require(core->lostState() == 0, "pool/free lifecycle failed to return indexed capacity");
    core->deviceDestroyed(1);
    core->createPool(1, pool, poolInfo); makeLayout(*core, &binding, 1);
    for (auto& set : sets) core->allocateSets(1, allocation, &set);
    core->allocateCommands(1, cp, commands.size(), commands.data());
    require(core->lostState() == 0, "device destruction retained occupied index entries");

    TestAccess::exhaustGeneration(*core);
    auto samplerInfo = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    core->createSampler(1, h<VkSampler>(0xdead), samplerInfo);
    require(core->lossCount(Reason::Capacity) == 1 && core->capacityLossCount(Table::Sampler) == 0,
            "generation exhaustion was confused with object-table capacity");
    core->createSampler(1, h<VkSampler>(0xbeef), samplerInfo);
    require(core->lossCount(Reason::Capacity) == 2 && core->lossCount(Reason::Stale) == 0,
            "generation exhaustion inserted a ghost or cleared sticky failure");
}

void metadataAndRevision() {
    auto core = std::make_unique<Core>(); seed(*core);
    const auto initial = core->snapshot(1, cb, pl);
    require(initial.sourceComplete && !initial.gpuConsumedCoherent, "source snapshot claimed GPU coherence or lost complete metadata");
    require(initial.layout_has_binding31 == Presence::Absent, "known layout did not certify slot 31 absence");
    const auto& b = at(initial, 0);
    require(b.state == BindingState::Written && b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && b.stages == VK_SHADER_STAGE_VERTEX_BIT,
            "descriptor definition omitted");
    require(b.buffer.identity.handle == handle(buffer) && b.buffer.identity.generation && b.buffer.size == 8192 &&
            b.buffer.memoryOffset == 256 && b.buffer.allocation.identity.handle == handle(memory) && b.buffer.allocation.memoryTypeIndex == 2,
            "buffer/memory identity metadata missing");
    require(b.bufferOffset == 64 && b.bufferRange == 128 && b.effectiveOffset == 64 && b.effectiveRange == 128,
            "buffer range not retained");
    require(at(initial, 9).state == BindingState::Absent && at(initial, 9).complete, "absent binding treated as missing observation");
    require(core->validate(initial), "unchanged source snapshot rejected");
    core->endCommand(1, cb);
    require(core->validate(initial), "ending recording changed descriptor provenance");
    writeBuffer(*core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, ds, buffer, 96);
    require(!core->validate(initial), "later descriptor update falsely preserved recorded source revision");
    const auto updated = core->snapshot(1, cb, pl);
    require(updated.sourceComplete && updated.setRevision != initial.setRevision && at(updated, 0).revision != b.revision,
            "descriptor update revision not captured");
    core->beginCommand(1, cb);
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    require(!core->validate(updated), "CB generation reuse preserved an old snapshot");
}
void devicesAndHandleReuse() {
    auto core = std::make_unique<Core>(); seed(*core, 1); seed(*core, 2);
    const auto one = core->snapshot(1, cb, pl), two = core->snapshot(2, cb, pl);
    require(one.sourceComplete && two.sourceComplete && one.set.generation != two.set.generation,
            "same numerical handles from different devices collided");
    core->destroyBuffer(1, buffer);
    require(!core->snapshot(1, cb, pl).sourceComplete && core->validate(two), "destroy on one device invalidated another device or left stale reference");
    auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO); create.size = 8192;
    core->createBuffer(1, buffer, create); core->bindBufferMemory(1, buffer, memory, 256);
    require(!core->snapshot(1, cb, pl).sourceComplete, "buffer handle reuse revived stale descriptor contents");
    writeBuffer(*core);
    auto renewed = core->snapshot(1, cb, pl);
    require(renewed.sourceComplete && at(renewed, 0).buffer.identity.generation != at(one, 0).buffer.identity.generation,
            "explicit write failed to bind renewed buffer generation");
    core->freeMemory(1, memory);
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO); allocation.allocationSize = 16384;
    core->allocateMemory(1, memory, allocation);
    require(!core->snapshot(1, cb, pl).sourceComplete && core->validate(two), "memory reuse revived stale binding");
    core->deviceDestroyed(2);
    require(!core->validate(two), "destroyed device retained a snapshot");
}
void descriptorCopiesAndPoolReuse() {
    auto core = std::make_unique<Core>(); seed(*core);
    const auto second = h<VkDescriptorSet>(41);
    auto allocation = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocation.descriptorPool = dp; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &sl;
    core->allocateSets(1, allocation, &second);
    auto copy = info<VkCopyDescriptorSet>(VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET);
    copy.srcSet = ds; copy.dstSet = second; copy.descriptorCount = 1;
    core->updateSets(1, 0, nullptr, 1, &copy);
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &second, 0, nullptr);
    auto copied = core->snapshot(1, cb, pl);
    require(copied.sourceComplete && copied.set.handle == handle(second) && at(copied, 0).buffer.identity.handle == handle(buffer),
            "descriptor copy omitted source resource identity");
    core->freeSets(1, dp, 1, &second);
    core->allocateSets(1, allocation, &second);
    require(!core->snapshot(1, cb, pl).sourceComplete, "descriptor set handle reuse revived old command binding");
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    auto beforeReset = core->snapshot(1, cb, pl);
    core->resetPool(1, dp);
    require(!core->validate(beforeReset), "pool reset retained allocated sets");
    core->allocateSets(1, allocation, &ds); writeBuffer(*core);
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    auto afterReset = core->snapshot(1, cb, pl);
    require(afterReset.sourceComplete && afterReset.set.generation != beforeReset.set.generation, "set after pool reset reused generation");
    core->destroyPool(1, dp);
    require(!core->validate(afterReset), "pool destruction retained allocated sets");
}
void dynamicOrderingAndBounds() {
    auto core = std::make_unique<Core>(); makeBuffer(*core);
    const VkDescriptorSetLayoutBinding bindings[] = {
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}
    };
    makeLayout(*core, bindings, 3); makeSets(*core);
    writeBuffer(*core, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
    writeBuffer(*core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    writeBuffer(*core, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    const std::uint32_t offsets[]{10, 20, 30}; record(*core, 1, offsets, 3);
    auto first = core->snapshot(1, cb, pl);
    require(first.sourceComplete && at(first, 0).dynamicOffset == 10 && at(first, 2).dynamicOffset == 20 && at(first, 4).dynamicOffset == 30,
            "dynamic offset order followed input order instead of ascending binding");
    require(at(first, 4).effectiveOffset == 94, "dynamic offset was not applied to descriptor base offset");
    const std::uint32_t changed[]{11, 21, 31};
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 3, changed);
    require(!core->validate(first), "later dynamic rebinding preserved an old source snapshot");
    auto second = core->snapshot(1, cb, pl);
    require(second.sourceComplete, "valid dynamic rebinding rejected");
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_COMPUTE, h<VkPipelineLayout>(999), 0, 1, &ds, 0, nullptr);
    require(core->validate(second), "compute binding disturbed graphics descriptor metadata");
    writeBuffer(*core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, ds, buffer, 8190, 16);
    require(!core->snapshot(1, cb, pl).sourceComplete, "out-of-bounds dynamic buffer range certified");
    writeBuffer(*core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, ds, buffer, 0, VK_WHOLE_SIZE);
    require(!core->snapshot(1, cb, pl).sourceComplete, "VK_WHOLE_SIZE with nonzero dynamic offset certified");
    core->beginCommand(1, cb);
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 2, offsets);
    require(!core->snapshot(1, cb, pl).sourceComplete, "short dynamic-offset list certified");
    core->beginCommand(1, cb);
    const std::uint32_t zeros[]{0, 0, 0};
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 3, zeros);
    require(core->snapshot(1, cb, pl).sourceComplete, "zero-offset whole-size buffer incorrectly rejected");
}
void incompatibleLayoutAndSecondaryExecution() {
    auto core = std::make_unique<Core>(); seed(*core);
    const auto secondLayout = h<VkPipelineLayout>(21);
    const VkDescriptorSetLayout layouts[]{sl, sl};
    auto create = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
    create.setLayoutCount = 2; create.pSetLayouts = layouts; create.pushConstantRangeCount = 1; create.pPushConstantRanges = &push;
    core->createPipelineLayout(1, secondLayout, create);
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, secondLayout, 1, 1, &ds, 0, nullptr);
    require(!core->snapshot(1, cb, secondLayout).sourceComplete && !core->snapshot(1, cb, pl).sourceComplete,
            "incompatible set1 bind left set0 certified");
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    auto rebound = core->snapshot(1, cb, pl); require(rebound.sourceComplete, "explicit set0 rebinding did not recover");
    core->executeCommands(1, cb);
    require(!core->snapshot(1, cb, pl).sourceComplete && !core->validate(rebound), "secondary execution retained primary bindings");
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    require(core->snapshot(1, cb, pl).sourceComplete, "primary rebinding after secondary execution rejected");
    core->resetCommandPool(1, cp);
    require(!core->snapshot(1, cb, pl).sourceComplete, "command pool reset retained binding");
    core->beginCommand(1, cb); core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    core->destroyCommandPool(1, cp);
    require(!core->snapshot(1, cb, pl).sourceComplete, "command pool destruction retained command");
}
void dynamicOrderAcrossSets() {
    auto core = std::make_unique<Core>(); makeBuffer(*core);
    const auto sl2 = h<VkDescriptorSetLayout>(11); const auto unusedPl = h<VkPipelineLayout>(22);
    const auto ds2 = h<VkDescriptorSet>(41); const auto dp2 = h<VkDescriptorPool>(31);
    const VkDescriptorSetLayoutBinding first[] = {
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}
    };
    makeLayout(*core, first, 2);
    VkDescriptorSetLayoutBinding second{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    makeLayout(*core, &second, 1, 1, sl2, unusedPl);
    makeSets(*core); makeSets(*core, 1, sl2, dp2, ds2);
    writeBuffer(*core, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC); writeBuffer(*core, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    writeBuffer(*core, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, ds2);
    const VkDescriptorSetLayout setLayouts[]{sl, sl2};
    const auto combinedLayout = h<VkPipelineLayout>(23);
    auto create = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    create.setLayoutCount = 2; create.pSetLayouts = setLayouts; core->createPipelineLayout(1, combinedLayout, create);
    core->allocateCommands(1, cp, 1, &cb); core->beginCommand(1, cb);
    const VkDescriptorSet sets[]{ds, ds2}; const std::uint32_t offsets[]{16, 32, 48};
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, combinedLayout, 0, 2, sets, 3, offsets);
    auto before = core->snapshot(1, cb, combinedLayout);
    require(before.sourceComplete && at(before, 0).dynamicOffset == 16 && at(before, 2).dynamicOffset == 32,
            "dynamic offsets did not traverse sets then ascending bindings");
    const std::uint32_t set1Offset = 64;
    core->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, combinedLayout, 1, 1, &ds2, 1, &set1Offset);
    auto after = core->snapshot(1, cb, combinedLayout);
    require(after.sourceComplete && at(after, 0).dynamicOffset == 16 && at(after, 2).dynamicOffset == 32,
            "identical-layout set1 rebinding discarded or altered valid set0 offsets");
    require(!core->validate(before), "source validation ignored later bound-set state change");
}
void imageAndSamplerProvenance() {
    auto core = std::make_unique<Core>();
    const auto image = h<VkImage>(90); const auto view = h<VkImageView>(91); const auto sampler = h<VkSampler>(92);
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO); allocation.allocationSize = 1024 * 1024;
    core->allocateMemory(1, memory, allocation);
    auto im = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    im.imageType = VK_IMAGE_TYPE_2D; im.format = VK_FORMAT_R8_UNORM; im.extent = {128, 64, 1}; im.mipLevels = 1; im.arrayLayers = 2;
    im.samples = VK_SAMPLE_COUNT_1_BIT; im.tiling = VK_IMAGE_TILING_OPTIMAL; im.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    core->createImage(1, image, im); core->bindImageMemory(1, image, memory, 512);
    auto vi = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format = im.format; vi.components.r = VK_COMPONENT_SWIZZLE_R;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1, 1}; core->createView(1, view, vi);
    auto si = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = 2;
    core->createSampler(1, sampler, si);
    VkDescriptorSetLayoutBinding binding{31, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    makeLayout(*core, &binding, 1); makeSets(*core);
    VkDescriptorImageInfo ii{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = ds; write.dstBinding = 31; write.descriptorCount = 1; write.descriptorType = binding.descriptorType; write.pImageInfo = &ii;
    core->updateSets(1, 1, &write, 0, nullptr); record(*core);
    auto first = core->snapshot(1, cb, pl);
    const auto& target = at(first, 31);
    require(first.sourceComplete && first.layout_has_binding31 == Presence::Present && target.image.extent.width == 128 &&
            target.image.extent.height == 64 && target.view.subresource.baseArrayLayer == 1 && target.image.arrayLayers == 2 &&
            target.view.identity.handle == handle(view) && target.sampler.magFilter == VK_FILTER_LINEAR && target.sampler.maxLod == 2,
            "image/view/sampler metadata omitted or mask slot not reported occupied");
    core->destroyView(1, view); core->createView(1, view, vi);
    require(!core->snapshot(1, cb, pl).sourceComplete, "view handle reuse revived stale descriptor");
    core->updateSets(1, 1, &write, 0, nullptr);
    auto rewritten = core->snapshot(1, cb, pl); require(rewritten.sourceComplete, "rewritten view did not recover");
    core->destroySampler(1, sampler); core->createSampler(1, sampler, si);
    require(!core->validate(rewritten), "sampler handle reuse revived stale descriptor");
    core->updateSets(1, 1, &write, 0, nullptr);
    core->destroyImage(1, image); core->createImage(1, image, im); core->bindImageMemory(1, image, memory, 512);
    require(!core->snapshot(1, cb, pl).sourceComplete, "image handle reuse revived stale view");
}
void incompleteResourceKeepsLayoutAndIdentity() {
    auto core = std::make_unique<Core>();
    const auto image = h<VkImage>(90); const auto view = h<VkImageView>(91);
    VkImportMemoryFdInfoKHR imported{}; imported.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR; imported.fd = -1;
    imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    auto memoryInfo = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    memoryInfo.pNext = &imported; memoryInfo.allocationSize = 65536; core->allocateMemory(1, memory, memoryInfo);
    VkExternalMemoryImageCreateInfo external{}; external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    auto im = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO); im.pNext = &external;
    im.imageType = VK_IMAGE_TYPE_2D; im.format = VK_FORMAT_R8_UNORM; im.extent = {64, 32, 1}; im.mipLevels = 1; im.arrayLayers = 1;
    core->createImage(1, image, im); core->bindImageMemory(1, image, memory, 0);
    auto vi = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO); vi.image = image; vi.format = im.format;
    core->createView(1, view, vi);
    VkDescriptorSetLayoutBinding binding{9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    makeLayout(*core, &binding, 1); makeSets(*core);
    VkDescriptorImageInfo ii{}; ii.imageView = view; ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = ds; write.dstBinding = 9; write.descriptorCount = 1; write.descriptorType = binding.descriptorType; write.pImageInfo = &ii;
    core->updateSets(1, 1, &write, 0, nullptr); record(*core);
    const auto snapshot = core->snapshot(1, cb, pl); const auto& target = at(snapshot, 9);
    require(!snapshot.sourceComplete && snapshot.layout_has_binding31 == Presence::Absent,
            "unsupported external resource invalidated independent layout coverage or claimed complete");
    require(target.view.identity.handle == handle(view) && target.image.identity.handle == handle(image) && target.image.extent.width == 64 &&
            target.image.memory.handle == handle(memory) && target.image.allocation.identity.handle == handle(memory) && !target.image.allocation.complete,
            "incomplete external resource metadata lost identities or extents");
    require(!core->validate(snapshot) && core->lostState() == 0, "known unsupported scalar resource claimed valid or caused unobserved-state loss");
}
void immutableSampler() {
    auto core = std::make_unique<Core>(); const auto sampler = h<VkSampler>(92);
    auto si = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO); core->createSampler(1, sampler, si);
    VkDescriptorSetLayoutBinding binding{25, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler};
    makeLayout(*core, &binding, 1); makeSets(*core); record(*core);
    auto snapshot = core->snapshot(1, cb, pl);
    require(snapshot.sourceComplete && at(snapshot, 25).sampler.identity.handle == handle(sampler), "immutable sampler required a nonexistent explicit write");
    core->destroySampler(1, sampler); core->createSampler(1, sampler, si);
    require(!core->validate(snapshot), "immutable sampler reuse revived layout reference");
}
void unsupportedAndCapacity() {
    {
        auto core = std::make_unique<Core>(); makeBuffer(*core);
        VkDescriptorSetLayoutBinding array{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        makeLayout(*core, &array, 1); makeSets(*core); record(*core);
        require(core->snapshot(1, cb, pl).layout_has_binding31 == Presence::Unknown, "unsupported descriptor array claimed full layout coverage");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core);
        auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET); write.dstSet = ds;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.descriptorCount = 2;
        write.pBufferInfo = reinterpret_cast<const VkDescriptorBufferInfo*>(1);
        core->updateSets(1, 1, &write, 0, nullptr);
        require(!core->snapshot(1, cb, pl).sourceComplete, "unsupported rollover descriptor write certified or read invalid payload");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core);
        VkWriteDescriptorSetInlineUniformBlock inlineInfo{};
        inlineInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK;
        auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = ds; write.descriptorCount = 1; write.pNext = &inlineInfo;
        write.pBufferInfo = reinterpret_cast<const VkDescriptorBufferInfo*>(1);
        core->updateSets(1, 1, &write, 0, nullptr);
        require(!core->snapshot(1, cb, pl).sourceComplete, "unsupported descriptor write pNext was read or certified");
    }
    {
        auto core = std::make_unique<Core>(); makeBuffer(*core);
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags{}; flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        auto create = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        create.bindingCount = 1; create.pBindings = &binding; create.pNext = &flags;
        core->createSetLayout(1, sl, create);
        auto pipeline = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &sl; core->createPipelineLayout(1, pl, pipeline);
        makeSets(*core); record(*core);
        require(core->snapshot(1, cb, pl).layout_has_binding31 == Presence::Unknown, "descriptor binding flags extension claimed complete layout");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core);
        core->updateSets(1, static_cast<std::uint32_t>(SetCapacity + 1), reinterpret_cast<const VkWriteDescriptorSet*>(1), 0, nullptr);
        require(core->lossCount(Reason::Bounds) == 1 && core->lossCount(Reason::Unsupported) == 0, "bounds reason counter was not isolated");
        require(core->lostState() == 1 && !core->snapshot(1, cb, pl).sourceComplete &&
                core->snapshot(1, cb, pl).layout_has_binding31 == Presence::Unknown, "oversize operation was read or failed to invalidate proof");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core);
        auto si = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        for (std::size_t i = 0; i < SamplerCapacity + 1; ++i) core->createSampler(3, h<VkSampler>(1000 + i), si);
        require(core->lostState() == 1 && !core->snapshot(1, cb, pl).sourceComplete, "capacity overflow silently retained proof");
        require(core->lossCount(Reason::Capacity) == 1 && core->capacityLossCount(Table::Sampler) == 1 &&
                core->capacityLossCount(Table::Set) == 0, "table capacity loss was not identified precisely");
        core->destroySampler(3, h<VkSampler>(1000)); core->createSampler(3, h<VkSampler>(9000), si);
        require(!core->snapshot(1, cb, pl).sourceComplete, "later free capacity erased observation gap");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core);
        { const auto lock = TestAccess::hold(*core); core->resetCommand(1, cb); }
        require(core->lostState() == 1 && !core->snapshot(1, cb, pl).sourceComplete, "try-lock contention was silently ignored");
        require(core->lossCount(Reason::Contention) == 1, "contention counter missing");
    }
    {
        auto core = std::make_unique<Core>(); seed(*core); core->reportUnsupported(1);
        require(!core->snapshot(1, cb, pl).sourceComplete && core->lostState() == 1, "unsupported command silently retained proof");
        require(core->lossCount(Reason::Unsupported) == 1, "unsupported counter missing");
    }
}
}

int main() {
    indexedCollisionsAndChurn(); indexedLifecycleCapacityRecovery();
    metadataAndRevision(); devicesAndHandleReuse(); descriptorCopiesAndPoolReuse(); dynamicOrderingAndBounds();
    incompatibleLayoutAndSecondaryExecution(); dynamicOrderAcrossSets(); imageAndSamplerProvenance(); incompleteResourceKeepsLayoutAndIdentity();
    immutableSampler(); unsupportedAndCapacity();
    std::cout << "passive binding metadata: 12 focused groups passed; Snapshot=" << sizeof(Snapshot) << " bytes, Core=" << sizeof(Core) << " bytes\n";
}
