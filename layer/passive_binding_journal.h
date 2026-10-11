#pragma once

// Hook threads only publish bounded, owned CPU metadata. A single background
// owner replays API ordering and constructs snapshots; no rendering thread
// enters Core, takes a lock, allocates, spins, waits or performs I/O.
#include "passive_binding_trace.h"
#include "passive_binding_extensions.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
#define FRAME_BINDINGS_JOURNAL_TLS __attribute__((tls_model("initial-exec")))
#else
#define FRAME_BINDINGS_JOURNAL_TLS
#endif

namespace frame_bindings {

class Journal {
    friend struct JournalTestAccess;
public:
    static constexpr std::size_t ProducerCapacity = 32;
    // Accommodate the selected 32-snapshot burst while the background owner
    // serializes results. Saturation still invalidates proof without blocking.
    static constexpr std::size_t LaneCapacity = 128;
    static constexpr std::uint32_t CallCapacity = 32;
    struct TransportCounters {
        std::uint64_t registry = 0, queue = 0, orderGap = 0, batch = 0;
        std::uint64_t orderWrap = 0, closedCalls = 0;
        std::uint64_t published = 0, replayed = 0;
    };

    Journal() noexcept : instance_(reserveIdentity(instances_)) {
        if (!instance_) { halted_.store(true); lose(Reason::Capacity, Transport::OrderWrap); }
    }
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    // Close never waits for hook threads. The caller normally quiesces admission
    // first. If a producer is still inside an observation, closure is explicitly
    // incomplete rather than certifying across a potentially interrupted record.
    void close() noexcept {
        if (closed_.exchange(true, std::memory_order_acq_rel)) return;
        if (active_.load(std::memory_order_acquire)) lose(Reason::Unknown, Transport::OrderGap);
    }
    bool empty() const noexcept {
        for (const auto& lane : lanes_)
            if (lane.read.load(std::memory_order_acquire) != lane.write.load(std::memory_order_acquire)) return false;
        return true;
    }
    bool finished() const noexcept {
        return closed_.load(std::memory_order_acquire) && !active_.load(std::memory_order_acquire) && empty() && (aborted_ || expected_ == tickets_.load(std::memory_order_acquire));
    }
    std::uint64_t lostState() const noexcept { return lost_.load(std::memory_order_relaxed) + core_.lostState(); }
    std::uint64_t lossCount(Reason reason) const noexcept {
        const auto i = static_cast<std::size_t>(reason);
        return (i < losses_.size() ? losses_[i].load(std::memory_order_relaxed) : 0) + core_.lossCount(reason);
    }
    std::uint64_t capacityLossCount(Table table) const noexcept { return core_.capacityLossCount(table); }
    TransportCounters transportCounters() const noexcept {
        return {transport_[0].load(), transport_[1].load(), transport_[2].load(),
                transport_[3].load(), transport_[4].load(), transport_[5].load(), published_.load(), replayed_.load()};
    }

    // One background thread exclusively owns drain(). An unpublished preceding
    // ticket defers replay, even if subsequent records are already visible.
    template<class Callback> std::size_t drain(Callback&& callback, std::size_t maxRecords = 256) noexcept {
        std::size_t drained = 0;
        while (drained < maxRecords) {
            Lane* chosen = nullptr;
            Record* record = nullptr;
            for (auto& lane : lanes_) {
                const auto read = lane.read.load(std::memory_order_relaxed);
                if (read == lane.write.load(std::memory_order_acquire)) continue;
                auto& candidate = lane.records[read % LaneCapacity];
                if ((!aborted_ && candidate.ticket == expected_) ||
                    (aborted_ && (!record || candidate.ticket < record->ticket))) {
                    chosen = &lane; record = &candidate;
                }
            }
            if (!chosen) {
                if (!aborted_ && closed_.load(std::memory_order_acquire) &&
                    !active_.load(std::memory_order_acquire) &&
                    expected_ != tickets_.load(std::memory_order_acquire)) {
                    lose(Reason::Unknown, Transport::OrderGap); aborted_ = true; continue;
                }
                break;
            }
            if (record->op == Op::Snapshot) {
                Snapshot result;
                if (!aborted_) result = core_.snapshot(record->device, record->payload.snapshot.command, record->payload.snapshot.layout);
                else result.device = record->device;
                if (lost_.load(std::memory_order_acquire)) {
                    result.sourceComplete = false; result.gpuConsumedCoherent = false;
                    result.layout_has_binding31 = Presence::Unknown;
                    result.reason = lastLoss_.load(std::memory_order_relaxed);
                }
                callback(record->payload.snapshot.cookie, result);
            } else if (!aborted_) replay(*record);
            if (!aborted_) ++expected_;
            chosen->read.fetch_add(1, std::memory_order_release);
            ++drained; replayed_.fetch_add(1, std::memory_order_relaxed);
        }
        return drained;
    }

