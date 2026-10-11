#include "layer/passive_binding_journal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <string_view>
#include <vector>

namespace {
thread_local bool monitorHookAllocations = false;
std::atomic<std::uint64_t> hookAllocations{0};
std::uint64_t checks = 0;
}
#if defined(__GNUC__) || defined(__clang__)
#define JOURNAL_TEST_ALLOCATION __attribute__((noinline))
#else
#define JOURNAL_TEST_ALLOCATION
#endif
// Keep the replacement allocation boundary observable to instrumentation and
// avoid GCC diagnosing its matching malloc/free wrappers after inlining.
JOURNAL_TEST_ALLOCATION void* operator new(std::size_t size) {
    if (monitorHookAllocations) hookAllocations.fetch_add(1);
    if (auto* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}
JOURNAL_TEST_ALLOCATION void* operator new[](std::size_t size) { return ::operator new(size); }
JOURNAL_TEST_ALLOCATION void operator delete(void* value) noexcept { std::free(value); }
JOURNAL_TEST_ALLOCATION void operator delete[](void* value) noexcept { std::free(value); }
JOURNAL_TEST_ALLOCATION void operator delete(void* value, std::size_t) noexcept { std::free(value); }
JOURNAL_TEST_ALLOCATION void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
#undef JOURNAL_TEST_ALLOCATION

namespace frame_bindings {
struct JournalTestAccess {
    static void delayedSnapshot(Journal& journal, std::atomic<bool>& entered, std::atomic<bool>& release, std::uint32_t cookie) {
        journal.publish(Journal::Op::Snapshot, 1, [&](Journal::Record& record) {
            record.payload.snapshot = {reinterpret_cast<VkCommandBuffer>(60), reinterpret_cast<VkPipelineLayout>(20), cookie};
            entered.store(true, std::memory_order_release);
            // Deliberately interrupted producer fixture, never production code.
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    }
    static void ticket(Journal& journal, std::uint64_t value) { journal.tickets_.store(value); }
    static void gap(Journal& journal) { journal.tickets_.fetch_add(1); }
    static std::uint64_t reserveIdentity(std::atomic<std::uint64_t>& serial) { return Journal::reserveIdentity(serial); }
    static bool validate(Journal& journal, const Snapshot& previous) { return journal.core_.validate(previous); }
    static std::size_t largeUpdateBytes() {
        return offsetof(Journal::UpdateRecord, entries) + Journal::CallCapacity * sizeof(Journal::UpdateEntry);
    }
    static void bytePosition(Journal& journal, std::uint64_t value) {
        auto* lane = journal.producerLane();
        if (!journal.empty()) std::abort();
        lane->byteRead.store(value); lane->byteWrite = value;
    }
    static void corruptHead(Journal& journal, unsigned kind) {
        auto* lane = journal.producerLane();
        auto& header = lane->records[lane->read.load() % Journal::LaneCapacity];
        if (kind == 0) header.op = static_cast<Journal::Op>(255);
        if (kind == 1) header.bytes = 1;
        if (kind == 2) ++header.byteStart;
        if (kind == 3) {
            const std::array<std::uint32_t, 2> counts{Journal::CallCapacity + 1, 0};
            Journal::copyToLane(*lane, header.byteStart, counts.data(), sizeof(counts));
        }
    }
};
}
namespace {
using namespace frame_bindings;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T out{}; out.sType = type; return out; }
const auto sl = h<VkDescriptorSetLayout>(10);
const auto pl = h<VkPipelineLayout>(20);
const auto dp = h<VkDescriptorPool>(30);
const auto ds = h<VkDescriptorSet>(40);
const auto cp = h<VkCommandPool>(50);
const auto cb = h<VkCommandBuffer>(60);
const auto buffer = h<VkBuffer>(70);
const auto memory = h<VkDeviceMemory>(80);

void seed(Journal& journal, std::uint64_t device = 1) {
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = 16384; allocation.memoryTypeIndex = 2;
    journal.allocateMemory(device, memory, allocation);
    auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    create.size = 8192; create.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    journal.createBuffer(device, buffer, create);
    journal.bindBufferMemory(device, buffer, memory, 256);
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, reinterpret_cast<const VkSampler*>(1)};
    auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = 1; layout.pBindings = &binding;
    journal.createSetLayout(device, sl, layout);
    auto pipeline = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &sl;
    journal.createPipelineLayout(device, pl, pipeline);
    auto pool = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool.maxSets = 16;
    journal.createPool(device, dp, pool);
    auto sets = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    sets.descriptorPool = dp; sets.descriptorSetCount = 1; sets.pSetLayouts = &sl;
    journal.allocateSets(device, sets, &ds);
    VkDescriptorBufferInfo bi{buffer, 64, 128};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = ds; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bi;
    write.pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(1);
    journal.updateSets(device, 1, &write, 0, nullptr);
    journal.allocateCommands(device, cp, 1, &cb);
    journal.beginCommand(device, cb);
    journal.bindSets(device, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
}
const Binding& at(const Snapshot& snapshot, std::uint32_t value) {
    for (const auto& binding : snapshot.bindings) if (binding.binding == value) return binding;
    std::abort();
}
Snapshot snap(Journal& journal, std::uint64_t device = 1, std::uint32_t cookie = 0) {
    require(journal.requestSnapshot(device, cb, pl, cookie), "snapshot request rejected unexpectedly");
    Snapshot out; bool received = false;
    while (journal.drain([&](std::uint32_t id, const Snapshot& value) noexcept {
        require(id == cookie, "snapshot cookie changed"); out = value; received = true;
    })) {}
    require(received, "ordered snapshot callback omitted");
    return out;
}
void writeBuffer(Journal& journal, VkDeviceSize offset, VkDescriptorSet destination = ds) {
    VkDescriptorBufferInfo bi{buffer, offset, 128};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = destination; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bi;
    write.pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(1);
    journal.updateSets(1, 1, &write, 0, nullptr);
    bi.buffer = VK_NULL_HANDLE; bi.offset = 9999; write.dstSet = VK_NULL_HANDLE;
}
void writeLargeBatch(Journal& journal, VkDeviceSize offset) {
    std::array<VkDescriptorBufferInfo, Journal::CallCapacity> buffers{};
    std::array<VkWriteDescriptorSet, Journal::CallCapacity> writes{};
    for (std::size_t i = 0; i < writes.size(); ++i) {
        buffers[i] = {buffer, offset + i, 128};
        writes[i] = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[i].dstSet = ds; writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[i].pBufferInfo = &buffers[i];
    }
    journal.updateSets(1, writes.size(), writes.data(), 0, nullptr);
    buffers = {}; writes = {};
}
void ownershipAndOrdering() {
    auto journal = std::make_unique<Journal>();
    require(!journal->finished() && journal->empty(), "open empty journal falsely finished");
    monitorHookAllocations = true; seed(*journal); monitorHookAllocations = false;
    const auto initial = snap(*journal);
    require(initial.sourceComplete && !initial.gpuConsumedCoherent && initial.layout_has_binding31 == Presence::Absent,
            "owned borrowed stack arrays or source-only flags were lost");
    require(at(initial, 0).effectiveOffset == 64 && at(initial, 0).buffer.memoryOffset == 256,
            "scalar metadata changed after caller storage expired");
    require(journal->requestSnapshot(1, cb, pl, 7), "first deferred snapshot rejected");
    writeBuffer(*journal, 96);
    require(journal->requestSnapshot(1, cb, pl, 8), "second deferred snapshot rejected");
    std::vector<Snapshot> snapshots;
    journal->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie == 7 + snapshots.size(), "snapshot observation order changed"); snapshots.push_back(value);
    });
    require(snapshots.size() == 2 && at(snapshots[0], 0).effectiveOffset == 64 && at(snapshots[1], 0).effectiveOffset == 96,
            "background snapshot used serialization-time rather than recording-time descriptor state");
    require(!JournalTestAccess::validate(*journal, initial), "later descriptor revision revived prior recording proof");
    const auto counters = journal->transportCounters();
    require(counters.published == counters.replayed && journal->lostState() == 0, "ordered replay lost ordinary observations");
    journal->close(); require(journal->finished(), "drained closed journal did not finish");
    require(hookAllocations.load() == 0, "hook publication allocated heap memory");
}
void selectedSnapshotBurst() {
    auto journal = std::make_unique<Journal>();
    monitorHookAllocations = true;
    seed(*journal);
    // No consumer or test backpressure: represent all 32 selected draws plus
    // adjacent descriptor mutations before the background owner is scheduled.
    for (std::uint32_t i = 0; i < 32; ++i) {
        writeBuffer(*journal, 32 * i);
        require(journal->requestSnapshot(1, cb, pl, i), "selected burst exceeded journal capacity");
        writeBuffer(*journal, 32 * i + 16);
    }
    monitorHookAllocations = false;
    std::uint32_t received = 0;
    journal->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie == received++, "selected burst reordered snapshots");
        require(value.sourceComplete && at(value, 0).effectiveOffset == 32 * cookie,
                "selected burst lost recording-time metadata");
    });
    require(received == 32 && journal->lostState() == 0,
            "unthrottled selected burst lost metadata");
    require(hookAllocations.load() == 0, "selected burst allocated on a hook");
    journal->close();
    require(journal->finished(), "selected burst did not finish after closure");
}
void writeBeforeCopyBatch() {
    auto journal = std::make_unique<Journal>(); seed(*journal);
    const auto second = h<VkDescriptorSet>(41);
    auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = dp; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &sl;
    journal->allocateSets(1, allocate, &second);
    VkDescriptorBufferInfo bi{buffer, 160, 128};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = ds; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bi;
    write.pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(1);
    auto copy = info<VkCopyDescriptorSet>(VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET);
    copy.srcSet = ds; copy.dstSet = second; copy.descriptorCount = 1;
    journal->updateSets(1, 1, &write, 1, &copy);
    bi = {}; copy = {}; write = {};
    journal->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &second, 0, nullptr);
    auto copied = snap(*journal);
    require(copied.sourceComplete && at(copied, 0).effectiveOffset == 160, "writes-before-copies batch lost input lifetime or atomicity");
    journal->freeSets(1, dp, 1, &second);
    journal->allocateSets(1, allocate, &second);
    require(!snap(*journal).sourceComplete, "set handle reuse revived old command dependency");
    journal->close(); require(journal->finished(), "copy batch journal remained pending");
}
void delayedPredecessorAndClosure() {
    auto journal = std::make_unique<Journal>(); seed(*journal); snap(*journal);
    std::atomic<bool> entered{false}, release{false};
    std::thread first([&] { JournalTestAccess::delayedSnapshot(*journal, entered, release, 10); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    require(journal->requestSnapshot(1, cb, pl, 11), "subsequent producer could not publish");
    std::vector<std::uint32_t> order;
    const auto callback = [&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(value.sourceComplete, "delayed but published lifecycle became incomplete"); order.push_back(cookie);
    };
    require(journal->drain(callback) == 0 && order.empty(), "consumer certified across an unpublished earlier ticket");
    release.store(true, std::memory_order_release); first.join();
    require(journal->drain(callback, 1) == 1 && order == std::vector<std::uint32_t>{10}, "bounded drain reordered first ticket");
    require(journal->drain(callback, 1) == 1 && order == std::vector<std::uint32_t>({10, 11}), "bounded drain omitted following ticket");
    journal->close(); require(journal->finished() && journal->lostState() == 0, "normal delayed publication poisoned proof");

    auto interrupted = std::make_unique<Journal>(); seed(*interrupted); snap(*interrupted);
    entered.store(false); release.store(false);
    std::thread pending([&] { JournalTestAccess::delayedSnapshot(*interrupted, entered, release, 12); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    interrupted->close(); require(!interrupted->finished(), "closure ignored an active unpublished observation");
    release.store(true, std::memory_order_release); pending.join();
    bool rejected = false;
    interrupted->drain([&](std::uint32_t, const Snapshot& value) noexcept { rejected = !value.sourceComplete; });
    require(rejected && interrupted->finished() && interrupted->transportCounters().orderGap == 1,
            "interrupted closure falsely certified or never finished");
}
void concurrentProducers() {
    auto journal = std::make_unique<Journal>();
    constexpr std::uint32_t threads = 16, rounds = 8;
    std::array<std::atomic<std::uint32_t>, threads> completed{};
    std::atomic<std::uint32_t> received{0}, invalid{0};
    std::thread reader([&] {
        while (!journal->finished()) {
            const auto count = journal->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
                if (!value.sourceComplete || value.gpuConsumedCoherent || value.device != cookie + 1) invalid.fetch_add(1);
                completed[cookie / rounds].fetch_add(1, std::memory_order_release); received.fetch_add(1);
            }, 256);
            if (!count) std::this_thread::yield();
        }
    });
    std::vector<std::thread> workers;
    for (std::uint32_t thread = 0; thread < threads; ++thread) workers.emplace_back([&, thread] {
        for (std::uint32_t round = 0; round < rounds; ++round) {
            const auto cookie = thread * rounds + round;
            monitorHookAllocations = true; seed(*journal, cookie + 1);
            const auto accepted = journal->requestSnapshot(cookie + 1, cb, pl, cookie); monitorHookAllocations = false;
            if (!accepted) invalid.fetch_add(1);
            // Throughput fixture admits a burst of four complete graphs per
            // producer; scheduling backpressure belongs to this test only.
            if (round % 4 == 3) while (completed[thread].load(std::memory_order_acquire) < round + 1) std::this_thread::yield();
        }
    });
    for (auto& worker : workers) worker.join();
    journal->close(); reader.join();
    require(received == threads * rounds && invalid == 0, "concurrent independent graphs lost complete recording-time metadata");
    require(journal->lostState() == 0 && journal->lossCount(Reason::Contention) == 0,
            "single background owner still suffered metadata contention");
    require(hookAllocations.load() == 0, "concurrent hook admission allocated memory");
    const auto counters = journal->transportCounters();
    require(counters.published == threads * rounds * 12 && counters.replayed == counters.published,
            "concurrent publication counters disagreed with accepted API records");
}
void registryQueueBatchAndWrap() {
    std::atomic<std::uint64_t> identitySerial{UINT64_MAX - 1};
    require(JournalTestAccess::reserveIdentity(identitySerial) == UINT64_MAX - 1,
            "last unique producer/instance identity was not retained");
    require(JournalTestAccess::reserveIdentity(identitySerial) == 0 && identitySerial.load() == UINT64_MAX,
            "identity allocator wrapped into an earlier SPSC owner/cache identity");
    require(JournalTestAccess::reserveIdentity(identitySerial) == 0, "identity exhaustion was not permanently saturated");
    auto registry = std::make_unique<Journal>();
    for (std::size_t i = 0; i < Journal::ProducerCapacity + 1; ++i) {
        std::thread producer([&] { auto bufferInfo = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            bufferInfo.size = 1; registry->createBuffer(1, h<VkBuffer>(i + 1), bufferInfo); }); producer.join();
    }
    registry->close(); while (!registry->finished()) registry->drain([](std::uint32_t, const Snapshot&) noexcept {});
    require(registry->transportCounters().registry == 1 && registry->lossCount(Reason::Capacity) == 1,
            "exhausted producer registry silently recycled a lane");

    auto queue = std::make_unique<Journal>();
    auto bufferInfo = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO); bufferInfo.size = 1;
    for (std::size_t i = 0; i < Journal::LaneCapacity + 1; ++i) queue->createBuffer(1, h<VkBuffer>(i + 1), bufferInfo);
    require(queue->transportCounters().queue == 1, "lane overflow silently evicted a metadata record");
    queue->drain([](std::uint32_t, const Snapshot&) noexcept {}); seed(*queue);
    require(!snap(*queue).sourceComplete, "queue overflow stopped being sticky after drainage");
    queue->close();

    auto batch = std::make_unique<Journal>(); seed(*batch); snap(*batch);
    auto sets = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    sets.descriptorPool = dp; sets.descriptorSetCount = Journal::CallCapacity + 1;
    // Deliberately inaccessible arrays demonstrate bound checks precede reads.
    sets.pSetLayouts = reinterpret_cast<const VkDescriptorSetLayout*>(1);
    batch->allocateSets(1, sets, reinterpret_cast<const VkDescriptorSet*>(1));
    batch->updateSets(1, Journal::CallCapacity, reinterpret_cast<const VkWriteDescriptorSet*>(1), 1,
                      reinterpret_cast<const VkCopyDescriptorSet*>(1));
    batch->allocateCommands(1, cp, Journal::CallCapacity + 1, reinterpret_cast<const VkCommandBuffer*>(1));
    require(batch->transportCounters().batch == 3 && batch->lossCount(Reason::Bounds) == 3,
            "oversized atomic APIs were read or partially accepted");
    require(!snap(*batch).sourceComplete, "oversized batch left future descriptor proof complete"); batch->close();

    auto wrap = std::make_unique<Journal>(); JournalTestAccess::ticket(*wrap, UINT64_MAX);
    require(!wrap->requestSnapshot(1, cb, pl, 1) && wrap->transportCounters().orderWrap == 1,
            "ticket wrap reused an earlier event identity");
    wrap->close(); while (!wrap->finished()) wrap->drain([](std::uint32_t, const Snapshot&) noexcept {});
    require(wrap->transportCounters().orderGap == 1, "wrapped ticket gap failed to close incomplete");

    auto gap = std::make_unique<Journal>(); seed(*gap); snap(*gap);
    JournalTestAccess::gap(*gap); require(gap->requestSnapshot(1, cb, pl, 99), "post-gap request not retained for incomplete callback");
    bool called = false; gap->close();
    while (!gap->finished()) gap->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie == 99 && !value.sourceComplete, "closed missing predecessor was skipped and certified"); called = true;
    });
    require(called && gap->transportCounters().orderGap == 1, "gap closure omitted an incomplete queued snapshot");
}
std::size_t operationIndex(std::string_view name) {
    for (std::size_t i = 0; i < Journal::OperationCount; ++i)
        if (name == Journal::operationName(i)) return i;
    std::abort();
}
void operationLossAttribution() {
    auto journal = std::make_unique<Journal>();
    for (std::size_t i = 0; i < Journal::LaneCapacity; ++i)
        journal->updateSets(1, 0, nullptr, 0, nullptr);
    journal->updateSets(1, 0, nullptr, 0, nullptr);
    require(journal->transportCounters().maxLaneOccupancy == Journal::LaneCapacity,
            "producer lane high-water omitted a full bounded burst");
    const auto update = operationIndex("update_sets");
    const auto begin = operationIndex("begin_command");
    const auto allocate = operationIndex("allocate_sets");
    const auto snapshot = operationIndex("snapshot");
    auto counters = journal->operationCounters();
    require(counters[update].publicationLoss == 1 && counters[update].replayLoss == 0,
            "queue loss was not attributed to its rejected operation");
    const auto callback = [](std::uint32_t, const Snapshot&) noexcept {};
    while (journal->drain(callback)) {}
    journal->beginCommand(1, h<VkCommandBuffer>(999));
    journal->drain(callback);
    counters = journal->operationCounters();
    require(counters[begin].replayLoss == 1 && counters[begin].publicationLoss == 0,
            "Core unknown-state loss was confused with publication rejection");
    auto allocateInfo = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocateInfo.descriptorSetCount = Journal::CallCapacity + 1;
    allocateInfo.pSetLayouts = reinterpret_cast<const VkDescriptorSetLayout*>(1);
    journal->allocateSets(1, allocateInfo, reinterpret_cast<const VkDescriptorSet*>(1));
    journal->close();
    require(!journal->requestSnapshot(1, cb, pl, 0), "closed journal retained a new snapshot");
    counters = journal->operationCounters();
    require(counters[allocate].publicationLoss == 1 && counters[snapshot].publicationLoss == 1,
            "batch or closed-call loss omitted its operation attribution");
    require(journal->transportCounters().maxLaneOccupancy == Journal::LaneCapacity &&
            counters[update].publicationLoss == 1 && counters[begin].replayLoss == 1,
            "later drainage or closure reset historical attribution/high-water");
    std::uint64_t publicationLoss = 0, replayLoss = 0;
    for (std::size_t i = 0; i < counters.size(); ++i) {
        require(std::string_view(Journal::operationName(i)) != "unknown", "operation name missing from diagnostics");
        for (std::size_t j = 0; j < i; ++j)
            require(std::string_view(Journal::operationName(i)) != Journal::operationName(j), "operation diagnostic names overlap");
        publicationLoss += counters[i].publicationLoss; replayLoss += counters[i].replayLoss;
    }
    require(std::string_view(Journal::operationName(Journal::OperationCount)) == "unknown",
            "out-of-range operation name access was not bounded");
    require(publicationLoss == 3 && replayLoss == 1 && journal->lostState() == 4,
            "operation counters changed or omitted aggregate sticky losses");
}
void compactByteBoundsAndReuse() {
    const auto callback = [](std::uint32_t, const Snapshot&) noexcept {};
    auto compact = std::make_unique<Journal>(); seed(*compact); snap(*compact);
    // A complete frame-sized burst of short updates must fit without borrowing
    // caller memory, evicting history, or requiring consumer scheduling.
    monitorHookAllocations = true;
    for (std::uint32_t i = 0; i < 512; ++i) writeBuffer(*compact, i);
    monitorHookAllocations = false;
    const auto complete = snap(*compact);
    require(complete.sourceComplete && at(complete, 0).effectiveOffset == 511,
            "compact frame burst failed to retain every ordered short update");
    require(compact->transportCounters().queue == 0 && hookAllocations == 0,
            "compact frame burst lost records or allocated in a hook");
    // Both the initial tiny prefix and a full write/copy pair cross the pool
    // boundary. Copy entries must have the correct active union member on restore.
    JournalTestAccess::bytePosition(*compact, Journal::ByteCapacity - 3);
    require(snap(*compact).sourceComplete, "snapshot prefix split at byte wrap changed its fields");
    const auto second = h<VkDescriptorSet>(41);
    auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = dp; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &sl;
    compact->allocateSets(1, allocate, &second); compact->drain(callback);
    JournalTestAccess::bytePosition(*compact, 2 * Journal::ByteCapacity - 5);
    VkDescriptorBufferInfo bi{buffer, 96, 128};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = ds; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bi;
    auto copy = info<VkCopyDescriptorSet>(VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET);
    copy.srcSet = ds; copy.dstSet = second; copy.descriptorCount = 1;
    compact->updateSets(1, 1, &write, 1, &copy);
    bi = {}; write = {}; copy = {};
    compact->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &second, 0, nullptr);
    require(at(snap(*compact), 0).effectiveOffset == 96, "wrapped write/copy prefix retained dead caller arrays or wrong union member");
    // Natural repeated byte/header reuse mixes the largest record, scalar
    // records and small binding/snapshot prefixes without changing capacities.
    for (std::uint32_t i = 0; i < 256; ++i) {
        writeLargeBatch(*compact, i);
        compact->endCommand(1, cb); compact->beginCommand(1, cb);
        compact->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
        const auto value = snap(*compact);
        require(value.sourceComplete && at(value, 0).effectiveOffset == i + Journal::CallCapacity - 1,
                "mixed compact byte/header wraps changed descriptor values or lifetimes");
    }
    compact->close(); require(compact->finished() && compact->lostState() == 0, "compact wrap fixture failed clean closure");

    auto saturated = std::make_unique<Journal>(); seed(*saturated); snap(*saturated);
    const auto largeBytes = JournalTestAccess::largeUpdateBytes();
    const auto accepted = Journal::ByteCapacity / largeBytes;
    for (std::size_t i = 0; i <= accepted; ++i) writeLargeBatch(*saturated, i);
    const auto counts = saturated->transportCounters();
    require(counts.queue == 1 && counts.byteQueue == 1 && counts.headerQueue == 0,
            "byte pool exhaustion was hidden or confused with header exhaustion");
    require(counts.maxLaneBytes == accepted * largeBytes && counts.maxLaneOccupancy == accepted,
            "byte/header high-water did not describe the retained full-payload burst");
    while (saturated->drain(callback)) {}
    require(!snap(*saturated).sourceComplete, "drainage erased historical byte-pool loss");
    saturated->close(); require(saturated->finished(), "byte-saturated journal failed bounded closure");

    auto wrapped = std::make_unique<Journal>(); seed(*wrapped); snap(*wrapped);
    JournalTestAccess::bytePosition(*wrapped, UINT64_MAX - sizeof(std::uint64_t));
    wrapped->endCommand(1, cb);
    wrapped->drain(callback);
    require(!wrapped->requestSnapshot(1, cb, pl, 0) && wrapped->transportCounters().orderWrap == 1,
            "byte cursor overflow reused an earlier byte generation");
    wrapped->close(); require(wrapped->finished() && wrapped->transportCounters().orderGap == 0,
            "byte overflow allocated a missing global ticket before rejection");
}
void malformedCompactRecords() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        auto journal = std::make_unique<Journal>(); seed(*journal); snap(*journal);
        writeBuffer(*journal, 96);
        JournalTestAccess::corruptHead(*journal, kind);
        journal->drain([](std::uint32_t, const Snapshot&) noexcept {});
        require(journal->lossCount(Reason::Bounds) == 1,
                "malformed compact header/prefix did not fail closed before replay");
        require(!journal->requestSnapshot(1, cb, pl, 0), "malformed compact state admitted a fresh proof request");
        journal->close(); require(journal->finished(), "malformed compact journal did not finish bounded cleanup");
    }
}
void laneWrapAndDependencies() {
    auto journal = std::make_unique<Journal>(); seed(*journal); auto initial = snap(*journal);
    for (std::uint32_t i = 0; i < 160; ++i) {
        writeBuffer(*journal, 64 + i);
        const auto current = snap(*journal);
        require(current.sourceComplete && at(current, 0).effectiveOffset == 64 + i, "SPSC slot wrap retained an older payload");
    }
    journal->destroyBuffer(1, buffer);
    auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO); create.size = 8192;
    journal->createBuffer(1, buffer, create); journal->bindBufferMemory(1, buffer, memory, 256);
    require(!snap(*journal).sourceComplete, "reused buffer handle revived a retained descriptor generation");
    writeBuffer(*journal, 64); auto rebound = snap(*journal);
    require(rebound.sourceComplete && at(rebound, 0).buffer.identity.generation != at(initial, 0).buffer.identity.generation,
            "explicit descriptor update did not select renewed resource generation");
    journal->executeCommands(1, cb);
    require(!snap(*journal).sourceComplete, "secondary command execution retained primary graphics bindings");
    journal->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    require(snap(*journal).sourceComplete, "primary rebind after secondary execution did not recover");
    journal->resetCommandPool(1, cp);
    require(!snap(*journal).sourceComplete, "command pool reset retained old descriptor provenance");
    journal->close();
}
void extensionOwnership() {
    auto journal = std::make_unique<Journal>();
    auto imageInfo = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
    imageInfo.extent = {1152, 4608, 1}; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT; imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    auto drm = info<VkImageDrmFormatModifierExplicitCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    std::array<VkSubresourceLayout, 2> planes{};
    planes[0].offset = 0; planes[0].rowPitch = 2304; planes[1].offset = 10616832; planes[1].rowPitch = 2304;
    drm.drmFormatModifier = 0x0500000000000001ULL; drm.drmFormatModifierPlaneCount = 2; drm.pPlaneLayouts = planes.data();
    auto external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; external.pNext = &drm; imageInfo.pNext = &external;
    const auto image = h<VkImage>(100); const auto view = h<VkImageView>(110); const auto sampler = h<VkSampler>(120);
    const auto conversion = h<VkSamplerYcbcrConversion>(130);
    auto conversionInfo = info<VkSamplerYcbcrConversionCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);
    conversionInfo.format = imageInfo.format; conversionInfo.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
    conversionInfo.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW; conversionInfo.chromaFilter = VK_FILTER_LINEAR;
    journal->createConversion(1, conversion, conversionInfo);
    auto attachment = info<VkSamplerYcbcrConversionInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO);
    attachment.conversion = conversion;
    journal->createImage(1, image, imageInfo);
    auto import = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; import.fd = 37;
    auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = image; import.pNext = &dedicated;
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = 20 * 1024 * 1024; allocation.pNext = &import;
    journal->allocateMemory(1, memory, allocation); journal->bindImageMemory(1, image, memory, 0);
    auto viewInfo = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; viewInfo.pNext = &attachment; journal->createView(1, view, viewInfo);
    auto border = info<VkSamplerCustomBorderColorCreateInfoEXT>(VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
    border.customBorderColor.float32[0] = 0.25F; border.format = VK_FORMAT_UNDEFINED; border.pNext = &attachment;
    auto samplerInfo = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT; samplerInfo.pNext = &border; journal->createSampler(1, sampler, samplerInfo);
    // Destroy every borrowed chain and nested array before background replay.
    imageInfo = {}; drm = {}; planes = {}; external = {}; import = {}; dedicated = {}; border = {}; samplerInfo = {};
    attachment = {}; conversionInfo = {};
    const VkDescriptorSetLayoutBinding bindings[] = {
        {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {25, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = 2; layout.pBindings = bindings; journal->createSetLayout(1, sl, layout);
    auto pipeline = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &sl; journal->createPipelineLayout(1, pl, pipeline);
    auto pool = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO); pool.maxSets = 1;
    journal->createPool(1, dp, pool);
    auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocate.descriptorPool = dp; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &sl; journal->allocateSets(1, allocate, &ds);
    VkDescriptorImageInfo di{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (auto& w : writes) { w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = ds; w.descriptorCount = 1; w.pImageInfo = &di; w.pBufferInfo = reinterpret_cast<const VkDescriptorBufferInfo*>(1); }
    writes[0].dstBinding = 9; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].dstBinding = 25; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    journal->updateSets(1, 2, writes.data(), 0, nullptr); di = {}; writes = {};
    journal->allocateCommands(1, cp, 1, &cb); journal->beginCommand(1, cb);
    journal->bindSets(1, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds, 0, nullptr);
    const auto result = snap(*journal);
    require(result.sourceComplete, "owned imported-image or custom-border chain failed complete replay");
    require(at(result, 9).image.extensions.drmModifier == 0x0500000000000001ULL &&
            at(result, 9).image.extensions.planes[1].offset == 10616832 &&
            at(result, 9).image.allocation.extensions.inputFD == 37 &&
            at(result, 25).sampler.extensions.hasCustomBorder,
            "extension scalar metadata changed after caller chain lifetime ended");
    require(at(result, 9).view.conversion.model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709 &&
            at(result, 25).sampler.conversion.identity.handle == handle(conversion),
            "owned conversion creation/attachment metadata did not reach snapshot");
    journal->destroyConversion(1, conversion);
    require(!snap(*journal).sourceComplete, "conversion destruction preserved dependent image/sampler proof");
    journal->close();
}
}
// A reproducible selector microbenchmark: the replay operation is an empty
// descriptor update so object lookup population cannot mask lane-selection
// cost. It measures real hook publication and background drain independently;
// it does not reproduce compositor scheduling or establish hardware throughput.
void benchmark() {
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t batches = 5000, batchSize = 128;
    for (const auto producerIndex : {std::size_t{0}, Journal::ProducerCapacity - 1}) {
        auto journal = std::make_unique<Journal>();
        for (std::size_t i = 0; i < producerIndex; ++i) {
            std::thread registerOnly([&] { journal->updateSets(1, 0, nullptr, 0, nullptr); });
            registerOnly.join();
        }
        const auto callback = [](std::uint32_t, const Snapshot&) noexcept {};
        journal->drain(callback);
        std::int64_t publicationNs = 0, drainNs = 0;
        for (std::size_t batch = 0; batch < batches; ++batch) {
            const auto begin = Clock::now();
            for (std::size_t i = 0; i < batchSize; ++i)
                journal->updateSets(1, 0, nullptr, 0, nullptr);
            const auto published = Clock::now();
            const auto count = journal->drain(callback, batchSize);
            const auto end = Clock::now();
            require(count == batchSize, "benchmark lost a retained operation");
            publicationNs += std::chrono::duration_cast<std::chrono::nanoseconds>(published - begin).count();
            drainNs += std::chrono::duration_cast<std::chrono::nanoseconds>(end - published).count();
        }
        require(journal->lostState() == 0, "benchmark incurred hidden state loss");
        journal->close(); require(journal->finished(), "benchmark did not finish");
        const auto records = batches * batchSize;
        std::cout << "{\"producer_index\":" << producerIndex << ",\"records\":" << records
                  << ",\"publication_ns_per_record\":" << static_cast<double>(publicationNs) / records
                  << ",\"drain_ns_per_record\":" << static_cast<double>(drainNs) / records << "}\n";
    }
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") { benchmark(); return 0; }
    ownershipAndOrdering(); selectedSnapshotBurst(); writeBeforeCopyBatch(); delayedPredecessorAndClosure(); concurrentProducers();
    registryQueueBatchAndWrap(); operationLossAttribution(); compactByteBoundsAndReuse(); malformedCompactRecords();
    laneWrapAndDependencies(); extensionOwnership();
    std::cout << checks << " journal checks passed; " << sizeof(Journal) << " bytes fixed state; no hook allocations\n";
}
