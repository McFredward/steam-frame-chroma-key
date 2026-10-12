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
#include <cstring>
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
    // Separate bounded headers from owned bytes: short scalar/update bursts
    // no longer reserve the maximum 2.5-KiB union per observation. The combined
    // lane storage stays below the previous 128 full-record lane budget.
    static constexpr std::size_t LaneCapacity = 2048;
    static constexpr std::size_t ByteCapacity = 256 * 1024;
    static constexpr std::uint32_t CallCapacity = 32;
    static constexpr std::size_t OperationCount = 38;
    struct TransportCounters {
        std::uint64_t registry = 0, queue = 0, orderGap = 0, batch = 0;
        std::uint64_t orderWrap = 0, closedCalls = 0;
        std::uint64_t published = 0, replayed = 0;
        std::uint64_t maxLaneOccupancy = 0, maxLaneBytes = 0;
        std::uint64_t headerQueue = 0, byteQueue = 0;
    };
    struct ReceiptCounters {
        std::uint64_t requested = 0, delivered = 0, unconsumed = 0, capRefused = 0;
    };
    struct OperationCounters {
        std::uint64_t publicationLoss = 0, replayLoss = 0;
    };

    static const char* operationName(std::size_t index) noexcept {
        static constexpr std::array<const char*, OperationCount> names{
            "create_set_layout", "destroy_set_layout", "create_pipeline_layout", "destroy_pipeline_layout",
            "create_pool", "reset_pool", "destroy_pool", "allocate_sets", "free_sets", "update_sets",
            "allocate_memory", "free_memory", "create_buffer", "destroy_buffer", "create_image", "destroy_image",
            "create_view", "destroy_view", "create_sampler", "destroy_sampler", "create_conversion", "destroy_conversion",
            "bind_buffer_memory", "bind_image_memory", "bind_buffer_memory_batch", "bind_image_memory_batch",
            "allocate_commands", "free_commands", "begin_command", "end_command", "reset_command",
            "reset_command_pool", "destroy_command_pool", "execute_commands", "bind_sets", "device_destroyed", "snapshot", "import_resource_receipt"};
        return index < names.size() ? names[index] : "unknown";
    }

    Journal() noexcept : instance_(reserveIdentity(instances_)), core_(instance_) {
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
        TransportCounters result{transport_[0].load(), transport_[1].load(), transport_[2].load(),
                transport_[3].load(), transport_[4].load(), transport_[5].load(), published_.load(), replayed_.load()};
        for (const auto& lane : lanes_) {
            const auto highWater = lane.highWater.load(std::memory_order_relaxed);
            if (highWater > result.maxLaneOccupancy) result.maxLaneOccupancy = highWater;
            const auto highBytes = lane.highWaterBytes.load(std::memory_order_relaxed);
            if (highBytes > result.maxLaneBytes) result.maxLaneBytes = highBytes;
        }
        result.headerQueue = headerQueue_.load(std::memory_order_relaxed);
        result.byteQueue = byteQueue_.load(std::memory_order_relaxed);
        return result;
    }
    std::array<OperationCounters, OperationCount> operationCounters() const noexcept {
        std::array<OperationCounters, OperationCount> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = {publicationLoss_[i].load(std::memory_order_relaxed), replayLoss_[i].load(std::memory_order_relaxed)};
        return result;
    }

    ReceiptCounters receiptCounters() const noexcept {
        return {receiptRequests_.load(std::memory_order_relaxed), receiptDelivered_.load(std::memory_order_relaxed),
                receiptUnconsumed_.load(std::memory_order_relaxed), receiptCapRefused_.load(std::memory_order_relaxed)};
    }

    // One background thread exclusively owns drain(). An unpublished preceding
    // ticket defers replay, even if subsequent records are already visible.
    template<class Callback> std::size_t drain(Callback&& callback, std::size_t maxRecords = 256) noexcept {
        std::size_t drained = 0;
        while (drained < maxRecords) {
            Lane* chosen = nullptr;
            RecordHeader* record = nullptr;
            // Native API bursts commonly come from one producer. Reuse that
            // lane only when its next published ticket is exactly expected;
            // a delayed earlier producer still forces a complete search.
            if (!aborted_ && preferred_) {
                auto* candidate = head(*preferred_);
                if (candidate && candidate->ticket == expected_) { chosen = preferred_; record = candidate; }
            }
            for (auto& lane : lanes_) {
                if (chosen && !aborted_) break;
                auto* candidate = head(lane);
                if (!candidate) continue;
                if ((!aborted_ && candidate->ticket == expected_) ||
                    (aborted_ && (!record || candidate->ticket < record->ticket))) {
                    chosen = &lane; record = candidate;
                }
            }
            if (!chosen) {
                if (!aborted_ && closed_.load(std::memory_order_acquire) &&
                    !active_.load(std::memory_order_acquire) &&
                    expected_ != tickets_.load(std::memory_order_acquire)) {
                    // The earlier scan may precede the final publication and
                    // close. Recheck after acquiring producer quiescence before
                    // declaring a missing ticket; cached empty boundaries are
                    // refreshed by head() on this authoritative second scan.
                    bool precedingPublished = false;
                    for (auto& lane : lanes_) {
                        const auto* candidate = head(lane);
                        if (candidate && candidate->ticket == expected_) { precedingPublished = true; break; }
                    }
                    if (precedingPublished) continue;
                    lose(Reason::Unknown, Transport::OrderGap); aborted_ = true; continue;
                }
                break;
            }
            Record observation;
            const auto restored = restore(*chosen, *record, observation);
            if (!restored) {
                lose(Reason::Bounds, Transport::None);
                halted_.store(true, std::memory_order_release);
                aborted_ = true;
            }
            const auto lossBefore = core_.lostState();
            if (record->op == Op::Snapshot) {
                Snapshot result;
                if (!aborted_ && restored) result = core_.snapshot(record->device, observation.payload.snapshot.command, observation.payload.snapshot.layout);
                else result.device = record->device;
                if (lost_.load(std::memory_order_acquire)) {
                    result.sourceComplete = false; result.gpuConsumedCoherent = false;
                    result.layout_has_binding31 = Presence::Unknown;
                    result.reason = lastLoss_.load(std::memory_order_relaxed);
                }
                callback(restored ? observation.payload.snapshot.cookie : 0, result);
            } else if (record->op == Op::ImportResourceReceipt) {
                ImportResourceReceipt result;
                if (!aborted_ && restored) {
                    const auto& p = observation.payload.importReceipt;
                    result = core_.importResourceReceipt(record->device, p.image, p.view, p.invocationTag);
                    result.cookie = p.cookie;
                } else { result.device = record->device; result.errors = ImportJournalLoss; }
                result.journalInstance = instance_;
                result.localReplayOrder = record->ticket;
                result.lossCount = lostState();
                if (result.lossCount || aborted_ || !restored) {
                    result.resourceComplete = false; result.errors |= ImportJournalLoss;
                    if (lost_.load(std::memory_order_relaxed)) {
                        result.reason = lastLoss_.load(std::memory_order_relaxed);
                        if (result.reason == Reason::Capacity) result.errors |= ImportCapacity;
                    }
                }
                // Opt-in capture consumers must provide the typed overload.
                // Old Snapshot-only consumers remain valid but cannot silently
                // claim receipts: their omissions are counted explicitly.
                if constexpr (std::is_invocable_v<Callback&, std::uint64_t, const ImportResourceReceipt&>) {
                    callback(result.cookie, result);
                    receiptDelivered_.store(receiptDelivered_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                } else {
                    receiptUnconsumed_.store(receiptUnconsumed_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                }
            } else if (!aborted_ && restored) replay(observation);
            const auto replayLoss = core_.lostState() - lossBefore;
            const auto operationIndex = static_cast<std::size_t>(record->op);
            if (replayLoss && operationIndex < OperationCount)
                replayLoss_[operationIndex].fetch_add(replayLoss, std::memory_order_relaxed);
            if (!aborted_) ++expected_;
            preferred_ = chosen;
            // Both values have exactly one mutating background owner. Readers
            // observe atomics, but neither counter needs a locked RMW operation.
            if (restored) chosen->byteRead.store(record->byteStart + record->bytes, std::memory_order_release);
            chosen->read.store(chosen->read.load(std::memory_order_relaxed) + 1, std::memory_order_release);
            ++drained;
            replayed_.store(replayed_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        return drained;
    }

    bool requestSnapshot(std::uint64_t device, VkCommandBuffer command, VkPipelineLayout layout, std::uint32_t cookie) noexcept {
        return publish(Op::Snapshot, device, [&](Record& r) { r.payload.snapshot = {command, layout, cookie}; });
    }
    // The verified native adapter calls this immediately after publishing the
    // matching successful tagged view creation. Hooks copy only owned scalars;
    // the journal worker resolves actual lifecycle generations asynchronously.
    bool requestImportResourceReceipt(std::uint64_t device, VkImage image, VkImageView view,
                                      std::uint64_t invocationTag, std::uint64_t ownedCookie) noexcept {
        if (!device || !handle(image) || !handle(view) || !invocationTag || !ownedCookie) {
            publicationFailure(Op::ImportResourceReceipt, Reason::Bounds, Transport::Batch); return false;
        }
        auto value = receiptRequests_.load(std::memory_order_relaxed);
        bool reserved = false;
        for (std::size_t attempt = 0; attempt <= ImportTagCapacity; ++attempt) {
            if (value >= ImportTagCapacity) break;
            if (receiptRequests_.compare_exchange_strong(value, value + 1, std::memory_order_relaxed)) {
                reserved = true; break;
            }
        }
        if (!reserved) {
            receiptCapRefused_.fetch_add(1, std::memory_order_relaxed);
            publicationFailure(Op::ImportResourceReceipt, Reason::Capacity, Transport::None); return false;
        }
        return publish(Op::ImportResourceReceipt, device, [&](Record& r) {
            auto& p = construct(r.payload.importReceipt); p = {image, view, invocationTag, ownedCookie};
        });
    }
    void reportUnsupported(std::uint64_t /*device*/) noexcept { lose(Reason::Unsupported, Transport::None); }

    void createSetLayout(std::uint64_t device, VkDescriptorSetLayout value, const VkDescriptorSetLayoutCreateInfo& info) noexcept {
        if (info.bindingCount > BindingCapacity || (info.bindingCount && !info.pBindings)) { rejectBatch(Op::CreateSetLayout); return; }
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
        }, arrayBytes<bool, BindingCapacity>(offsetof(SetLayoutRecord, hasImmutable), info.bindingCount));
    }
    void createPipelineLayout(std::uint64_t device, VkPipelineLayout value, const VkPipelineLayoutCreateInfo& info) noexcept {
        if (info.setLayoutCount > SetIndexCapacity || info.pushConstantRangeCount > CallCapacity ||
            (info.setLayoutCount && !info.pSetLayouts) || (info.pushConstantRangeCount && !info.pPushConstantRanges)) { rejectBatch(Op::CreatePipelineLayout); return; }
        publish(Op::CreatePipelineLayout, device, [&](Record& r) {
            auto& p = construct(r.payload.pipelineLayout); p = {}; p.value = value; p.info = info;
            p.unsupported = info.pNext != nullptr; p.info.pNext = nullptr; p.info.pSetLayouts = nullptr; p.info.pPushConstantRanges = nullptr;
            for (std::uint32_t i = 0; i < info.setLayoutCount; ++i) p.layouts[i] = info.pSetLayouts[i];
            for (std::uint32_t i = 0; i < info.pushConstantRangeCount; ++i) p.ranges[i] = info.pPushConstantRanges[i];
        }, info.pushConstantRangeCount ? arrayBytes<VkPushConstantRange, CallCapacity>(offsetof(PipelineLayoutRecord, ranges), info.pushConstantRangeCount) : arrayBytes<VkDescriptorSetLayout, SetIndexCapacity>(offsetof(PipelineLayoutRecord, layouts), info.setLayoutCount));
    }
    void createPool(std::uint64_t device, VkDescriptorPool value, const VkDescriptorPoolCreateInfo& info) noexcept {
        // Core does not consume pPoolSizes; retain no caller-owned pointer.
        publish(Op::CreatePool, device, [&](Record& r) {
            r.payload.pool = {value, info, info.pNext != nullptr};
            r.payload.pool.info.pNext = nullptr; r.payload.pool.info.pPoolSizes = nullptr;
        });
    }
    void allocateSets(std::uint64_t device, const VkDescriptorSetAllocateInfo& info, const VkDescriptorSet* values) noexcept {
        if (info.descriptorSetCount > CallCapacity || (info.descriptorSetCount && (!values || !info.pSetLayouts))) { rejectBatch(Op::AllocateSets); return; }
        publish(Op::AllocateSets, device, [&](Record& r) {
            auto& p = construct(r.payload.sets); p = {}; p.info = info; p.unsupported = info.pNext != nullptr;
            p.info.pNext = nullptr; p.info.pSetLayouts = nullptr;
            for (std::uint32_t i = 0; i < info.descriptorSetCount; ++i) { p.values[i] = values[i]; p.layouts[i] = info.pSetLayouts[i]; }
        }, arrayBytes<VkDescriptorSetLayout, CallCapacity>(offsetof(SetsRecord, layouts), info.descriptorSetCount));
    }
    void freeSets(std::uint64_t device, VkDescriptorPool pool, std::uint32_t count, const VkDescriptorSet* values) noexcept {
        if (count > CallCapacity || (count && !values)) { rejectBatch(Op::FreeSets); return; }
        publish(Op::FreeSets, device, [&](Record& r) {
            r.payload.freeSets = {}; r.payload.freeSets.pool = pool; r.payload.freeSets.count = count;
            for (std::uint32_t i = 0; i < count; ++i) r.payload.freeSets.values[i] = values[i];
        }, arrayBytes<VkDescriptorSet, CallCapacity>(offsetof(FreeSetsRecord, values), count));
    }
    void updateSets(std::uint64_t device, std::uint32_t writeCount, const VkWriteDescriptorSet* writes,
                    std::uint32_t copyCount, const VkCopyDescriptorSet* copies) noexcept {
        if (writeCount > CallCapacity || copyCount > CallCapacity - writeCount ||
            (writeCount && !writes) || (copyCount && !copies)) { rejectBatch(Op::UpdateSets); return; }
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
        }, arrayBytes<UpdateEntry, CallCapacity>(offsetof(UpdateRecord, entries), writeCount + copyCount));
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
        (void)createImageTagged(device, value, info, 0);
    }
    bool createImageTagged(std::uint64_t device, VkImage value, const VkImageCreateInfo& info,
                           std::uint64_t invocationTag) noexcept {
        return publish(Op::CreateImage, device, [&](Record& r) {
            auto& p = construct(r.payload.image); p = {value, info, extensions::parseImage(info.pNext), invocationTag};
            p.info.pNext = nullptr; p.info.pQueueFamilyIndices = nullptr;
        });
    }
    void createView(std::uint64_t device, VkImageView value, const VkImageViewCreateInfo& info) noexcept {
        (void)createViewTagged(device, value, info, 0);
    }
    bool createViewTagged(std::uint64_t device, VkImageView value, const VkImageViewCreateInfo& info,
                          std::uint64_t invocationTag) noexcept {
        return publish(Op::CreateView, device, [&](Record& r) {
            auto& p = construct(r.payload.view); p = {value, info, extensions::parseView(info.pNext), invocationTag}; p.info.pNext = nullptr;
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
        if (count > CallCapacity || (count && !info)) { rejectBatch(Op::BindBufferMemoryBatch); return; }
        publish(Op::BindBufferMemoryBatch, device, [&](Record& r) {
            auto& p = construct(r.payload.memoryBatch); p.count = count;
            for (std::uint32_t i = 0; i < count; ++i)
                p.values[i] = {handle(info[i].buffer), info[i].memory, info[i].memoryOffset, !info[i].pNext};
        }, arrayBytes<MemoryBindingRecord, CallCapacity>(offsetof(MemoryBindingBatchRecord, values), count));
    }
    void bindImageMemory2(std::uint64_t device, std::uint32_t count, const VkBindImageMemoryInfo* info) noexcept {
        if (count > CallCapacity || (count && !info)) { rejectBatch(Op::BindImageMemoryBatch); return; }
        publish(Op::BindImageMemoryBatch, device, [&](Record& r) {
            auto& p = construct(r.payload.memoryBatch); p.count = count;
            for (std::uint32_t i = 0; i < count; ++i)
                p.values[i] = {handle(info[i].image), info[i].memory, info[i].memoryOffset, !info[i].pNext};
        }, arrayBytes<MemoryBindingRecord, CallCapacity>(offsetof(MemoryBindingBatchRecord, values), count));
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
            (count && !values) || (dynamicCount && !offsets)) { rejectBatch(Op::BindSets); return; }
        publish(Op::BindSets, device, [&](Record& r) {
            auto& p = construct(r.payload.bindSets); p = {}; p.command = command; p.point = point; p.layout = layout;
            p.first = firstSet; p.count = count; p.dynamicCount = dynamicCount;
            for (std::uint32_t i = 0; i < count; ++i) p.values[i] = values[i];
            for (std::uint32_t i = 0; i < dynamicCount; ++i) p.offsets[i] = offsets[i];
        }, dynamicCount ? arrayBytes<std::uint32_t, SetIndexCapacity * BindingCapacity>(offsetof(BindSetsRecord, offsets), dynamicCount) : arrayBytes<VkDescriptorSet, SetIndexCapacity>(offsetof(BindSetsRecord, values), count));
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
        ResetCommand, ResetCommandPool, DestroyCommandPool, ExecuteCommands, BindSets, DeviceDestroyed, Snapshot, ImportResourceReceipt
    };
    static_assert(static_cast<std::size_t>(Op::ImportResourceReceipt) + 1 == OperationCount, "Operation names and counters must cover every record");
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
    struct ImageRecord { VkImage value; VkImageCreateInfo info; extensions::ImageExtensions chain; std::uint64_t invocationTag; };
    struct ViewRecord { VkImageView value; VkImageViewCreateInfo info; extensions::ViewExtensions chain; std::uint64_t invocationTag; };
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
    struct ImportReceiptRecord { VkImage image; VkImageView view; std::uint64_t invocationTag, cookie; };
    union Payload {
        std::uint64_t scalar;
        SetLayoutRecord setLayout; PipelineLayoutRecord pipelineLayout; PoolRecord pool; SetsRecord sets;
        FreeSetsRecord freeSets; UpdateRecord updates; MemoryRecord memory; BufferRecord buffer; ImageRecord image;
        ViewRecord view; SamplerRecord sampler; ConversionRecord conversion; MemoryBindingRecord binding;
        MemoryBindingBatchRecord memoryBatch; CommandRecord commands; BindSetsRecord bindSets; SnapshotRecord snapshot; ImportReceiptRecord importReceipt;
        Payload() noexcept {}
    };
    struct Record { std::uint64_t ticket = 0, device = 0; Op op = Op::Snapshot; Payload payload; };
    struct RecordHeader {
        std::uint64_t ticket = 0, device = 0, byteStart = 0;
        std::uint32_t bytes = 0;
        Op op = Op::Snapshot;
    };
    struct Lane {
        std::atomic<std::uint64_t> owner{0}, read{0}, write{0}, highWater{0};
        std::atomic<std::uint64_t> byteRead{0}, highWaterBytes{0};
        std::uint64_t acquiredWrite = 0; // Background owner only.
        std::uint64_t byteWrite = 0; // Registered producer only.
        std::array<RecordHeader, LaneCapacity> records;
        std::array<unsigned char, ByteCapacity> bytes;
    };
    template<class... T> struct OwnedRepresentation : std::bool_constant<
        (... && (std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>))> {};
    static_assert(OwnedRepresentation<SetLayoutRecord, PipelineLayoutRecord, PoolRecord, SetsRecord,
        FreeSetsRecord, WriteRecord, CopyRecord, UpdateEntry, UpdateRecord, MemoryRecord, BufferRecord,
        ImageRecord, ViewRecord, SamplerRecord, ConversionRecord, MemoryBindingRecord,
        MemoryBindingBatchRecord, CommandRecord, BindSetsRecord, SnapshotRecord, ImportReceiptRecord>::value,
        "Owned compact metadata must have standard layout and a trivially copyable representation");
    static_assert(sizeof(RecordHeader) == 32, "Compact headers exceeded their bounded lane budget");
    static_assert(sizeof(Lane) <= 128 * sizeof(Record), "Compact lanes must not increase the old memory budget");
    static_assert(std::is_trivially_copyable_v<Payload>, "Metadata records must own scalar storage");
    static_assert(sizeof(Record) <= 4096, "Journal record exceeded its memory budget");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Ticket and lane atomics must be lock-free");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Admission atomics must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "Admission atomics must be lock-free");
    static_assert(std::atomic<Reason>::is_always_lock_free, "Loss atomics must be lock-free");

    template<class T> static T& construct(T& storage) noexcept { return *::new (static_cast<void*>(&storage)) T{}; }
    template<class T, std::size_t N> static constexpr std::uint32_t arrayBytes(std::size_t offset, std::uint32_t count) noexcept {
        static_assert(std::is_standard_layout_v<std::array<T, N>> && sizeof(std::array<T, N>) == N * sizeof(T),
                      "Compact prefixes require contiguous array storage without extra representation bytes");
        return static_cast<std::uint32_t>(offset + count * sizeof(T));
    }
    static std::uint32_t fullBytes(Op op) noexcept {
        switch (op) {
        case Op::CreateSetLayout: return sizeof(SetLayoutRecord);
        case Op::CreatePipelineLayout: return sizeof(PipelineLayoutRecord);
        case Op::CreatePool: return sizeof(PoolRecord);
        case Op::AllocateSets: return sizeof(SetsRecord);
        case Op::FreeSets: return sizeof(FreeSetsRecord);
        case Op::UpdateSets: return sizeof(UpdateRecord);
        case Op::AllocateMemory: return sizeof(MemoryRecord);
        case Op::CreateBuffer: return sizeof(BufferRecord);
        case Op::CreateImage: return sizeof(ImageRecord);
        case Op::CreateView: return sizeof(ViewRecord);
        case Op::CreateSampler: return sizeof(SamplerRecord);
        case Op::CreateConversion: return sizeof(ConversionRecord);
        case Op::BindBufferMemory: case Op::BindImageMemory: return sizeof(MemoryBindingRecord);
        case Op::BindBufferMemoryBatch: case Op::BindImageMemoryBatch: return sizeof(MemoryBindingBatchRecord);
        case Op::AllocateCommands: case Op::FreeCommands: return sizeof(CommandRecord);
        case Op::BindSets: return sizeof(BindSetsRecord);
        case Op::Snapshot: return sizeof(SnapshotRecord);
        case Op::ImportResourceReceipt: return sizeof(ImportReceiptRecord);
        default: return sizeof(std::uint64_t);
        }
    }
    static std::uint32_t minimumBytes(Op op) noexcept {
        switch (op) {
        case Op::CreateSetLayout: return offsetof(SetLayoutRecord, hasImmutable);
        case Op::CreatePipelineLayout: return offsetof(PipelineLayoutRecord, layouts);
        case Op::AllocateSets: return offsetof(SetsRecord, layouts);
        case Op::FreeSets: return offsetof(FreeSetsRecord, values);
        case Op::UpdateSets: return offsetof(UpdateRecord, entries);
        case Op::BindBufferMemoryBatch: case Op::BindImageMemoryBatch: return offsetof(MemoryBindingBatchRecord, values);
        case Op::AllocateCommands: case Op::FreeCommands: return offsetof(CommandRecord, values);
        case Op::BindSets: return offsetof(BindSetsRecord, values);
        default: return fullBytes(op);
        }
    }
    static std::uint32_t usedBytes(const Record& r) noexcept {
        switch (r.op) {
        case Op::CreateSetLayout: {
            const auto count = r.payload.setLayout.info.bindingCount;
            return count <= BindingCapacity ? arrayBytes<bool, BindingCapacity>(offsetof(SetLayoutRecord, hasImmutable), count) : 0;
        }
        case Op::CreatePipelineLayout: {
            const auto& info = r.payload.pipelineLayout.info;
            if (info.setLayoutCount > SetIndexCapacity || info.pushConstantRangeCount > CallCapacity) return 0;
            return info.pushConstantRangeCount ? arrayBytes<VkPushConstantRange, CallCapacity>(offsetof(PipelineLayoutRecord, ranges), info.pushConstantRangeCount)
                : arrayBytes<VkDescriptorSetLayout, SetIndexCapacity>(offsetof(PipelineLayoutRecord, layouts), info.setLayoutCount);
        }
        case Op::AllocateSets: {
            const auto count = r.payload.sets.info.descriptorSetCount;
            return count <= CallCapacity ? arrayBytes<VkDescriptorSetLayout, CallCapacity>(offsetof(SetsRecord, layouts), count) : 0;
        }
        case Op::FreeSets: {
            const auto count = r.payload.freeSets.count;
            return count <= CallCapacity ? arrayBytes<VkDescriptorSet, CallCapacity>(offsetof(FreeSetsRecord, values), count) : 0;
        }
        case Op::UpdateSets: {
            const auto& p = r.payload.updates;
            if (p.writes > CallCapacity || p.copies > CallCapacity - p.writes) return 0;
            return arrayBytes<UpdateEntry, CallCapacity>(offsetof(UpdateRecord, entries), p.writes + p.copies);
        }
        case Op::BindBufferMemoryBatch: case Op::BindImageMemoryBatch: {
            const auto count = r.payload.memoryBatch.count;
            return count <= CallCapacity ? arrayBytes<MemoryBindingRecord, CallCapacity>(offsetof(MemoryBindingBatchRecord, values), count) : 0;
        }
        case Op::AllocateCommands: case Op::FreeCommands: {
            const auto count = r.payload.commands.count;
            return count <= CallCapacity ? arrayBytes<VkCommandBuffer, CallCapacity>(offsetof(CommandRecord, values), count) : 0;
        }
        case Op::BindSets: {
            const auto& p = r.payload.bindSets;
            if (p.count > SetIndexCapacity || p.dynamicCount > SetIndexCapacity * BindingCapacity) return 0;
            return p.dynamicCount ? arrayBytes<std::uint32_t, SetIndexCapacity * BindingCapacity>(offsetof(BindSetsRecord, offsets), p.dynamicCount)
                : arrayBytes<VkDescriptorSet, SetIndexCapacity>(offsetof(BindSetsRecord, values), p.count);
        }
        default: return fullBytes(r.op);
        }
    }
    static void* initializePayload(Record& r) noexcept {
        // Explicitly start the chosen member's lifetime before restoring bytes.
        // Value initialization also gives every unused array tail a valid value.
        switch (r.op) {
        case Op::CreateSetLayout: return &construct(r.payload.setLayout);
        case Op::CreatePipelineLayout: return &construct(r.payload.pipelineLayout);
        case Op::CreatePool: return &construct(r.payload.pool);
        case Op::AllocateSets: return &construct(r.payload.sets);
        case Op::FreeSets: return &construct(r.payload.freeSets);
        case Op::UpdateSets: return &construct(r.payload.updates);
        case Op::AllocateMemory: return &construct(r.payload.memory);
        case Op::CreateBuffer: return &construct(r.payload.buffer);
        case Op::CreateImage: return &construct(r.payload.image);
        case Op::CreateView: return &construct(r.payload.view);
        case Op::CreateSampler: return &construct(r.payload.sampler);
        case Op::CreateConversion: return &construct(r.payload.conversion);
        case Op::BindBufferMemory: case Op::BindImageMemory: return &construct(r.payload.binding);
        case Op::BindBufferMemoryBatch: case Op::BindImageMemoryBatch: return &construct(r.payload.memoryBatch);
        case Op::AllocateCommands: case Op::FreeCommands: return &construct(r.payload.commands);
        case Op::BindSets: return &construct(r.payload.bindSets);
        case Op::Snapshot: return &construct(r.payload.snapshot);
        case Op::ImportResourceReceipt: return &construct(r.payload.importReceipt);
        default: return &construct(r.payload.scalar);
        }
    }
    static void copyToLane(Lane& lane, std::uint64_t start, const void* source, std::uint32_t bytes) noexcept {
        const auto offset = static_cast<std::size_t>(start % ByteCapacity);
        const auto first = bytes < ByteCapacity - offset ? bytes : ByteCapacity - offset;
        std::memcpy(lane.bytes.data() + offset, source, first);
        if (first != bytes)
            std::memcpy(lane.bytes.data(), static_cast<const unsigned char*>(source) + first, bytes - first);
    }
    static void copyFromLane(const Lane& lane, std::uint64_t start, void* destination, std::uint32_t bytes) noexcept {
        const auto offset = static_cast<std::size_t>(start % ByteCapacity);
        const auto first = bytes < ByteCapacity - offset ? bytes : ByteCapacity - offset;
        std::memcpy(destination, lane.bytes.data() + offset, first);
        if (first != bytes)
            std::memcpy(static_cast<unsigned char*>(destination) + first, lane.bytes.data(), bytes - first);
    }
    static bool restore(Lane& lane, const RecordHeader& header, Record& record) noexcept {
        if (static_cast<std::size_t>(header.op) >= OperationCount ||
            header.bytes < minimumBytes(header.op) || header.bytes > fullBytes(header.op) ||
            header.byteStart != lane.byteRead.load(std::memory_order_relaxed) ||
            header.byteStart > std::numeric_limits<std::uint64_t>::max() - header.bytes) return false;
        record.ticket = header.ticket; record.device = header.device; record.op = header.op;
        auto* destination = initializePayload(record);
        if (header.op == Op::UpdateSets) {
            // The initialized array has WriteRecord members active. Select each
            // CopyRecord member before copying its representation (C++17 does
            // not start an inactive union member's lifetime via memcpy).
            std::array<std::uint32_t, 2> counts{};
            if (header.bytes < sizeof(counts)) return false;
            copyFromLane(lane, header.byteStart, counts.data(), sizeof(counts));
            if (counts[0] > CallCapacity || counts[1] > CallCapacity - counts[0]) return false;
            for (std::uint32_t i = 0; i < counts[1]; ++i)
                construct(record.payload.updates.entries[counts[0] + i].copy);
        }
        copyFromLane(lane, header.byteStart, destination, header.bytes);
        return usedBytes(record) == header.bytes;
    }
    template<class T> static T fromHandle(std::uint64_t value) noexcept {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
        else return static_cast<T>(value);
    }
    static const VkBaseInStructure* unsupported(bool present) noexcept {
        static constexpr VkBaseInStructure invalid{VK_STRUCTURE_TYPE_MAX_ENUM, nullptr};
        return present ? &invalid : nullptr;
    }
    void rejectBatch(Op op) noexcept { publicationFailure(op, Reason::Bounds, Transport::Batch); }
    void publicationFailure(Op op, Reason reason, Transport transport) noexcept {
        publicationLoss_[static_cast<std::size_t>(op)].fetch_add(1, std::memory_order_relaxed);
        lose(reason, transport);
    }
    static RecordHeader* head(Lane& lane) noexcept {
        const auto read = lane.read.load(std::memory_order_relaxed);
        // A release/acquire publication boundary covers every preceding slot.
        // Retain that boundary until exhausted; never inspect an unpublished
        // slot or allow a producer to reuse it before read is released.
        if (read == lane.acquiredWrite) {
            lane.acquiredWrite = lane.write.load(std::memory_order_acquire);
            if (read == lane.acquiredWrite) return nullptr;
        }
        return &lane.records[read % LaneCapacity];
    }
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
    template<class Fill> bool publish(Op op, std::uint64_t device, Fill&& fill, std::uint32_t bytes = 0) noexcept {
        active_.fetch_add(1, std::memory_order_acq_rel);
        if (closed_.load(std::memory_order_acquire) || halted_.load(std::memory_order_acquire)) {
            publicationFailure(op, Reason::Unknown, Transport::ClosedCalls); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        auto* lane = producerLane();
        if (!lane) {
            // producerLane already records the exact aggregate registry loss.
            publicationLoss_[static_cast<std::size_t>(op)].fetch_add(1, std::memory_order_relaxed);
            active_.fetch_sub(1, std::memory_order_release); return false;
        }
        const auto write = lane->write.load(std::memory_order_relaxed);
        const auto read = lane->read.load(std::memory_order_acquire);
        if (write - read >= LaneCapacity) {
            headerQueue_.fetch_add(1, std::memory_order_relaxed);
            publicationFailure(op, Reason::Capacity, Transport::Queue); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        if (!bytes) bytes = fullBytes(op);
        const auto byteRead = lane->byteRead.load(std::memory_order_acquire);
        const auto byteWrite = lane->byteWrite;
        if (!bytes || bytes > fullBytes(op)) {
            publicationFailure(op, Reason::Bounds, Transport::Batch); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        if (byteWrite - byteRead > ByteCapacity || bytes > ByteCapacity - (byteWrite - byteRead)) {
            byteQueue_.fetch_add(1, std::memory_order_relaxed);
            publicationFailure(op, Reason::Capacity, Transport::Queue); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        if (write == std::numeric_limits<std::uint64_t>::max() || byteWrite > std::numeric_limits<std::uint64_t>::max() - bytes) {
            halted_.store(true, std::memory_order_release);
            publicationFailure(op, Reason::Capacity, Transport::OrderWrap); active_.fetch_sub(1, std::memory_order_release); return false;
        }
        const auto ticket = tickets_.fetch_add(1, std::memory_order_acq_rel);
        if (!ticket || ticket == std::numeric_limits<std::uint64_t>::max()) {
            halted_.store(true, std::memory_order_release); publicationFailure(op, Reason::Capacity, Transport::OrderWrap);
            active_.fetch_sub(1, std::memory_order_release); return false;
        }
        // Fill after ticket reservation so an interrupted producer retains its
        // exact predecessor position. The stack object owns active typed members;
        // the byte pool itself is never interpreted as a C++ object.
        Record record;
        record.ticket = ticket; record.device = device; record.op = op;
        fill(record);
        copyToLane(*lane, byteWrite, &record.payload, bytes);
        lane->records[write % LaneCapacity] = {ticket, device, byteWrite, bytes, op};
        lane->byteWrite = byteWrite + bytes;
        // Only this producer writes its lane maximum. Store on a new maximum;
        // no CAS retry or counter work is needed once a burst reached capacity.
        const auto occupancy = write - read + 1;
        if (occupancy > lane->highWater.load(std::memory_order_relaxed))
            lane->highWater.store(occupancy, std::memory_order_relaxed);
        const auto byteOccupancy = byteWrite - byteRead + bytes;
        if (byteOccupancy > lane->highWaterBytes.load(std::memory_order_relaxed))
            lane->highWaterBytes.store(byteOccupancy, std::memory_order_relaxed);
        lane->write.store(write + 1, std::memory_order_release);
        published_.fetch_add(1, std::memory_order_relaxed);
        active_.fetch_sub(1, std::memory_order_release);
        return true;
    }
    void scalar(Op op, std::uint64_t device, std::uint64_t value) noexcept {
        publish(op, device, [&](Record& r) { r.payload.scalar = value; });
    }
    void commandArray(Op op, std::uint64_t device, VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* values) noexcept {
        if (count > CallCapacity || (count && !values)) { rejectBatch(op); return; }
        publish(op, device, [&](Record& r) {
            auto& p = construct(r.payload.commands); p.pool = pool; p.count = count;
            for (std::uint32_t i = 0; i < count; ++i) p.values[i] = values[i];
        }, arrayBytes<VkCommandBuffer, CallCapacity>(offsetof(CommandRecord, values), count));
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
        case Op::CreateImage: { const auto& p = r.payload.image; core_.createImage(d, p.value, p.info, p.chain, p.invocationTag); break; }
        case Op::CreateView: { const auto& p = r.payload.view; core_.createView(d, p.value, p.info, p.chain, p.invocationTag); break; }
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
        case Op::Snapshot: case Op::ImportResourceReceipt: break;
        }
    }

    std::array<Lane, ProducerCapacity> lanes_;
    const std::uint64_t instance_;
    Core core_;
    std::atomic<std::uint64_t> tickets_{1}, lost_{0}, published_{0}, replayed_{0};
    std::atomic<std::uint32_t> active_{0};
    std::atomic<bool> closed_{false}, halted_{false};
    std::atomic<Reason> lastLoss_{Reason::Unknown};
    std::array<std::atomic<std::uint64_t>, 8> losses_{};
    std::array<std::atomic<std::uint64_t>, 6> transport_{};
    std::array<std::atomic<std::uint64_t>, OperationCount> publicationLoss_{}, replayLoss_{};
    std::atomic<std::uint64_t> headerQueue_{0}, byteQueue_{0};
    std::atomic<std::uint64_t> receiptRequests_{0}, receiptDelivered_{0}, receiptUnconsumed_{0}, receiptCapRefused_{0};
    Lane* preferred_ = nullptr; // Background owner only; never read by hooks.
    std::uint64_t expected_ = 1;
    bool aborted_ = false;
    inline static std::atomic<std::uint64_t> instances_{1}, producers_{1};
};

static_assert(sizeof(Journal) < 16 * 1024 * 1024, "Journal exceeded its fixed-memory budget");

} // namespace frame_bindings

#undef FRAME_BINDINGS_JOURNAL_TLS