    bool requestSnapshot(std::uint64_t device, VkCommandBuffer command, VkPipelineLayout layout, std::uint32_t cookie) noexcept {
        return publish(Op::Snapshot, device, [&](Record& r) { r.payload.snapshot = {command, layout, cookie}; });
    }
    void reportUnsupported(std::uint64_t /*device*/) noexcept { lose(Reason::Unsupported, Transport::None); }

    void createSetLayout(std::uint64_t device, VkDescriptorSetLayout value, const VkDescriptorSetLayoutCreateInfo& info) noexcept {
        if (info.bindingCount > BindingCapacity || (info.bindingCount && !info.pBindings)) { rejectBatch(); return; }
        publish(Op::CreateSetLayout, device, [&](Record& r) {
            auto& p = construct(r.payload.setLayout); p = {}; p.value = value; p.info = info;
            p.unsupported = info.pNext != nullptr; p.info.pNext = nullptr; p.info.pBindings = nullptr;
            for (std::uint32_t i = 0; i < info.bindingCount; ++i) {
                p.bindings[i] = info.pBindings[i]; p.bindings[i].pImmutableSamplers = nullptr;
                if (info.pBindings[i].pImmutableSamplers && info.pBindings[i].descriptorCount == 1 &&
                    (info.pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                     info.pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
                    p.immutable[i] = info.pBindings[i].pImmutableSamplers[0]; p.hasImmutable[i] = true;
                }
            }
        });
    }
    void createPipelineLayout(std::uint64_t device, VkPipelineLayout value, const VkPipelineLayoutCreateInfo& info) noexcept {
        if (info.setLayoutCount > SetIndexCapacity || info.pushConstantRangeCount > CallCapacity ||
            (info.setLayoutCount && !info.pSetLayouts) || (info.pushConstantRangeCount && !info.pPushConstantRanges)) { rejectBatch(); return; }
        publish(Op::CreatePipelineLayout, device, [&](Record& r) {
            auto& p = construct(r.payload.pipelineLayout); p = {}; p.value = value; p.info = info;
            p.unsupported = info.pNext != nullptr; p.info.pNext = nullptr; p.info.pSetLayouts = nullptr; p.info.pPushConstantRanges = nullptr;
            for (std::uint32_t i = 0; i < info.setLayoutCount; ++i) p.layouts[i] = info.pSetLayouts[i];
            for (std::uint32_t i = 0; i < info.pushConstantRangeCount; ++i) p.ranges[i] = info.pPushConstantRanges[i];
        });
    }
    void createPool(std::uint64_t device, VkDescriptorPool value, const VkDescriptorPoolCreateInfo& info) noexcept {
        // Core does not consume pPoolSizes; retain no caller-owned pointer.
        publish(Op::CreatePool, device, [&](Record& r) {
            r.payload.pool = {value, info, info.pNext != nullptr};
            r.payload.pool.info.pNext = nullptr; r.payload.pool.info.pPoolSizes = nullptr;
        });
    }
    void allocateSets(std::uint64_t device, const VkDescriptorSetAllocateInfo& info, const VkDescriptorSet* values) noexcept {
        if (info.descriptorSetCount > CallCapacity || (info.descriptorSetCount && (!values || !info.pSetLayouts))) { rejectBatch(); return; }
        publish(Op::AllocateSets, device, [&](Record& r) {
            auto& p = construct(r.payload.sets); p = {}; p.info = info; p.unsupported = info.pNext != nullptr;
            p.info.pNext = nullptr; p.info.pSetLayouts = nullptr;
            for (std::uint32_t i = 0; i < info.descriptorSetCount; ++i) { p.values[i] = values[i]; p.layouts[i] = info.pSetLayouts[i]; }
        });
    }
    void freeSets(std::uint64_t device, VkDescriptorPool pool, std::uint32_t count, const VkDescriptorSet* values) noexcept {
        if (count > CallCapacity || (count && !values)) { rejectBatch(); return; }
        publish(Op::FreeSets, device, [&](Record& r) {
            r.payload.freeSets = {}; r.payload.freeSets.pool = pool; r.payload.freeSets.count = count;
            for (std::uint32_t i = 0; i < count; ++i) r.payload.freeSets.values[i] = values[i];
        });
    }
    void updateSets(std::uint64_t device, std::uint32_t writeCount, const VkWriteDescriptorSet* writes,
                    std::uint32_t copyCount, const VkCopyDescriptorSet* copies) noexcept {
        if (writeCount > CallCapacity || copyCount > CallCapacity - writeCount ||
            (writeCount && !writes) || (copyCount && !copies)) { rejectBatch(); return; }
        publish(Op::UpdateSets, device, [&](Record& r) {
            auto& p = construct(r.payload.updates); p = {}; p.writes = writeCount; p.copies = copyCount;
            for (std::uint32_t i = 0; i < writeCount; ++i) {
                const auto& in = writes[i]; auto& out = construct(p.entries[i].write);
                out = {}; out.set = in.dstSet; out.binding = in.dstBinding; out.arrayElement = in.dstArrayElement;
                out.count = in.descriptorCount; out.type = in.descriptorType; out.sType = in.sType;
                out.unsupported = in.pNext != nullptr;
                const bool bufferType = in.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    in.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                    in.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                    in.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                const bool imageType = in.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                    in.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                    in.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || in.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER;
                out.hasBuffer = bufferType && in.pBufferInfo != nullptr;
                out.hasImage = imageType && in.pImageInfo != nullptr;
                // Core only supports exactly one descriptor. Larger writes are
                // retained as incomplete scalar operations, never partially read.
                if (in.descriptorCount == 1) {
                    if (out.hasBuffer) out.buffer = in.pBufferInfo[0];
                    if (out.hasImage) {
                        // Vulkan ignores sampler for sampled/storage images,
                        // and imageView/layout for standalone samplers. Read
                        // only members selected by the descriptor type.
                        if (in.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                            in.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                            out.image.sampler = in.pImageInfo[0].sampler;
                        if (in.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER) {
                            out.image.imageView = in.pImageInfo[0].imageView;
                            out.image.imageLayout = in.pImageInfo[0].imageLayout;
                        }
                    }
                }
            }
            for (std::uint32_t i = 0; i < copyCount; ++i) {
                const auto& in = copies[i]; auto& out = construct(p.entries[writeCount + i].copy);
                out = {in.srcSet, in.dstSet, in.srcBinding, in.dstBinding, in.srcArrayElement, in.dstArrayElement,
                       in.descriptorCount, in.sType, in.pNext != nullptr};
            }
        });
    }

    void allocateMemory(std::uint64_t device, VkDeviceMemory value, const VkMemoryAllocateInfo& info) noexcept {
        publish(Op::AllocateMemory, device, [&](Record& r) {
            auto& p = construct(r.payload.memory); p = {value, info, extensions::parseMemory(info.pNext)}; p.info.pNext = nullptr;
        });
    }
    void createBuffer(std::uint64_t device, VkBuffer value, const VkBufferCreateInfo& info) noexcept {
        publish(Op::CreateBuffer, device, [&](Record& r) {
            r.payload.buffer = {value, info, info.pNext != nullptr};
            r.payload.buffer.info.pNext = nullptr; r.payload.buffer.info.pQueueFamilyIndices = nullptr;
        });
    }
    void createImage(std::uint64_t device, VkImage value, const VkImageCreateInfo& info) noexcept {
        publish(Op::CreateImage, device, [&](Record& r) {
            auto& p = construct(r.payload.image); p = {value, info, extensions::parseImage(info.pNext)};
            p.info.pNext = nullptr; p.info.pQueueFamilyIndices = nullptr;
        });
    }
    void createView(std::uint64_t device, VkImageView value, const VkImageViewCreateInfo& info) noexcept {
        publish(Op::CreateView, device, [&](Record& r) {
            auto& p = construct(r.payload.view); p = {value, info, extensions::parseView(info.pNext)}; p.info.pNext = nullptr;
        });
    }
    void createSampler(std::uint64_t device, VkSampler value, const VkSamplerCreateInfo& info) noexcept {
        publish(Op::CreateSampler, device, [&](Record& r) {
            auto& p = construct(r.payload.sampler); p = {value, info, extensions::parseSampler(info.pNext)}; p.info.pNext = nullptr;
        });
    }
    void createConversion(std::uint64_t device, VkSamplerYcbcrConversion value, const VkSamplerYcbcrConversionCreateInfo& info) noexcept {
        publish(Op::CreateConversion, device, [&](Record& r) {
            auto& p = construct(r.payload.conversion); p = {value, info, extensions::parseConversion(info.pNext)}; p.info.pNext = nullptr;
        });
    }
    void bindBufferMemory(std::uint64_t device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset, bool supported = true) noexcept {
        publish(Op::BindBufferMemory, device, [&](Record& r) { r.payload.binding = {handle(buffer), memory, offset, supported}; });
    }
    void bindImageMemory(std::uint64_t device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset, bool supported = true) noexcept {
        publish(Op::BindImageMemory, device, [&](Record& r) { r.payload.binding = {handle(image), memory, offset, supported}; });
    }
    void bindBufferMemory2(std::uint64_t device, std::uint32_t count, const VkBindBufferMemoryInfo* info) noexcept {
        if (count > CallCapacity || (count && !info)) { rejectBatch(); return; }
        publish(Op::BindBufferMemoryBatch, device, [&](Record& r) {
            auto& p = construct(r.payload.memoryBatch); p.count = count;
            for (std::uint32_t i = 0; i < count; ++i)
                p.values[i] = {handle(info[i].buffer), info[i].memory, info[i].memoryOffset, !info[i].pNext};
        });
    }
    void bindImageMemory2(std::uint64_t device, std::uint32_t count, const VkBindImageMemoryInfo* info) noexcept {
        if (count > CallCapacity || (count && !info)) { rejectBatch(); return; }
        publish(Op::BindImageMemoryBatch, device, [&](Record& r) {
            auto& p = construct(r.payload.memoryBatch); p.count = count;
            for (std::uint32_t i = 0; i < count; ++i)
                p.values[i] = {handle(info[i].image), info[i].memory, info[i].memoryOffset, !info[i].pNext};
        });
    }
    void allocateCommands(std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        commandArray(Op::AllocateCommands, device, pool, count, values);
    }
    void freeCommands(std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        commandArray(Op::FreeCommands, device, pool, count, values);
    }
    void bindSets(std::uint64_t device, VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout,
                  std::uint32_t firstSet, std::uint32_t count, const VkDescriptorSet* values,
                  std::uint32_t dynamicCount, const std::uint32_t* offsets) noexcept {
        if (point != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
        if (count > SetIndexCapacity || dynamicCount > SetIndexCapacity * BindingCapacity ||
            (count && !values) || (dynamicCount && !offsets)) { rejectBatch(); return; }
        publish(Op::BindSets, device, [&](Record& r) {
            auto& p = construct(r.payload.bindSets); p = {}; p.command = command; p.point = point; p.layout = layout;
            p.first = firstSet; p.count = count; p.dynamicCount = dynamicCount;
            for (std::uint32_t i = 0; i < count; ++i) p.values[i] = values[i];
            for (std::uint32_t i = 0; i < dynamicCount; ++i) p.offsets[i] = offsets[i];
        });
    }

    void destroySetLayout(std::uint64_t d, VkDescriptorSetLayout v) noexcept { scalar(Op::DestroySetLayout, d, handle(v)); }
    void destroyPipelineLayout(std::uint64_t d, VkPipelineLayout v) noexcept { scalar(Op::DestroyPipelineLayout, d, handle(v)); }
    void resetPool(std::uint64_t d, VkDescriptorPool v) noexcept { scalar(Op::ResetPool, d, handle(v)); }
    void destroyPool(std::uint64_t d, VkDescriptorPool v) noexcept { scalar(Op::DestroyPool, d, handle(v)); }
    void freeMemory(std::uint64_t d, VkDeviceMemory v) noexcept { scalar(Op::FreeMemory, d, handle(v)); }
    void destroyBuffer(std::uint64_t d, VkBuffer v) noexcept { scalar(Op::DestroyBuffer, d, handle(v)); }
    void destroyImage(std::uint64_t d, VkImage v) noexcept { scalar(Op::DestroyImage, d, handle(v)); }
    void destroyView(std::uint64_t d, VkImageView v) noexcept { scalar(Op::DestroyView, d, handle(v)); }
    void destroySampler(std::uint64_t d, VkSampler v) noexcept { scalar(Op::DestroySampler, d, handle(v)); }
    void destroyConversion(std::uint64_t d, VkSamplerYcbcrConversion v) noexcept { scalar(Op::DestroyConversion, d, handle(v)); }
    void beginCommand(std::uint64_t d, VkCommandBuffer v) noexcept { scalar(Op::BeginCommand, d, handle(v)); }
    void endCommand(std::uint64_t d, VkCommandBuffer v) noexcept { scalar(Op::EndCommand, d, handle(v)); }
    void resetCommand(std::uint64_t d, VkCommandBuffer v) noexcept { scalar(Op::ResetCommand, d, handle(v)); }
    void resetCommandPool(std::uint64_t d, VkCommandPool v) noexcept { scalar(Op::ResetCommandPool, d, handle(v)); }
    void destroyCommandPool(std::uint64_t d, VkCommandPool v) noexcept { scalar(Op::DestroyCommandPool, d, handle(v)); }
    void executeCommands(std::uint64_t d, VkCommandBuffer v) noexcept { scalar(Op::ExecuteCommands, d, handle(v)); }
    void deviceDestroyed(std::uint64_t d) noexcept { scalar(Op::DeviceDestroyed, d, 0); }

private:
    enum class Transport : std::uint8_t { Registry, Queue, OrderGap, Batch, OrderWrap, ClosedCalls, None };
    enum class Op : std::uint8_t {
        CreateSetLayout, DestroySetLayout, CreatePipelineLayout, DestroyPipelineLayout,
        CreatePool, ResetPool, DestroyPool, AllocateSets, FreeSets, UpdateSets,
        AllocateMemory, FreeMemory, CreateBuffer, DestroyBuffer, CreateImage, DestroyImage,
        CreateView, DestroyView, CreateSampler, DestroySampler, CreateConversion, DestroyConversion,
        BindBufferMemory, BindImageMemory, BindBufferMemoryBatch, BindImageMemoryBatch, AllocateCommands, FreeCommands, BeginCommand, EndCommand,
        ResetCommand, ResetCommandPool, DestroyCommandPool, ExecuteCommands, BindSets, DeviceDestroyed, Snapshot
    };
    struct SetLayoutRecord {
        VkDescriptorSetLayout value; VkDescriptorSetLayoutCreateInfo info; bool unsupported;
        std::array<VkDescriptorSetLayoutBinding, BindingCapacity> bindings;
        std::array<VkSampler, BindingCapacity> immutable;
        std::array<bool, BindingCapacity> hasImmutable;
    };
    struct PipelineLayoutRecord {
        VkPipelineLayout value; VkPipelineLayoutCreateInfo info; bool unsupported;
        std::array<VkDescriptorSetLayout, SetIndexCapacity> layouts;
        std::array<VkPushConstantRange, CallCapacity> ranges;
    };
    struct PoolRecord { VkDescriptorPool value; VkDescriptorPoolCreateInfo info; bool unsupported; };
    struct SetsRecord {
        VkDescriptorSetAllocateInfo info; bool unsupported;
        std::array<VkDescriptorSet, CallCapacity> values;
        std::array<VkDescriptorSetLayout, CallCapacity> layouts;
    };
    struct FreeSetsRecord { VkDescriptorPool pool; std::uint32_t count; std::array<VkDescriptorSet, CallCapacity> values; };
    struct WriteRecord {
        VkDescriptorSet set; std::uint32_t binding, arrayElement, count; VkDescriptorType type; VkStructureType sType;
        bool unsupported, hasBuffer, hasImage; VkDescriptorBufferInfo buffer; VkDescriptorImageInfo image;
    };
    struct CopyRecord {
        VkDescriptorSet source, destination; std::uint32_t sourceBinding, destinationBinding, sourceElement, destinationElement, count;
        VkStructureType sType; bool unsupported;
    };
    union UpdateEntry { WriteRecord write; CopyRecord copy; };
    struct UpdateRecord { std::uint32_t writes, copies; std::array<UpdateEntry, CallCapacity> entries; };
    struct MemoryRecord { VkDeviceMemory value; VkMemoryAllocateInfo info; extensions::MemoryExtensions chain; };
    struct BufferRecord { VkBuffer value; VkBufferCreateInfo info; bool unsupported; };
    struct ImageRecord { VkImage value; VkImageCreateInfo info; extensions::ImageExtensions chain; };
    struct ViewRecord { VkImageView value; VkImageViewCreateInfo info; extensions::ViewExtensions chain; };
    struct SamplerRecord { VkSampler value; VkSamplerCreateInfo info; extensions::SamplerExtensions chain; };
    struct ConversionRecord { VkSamplerYcbcrConversion value; VkSamplerYcbcrConversionCreateInfo info; extensions::ConversionExtensions chain; };
    struct MemoryBindingRecord { std::uint64_t resource; VkDeviceMemory memory; VkDeviceSize offset; bool supported; };
    struct MemoryBindingBatchRecord { std::uint32_t count; std::array<MemoryBindingRecord, CallCapacity> values; };
    struct CommandRecord { VkCommandPool pool; std::uint32_t count; std::array<VkCommandBuffer, CallCapacity> values; };
    struct BindSetsRecord {
        VkCommandBuffer command; VkPipelineBindPoint point; VkPipelineLayout layout; std::uint32_t first, count, dynamicCount;
        std::array<VkDescriptorSet, SetIndexCapacity> values;
        std::array<std::uint32_t, SetIndexCapacity * BindingCapacity> offsets;
    };
    struct SnapshotRecord { VkCommandBuffer command; VkPipelineLayout layout; std::uint32_t cookie; };
    union Payload {
        std::uint64_t scalar;
        SetLayoutRecord setLayout; PipelineLayoutRecord pipelineLayout; PoolRecord pool; SetsRecord sets;
        FreeSetsRecord freeSets; UpdateRecord updates; MemoryRecord memory; BufferRecord buffer; ImageRecord image;
        ViewRecord view; SamplerRecord sampler; ConversionRecord conversion; MemoryBindingRecord binding;
        MemoryBindingBatchRecord memoryBatch; CommandRecord commands; BindSetsRecord bindSets; SnapshotRecord snapshot;
        Payload() noexcept {}
    };
    struct Record { std::uint64_t ticket = 0, device = 0; Op op = Op::Snapshot; Payload payload; };
    struct Lane {
        std::atomic<std::uint64_t> owner{0}, read{0}, write{0};
        std::array<Record, LaneCapacity> records;
    };
    static_assert(std::is_trivially_copyable_v<Payload>, "Metadata records must own scalar storage");
    static_assert(sizeof(Record) <= 4096, "Journal record exceeded its memory budget");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Ticket and lane atomics must be lock-free");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Admission atomics must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "Admission atomics must be lock-free");
    static_assert(std::atomic<Reason>::is_always_lock_free, "Loss atomics must be lock-free");

    template<class T> static T& construct(T& storage) noexcept { return *::new (static_cast<void*>(&storage)) T{}; }
    template<class T> static T fromHandle(std::uint64_t value) noexcept {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
        else return static_cast<T>(value);
    }
    static const VkBaseInStructure* unsupported(bool present) noexcept {
        static constexpr VkBaseInStructure invalid{VK_STRUCTURE_TYPE_MAX_ENUM, nullptr};
        return present ? &invalid : nullptr;
    }
    void rejectBatch() noexcept { lose(Reason::Bounds, Transport::Batch); }
    void lose(Reason reason, Transport transport) noexcept {
        lost_.fetch_add(1, std::memory_order_relaxed);
        lastLoss_.store(reason, std::memory_order_relaxed);
        losses_[static_cast<std::size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
        if (transport != Transport::None) transport_[static_cast<std::size_t>(transport)].fetch_add(1, std::memory_order_relaxed);
    }
    static std::uint64_t reserveIdentity(std::atomic<std::uint64_t>& serial) noexcept {
        // Cold registration only: at most 64 changed-value CAS attempts, never
        // waiting for an owner/publication. Saturation cannot reuse an ID.
        auto value = serial.load(std::memory_order_relaxed);
        for (std::size_t attempt = 0; attempt < 64; ++attempt) {
            if (!value || value == std::numeric_limits<std::uint64_t>::max()) return 0;
            if (serial.compare_exchange_strong(value, value + 1, std::memory_order_relaxed)) return value;
        }
        return 0;
    }
    Lane* producerLane() noexcept {
        // Tokens are scalar TLS and require no registration allocation. Lanes
        // are never evicted: a retired thread cannot race a new lane owner.
        // The implicit Linux layer reserves 24 bytes of initial-exec TLS at
        // dlopen. POD zero initialization has no TLS initialization guard or
        // first-hook __tls_get_addr allocation. Loader reservation failure
        // rejects the optional layer instead of modifying native rendering.
        static thread_local std::uint64_t token FRAME_BINDINGS_JOURNAL_TLS = 0;
        struct Cache { std::uint64_t instance; Lane* lane; };
        static thread_local Cache cache FRAME_BINDINGS_JOURNAL_TLS = {};
        if (!token) token = reserveIdentity(producers_);
        if (!token) { lose(Reason::Capacity, Transport::Registry); return nullptr; }
        if (cache.instance == instance_) return cache.lane;
        for (auto& lane : lanes_) {
            auto owner = lane.owner.load(std::memory_order_acquire);
            if (owner == token || (!owner && lane.owner.compare_exchange_strong(owner, token, std::memory_order_acq_rel))) {
                cache = {instance_, &lane}; return &lane;
            }
        }
        lose(Reason::Capacity, Transport::Registry); return nullptr;
    }
    template<class Fill> bool publish(Op op, std::uint64_t device, Fill&& fill) noexcept {
        active_.fetch_add(1, std::memory_order_acq_rel);
        if (closed_.load(std::memory_order_acquire) || halted_.load(std::memory_order_acquire)) {
            lose(Reason::Unknown, Transport::ClosedCalls); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        auto* lane = producerLane();
        if (!lane) { active_.fetch_sub(1, std::memory_order_release); return false; }
        const auto write = lane->write.load(std::memory_order_relaxed);
        const auto read = lane->read.load(std::memory_order_acquire);
        if (write - read >= LaneCapacity || write == std::numeric_limits<std::uint64_t>::max()) {
            lose(Reason::Capacity, Transport::Queue); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        const auto ticket = tickets_.fetch_add(1, std::memory_order_acq_rel);
        if (!ticket || ticket == std::numeric_limits<std::uint64_t>::max()) {
            halted_.store(true, std::memory_order_release); lose(Reason::Capacity, Transport::OrderWrap);
            active_.fetch_sub(1, std::memory_order_release); return false;
        }
        auto& record = lane->records[write % LaneCapacity];
        record.ticket = ticket; record.device = device; record.op = op;
        fill(record);
        lane->write.store(write + 1, std::memory_order_release);
        published_.fetch_add(1, std::memory_order_relaxed);
        active_.fetch_sub(1, std::memory_order_release);
        return true;
    }
    void scalar(Op op, std::uint64_t device, std::uint64_t value) noexcept {
        publish(op, device, [&](Record& r) { r.payload.scalar = value; });
    }
    void commandArray(Op op, std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        if (count > CallCapacity || (count && !values)) { rejectBatch(); return; }
        publish(op, device, [&](Record& r) {
            auto& p = construct(r.payload.commands); p.pool = pool; p.count = count;
            for (std::uint32_t i = 0; i < count; ++i) p.values[i] = values[i];
        });
    }
    void replay(Record& r) noexcept {
        const auto d = r.device;
        switch (r.op) {
        case Op::CreateSetLayout: {
            auto& p = r.payload.setLayout; p.info.pNext = unsupported(p.unsupported); p.info.pBindings = p.bindings.data();
            for (std::uint32_t i = 0; i < p.info.bindingCount; ++i) if (p.hasImmutable[i]) p.bindings[i].pImmutableSamplers = &p.immutable[i];
            core_.createSetLayout(d, p.value, p.info); break;
        }
        case Op::CreatePipelineLayout: {
            auto& p = r.payload.pipelineLayout; p.info.pNext = unsupported(p.unsupported);
            p.info.pSetLayouts = p.layouts.data(); p.info.pPushConstantRanges = p.ranges.data();
            core_.createPipelineLayout(d, p.value, p.info); break;
        }
        case Op::CreatePool: {
            auto& p = r.payload.pool; p.info.pNext = unsupported(p.unsupported); core_.createPool(d, p.value, p.info); break;
        }
        case Op::AllocateSets: {
            auto& p = r.payload.sets; p.info.pNext = unsupported(p.unsupported); p.info.pSetLayouts = p.layouts.data();
            core_.allocateSets(d, p.info, p.values.data()); break;
        }
        case Op::FreeSets: { auto& p = r.payload.freeSets; core_.freeSets(d, p.pool, p.count, p.values.data()); break; }
        case Op::UpdateSets: {
            auto& p = r.payload.updates;
            std::array<VkWriteDescriptorSet, CallCapacity> writes{};
            std::array<VkCopyDescriptorSet, CallCapacity> copies{};
            for (std::uint32_t i = 0; i < p.writes; ++i) {
                auto& in = p.entries[i].write; auto& out = writes[i]; out.sType = in.sType; out.pNext = unsupported(in.unsupported);
                out.dstSet = in.set; out.dstBinding = in.binding; out.dstArrayElement = in.arrayElement; out.descriptorCount = in.count;
                out.descriptorType = in.type; out.pBufferInfo = in.hasBuffer ? &in.buffer : nullptr; out.pImageInfo = in.hasImage ? &in.image : nullptr;
            }
            for (std::uint32_t i = 0; i < p.copies; ++i) {
                auto& in = p.entries[p.writes + i].copy; auto& out = copies[i]; out.sType = in.sType; out.pNext = unsupported(in.unsupported);
                out.srcSet = in.source; out.dstSet = in.destination; out.srcBinding = in.sourceBinding; out.dstBinding = in.destinationBinding;
                out.srcArrayElement = in.sourceElement; out.dstArrayElement = in.destinationElement; out.descriptorCount = in.count;
            }
            core_.updateSets(d, p.writes, writes.data(), p.copies, copies.data()); break;
        }
        case Op::AllocateMemory: { const auto& p = r.payload.memory; core_.allocateMemory(d, p.value, p.info, p.chain); break; }
        case Op::CreateBuffer: { auto& p = r.payload.buffer; p.info.pNext = unsupported(p.unsupported); core_.createBuffer(d, p.value, p.info); break; }
        case Op::CreateImage: { const auto& p = r.payload.image; core_.createImage(d, p.value, p.info, p.chain); break; }
        case Op::CreateView: { const auto& p = r.payload.view; core_.createView(d, p.value, p.info, p.chain); break; }
        case Op::CreateSampler: { const auto& p = r.payload.sampler; core_.createSampler(d, p.value, p.info, p.chain); break; }
        case Op::CreateConversion: { const auto& p = r.payload.conversion; core_.createConversion(d, p.value, p.info, p.chain); break; }
        case Op::BindBufferMemory: { const auto& p = r.payload.binding; core_.bindBufferMemory(d, fromHandle<VkBuffer>(p.resource), p.memory, p.offset, p.supported); break; }
        case Op::BindImageMemory: { const auto& p = r.payload.binding; core_.bindImageMemory(d, fromHandle<VkImage>(p.resource), p.memory, p.offset, p.supported); break; }
        case Op::BindBufferMemoryBatch: {
            const auto& p = r.payload.memoryBatch;
            for (std::uint32_t i = 0; i < p.count; ++i) {
                const auto& b = p.values[i]; core_.bindBufferMemory(d, fromHandle<VkBuffer>(b.resource), b.memory, b.offset, b.supported);
            }
            break;
        }
        case Op::BindImageMemoryBatch: {
            const auto& p = r.payload.memoryBatch;
            for (std::uint32_t i = 0; i < p.count; ++i) {
                const auto& b = p.values[i]; core_.bindImageMemory(d, fromHandle<VkImage>(b.resource), b.memory, b.offset, b.supported);
            }
            break;
        }
        case Op::AllocateCommands: { const auto& p = r.payload.commands; core_.allocateCommands(d, p.pool, p.count, p.values.data()); break; }
        case Op::FreeCommands: { const auto& p = r.payload.commands; core_.freeCommands(d, p.pool, p.count, p.values.data()); break; }
        case Op::BindSets: {
            const auto& p = r.payload.bindSets;
            core_.bindSets(d, p.command, p.point, p.layout, p.first, p.count, p.values.data(), p.dynamicCount, p.offsets.data()); break;
        }
        case Op::DestroySetLayout: core_.destroySetLayout(d, fromHandle<VkDescriptorSetLayout>(r.payload.scalar)); break;
        case Op::DestroyPipelineLayout: core_.destroyPipelineLayout(d, fromHandle<VkPipelineLayout>(r.payload.scalar)); break;
        case Op::ResetPool: core_.resetPool(d, fromHandle<VkDescriptorPool>(r.payload.scalar)); break;
        case Op::DestroyPool: core_.destroyPool(d, fromHandle<VkDescriptorPool>(r.payload.scalar)); break;
        case Op::FreeMemory: core_.freeMemory(d, fromHandle<VkDeviceMemory>(r.payload.scalar)); break;
        case Op::DestroyBuffer: core_.destroyBuffer(d, fromHandle<VkBuffer>(r.payload.scalar)); break;
        case Op::DestroyImage: core_.destroyImage(d, fromHandle<VkImage>(r.payload.scalar)); break;
        case Op::DestroyView: core_.destroyView(d, fromHandle<VkImageView>(r.payload.scalar)); break;
        case Op::DestroySampler: core_.destroySampler(d, fromHandle<VkSampler>(r.payload.scalar)); break;
        case Op::DestroyConversion: core_.destroyConversion(d, fromHandle<VkSamplerYcbcrConversion>(r.payload.scalar)); break;
        case Op::BeginCommand: core_.beginCommand(d, fromHandle<VkCommandBuffer>(r.payload.scalar)); break;
        case Op::EndCommand: core_.endCommand(d, fromHandle<VkCommandBuffer>(r.payload.scalar)); break;
        case Op::ResetCommand: core_.resetCommand(d, fromHandle<VkCommandBuffer>(r.payload.scalar)); break;
        case Op::ResetCommandPool: core_.resetCommandPool(d, fromHandle<VkCommandPool>(r.payload.scalar)); break;
        case Op::DestroyCommandPool: core_.destroyCommandPool(d, fromHandle<VkCommandPool>(r.payload.scalar)); break;
        case Op::ExecuteCommands: core_.executeCommands(d, fromHandle<VkCommandBuffer>(r.payload.scalar)); break;
        case Op::DeviceDestroyed: core_.deviceDestroyed(d); break;
        case Op::Snapshot: break;
        }
    }

    std::array<Lane, ProducerCapacity> lanes_;
    Core core_;
    const std::uint64_t instance_;
    std::atomic<std::uint64_t> tickets_{1}, lost_{0}, published_{0}, replayed_{0};
    std::atomic<std::uint32_t> active_{0};
    std::atomic<bool> closed_{false}, halted_{false};
    std::atomic<Reason> lastLoss_{Reason::Unknown};
    std::array<std::atomic<std::uint64_t>, 8> losses_{};
    std::array<std::atomic<std::uint64_t>, 6> transport_{};
    std::uint64_t expected_ = 1;
    bool aborted_ = false;
    inline static std::atomic<std::uint64_t> instances_{1}, producers_{1};
};

static_assert(sizeof(Journal) < 16 * 1024 * 1024, "Journal exceeded its fixed-memory budget");

} // namespace frame_bindings

#undef FRAME_BINDINGS_JOURNAL_TLS
