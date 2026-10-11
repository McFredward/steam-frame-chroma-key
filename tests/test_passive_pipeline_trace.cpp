#include "layer/passive_pipeline_trace.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace frame_trace {
// Deterministic contention evidence without scheduling-dependent stress tests.
struct TestAccess {
    static std::unique_lock<std::mutex> hold(Core& core) { return std::unique_lock<std::mutex>(core.mutex_); }
    static std::unique_lock<std::mutex> hold(EventRing& ring) { return std::unique_lock<std::mutex>(ring.mutex_); }
    static auto observe(Core& core) { return core.acquire(); }
};
}

namespace {
using namespace frame_trace;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
std::vector<Event> drain(EventRing& ring) {
    std::vector<Event> events;
    Event event;
    while (ring.pop(event)) events.push_back(event);
    return events;
}
std::vector<Event> kind(const std::vector<Event>& events, Kind type) {
    std::vector<Event> result;
    for (const auto& event : events) if (event.kind == type) result.push_back(event);
    return result;
}
const std::uint32_t vertex[] = {0x07230203, 0x10000, 5, 2, 0};
const std::uint32_t fragment[] = {0x07230203, 0x10000, 7, 3, 0};
std::array<Stage, 2> inputs() {
    std::array<Stage, 2> stages{};
    stages[0].stage = 1;
    stages[0].module = 10;
    stages[0].complete = copyEntry(stages[0].entry, "main");
    stages[1].stage = 0x10;
    stages[1].module = 11;
    stages[1].complete = copyEntry(stages[1].entry, "main");
    return stages;
}
void seed(Core& core, std::uint64_t pipeline = 20) {
    core.shaderCreated(1, 10, vertex, sizeof(vertex));
    core.shaderCreated(1, 11, fragment, sizeof(fragment));
    const auto stages = inputs();
    core.pipelineCreated(1, pipeline, 99, stages.data(), stages.size(), true);
}
void begin(Core& core, std::uint64_t command = 30) {
    core.commandAllocated(1, command, 40);
    core.commandBegin(1, command);
    core.bind(1, command, 20);
}
void submit(Core& core, std::uint64_t device, std::uint64_t command, std::uint64_t serial) {
    const auto ticket = core.prepareSubmit(device, command);
    core.commitSubmit(ticket, serial, true);
}
void disabledAndFingerprint() {
    EventRing ring;
    Core core(ring);
    // Disabled paths must not touch even deliberately invalid code pointers.
    core.shaderCreated(1, 10, reinterpret_cast<const void*>(1), 20);
    seed(core);
    begin(core);
    core.draw(1, 30, 1);
    submit(core, 1, 30, 1);
    require(drain(ring).empty(), "disabled state emitted events");
    require(fingerprint("hello", 5) == 0xa430d84680aabd0bULL, "standard FNV-1a fingerprint changed");
}
void stageLifecycleAndActualDraw() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    auto initial = drain(ring);
    require(kind(initial, Kind::Draw).empty(), "pipeline creation was incorrectly called a draw");
    auto created = kind(initial, Kind::Pipeline);
    require(created.size() == 1 && created[0].complete, "known shader tuple was not complete");
    const auto originalVertexGeneration = created[0].stages[0].generation;
    const auto originalPipelineGeneration = created[0].pipelineGeneration;
    core.shaderDestroyed(1, 10);
    const std::uint32_t changed[] = {0x07230203, 0x10000, 100, 3, 0};
    core.shaderCreated(1, 10, changed, sizeof(changed));
    begin(core);
    core.draw(1, 30, 1);
    core.draw(1, 30, 1);
    core.commandEnd(1, 30);
    submit(core, 1, 30, 7);
    submit(core, 1, 30, 8);
    auto events = drain(ring);
    auto draws = kind(events, Kind::Draw);
    auto submissions = kind(events, Kind::Submit);
    require(draws.size() == 1, "same tuple was redundantly logged within one CB generation");
    require(draws[0].complete && draws[0].stageCount == 2, "actual draw lost the stage tuple");
    require(draws[0].stages[0].generation == originalVertexGeneration, "shader handle reuse changed an existing pipeline");
    require(draws[0].stages[0].hash == fingerprint(vertex, sizeof(vertex)), "pipeline shader fingerprint followed reused module handle");
    require(submissions.size() == 1 && submissions[0].complete, "accepted submit was not recorded once");
    require(submissions[0].commandGeneration == draws[0].commandGeneration, "submit did not reference drawn CB generation");
    core.commandBegin(1, 30);
    core.bind(1, 30, 20);
    core.pipelineDestroyed(1, 20);
    const auto stages = inputs();
    core.pipelineCreated(1, 20, 99, stages.data(), stages.size(), true);
    core.draw(1, 30, 1);
    events = drain(ring);
    require(kind(events, Kind::Pipeline)[0].pipelineGeneration != originalPipelineGeneration, "pipeline generation was reused");
    draws = kind(events, Kind::Draw);
    require(draws.size() == 1 && !draws[0].complete && draws[0].reason == Core::UnknownPipeline,
            "reused pipeline handle falsely satisfied a stale binding");
}
void resetAndSecondary() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    begin(core, 31);
    core.draw(1, 31, 2);
    core.commandEnd(1, 31);
    core.commandAllocated(1, 30, 40);
    core.commandBegin(1, 30);
    core.execute(1, 30, 31);
    core.commandEnd(1, 30);
    submit(core, 1, 30, 5);
    auto events = drain(ring);
    auto draws = kind(events, Kind::Draw);
    auto execute = kind(events, Kind::Execute);
    require(execute.size() == 1 && execute[0].complete, "valid secondary execution link missing");
    require(execute[0].relatedGeneration == draws[0].commandGeneration, "secondary execution generation mismatched");
    core.poolReset(1, 40, false);
    core.commandBegin(1, 31);
    core.draw(1, 31, 2);
    draws = kind(drain(ring), Kind::Draw);
    require(draws.size() == 1 && !draws[0].complete, "pool reset retained a pipeline binding");
    core.commandReset(1, 31, true);
    core.commandAllocated(1, 31, 41);
    core.commandBegin(1, 31);
    core.bind(1, 31, 20);
    core.draw(1, 31, 1);
    const auto fresh = kind(drain(ring), Kind::Draw);
    require(fresh.size() == 1 && fresh[0].commandGeneration != draws[0].commandGeneration,
            "freed command handle reused its old generation");
    core.deviceDestroyed(1);
    core.draw(1, 31, 1);
    require(!kind(drain(ring), Kind::Draw)[0].complete, "device destruction retained command state");
}
void failuresAndBounds() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    drain(ring);
    const auto stages = inputs();
    core.pipelineCreated(1, 22, 99, stages.data(), stages.size(), false);
    core.pipelineCreated(1, 0, 99, stages.data(), stages.size(), false);
    auto events = drain(ring);
    require(kind(events, Kind::Incomplete).size() == 2, "aggregate partial-creation error not reported");
    require(kind(events, Kind::Pipeline).size() == 1 && kind(events, Kind::Pipeline)[0].complete,
            "non-null successful handle in partial creation was discarded");
    core.pipelineCreated(1, 23, 99, stages.data(), stages.size(), true, false);
    require(!kind(drain(ring), Kind::Pipeline)[0].complete, "pipeline library was falsely certified");
    core.shaderCreated(1, 12, reinterpret_cast<const void*>(1), MaxShaderBytes + 1);
    require(!kind(drain(ring), Kind::Shader)[0].complete, "oversize shader bytes were read or certified");
    Stage unknown{};
    unknown.stage = 1;
    unknown.module = 12345;
    unknown.complete = copyEntry(unknown.entry, "main");
    core.pipelineCreated(1, 24, 99, &unknown, 1, true);
    require(!kind(drain(ring), Kind::Pipeline)[0].complete, "unknown module certified complete");
    std::string tooLong(EntryCapacity, 'a');
    require(!copyEntry(unknown.entry, tooLong.c_str()), "truncated entry point claimed exact");
    for (std::size_t i = 0; i < ShaderCapacity + 1; ++i) {
        core.shaderCreated(2, i + 1, vertex, sizeof(vertex));
        drain(ring);
    }
    begin(core);
    core.draw(1, 30, 1);
    require(!kind(drain(ring), Kind::Draw)[0].complete, "capacity loss did not make evidence incomplete");
    EventRing full;
    Event dummy;
    for (std::size_t i = 0; i < EventCapacity; ++i) require(full.push(dummy), "ring filled prematurely");
    require(!full.push(dummy) && full.dropped() == 1, "full ring did not drop without blocking");
    drain(full);
    for (std::size_t i = EventCapacity; i < EventLimit; ++i) {
        require(full.push(dummy), "event lifetime cap reached prematurely");
        drain(full);
    }
    require(full.finished() && !full.push(dummy), "lifetime event cap not enforced");
    require(full.dropped() == 1, "intentional lifetime closure was reported as an open-window drop");
}
void multiRecordCallClosesCleanly() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    drain(ring);
    Event dummy;
    while (ring.emitted() < EventLimit - 1) {
        require(ring.push(dummy), "multi-record boundary setup failed");
        drain(ring);
    }
    const auto stages = inputs();
    // The aggregate failure reports Incomplete, then the valid returned handle
    // reports Pipeline, within one enabled() check. Only one budget slot remains.
    core.pipelineCreated(1, 22, 99, stages.data(), stages.size(), false);
    const auto records = drain(ring);
    require(records.size() == 1 && records[0].kind == Kind::Incomplete &&
            records[0].reason == Core::PartialCreation && records[0].serial == EventLimit,
            "the last lifetime record did not retain the first aggregate-failure report");
    require(ring.finished() && !core.enabled() && ring.dropped() == 0,
            "a trailing record after intentional closure tainted the capture");
}
void closureKeepsEarlierLoss() {
    EventRing ring;
    Event dummy, consumed;
    while (ring.emitted() < EventLimit - EventCapacity - 1) {
        require(ring.push(dummy), "open overflow setup failed");
        drain(ring);
    }
    for (std::size_t i = 0; i < EventCapacity; ++i)
        require(ring.push(dummy), "ring filled before boundary setup completed");
    require(!ring.finished() && !ring.push(dummy) && ring.dropped() == 1,
            "open-window overflow near the lifetime boundary was concealed");
    require(ring.pop(consumed) && ring.push(dummy) && ring.finished(), "last lifetime slot was lost");
    require(!ring.push(dummy) && ring.dropped() == 1,
            "closure erased real loss or counted a full closed queue as new loss");
    drain(ring);
}
void concurrentBoundaryAccounting() {
    constexpr unsigned Producers = 8;
    EventRing open;
    {
        auto held = TestAccess::hold(open);
        std::vector<std::thread> producers;
        for (unsigned i = 0; i < Producers; ++i)
            producers.emplace_back([&] { require(!open.push(Event{}), "contended ring accepted an event"); });
        for (auto& producer : producers) producer.join();
    }
    require(open.emitted() == 0 && open.dropped() == Producers,
            "genuine concurrent contention in an open window was concealed");

    EventRing boundary;
    while (boundary.emitted() < EventLimit - 1) {
        require(boundary.push(Event{}), "concurrent boundary setup failed");
        drain(boundary);
    }
    std::atomic<bool> ready{false};
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < Producers; ++i) {
        producers.emplace_back([&] {
            while (!ready.load()) std::this_thread::yield();
            if (boundary.push(Event{})) ++accepted;
        });
    }
    ready.store(true);
    for (auto& producer : producers) producer.join();
    require(accepted == 1 && boundary.emitted() == EventLimit && boundary.finished(),
            "concurrent producers exceeded or failed to close the lifetime window");
    // Pre-close contenders may legitimately lose try_lock; do not assert that
    // their scheduling-dependent loss is zero or reinterpret it as closure.
    const auto earlierDrops = boundary.dropped();
    require(earlierDrops <= Producers - 1, "boundary contention accounting exceeded rejected attempts");
    {
        auto held = TestAccess::hold(boundary);
        producers.clear();
        for (unsigned i = 0; i < Producers; ++i) {
            producers.emplace_back([&] {
                for (unsigned j = 0; j < 32; ++j)
                    require(!boundary.push(Event{}), "closed window accepted concurrent traffic");
            });
        }
        for (auto& producer : producers) producer.join();
    }
    require(boundary.dropped() == earlierDrops,
            "post-close concurrent traffic was conflated with open-window loss");
    const auto last = drain(boundary);
    require(last.size() == 1 && last[0].serial == EventLimit,
            "concurrent lifetime closure altered the accepted final record");
}
void writerNeverTakesTheProducerMutex() {
    EventRing ring;
    Event event, consumed;
    event.commandGeneration = 712;
    require(ring.push(event), "single-consumer setup did not publish its event");
    {
        auto producer = TestAccess::hold(ring);
        require(ring.pop(consumed) && consumed.serial == 1 && consumed.commandGeneration == 712,
                "writer drain contended with the producer mutex");
        require(ring.empty() && !ring.pop(consumed),
                "writer empty check contended with the producer mutex");
        require(!ring.push(event) && ring.dropped() == 1,
                "removing reader contention also concealed genuine producer contention");
    }
    require(ring.push(event) && ring.pop(consumed) && consumed.serial == 2,
            "single consumer did not release its slot for the next producer");
}
void singleProducerAndWriterPreserveEveryPublishedSlot() {
    EventRing ring;
    constexpr std::uint64_t Records = EventLimit;
    std::atomic<std::uint64_t> drained{0};
    std::thread writer([&] {
        Event event;
        for (std::uint64_t expected = 1; expected <= Records;) {
            if (!ring.pop(event)) { std::this_thread::yield(); continue; }
            require(event.serial == expected && event.commandGeneration == expected &&
                    event.relatedGeneration == (expected ^ 0x55aa),
                    "concurrent writer observed a torn or reordered published slot");
            ++expected;
            drained.fetch_add(1);
        }
    });
    for (std::uint64_t serial = 1; serial <= Records; ++serial) {
        // Deliberately keep below capacity; only reader/producer overlap is
        // under test. A real capacity overflow remains a separate rejected loss.
        while (serial - drained.load() > EventCapacity / 2) std::this_thread::yield();
        Event event;
        event.commandGeneration = serial;
        event.relatedGeneration = serial ^ 0x55aa;
        require(ring.push(event), "background draining dropped single-producer metadata");
    }
    writer.join();
    ring.close();
    require(ring.empty() && ring.emitted() == Records && ring.dropped() == 0,
            "concurrent single-consumer drain changed final counts or lost metadata");
}
void writerClosureWaitsForBookkeeping() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    const auto emitted = ring.emitted();
    std::atomic<bool> observerReady{false}, releaseObserver{false}, writerDone{false};
    std::thread observer([&] {
        auto observation = TestAccess::observe(core);
        require(observation.owns_lock(), "closure setup did not admit bookkeeping");
        observerReady.store(true);
        while (!releaseObserver.load()) std::this_thread::yield();
    });
    while (!observerReady.load()) std::this_thread::yield();
    std::thread writer([&] {
        core.enable(false);
        ring.close();
        writerDone.store(true);
    });
    while (!ring.closed()) std::this_thread::yield();
    require(!writerDone.load(), "writer closed before in-flight bookkeeping quiesced");
    require(!ring.push(Event{}), "closed admission accepted a later event");
    releaseObserver.store(true);
    observer.join();
    writer.join();
    const auto queued = drain(ring);
    require(queued.size() == emitted && ring.emitted() == emitted,
            "writer closure discarded accepted metadata or changed its count");
    require(ring.empty() && ring.dropped() == 0 && core.lostState() == 0,
            "intentional close reported a loss or left queued metadata");
    ring.close(); // Idempotent, including an already drained queue.
}
void closedBookkeepingCannotRaceFooter() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    begin(core);
    drain(ring);
    {
        auto held = TestAccess::hold(core);
        core.pipelineDestroyed(1, 20);
    }
    {
        auto held = TestAccess::hold(ring);
        require(!ring.push(Event{}), "closure loss setup unexpectedly enqueued");
    }
    const auto priorStateLoss = core.lostState(), priorEventLoss = ring.dropped();
    require(priorStateLoss == 1 && priorEventLoss == 1,
            "closure setup did not retain genuine pre-close losses");
    // Closing admission itself protects already-enabled hooks; their initial
    // enabled() check need not have seen the writer disable Core yet.
    ring.close();
    const auto priorEmitted = ring.emitted();
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < 8; ++i) producers.emplace_back([&] {
        for (unsigned j = 0; j < 64; ++j) {
            core.report(1, Core::Capacity);
            core.draw(1, 30, 1);
            core.shaderCreated(1, 10, vertex, sizeof(vertex));
            require(!ring.push(Event{}), "closed producer accepted new metadata");
        }
    });
    for (auto& producer : producers) producer.join();
    require(ring.emitted() == priorEmitted && ring.dropped() == priorEventLoss &&
            core.lostState() == priorStateLoss && ring.empty(),
            "post-close observation changed footer counts or resumed bookkeeping");
}
void concurrentWriterClosureKeepsAStableDrain() {
    EventRing ring;
    constexpr unsigned Producers = 8;
    std::atomic<bool> ready{false}, stop{false};
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < Producers; ++i) producers.emplace_back([&] {
        while (!ready.load()) std::this_thread::yield();
        while (!stop.load()) ring.push(Event{});
    });
    ready.store(true);
    while (ring.emitted() < EventCapacity) std::this_thread::yield();
    ring.close();
    const auto emitted = ring.emitted(), dropped = ring.dropped();
    const auto queued = drain(ring);
    // Closed calls keep returning immediately while the writer serializes its
    // final records, so no process shutdown/join is needed to freeze counters.
    for (unsigned i = 0; i < 32; ++i) std::this_thread::yield();
    require(ring.emitted() == emitted && ring.dropped() == dropped && ring.empty(),
            "render producers raced the closed writer drain/footer");
    stop.store(true);
    for (auto& producer : producers) producer.join();
    require(queued.size() == emitted && emitted == EventCapacity,
            "concurrent close lost queued records or changed the capacity bound");
    for (std::size_t i = 0; i < queued.size(); ++i)
        require(queued[i].serial == i + 1, "closed concurrent drain has a serial gap");
}
void contentionCannotCertifyStaleState() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    begin(core);
    drain(ring);
    {
        auto lock = TestAccess::hold(core);
        core.pipelineDestroyed(1, 20); // deterministic failed try_lock
    }
    require(core.lostState() == 1, "state contention was not counted");
    core.draw(1, 30, 1);
    const auto lostDraw = kind(drain(ring), Kind::Draw);
    require(lostDraw.size() == 1 && !lostDraw[0].complete, "missed destruction preserved false evidence");
    seed(core);
    begin(core);
    core.draw(1, 30, 1);
    require(!kind(drain(ring), Kind::Draw)[0].complete, "state-loss taint was not sticky");
}
void fragmentFilter() {
    EventRing ring;
    Core core(ring, true);
    core.filterFragment(fingerprint(fragment, sizeof(fragment)));
    seed(core);
    begin(core, 31);
    core.draw(1, 31, 1);
    core.commandEnd(1, 31);
    core.commandAllocated(1, 30, 40);
    core.commandBegin(1, 30);
    core.execute(1, 30, 31);
    core.commandEnd(1, 30);
    submit(core, 1, 30, 1);
    auto events = drain(ring);
    require(kind(events, Kind::Draw).size() == 1 && kind(events, Kind::Submit).size() == 1,
            "matching filter lost draw/secondary submission");
    const auto stages = inputs();
    core.pipelineCreated(1, 21, 99, stages.data(), 1, true);
    core.commandBegin(1, 31);
    core.bind(1, 31, 21);
    core.draw(1, 31, 1);
    core.commandEnd(1, 31);
    submit(core, 1, 31, 2);
    events = drain(ring);
    require(kind(events, Kind::Draw).empty() && kind(events, Kind::Submit).empty(), "non-matching shader consumed filtered draw budget");
}
void submitRaceKeepsPreCallGeneration() {
    EventRing ring;
    Core core(ring, true);
    seed(core);
    core.commandAllocated(1, 32, 40);
    submit(core, 1, 32, 99);
    require(!kind(drain(ring), Kind::Submit)[0].complete, "allocated initial CB falsely marked executable");
    begin(core);
    core.draw(1, 30, 1);
    core.commandEnd(1, 30);
    const auto original = kind(drain(ring), Kind::Draw)[0];
    const auto failedTicket = core.prepareSubmit(1, 30);
    core.commitSubmit(failedTicket, 100, false);
    require(kind(drain(ring), Kind::Submit).empty(), "failed native submission produced accepted evidence");
    const auto raceTicket = core.prepareSubmit(1, 30);
    core.commandReset(1, 30, false);
    core.commandBegin(1, 30);
    core.bind(1, 30, 20);
    core.draw(1, 30, 1);
    core.commandEnd(1, 30);
    const auto replacement = kind(drain(ring), Kind::Draw)[0];
    require(replacement.commandGeneration != original.commandGeneration, "race setup did not change generation");
    core.commitSubmit(raceTicket, 101, true);
    const auto accepted = kind(drain(ring), Kind::Submit);
    require(accepted.size() == 1 && !accepted[0].complete && accepted[0].reason == Core::ChangedGeneration,
            "post-submit reset was incorrectly certified");
    require(accepted[0].commandGeneration == original.commandGeneration,
            "submit race attributed accepted work to a new command-buffer generation");
    // The previous ticket must not mark the replacement recording as reported.
    submit(core, 1, 30, 102);
    const auto current = kind(drain(ring), Kind::Submit);
    require(current.size() == 1 && current[0].complete &&
            current[0].commandGeneration == replacement.commandGeneration,
            "old submit ticket consumed replacement-generation evidence");
}
void selectedDrawSnapshotsDoNotCollapseTheSecondEye() {
    EventRing ring;
    Core core(ring, true);
    core.filterFragment(fingerprint(fragment, sizeof(fragment)));
    seed(core);
    begin(core);
    drain(ring);
    core.draw(1, 30, 1);
    Event first, second;
    require(core.describeDraw(1, 30, first) && first.complete, "first selected binding query missing");
    core.draw(1, 30, 1);
    require(core.describeDraw(1, 30, second) && second.complete &&
            second.commandGeneration == first.commandGeneration && second.layout == 99,
            "same-pipeline second-eye query was deduplicated");
    require(kind(drain(ring), Kind::Draw).size() == 1,
            "source queries changed ordinary pipeline event deduplication");
    core.commandEnd(1, 30);
    require(!core.describeDraw(1, 30, second), "non-recording command described as a new draw");
    core.commandBegin(1, 30);
    core.bind(1, 30, 20);
    core.filterFragment(1);
    require(!core.describeDraw(1, 30, second), "unrelated shader escaped resource filter");
    core.filterFragment(fingerprint(fragment, sizeof(fragment)));
    core.pipelineDestroyed(1, 20);
    require(!core.describeDraw(1, 30, second), "destroyed pipeline satisfied binding query");
}
}

int main() {
    disabledAndFingerprint();
    stageLifecycleAndActualDraw();
    resetAndSecondary();
    failuresAndBounds();
    multiRecordCallClosesCleanly();
    closureKeepsEarlierLoss();
    concurrentBoundaryAccounting();
    writerNeverTakesTheProducerMutex();
    singleProducerAndWriterPreserveEveryPublishedSlot();
    writerClosureWaitsForBookkeeping();
    closedBookkeepingCannotRaceFooter();
    concurrentWriterClosureKeepsAStableDrain();
    contentionCannotCertifyStaleState();
    fragmentFilter();
    submitRaceKeepsPreCallGeneration();
    selectedDrawSnapshotsDoNotCollapseTheSecondEye();
    std::cout << "passive pipeline trace: lifecycle, exact stage metadata, submit links, bounds, contention and disabled paths passed\n";
}
