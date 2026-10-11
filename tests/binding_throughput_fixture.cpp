#include "layer/passive_binding_journal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Original, CPU-only public-API-shaped metadata workload. No Vulkan device,
// native compositor code, resource contents or hardware capture is required.
// Timing and losses are measurements, never pass/fail performance thresholds.
namespace {
using namespace frame_bindings;
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
constexpr std::uint64_t Device = 1;
constexpr std::uint32_t Selected = 32;

void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T out{}; out.sType = type; return out; }
std::uint64_t elapsed(Clock::time_point before) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<Nanoseconds>(Clock::now() - before).count());
}
struct Slot {
    VkDescriptorSetLayout layout;
    VkPipelineLayout pipeline;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkCommandPool commandPool;
    VkCommandBuffer command;
    VkBuffer buffer;
    VkDeviceMemory bufferMemory;
    VkImage image;
    VkDeviceMemory imageMemory;
    VkImageView view;
    VkSampler sampler;
};
Slot slot(std::uint32_t index, std::uint32_t producers, std::uint32_t objects) {
    const auto family = index / 2;
    const auto owner = index / (objects / producers);
    return {h<VkDescriptorSetLayout>(1000 + family), h<VkPipelineLayout>(2000 + family),
            h<VkDescriptorPool>(3000 + owner), h<VkDescriptorSet>(4000 + index),
            h<VkCommandPool>(5000 + owner), h<VkCommandBuffer>(6000 + index),
            h<VkBuffer>(7000 + index), h<VkDeviceMemory>(8000 + 2 * index),
            h<VkImage>(10000 + index), h<VkDeviceMemory>(8001 + 2 * index),
            h<VkImageView>(11000 + index), h<VkSampler>(12000 + family)};
}
VkDeviceSize offset(std::uint32_t sequence) { return 320 + 640 * (sequence & 1); }
template<class Target> void write(Target& target, const Slot& s, std::uint32_t sequence) {
    const VkDescriptorBufferInfo buffer{s.buffer, offset(sequence), 272};
    const VkDescriptorImageInfo image{s.sampler, s.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 4> writes{};
    const std::array<std::uint32_t, 4> bindings{{0, 4, 9, 25}};
    const std::array<VkDescriptorType, 4> types{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER}};
    for (std::size_t i = 0; i < writes.size(); ++i) {
        writes[i] = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[i].dstSet = s.set; writes[i].dstBinding = bindings[i];
        writes[i].descriptorCount = 1; writes[i].descriptorType = types[i];
        if (i < 2) writes[i].pBufferInfo = &buffer;
        else writes[i].pImageInfo = &image;
    }
    target.updateSets(Device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
template<class Target> void allocate(Target& target, const Slot& s) {
    auto create = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    create.descriptorPool = s.pool; create.descriptorSetCount = 1; create.pSetLayouts = &s.layout;
    target.allocateSets(Device, create, &s.set);
}
template<class Target> void bind(Target& target, const Slot& s) {
    const std::uint32_t dynamicOffset = 64;
    target.bindSets(Device, s.command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline,
                    0, 1, &s.set, 1, &dynamicOffset);
}

// Construction is deliberately separate from every measured producer window.
// Journal seeding drains periodically; stress publication never waits for replay.
template<class Target, class Flush> void seed(Target& target, std::uint32_t producers,
                                             std::uint32_t objects, Flush&& flush, bool allowLoss = false) {
    for (std::uint32_t owner = 0; owner < producers; ++owner) {
        auto pool = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool.maxSets = objects / producers;
        target.createPool(Device, slot(owner * (objects / producers), producers, objects).pool, pool);
    }
    flush();
    for (std::uint32_t family = 0; family < objects / 2; ++family) {
        const auto s = slot(family * 2, producers, objects);
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {25, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
        auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size()); layout.pBindings = bindings.data();
        target.createSetLayout(Device, s.layout, layout);
        auto pipeline = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &s.layout;
        target.createPipelineLayout(Device, s.pipeline, pipeline);
        auto sampler = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        sampler.maxLod = 1;
        target.createSampler(Device, s.sampler, sampler);
        auto conversion = info<VkSamplerYcbcrConversionCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);
        conversion.format = VK_FORMAT_R8G8B8A8_UNORM;
        target.createConversion(Device, h<VkSamplerYcbcrConversion>(13000 + family), conversion);
        if (family % 16 == 15) flush();
    }
    flush();
    for (std::uint32_t i = 0; i < objects; ++i) {
        const auto s = slot(i, producers, objects);
        auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        memory.allocationSize = 65536;
        target.allocateMemory(Device, s.bufferMemory, memory);
        target.allocateMemory(Device, s.imageMemory, memory);
        auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        buffer.size = 65536; buffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        target.createBuffer(Device, s.buffer, buffer);
        target.bindBufferMemory(Device, s.buffer, s.bufferMemory, 0);
        auto image = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {64, 64, 1}; image.mipLevels = 1; image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        target.createImage(Device, s.image, image);
        target.bindImageMemory(Device, s.image, s.imageMemory, 0);
        auto view = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = s.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        target.createView(Device, s.view, view);
        allocate(target, s);
        target.allocateCommands(Device, s.commandPool, 1, &s.command);
        target.beginCommand(Device, s.command); write(target, s, 0); bind(target, s); target.endCommand(Device, s.command);
        if (i % 8 == 7) flush();
    }
    flush();
    if (!allowLoss) require(target.lostState() == 0, "seed exceeded capacities or rejected valid synthetic metadata");
}
template<class Target> std::uint64_t cycle(Target& target, const Slot& s, std::uint32_t sequence) {
    std::uint64_t operations = 0;
    // The producer owns this set and command. There are no simultaneous Vulkan
    // host accesses to one mutable descriptor/command object in this fixture.
    target.resetCommand(Device, s.command); ++operations;
    if ((sequence % 16) == 0) {
        target.freeSets(Device, s.pool, 1, &s.set); allocate(target, s); operations += 2;
        target.freeCommands(Device, s.commandPool, 1, &s.command);
        target.allocateCommands(Device, s.commandPool, 1, &s.command); operations += 2;
    }
    target.beginCommand(Device, s.command); ++operations;
    write(target, s, sequence); ++operations;
    bind(target, s); ++operations;
    target.endCommand(Device, s.command); ++operations;
    return operations;
}
void checkSnapshot(const Snapshot& value, const Slot& expected, std::uint32_t sequence) {
    require(!value.gpuConsumedCoherent, "CPU fixture claimed GPU-consumed coherence");
    if (!value.sourceComplete) return;
    require(value.reason == Reason::None && value.layout_has_binding31 == Presence::Absent,
            "complete snapshot did not retain known layout absence");
    require(value.command.handle == handle(expected.command) && value.set.handle == handle(expected.set),
            "snapshot used another command or descriptor set");
    const auto& b = value.bindings[0];
    require(b.binding == 0 && b.complete && b.buffer.identity.handle == handle(expected.buffer) &&
            b.effectiveOffset == offset(sequence) + 64, "snapshot did not preserve recording-time descriptor offset");
}
void invariants() {
    auto journal = std::make_unique<Journal>();
    const auto drain = [&] { while (journal->drain([](std::uint32_t, const Snapshot&) noexcept {})) {} };
    seed(*journal, 1, 32, drain);
    for (std::uint32_t i = 0; i < Selected; ++i) {
        const auto s = slot(i % 32, 1, 32);
        cycle(*journal, s, i);
        require(journal->requestSnapshot(Device, s.command, s.pipeline, i), "lossless check snapshot publication failed");
        bool observed = false;
        journal->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
            require(cookie == i && !observed && value.sourceComplete, "lossless check callback reordered or became incomplete");
            checkSnapshot(value, s, i); observed = true;
        });
        require(observed && journal->lostState() == 0, "lossless lifecycle check did not replay completely");
    }
    // This pause is an intentional negative semantic check, not a benchmark.
    const auto s = slot(31, 1, 32);
    for (std::size_t i = 0; i <= Journal::LaneCapacity; ++i) journal->resetCommand(Device, s.command);
    require(journal->transportCounters().queue != 0, "explicit stopped-consumer saturation was not reported");
    drain();
    cycle(*journal, s, 99);
    require(journal->requestSnapshot(Device, s.command, s.pipeline, 99), "post-loss snapshot was not retained");
    bool invalid = false;
    journal->drain([&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie == 99, "post-loss snapshot cookie changed");
        invalid = !value.sourceComplete && !value.gpuConsumedCoherent && value.layout_has_binding31 == Presence::Unknown;
    });
    require(invalid, "later valid mutation erased historical loss");
    journal->close(); require(journal->finished(), "closed semantic-check journal did not finish");
    const auto counters = journal->transportCounters();
    require(counters.published == counters.replayed && counters.orderGap == 0 && counters.registry == 0,
            "semantic-check closure omitted accepted observations");
    std::cout << "{\"kind\":\"invariants\",\"passed\":true,\"selected_snapshots\":32,\"sticky_loss_verified\":true}\n";
}
void coreBaseline(std::uint32_t objects, std::uint32_t iterations) {
    auto core = std::make_unique<Core>(); seed(*core, 1, objects, [] {});
    std::uint64_t operations = 0;
    const auto before = Clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) operations += cycle(*core, slot(i % objects, 1, objects), i);
    const auto ns = elapsed(before);
    require(core->lostState() == 0, "direct-Core synthetic lifecycle rejected valid operations");
    const auto s = slot((iterations - 1) % objects, 1, objects);
    const auto snapshot = core->snapshot(Device, s.command, s.pipeline);
    require(snapshot.sourceComplete, "direct-Core final snapshot became incomplete"); checkSnapshot(snapshot, s, iterations - 1);
    std::cout << "{\"kind\":\"direct_core\",\"objects\":" << objects << ",\"iterations\":" << iterations
              << ",\"operations\":" << operations << ",\"replay_ns\":" << ns
              << ",\"us_per_operation\":" << static_cast<double>(ns) / operations / 1000.0 << "}\n";
}
void startupBenchmark(std::uint32_t idleUs) {
    constexpr std::uint32_t objects = 384;
    auto journal = std::make_unique<Journal>();
    std::atomic<bool> ready{false}, start{false};
    std::uint64_t replayNs = 0, callbacks = 0;
    bool complete = false;
    const auto s = slot(objects - 1, 1, objects);
    const auto callback = [&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie == 0 && !callbacks, "startup callback was duplicated or reordered");
        ++callbacks; complete = value.sourceComplete; checkSnapshot(value, s, 0);
    };
    std::thread consumer([&] {
        ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        while (!journal->finished()) {
            const auto before = Clock::now(); const auto count = journal->drain(callback, 256); replayNs += elapsed(before);
            if (!count) std::this_thread::sleep_for(std::chrono::microseconds(idleUs));
        }
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    const auto before = Clock::now(); start.store(true, std::memory_order_release);
    // Unlike the separate warm-state benchmark, this is actual asynchronous
    // initial resource creation: no producer drains, backpressure or retries.
    seed(*journal, 1, objects, [] {}, true);
    // Recover publication space only after the measured creation window has
    // ended, to observe sticky invalidation; this does not retry lost metadata.
    while (!journal->empty()) std::this_thread::yield();
    require(journal->requestSnapshot(Device, s.command, s.pipeline, 0), "post-startup snapshot request rejected");
    journal->close(); consumer.join();
    const auto ns = elapsed(before);
    const auto counters = journal->transportCounters();
    require(journal->finished() && counters.published == counters.replayed && callbacks == 1,
            "startup closure omitted accepted metadata");
    require(counters.registry == 0 && counters.orderGap == 0 && counters.batch == 0 && counters.closedCalls == 0,
            "startup produced unrelated journal transport failures");
    require(!journal->lostState() || !complete, "startup erased pre-arm loss");
    const auto attempted = static_cast<std::uint64_t>(15 * objects + 2);
    require(attempted == counters.published + counters.queue, "startup operation accounting diverged");
    std::cout << "{\"kind\":\"async_startup\",\"objects\":" << objects << ",\"idle_us\":" << idleUs
              << ",\"attempted\":" << attempted << ",\"published\":" << counters.published << ",\"replayed\":" << counters.replayed
              << ",\"queue_loss\":" << counters.queue << ",\"lost_state\":" << journal->lostState()
              << ",\"unknown_loss\":" << journal->lossCount(Reason::Unknown) << ",\"snapshot_complete\":" << (complete ? "true" : "false")
              << ",\"total_ns\":" << ns << ",\"replay_ns\":" << replayNs
              << ",\"us_per_replayed_operation\":" << static_cast<double>(replayNs) / counters.replayed / 1000.0
              << ",\"queue_loss_percent\":" << static_cast<double>(counters.queue) * 100.0 / attempted << "}\n";
}
struct Configuration {
    std::uint32_t producers = 1, objects = 32, iterations = 4096;
    std::uint32_t idleUs = 1000, writerDelayUs = 0, periodUs = 0;
    bool sleepWhileSnapshotPending = false;
    std::uint32_t scheduledBatchCycles = 16;
};
void journalBenchmark(Configuration config, bool requireLossless = false) {
    auto journal = std::make_unique<Journal>();
    const auto initialDrain = [&] { while (journal->drain([](std::uint32_t, const Snapshot&) noexcept {})) {} };
    seed(*journal, config.producers, config.objects, initialDrain);
    const auto baseline = journal->transportCounters();
    struct Expected { Slot s{}; std::uint32_t sequence = 0; };
    std::array<Expected, Selected + 1> expected{};
    std::array<bool, Selected + 1> observed{};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> ready{0}, pending{0};
    std::atomic<std::uint64_t> attempts{0}, requests{0}, accepted{0};
    std::uint64_t replayNs = 0, callbacks = 0, complete = 0, stalledUs = 0, maxPending = 0;
    bool finalInvalid = false;
    const auto callback = [&](std::uint32_t cookie, const Snapshot& value) noexcept {
        require(cookie <= Selected && !observed[cookie], "stress callback cookie was corrupt or duplicated");
        observed[cookie] = true; ++callbacks;
        checkSnapshot(value, expected[cookie].s, expected[cookie].sequence);
        if (value.sourceComplete) ++complete;
        if (cookie == Selected) finalInvalid = !value.sourceComplete && value.layout_has_binding31 == Presence::Unknown;
        pending.fetch_sub(1, std::memory_order_relaxed);
    };
    std::thread reader([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        while (!journal->finished()) {
            const auto before = Clock::now();
            const auto count = journal->drain(callback, 256);
            replayNs += elapsed(before);
            const auto queued = journal->transportCounters();
            // Independently sampled atomic counters can transiently observe
            // replay before publication's statistics increment. Never turn that
            // sampling skew into an unsigned underflow/backlog claim.
            if (queued.published >= queued.replayed)
                maxPending = std::max(maxPending, queued.published - queued.replayed);
            if (!count || (config.sleepWhileSnapshotPending && pending.load(std::memory_order_relaxed))) {
                if (config.idleUs) {
                    std::this_thread::sleep_for(std::chrono::microseconds(config.idleUs)); stalledUs += config.idleUs;
                } else std::this_thread::yield();
            } else if (config.writerDelayUs) {
                // Models periodic background output/scheduling cost. Producers
                // keep running and can overflow; no backpressure conceals it.
                std::this_thread::sleep_for(std::chrono::microseconds(config.writerDelayUs));
                stalledUs += config.writerDelayUs;
            }
        }
    });
    std::vector<std::thread> producers;
    for (std::uint32_t owner = 0; owner < config.producers; ++owner) producers.emplace_back([&, owner] {
        const auto perOwner = config.objects / config.producers;
        const auto selectedPerOwner = Selected / config.producers;
        std::uint64_t localAttempts = 0, localRequests = 0, localAccepted = 0;
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        auto scheduled = Clock::now();
        for (std::uint32_t i = 0; i < config.iterations; ++i) {
            const auto s = slot(owner * perOwner + (i % perOwner), config.producers, config.objects);
            localAttempts += cycle(*journal, s, i);
            if (i == config.iterations / 2) for (std::uint32_t j = 0; j < selectedPerOwner; ++j) {
                const auto cookie = owner * selectedPerOwner + j;
                expected[cookie] = {s, i};
                pending.fetch_add(1, std::memory_order_relaxed); ++localRequests; ++localAttempts;
                if (journal->requestSnapshot(Device, s.command, s.pipeline, cookie)) ++localAccepted;
                else pending.fetch_sub(1, std::memory_order_relaxed);
            }
            if (config.periodUs && (i % config.scheduledBatchCycles) == config.scheduledBatchCycles - 1) {
                // Explicit scheduled-burst comparison only. The default stress
                // has periodUs=0; this never adapts to backlog or rejected calls.
                scheduled += std::chrono::microseconds(config.periodUs); std::this_thread::sleep_until(scheduled);
            }
        }
        attempts.fetch_add(localAttempts); requests.fetch_add(localRequests); accepted.fetch_add(localAccepted);
    });
    while (ready.load(std::memory_order_acquire) != config.producers + 1) std::this_thread::yield();
    const auto before = Clock::now(); start.store(true, std::memory_order_release);
    for (auto& producer : producers) producer.join();
    const auto producerNs = elapsed(before);
    const auto finalSlot = slot((config.iterations - 1) % (config.objects / config.producers), config.producers, config.objects);
    expected[Selected] = {finalSlot, config.iterations - 1};
    pending.fetch_add(1); attempts.fetch_add(1); requests.fetch_add(1);
    require(journal->requestSnapshot(Device, finalSlot.command, finalSlot.pipeline, Selected), "reserved final producer lane rejected snapshot");
    accepted.fetch_add(1); journal->close(); reader.join();
    const auto totalNs = elapsed(before);
    const auto all = journal->transportCounters();
    const auto published = all.published - baseline.published;
    const auto replayed = all.replayed - baseline.replayed;
    const auto queue = all.queue - baseline.queue;
    require(journal->finished() && published == replayed && callbacks == accepted.load() && pending.load() == 0,
            "stress closure omitted accepted records or snapshots");
    require(all.registry == 0 && all.orderGap == 0 && all.batch == 0 && all.orderWrap == 0 && all.closedCalls == 0,
            "valid stress triggered unrelated journal transport failure");
    if (!journal->lostState()) require(complete == callbacks && !finalInvalid, "lossless stress produced incomplete snapshots");
    else require(finalInvalid, "stress silently erased prior loss before its final snapshot");
    if (requireLossless) require(journal->lostState() == 0 && complete == Selected + 1,
                                "bounded concurrent semantic check unexpectedly lost metadata");
    for (std::size_t i = 0; i < static_cast<std::size_t>(Table::Count); ++i)
        require(journal->capacityLossCount(static_cast<Table>(i)) == 0, "stress exceeded fixed object-table capacity");
    require(attempts.load() == published + queue, "accepted and rejected publication accounting diverged");
    std::cout << "{\"kind\":\"journal\",\"producers\":" << config.producers << ",\"objects\":" << config.objects
        << ",\"iterations_per_producer\":" << config.iterations << ",\"idle_us\":" << config.idleUs
        << ",\"writer_delay_us\":" << config.writerDelayUs << ",\"scheduled_batch_period_us\":" << config.periodUs
        << ",\"scheduled_batch_cycles\":" << config.scheduledBatchCycles
        << ",\"sleep_on_pending_snapshot_progress\":" << (config.sleepWhileSnapshotPending ? "true" : "false")
        << ",\"attempted\":" << attempts.load() << ",\"published\":" << published << ",\"replayed\":" << replayed
        << ",\"queue_loss\":" << queue << ",\"lost_state\":" << journal->lostState()
        << ",\"unknown_loss\":" << journal->lossCount(Reason::Unknown) << ",\"snapshot_requests\":" << requests.load()
        << ",\"snapshot_accepted\":" << accepted.load() << ",\"snapshot_callbacks\":" << callbacks
        << ",\"snapshot_complete\":" << complete << ",\"max_observed_pending\":" << maxPending
        << ",\"producer_ns\":" << producerNs << ",\"total_ns\":" << totalNs << ",\"replay_ns\":" << replayNs
        << ",\"requested_consumer_stall_us\":" << stalledUs
        << ",\"us_per_replayed_operation\":" << static_cast<double>(replayNs) / replayed / 1000.0
        << ",\"queue_loss_percent\":" << static_cast<double>(queue) * 100.0 / attempts.load() << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    bool benchmark = false;
    std::uint32_t iterations = 4096;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--benchmark") benchmark = true;
        else if (argument == "--check") benchmark = false;
        else if (argument == "--iterations" && i + 1 < argc) {
            char* end = nullptr; const auto value = std::strtoul(argv[++i], &end, 10);
            require(end && !*end && value >= 64 && value <= 65536, "iterations must be in [64,65536]");
            iterations = static_cast<std::uint32_t>(value);
        } else { std::cerr << "Usage: binding_throughput_fixture [--check|--benchmark] [--iterations 64..65536]\n"; return 2; }
    }
    std::cout << std::fixed << std::setprecision(6);
    invariants();
    // Semantics-only concurrent publication. Each lane's finite complete batch
    // fits without a consumer, so losslessness is independent of machine speed.
    journalBenchmark({8, 32, 4, 100, 0, 0, false}, true);
    if (!benchmark) return 0;
    coreBaseline(32, iterations); coreBaseline(384, iterations);
    startupBenchmark(1000); startupBenchmark(100);
    for (const auto objects : {32u, 384u}) for (const auto producers : {1u, 8u, 16u}) {
        journalBenchmark({producers, objects, iterations, 1000, 0, 0, true});
        journalBenchmark({producers, objects, iterations, 100, 0, 0, false});
        journalBenchmark({producers, objects, iterations, 100, 250, 0, false});
    }
    // Separately labelled average-rate comparisons do not replace the above
    // unthrottled cases. Bursts remain unthrottled within each scheduled batch.
    // A 90-Hz burst source compares roughly 128/256/512 observed API calls per
    // frame. Its one-second schedule is independent of queue health and uses
    // identical work for both writer-poll cases.
    for (const auto cycles : {24u, 48u, 96u}) {
        journalBenchmark({1, 384, cycles * 90, 1000, 0, 11111, true, cycles});
        journalBenchmark({1, 384, cycles * 90, 100, 0, 11111, false, cycles});
    }
    return 0;
}
