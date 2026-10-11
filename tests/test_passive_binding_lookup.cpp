// Dense public-API replay-shaped lookup fixture. --benchmark reports timings,
// but correctness never depends on a machine-specific performance threshold.
#include "layer/passive_binding_trace.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace frame_bindings;
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

int main(int argc, char** argv) {
    const bool benchmark = argc == 2 && std::string(argv[1]) == "--benchmark";
    if (argc != 1 && !benchmark) return 2;
    for (const unsigned population : {1u, 64u, 480u}) {
        auto core = std::make_unique<Core>();
        const auto pool = h<VkDescriptorPool>(100);
        const auto setLayout = h<VkDescriptorSetLayout>(200);
        const auto pipelineLayout = h<VkPipelineLayout>(300);
        const auto commandPool = h<VkCommandPool>(400);
        auto poolInfo = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; poolInfo.maxSets = 512;
        core->createPool(1, pool, poolInfo);
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        auto setInfo = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        setInfo.bindingCount = 1; setInfo.pBindings = &binding;
        core->createSetLayout(1, setLayout, setInfo);
        auto pipelineInfo = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipelineInfo.setLayoutCount = 1; pipelineInfo.pSetLayouts = &setLayout;
        core->createPipelineLayout(1, pipelineLayout, pipelineInfo);
        std::array<VkCommandBuffer, 512> commands{};
        std::array<VkDescriptorSet, 512> sets{};
        std::array<VkBuffer, 512> buffers{};
        auto allocation = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocation.descriptorPool = pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &setLayout;
        for (unsigned i = 0; i < population; ++i) {
            commands[i] = h<VkCommandBuffer>(UINT64_C(0x100000000) + i * 4096);
            sets[i] = h<VkDescriptorSet>(UINT64_C(0x200000000) + i * 4096);
            buffers[i] = h<VkBuffer>(UINT64_C(0x300000000) + i * 4096);
            auto bufferInfo = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            bufferInfo.size = 4096; bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            core->createBuffer(1, buffers[i], bufferInfo);
            const auto memory = h<VkDeviceMemory>(UINT64_C(0x400000000) + i * 4096);
            auto memoryInfo = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            memoryInfo.allocationSize = 8192;
            core->allocateMemory(1, memory, memoryInfo);
            core->bindBufferMemory(1, buffers[i], memory, 0);
            core->allocateSets(1, allocation, &sets[i]);
            core->allocateCommands(1, commandPool, 1, &commands[i]);
        }
        const unsigned iterations = benchmark ? 200000 : 2048;
        constexpr unsigned SnapshotsEvery = 4093;
        const unsigned calls = iterations * 5 + ((iterations - 1) / SnapshotsEvery + 1);
        for (unsigned round = 0; round < (benchmark ? 5u : 1u); ++round) {
            std::uint64_t checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                const unsigned index = (i * 37u) % population;
                core->beginCommand(1, commands[index]);
                VkDescriptorBufferInfo bufferInfo{buffers[index], i % 128, 256};
                auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                write.dstSet = sets[index]; write.dstBinding = 0; write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bufferInfo;
                core->updateSets(1, 1, &write, 0, nullptr);
                core->bindSets(1, commands[index], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sets[index], 0, nullptr);
                if (i % SnapshotsEvery == 0) {
                    const auto snapshot = core->snapshot(1, commands[index], pipelineLayout);
                    if (!snapshot.sourceComplete) std::abort();
                    checksum += snapshot.bindings[0].buffer.identity.handle;
                }
                core->endCommand(1, commands[index]);
                core->resetCommand(1, commands[index]);
            }
            if (core->lostState() || !checksum) std::abort();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            std::printf("population=%u round=%u calls=%u ns=%lld ns_per_call=%.3f checksum=%llu lost=%llu core_bytes=%zu\n",
                        population, round, calls, static_cast<long long>(ns), static_cast<double>(ns) / calls,
                        static_cast<unsigned long long>(checksum), static_cast<unsigned long long>(core->lostState()), sizeof(Core));
        }
    }
}
