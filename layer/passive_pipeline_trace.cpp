// Opt-in passive Vulkan metadata layer. No shader replacement, descriptor
// mutation, image/memory readback, GPU work, or render-thread log I/O.
#include "passive_pipeline_trace.h"
#include "passive_binding_journal.h"
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define TRACE_EXPORT extern "C" __attribute__((visibility("default")))

namespace {
using namespace frame_trace;
constexpr std::size_t OutputLimit = 1024 * 1024;
// Reserve room for bounded aggregate replay diagnostics within the unchanged
// 1-MiB file cap. No per-call trace or native payload is appended to the footer.
constexpr std::size_t FooterReserve = 8192;
constexpr std::uint32_t CallRecordLimit = 512;

struct Capture {
    EventRing events;
    Core core{events};
    frame_bindings::Journal bindings;
    std::atomic<bool> stop{false};
    std::thread writer;
    std::once_flag once;
    bool processAllowed = false;
    std::uint64_t fragmentFilter = 0;
    bool bindingsEnabled = false;
    std::atomic<bool> bindingsArmed{false};
    std::atomic<std::uint32_t> bindingSnapshots{0};
    static constexpr std::uint32_t BindingSnapshotLimit = 32;
    // Writer-owned results; a request is published before its placeholder event.
    // Keep the event's draw-time serial, even when background replay is delayed.
    std::array<frame_bindings::Snapshot, BindingSnapshotLimit> bindingResults{};
    std::array<bool, BindingSnapshotLimit> bindingReady{};
    std::size_t drainBindings() {
        if (!bindingsEnabled) return 0;
        return bindings.drain([this](std::uint32_t cookie, const frame_bindings::Snapshot& result) {
            if (cookie < BindingSnapshotLimit) {
                bindingResults[cookie] = result;
                bindingReady[cookie] = true;
            }
        });
    }

    bool drawsReady() const noexcept {
        // Keep lifecycle metadata before arming, but reserve draw/submit event
        // capacity for the operator's actual stream instead of native Home.
        return core.enabled() && (!bindingsEnabled || bindingsArmed.load(std::memory_order_acquire));
    }

    void start() {
        std::call_once(once, [&] {
            const auto* enabled = std::getenv("FRAME_MASK_TRACE");
            const auto* disabled = std::getenv("FRAME_MASK_TRACE_DISABLE");
            char executable[4096]{};
            auto size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
            if (!enabled || std::strcmp(enabled, "1") ||
                (disabled && !std::strcmp(disabled, "1")) || size <= 0) return;
            executable[size] = 0;
            const auto* basename = std::strrchr(executable, '/');
            if (std::strcmp(basename ? basename + 1 : executable, "vrcompositor")) return;
            processAllowed = true;
            const auto* bindingFlag = std::getenv("FRAME_MASK_TRACE_BINDINGS");
            bindingsEnabled = bindingFlag && !std::strcmp(bindingFlag, "1");
            const auto* arm = std::getenv("FRAME_MASK_TRACE_BINDINGS_ARM_FILE");
            if (arm && (arm[0] != '/' || strnlen(arm, 4096) >= 4096)) {
                processAllowed = false;
                return;
            }
            std::string armPath = bindingsEnabled && arm ? arm : "";
            bindingsArmed.store(bindingsEnabled && armPath.empty());
            const auto* filter = std::getenv("FRAME_MASK_TRACE_FRAGMENT_HASH");
            std::uint64_t parsedFilter = 0;
            if (filter) {
                if (strnlen(filter, 17) != 16) { processAllowed = false; return; }
                for (std::size_t i = 0; i < 16; ++i) {
                    const char value = filter[i];
                    const unsigned digit = value >= '0' && value <= '9' ? value - '0' :
                        value >= 'a' && value <= 'f' ? value - 'a' + 10 :
                        value >= 'A' && value <= 'F' ? value - 'A' + 10 : 16;
                    if (digit == 16) { processAllowed = false; return; }
                    parsedFilter = (parsedFilter << 4) | digit;
                }
                if (!parsedFilter) { processAllowed = false; return; }
                fragmentFilter = parsedFilter;
                core.filterFragment(parsedFilter);
            }
            // Initialization happens before native instance creation. The writer
            // owns file creation, serialization, flush and close; hooks enqueue
            // fixed metadata only and never wait for the writer.
            const auto* configured = std::getenv("FRAME_MASK_TRACE_OUTPUT");
            std::string path;
            if (configured && configured[0] == '/' && strnlen(configured, 4096) < 4096)
                path = configured;
            else path = "/tmp/frame-mask-trace-" + std::to_string(getpid()) + ".jsonl";
            core.enable(true);
            try { writer = std::thread([this, path, armPath] { write(path, armPath); }); }
            catch (...) { core.enable(false); processAllowed = false; }
        });
    }
    ~Capture() {
        core.enable(false);
        stop.store(true);
        if (writer.joinable()) writer.join(); // Process/layer teardown only.
    }

    static bool append(char* out, std::size_t capacity, std::size_t& used,
                       const char* format, ...) {
        if (used >= capacity) return false;
        va_list args;
        va_start(args, format);
        int count = std::vsnprintf(out + used, capacity - used, format, args);
        va_end(args);
        if (count < 0 || static_cast<std::size_t>(count) >= capacity - used) return false;
        used += static_cast<std::size_t>(count);
        return true;
    }
    static void escapedEntry(const char* source, char (&target)[EntryCapacity * 6 + 1]) {
        std::size_t used = 0;
        for (std::size_t i = 0; i < EntryCapacity && source[i]; ++i) {
            const auto value = static_cast<unsigned char>(source[i]);
            if (value == '\\' || value == '"') {
                target[used++] = '\\';
                target[used++] = static_cast<char>(value);
            } else if (value < 32 || value > 126) {
                std::snprintf(target + used, 7, "\\u%04x", value);
                used += 6;
            } else target[used++] = static_cast<char>(value);
        }
        target[used] = 0;
    }
    static std::size_t serialize(const Event& e, char (&out)[32768],
                                 const frame_bindings::Snapshot* binding = nullptr) {
        std::size_t used = 0;
        const char* names[] = {"shader", "pipeline", "draw_recorded", "execute_recorded",
                               "submit_accepted", "incomplete", "bindings_recorded", "bindings_armed"};
        bool valid = append(out, sizeof(out), used,
            "{\"event\":\"%s\",\"serial\":%llu,\"device\":%llu,\"complete\":%s,"
            "\"reason\":%u,\"command\":\"%llx\",\"command_generation\":%llu,"
            "\"related_command\":\"%llx\",\"related_generation\":%llu,"
            "\"pipeline\":\"%llx\",\"pipeline_generation\":%llu,\"layout\":\"%llx\","
            "\"shader\":\"%llx\",\"shader_generation\":%llu,\"fnv1a64\":\"%016llx\","
            "\"bytes\":%u,\"draw_kind\":%u,\"submission\":%llu,\"stages\":[",
            names[static_cast<unsigned>(e.kind)], static_cast<unsigned long long>(e.serial),
            static_cast<unsigned long long>(e.device), e.complete ? "true" : "false", e.reason,
            static_cast<unsigned long long>(e.command), static_cast<unsigned long long>(e.commandGeneration),
            static_cast<unsigned long long>(e.relatedCommand), static_cast<unsigned long long>(e.relatedGeneration),
            static_cast<unsigned long long>(e.pipeline), static_cast<unsigned long long>(e.pipelineGeneration),
            static_cast<unsigned long long>(e.layout), static_cast<unsigned long long>(e.shader),
            static_cast<unsigned long long>(e.shaderGeneration), static_cast<unsigned long long>(e.hash),
            e.bytes, e.drawKind, static_cast<unsigned long long>(e.submission));
        for (std::uint32_t i = 0; valid && i < e.stageCount; ++i) {
            const auto& stage = e.stages[i];
            char entry[EntryCapacity * 6 + 1]{};
            escapedEntry(stage.entry, entry);
            valid = append(out, sizeof(out), used,
                "%s{\"stage\":%u,\"module\":\"%llx\",\"generation\":%llu,"
                "\"fnv1a64\":\"%016llx\",\"bytes\":%u,\"entry\":\"%s\",\"complete\":%s}",
                i ? "," : "", stage.stage, static_cast<unsigned long long>(stage.module),
                static_cast<unsigned long long>(stage.generation), static_cast<unsigned long long>(stage.hash),
                stage.bytes, entry, stage.complete ? "true" : "false");
        }
        valid = valid && append(out, sizeof(out), used, "]");
        if (e.kind == Kind::Bindings)
            valid = valid && binding && serializeBindings(*binding, out, sizeof(out), used);
        valid = valid && append(out, sizeof(out), used, "}\n");
        return valid ? used : 0;
    }
    static bool writeAll(int fd, const char* bytes, std::size_t count) {
        while (count) {
            auto written = ::write(fd, bytes, count);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return false;
            bytes += written;
            count -= static_cast<std::size_t>(written);
        }
        return true;
    }
    static bool armRequested(const std::string& path) {
        // Writer thread only. Do not spend the bounded binding budget on native
        // Home rendering while the operator reconnects the actual VR stream.
        const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) return false;
        struct stat status{};
        constexpr char expected[] = "FRAME_MASK_TRACE_BINDINGS/1\n";
        char bytes[sizeof(expected)]{};
        const bool owned = !fstat(fd, &status) && S_ISREG(status.st_mode) && status.st_uid == getuid() &&
            (status.st_mode & 0777) == 0600 && status.st_size == sizeof(expected) - 1;
        const bool valid = owned && read(fd, bytes, sizeof(bytes)) == sizeof(expected) - 1 &&
            !std::memcmp(bytes, expected, sizeof(expected) - 1);
        close(fd);
        return valid;
    }
    static bool threadCpuNanos(std::uint64_t& result) {
        // Background writer only, once at each end of the measurement window.
        // Separate actual writer CPU use from wall time and scheduler stalls.
        timespec stamp{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stamp) || stamp.tv_sec < 0 ||
            stamp.tv_nsec < 0 || stamp.tv_nsec >= 1000000000) return false;
        result = static_cast<std::uint64_t>(stamp.tv_sec) * UINT64_C(1000000000) +
                 static_cast<std::uint64_t>(stamp.tv_nsec);
        return true;
    }
    void write(const std::string& path, const std::string& armPath) {
        // O_EXCL/O_NOFOLLOW prevents replacing an existing file or following a
        // symlink. The trace contains no binary shader payload or private image.
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { core.enable(false); return; }
        // Control helper can reject closed/crashed captures before creating an
        // arm marker. This lock belongs only to the background writer.
        if (flock(fd, LOCK_EX | LOCK_NB)) { core.enable(false); close(fd); return; }
        if (!armPath.empty() && armRequested(armPath)) bindingsArmed.store(true, std::memory_order_release);
        // When timed recovery is enabled, leave time for its independently
        // armed 180-second watchdog to restore SteamVR after capture closure.
        // Manual recovery uses these same recording bounds without a watchdog.
        const unsigned deadlineSeconds = fragmentFilter ? 120 : 30;
        char header[1024];
        const auto headerLength = std::snprintf(header, sizeof(header),
            "{\"event\":\"header\",\"version\":1,\"layer\":\"VK_LAYER_FRAME_MASK_trace\","
            "\"scope\":\"passive_pipeline_metadata\",\"hash\":\"fnv1a64_standard\","
            "\"limit_events\":2048,\"limit_bytes\":1048576,\"gpu_completion_proven\":false,"
            "\"descriptor_uv_frame_binding_proven\":false,\"fragment_filter\":\"%016llx\","
            "\"binding_metadata_version\":%u,\"limit_binding_snapshots\":%u,"
            "\"binding_lane_capacity\":%zu,"
            "\"binding_lane_byte_capacity\":%zu,"
            "\"binding_arm_required\":%s,"
            "\"pre_arm_draws_recorded\":false,"
            "\"binding_ready_at_header\":%s,"
            "\"deadline_seconds\":%u}\n", static_cast<unsigned long long>(fragmentFilter),
            bindingsEnabled ? 1 : 0, bindingsEnabled ? BindingSnapshotLimit : 0,
            frame_bindings::Journal::LaneCapacity,
            frame_bindings::Journal::ByteCapacity,
            armPath.empty() ? "false" : "true", bindingsArmed.load() ? "true" : "false", deadlineSeconds);
        std::size_t total = static_cast<std::size_t>(headerLength);
        bool ioGood = writeAll(fd, header, total), capped = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(deadlineSeconds);
        auto armCheck = std::chrono::steady_clock::now();
        const auto writerStart = armCheck;
        std::uint64_t cpuStart = 0;
        const bool cpuStartValid = threadCpuNanos(cpuStart);
        bool draining = false;
        Event event;
        bool pending = false;
        std::uint64_t writerIterations = 0, replayRecords = 0, replayNanos = 0, maxReplayNanos = 0;
        std::uint64_t snapshotWaits = 0, idleWaits = 0, serializationNanos = 0, outputNanos = 0, maxOutputNanos = 0;
        const auto elapsedNanos = [](const auto& since) {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - since).count());
        };
        for (;;) {
            ++writerIterations;
            const auto replayStart = std::chrono::steady_clock::now();
            const auto metadataRecords = drainBindings();
            const auto replayElapsed = elapsedNanos(replayStart);
            replayRecords += metadataRecords;
            replayNanos += replayElapsed;
            maxReplayNanos = std::max(maxReplayNanos, replayElapsed);
            const auto now = std::chrono::steady_clock::now();
            if (!draining && bindingsEnabled && !bindingsArmed.load() && !armPath.empty() && now >= armCheck) {
                if (now < deadline && armRequested(armPath)) {
                    Event armed;
                    armed.kind = Kind::BindingsArmed;
                    armed.complete = true;
                    if (events.push(armed)) bindingsArmed.store(true, std::memory_order_release);
                }
                armCheck = now + std::chrono::milliseconds(100);
            }
            // Check even while records arrive continuously. Disabling producers
            // leaves only the fixed-capacity ring and bounded in-flight records
            // to drain before the footer; queue traffic cannot defer expiration.
            if (!draining && (stop.load() || events.finished() ||
                              std::chrono::steady_clock::now() >= deadline)) {
                core.enable(false);
                events.close();
                bindings.close();
                draining = true;
            }
            if (!pending) pending = events.pop(event);
            if (pending) {
                frame_bindings::Snapshot failure;
                const frame_bindings::Snapshot* binding = nullptr;
                if (event.kind == Kind::Bindings && event.bindingCookie < BindingSnapshotLimit) {
                    if (!bindingReady[event.bindingCookie]) {
                        // Only the writer defers an unresolved predecessor; the
                        // rendering thread forwards immediately. A closed gap
                        // must produce incomplete evidence, never guessed state.
                        if (!bindings.finished()) {
                            // More replayed metadata may precede this request.
                            // Waiting after each productive batch unnecessarily
                            // caps catch-up at 256 records per millisecond.
                            // A genuinely unpublished predecessor makes zero
                            // progress and still yields instead of busy looping.
                            if (!metadataRecords) {
                                ++snapshotWaits;
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                            continue;
                        }
                        failure.device = event.device;
                        binding = &failure;
                    } else binding = &bindingResults[event.bindingCookie];
                } else if (event.kind == Kind::Bindings) {
                    failure.device = event.device;
                    failure.reason = event.bindingFailure;
                    binding = &failure;
                }
                if (binding) event.complete = event.complete && binding->sourceComplete;
                char line[32768];
                const auto serializationStart = std::chrono::steady_clock::now();
                const auto bytes = serialize(event, line, binding);
                serializationNanos += elapsedNanos(serializationStart);
                if (!bytes || total + bytes > OutputLimit - FooterReserve) {
                    capped = true;
                    core.enable(false);
                    break;
                }
                const auto outputStart = std::chrono::steady_clock::now();
                const bool written = writeAll(fd, line, bytes);
                const auto outputElapsed = elapsedNanos(outputStart);
                outputNanos += outputElapsed;
                maxOutputNanos = std::max(maxOutputNanos, outputElapsed);
                if (!written) { ioGood = false; break; }
                total += bytes;
                pending = false;
            } else {
                if (draining && events.empty() && (!bindingsEnabled || bindings.finished())) break;
                // Continue existing background work without an idle delay. A
                // missing predecessor makes zero progress and still yields.
                if (metadataRecords && !bindings.empty()) continue;
                ++idleWaits;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        core.enable(false);
        events.close();
        bindings.close();
        // Byte/time/event closure must include admitted metadata in the stable
        // footer, even when trailing output records no longer fit the file.
        while (bindingsEnabled && !bindings.finished()) {
            const auto replayStart = std::chrono::steady_clock::now();
            const auto replayed = drainBindings();
            replayRecords += replayed;
            const auto replayElapsed = elapsedNanos(replayStart);
            replayNanos += replayElapsed;
            maxReplayNanos = std::max(maxReplayNanos, replayElapsed);
            if (!replayed && !bindings.finished()) {
                ++snapshotWaits;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        char footer[FooterReserve];
        const auto journal = bindings.transportCounters();
        std::uint64_t cpuEnd = 0;
        const bool cpuValid = threadCpuNanos(cpuEnd) && cpuStartValid && cpuEnd >= cpuStart;
        const auto cpuElapsed = cpuValid ? cpuEnd - cpuStart : 0;
        const auto writerElapsed = elapsedNanos(writerStart);
        const auto count = std::snprintf(footer, sizeof(footer),
            "{\"event\":\"footer\",\"emitted\":%llu,\"dropped_events\":%llu,"
            "\"lost_state\":%llu,\"lost_binding_state\":%llu,\"binding_capture_armed\":%s,"
            "\"binding_snapshots_attempted\":%u,\"byte_cap_reached\":%s,\"io_ok\":%s,\"capture_bounded\":true,"
            "\"binding_loss_reasons\":{\"capacity\":%llu,\"contention\":%llu,\"unknown\":%llu,"
            "\"unsupported\":%llu,\"stale\":%llu,\"bounds\":%llu,\"layout_mismatch\":%llu},"
            "\"binding_capacity_tables\":{\"set_layout\":%llu,\"pipeline_layout\":%llu,\"pool\":%llu,"
            "\"set\":%llu,\"buffer\":%llu,\"image\":%llu,\"view\":%llu,\"sampler\":%llu,"
            "\"memory\":%llu,\"command\":%llu,\"conversion\":%llu},"
            "\"binding_journal\":{\"registry\":%llu,\"queue\":%llu,\"order_gap\":%llu,"
            "\"batch\":%llu,\"order_wrap\":%llu,\"closed_calls\":%llu,\"published\":%llu,\"replayed\":%llu,"
            "\"header_queue\":%llu,\"byte_queue\":%llu}",
            static_cast<unsigned long long>(events.emitted()), static_cast<unsigned long long>(events.dropped()),
            static_cast<unsigned long long>(core.lostState()), static_cast<unsigned long long>(bindings.lostState()),
            bindingsArmed.load() ? "true" : "false", std::min(bindingSnapshots.load(), BindingSnapshotLimit),
            capped ? "true" : "false", ioGood ? "true" : "false",
#define LOSS(reason) static_cast<unsigned long long>(bindings.lossCount(frame_bindings::Reason::reason))
            LOSS(Capacity), LOSS(Contention), LOSS(Unknown), LOSS(Unsupported), LOSS(Stale), LOSS(Bounds), LOSS(LayoutMismatch),
#undef LOSS
#define TABLE(name) static_cast<unsigned long long>(bindings.capacityLossCount(frame_bindings::Table::name))
            TABLE(SetLayout), TABLE(PipelineLayout), TABLE(Pool), TABLE(Set), TABLE(Buffer), TABLE(Image),
            TABLE(View), TABLE(Sampler), TABLE(Memory), TABLE(Command), TABLE(Conversion),
            static_cast<unsigned long long>(journal.registry), static_cast<unsigned long long>(journal.queue),
            static_cast<unsigned long long>(journal.orderGap), static_cast<unsigned long long>(journal.batch),
            static_cast<unsigned long long>(journal.orderWrap), static_cast<unsigned long long>(journal.closedCalls),
            static_cast<unsigned long long>(journal.published), static_cast<unsigned long long>(journal.replayed),
            static_cast<unsigned long long>(journal.headerQueue), static_cast<unsigned long long>(journal.byteQueue));
#undef TABLE
        std::size_t footerBytes = count > 0 ? static_cast<std::size_t>(count) : sizeof(footer);
        bool footerValid = append(footer, sizeof(footer), footerBytes,
            ",\"binding_writer\":{\"iterations\":%llu,\"replayed\":%llu,\"replay_ns\":%llu,"
            "\"max_replay_batch_ns\":%llu,\"snapshot_waits\":%llu,\"idle_waits\":%llu,"
            "\"serialization_ns\":%llu,\"output_ns\":%llu,\"max_output_ns\":%llu,\"max_lane_occupancy\":%llu,"
            "\"max_lane_bytes\":%llu,\"elapsed_ns\":%llu,\"cpu_clock_valid\":%s,\"cpu_ns\":%llu},"
            "\"binding_operation_losses\":{",
            static_cast<unsigned long long>(writerIterations), static_cast<unsigned long long>(replayRecords),
            static_cast<unsigned long long>(replayNanos), static_cast<unsigned long long>(maxReplayNanos),
            static_cast<unsigned long long>(snapshotWaits), static_cast<unsigned long long>(idleWaits),
            static_cast<unsigned long long>(serializationNanos), static_cast<unsigned long long>(outputNanos),
            static_cast<unsigned long long>(maxOutputNanos),
            static_cast<unsigned long long>(journal.maxLaneOccupancy),
            static_cast<unsigned long long>(journal.maxLaneBytes),
            static_cast<unsigned long long>(writerElapsed), cpuValid ? "true" : "false",
            static_cast<unsigned long long>(cpuElapsed));
        const auto operations = bindings.operationCounters();
        for (std::size_t i = 0; footerValid && i < operations.size(); ++i) {
            footerValid = append(footer, sizeof(footer), footerBytes,
                "%s\"%s\":{\"publication\":%llu,\"replay\":%llu}", i ? "," : "",
                frame_bindings::Journal::operationName(i),
                static_cast<unsigned long long>(operations[i].publicationLoss),
                static_cast<unsigned long long>(operations[i].replayLoss));
        }
        footerValid = footerValid && append(footer, sizeof(footer), footerBytes, "}}\n");
        if (ioGood && footerValid) writeAll(fd, footer, footerBytes);
        close(fd);
    }
    static bool serializeBindings(const frame_bindings::Snapshot&, char*, std::size_t, std::size_t&);
};
#include "passive_binding_serialize.inc"

Capture& capture() { static Capture instance; return instance; }
template <typename T> std::uint64_t number(T handle) { return (std::uint64_t)(std::uintptr_t)handle; }
template <typename T> void* dispatchKey(T handle) { return *reinterpret_cast<void**>(handle); }

template <typename Observer> void observeBindings(Observer observer) {
    auto& c = capture();
    if (!c.bindingsEnabled || !c.core.enabled()) return;
    // Admission includes owned scalar journal publications in the stable footer.
    // Object-table lookup/mutation is exclusively replayed by the writer. This
    // scope never spans a driver call or waits for the writer.
    const auto admission = c.events.admit();
    if (admission && c.core.enabled()) observer(c.bindings);
}
void recordedDraw(std::uint64_t device, VkCommandBuffer command, std::uint32_t kind) {
    auto& c = capture();
    const auto admission = c.events.admit();
    if (!admission || !c.drawsReady()) return;
    c.core.draw(device, number(command), kind);
    if (!c.bindingsEnabled || !c.bindingsArmed.load(std::memory_order_acquire) ||
        c.bindingSnapshots.load(std::memory_order_relaxed) >= Capture::BindingSnapshotLimit) return;
    Event event;
    if (!c.core.describeDraw(device, number(command), event)) return;
    const auto cookie = c.bindingSnapshots.fetch_add(1, std::memory_order_relaxed);
    if (cookie >= Capture::BindingSnapshotLimit) return;
    event.kind = Kind::Bindings;
    event.drawKind = kind;
    event.bindingCookie = cookie;
    if (!c.bindings.requestSnapshot(device, command, (VkPipelineLayout)(std::uintptr_t)event.layout, cookie)) {
        event.bindingCookie = std::numeric_limits<std::uint32_t>::max();
        event.bindingFailure = frame_bindings::Reason::Capacity;
        event.complete = false;
    }
    c.events.push(event);
}

struct Instance {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_GetPhysicalDeviceProcAddr gpdpa = nullptr;
    VkInstance native = VK_NULL_HANDLE;
};
struct Device {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    VkDevice native = VK_NULL_HANDLE;
    std::uint64_t id = 0;
#define FIELD(name) PFN_vk##name name = nullptr;
    FIELD(DestroyDevice) FIELD(CreateShaderModule) FIELD(DestroyShaderModule)
    FIELD(CreateGraphicsPipelines) FIELD(DestroyPipeline)
    FIELD(AllocateCommandBuffers) FIELD(FreeCommandBuffers)
    FIELD(BeginCommandBuffer) FIELD(EndCommandBuffer) FIELD(ResetCommandBuffer)
    FIELD(ResetCommandPool) FIELD(DestroyCommandPool) FIELD(CmdBindPipeline)
    FIELD(CmdDraw) FIELD(CmdDrawIndexed) FIELD(CmdDrawIndirect) FIELD(CmdDrawIndexedIndirect)
    FIELD(CmdDrawIndirectCount) FIELD(CmdDrawIndexedIndirectCount)
    FIELD(CmdExecuteCommands) FIELD(QueueSubmit) FIELD(QueueSubmit2)
    FIELD(CmdBindShadersEXT)
#define BINDING_FIELD(name) FIELD(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#undef FIELD
};

// These are required loader forwarding records (one small allocation per live
// native context), not observation caches. Allocations occur only at creation;
// records are freed at destruction. Observation/event storage is fixed-capacity.
std::mutex contextsMutex;
std::unordered_map<void*, std::unique_ptr<Instance>> instances;
std::unordered_map<void*, std::unique_ptr<Device>> devices;
std::atomic<std::uint64_t> nextDevice{0}, nextSubmission{0};
template <typename T> Instance* instanceFor(T handle) {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lock(contextsMutex);
    auto it = instances.find(dispatchKey(handle));
    return it == instances.end() ? nullptr : it->second.get();
}
template <typename T> Device* deviceFor(T handle) {
    if (!handle) return nullptr;
    std::lock_guard<std::mutex> lock(contextsMutex);
    auto it = devices.find(dispatchKey(handle));
    return it == devices.end() ? nullptr : it->second.get();
}
std::uint32_t limited(Device* d, std::uint32_t count) {
    if (count > CallRecordLimit) capture().core.report(d->id, Core::Capacity);
    return std::min(count, CallRecordLimit);
}
bool ordinaryPipeline(const VkGraphicsPipelineCreateInfo& info) {
    if (info.flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) return false;
    const auto* next = static_cast<const VkBaseInStructure*>(info.pNext);
    std::uint32_t budget = 32;
    while (next && budget--) {
        if (next->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR &&
            reinterpret_cast<const VkPipelineLibraryCreateInfoKHR*>(next)->libraryCount) return false;
        if (next->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT) return false;
        if (next->sType == VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR &&
            (reinterpret_cast<const VkPipelineCreateFlags2CreateInfoKHR*>(next)->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)) return false;
        next = next->pNext;
    }
    return !next;
}
} // namespace

TRACE_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
TRACE_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

VKAPI_ATTR VkResult VKAPI_CALL traceCreateInstance(const VkInstanceCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkInstance* output) {
    capture().start();
    auto* chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO))
        chain = static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto record = std::make_unique<Instance>();
    record->gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    record->gpdpa = chain->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(record->gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    auto result = create(info, allocator, output);
    if (result == VK_SUCCESS) {
        record->native = *output;
        std::lock_guard<std::mutex> lock(contextsMutex);
        instances[dispatchKey(*output)] = std::move(record);
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    auto* record = instanceFor(instance);
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(record->gipa(instance, "vkDestroyInstance"));
    const auto key = dispatchKey(instance);
    destroy(instance, allocator);
    std::lock_guard<std::mutex> lock(contextsMutex);
    instances.erase(key);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDevice* output) {
    auto* instance = instanceFor(physical);
    auto* chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO))
        chain = static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!instance || !chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto record = std::make_unique<Device>();
    auto gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    record->gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(instance->native, "vkCreateDevice"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    auto result = create(physical, info, allocator, output);
    if (result == VK_SUCCESS) {
        record->native = *output;
        record->id = ++nextDevice;
#define LOAD(name) record->name = reinterpret_cast<PFN_vk##name>(record->gdpa(*output, "vk" #name));
        LOAD(DestroyDevice) LOAD(CreateShaderModule) LOAD(DestroyShaderModule)
        LOAD(CreateGraphicsPipelines) LOAD(DestroyPipeline)
        LOAD(AllocateCommandBuffers) LOAD(FreeCommandBuffers)
        LOAD(BeginCommandBuffer) LOAD(EndCommandBuffer) LOAD(ResetCommandBuffer)
        LOAD(ResetCommandPool) LOAD(DestroyCommandPool) LOAD(CmdBindPipeline)
        LOAD(CmdDraw) LOAD(CmdDrawIndexed) LOAD(CmdDrawIndirect) LOAD(CmdDrawIndexedIndirect)
        LOAD(CmdDrawIndirectCount) LOAD(CmdDrawIndexedIndirectCount)
        LOAD(CmdExecuteCommands) LOAD(QueueSubmit) LOAD(QueueSubmit2)
        LOAD(CmdBindShadersEXT)
#define BINDING_FIELD(name) LOAD(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#define BINDING_ALIAS(alias, name) if (!record->name) record->name = reinterpret_cast<PFN_vk##name>(record->gdpa(*output, alias));
#include "passive_binding_aliases.inc"
#undef BINDING_ALIAS
#undef LOAD
        if (!record->QueueSubmit2) record->QueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(record->gdpa(*output, "vkQueueSubmit2KHR"));
        if (!record->CmdDrawIndirectCount) record->CmdDrawIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndirectCount>(record->gdpa(*output, "vkCmdDrawIndirectCountKHR"));
        if (!record->CmdDrawIndexedIndirectCount) record->CmdDrawIndexedIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(record->gdpa(*output, "vkCmdDrawIndexedIndirectCountKHR"));
        std::lock_guard<std::mutex> lock(contextsMutex);
        devices[dispatchKey(*output)] = std::move(record);
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    capture().core.deviceDestroyed(d->id);
    observeBindings([&](auto& b) { b.deviceDestroyed(d->id); });
    const auto key = dispatchKey(device);
    d->DestroyDevice(device, allocator);
    std::lock_guard<std::mutex> lock(contextsMutex);
    devices.erase(key);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkShaderModule* output) {
    auto* d = deviceFor(device);
    auto result = d->CreateShaderModule(device, info, allocator, output);
    if (result == VK_SUCCESS) capture().core.shaderCreated(d->id, number(*output), info->pCode, info->codeSize);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyShaderModule(VkDevice device, VkShaderModule module,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    capture().core.shaderDestroyed(d->id, number(module));
    d->DestroyShaderModule(device, module, allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateGraphicsPipelines(VkDevice device, VkPipelineCache cache,
    std::uint32_t count, const VkGraphicsPipelineCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipeline* output) {
    auto* d = deviceFor(device);
    auto result = d->CreateGraphicsPipelines(device, cache, count, info, allocator, output);
    if (capture().core.enabled()) {
        for (std::uint32_t i = 0; i < limited(d, count); ++i) {
            std::array<Stage, StageCapacity> stages{};
            const auto tracked = std::min<std::uint32_t>(info[i].stageCount, StageCapacity);
            for (std::uint32_t j = 0; j < tracked; ++j) {
                stages[j].stage = info[i].pStages[j].stage;
                stages[j].module = number(info[i].pStages[j].module);
                stages[j].complete = copyEntry(stages[j].entry, info[i].pStages[j].pName);
            }
            // Inline shader-module creation and graphics-pipeline libraries are
            // incomplete unless ordinary module handles are fully observed.
            capture().core.pipelineCreated(d->id, number(output[i]), number(info[i].layout),
                stages.data(), info[i].stageCount, result == VK_SUCCESS, ordinaryPipeline(info[i]));
        }
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyPipeline(VkDevice device, VkPipeline pipeline,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    capture().core.pipelineDestroyed(d->id, number(pipeline));
    d->DestroyPipeline(device, pipeline, allocator);
}
#include "passive_binding_hooks.inc"
VKAPI_ATTR VkResult VKAPI_CALL traceAllocateCommandBuffers(VkDevice device,
    const VkCommandBufferAllocateInfo* info, VkCommandBuffer* output) {
    auto* d = deviceFor(device);
    auto result = d->AllocateCommandBuffers(device, info, output);
    if (result == VK_SUCCESS) {
        for (std::uint32_t i = 0; i < limited(d, info->commandBufferCount); ++i)
            capture().core.commandAllocated(d->id, number(output[i]), number(info->commandPool));
        observeBindings([&](auto& b) { b.allocateCommands(d->id, info->commandPool, info->commandBufferCount, output); });
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceFreeCommandBuffers(VkDevice device, VkCommandPool pool,
    std::uint32_t count, const VkCommandBuffer* commands) {
    auto* d = deviceFor(device);
    for (std::uint32_t i = 0; i < limited(d, count); ++i) capture().core.commandReset(d->id, number(commands[i]), true);
    observeBindings([&](auto& b) { b.freeCommands(d->id, pool, count, commands); });
    d->FreeCommandBuffers(device, pool, count, commands);
}
VKAPI_ATTR VkResult VKAPI_CALL traceBeginCommandBuffer(VkCommandBuffer command, const VkCommandBufferBeginInfo* info) {
    auto* d = deviceFor(command);
    auto result = d->BeginCommandBuffer(command, info);
    if (result == VK_SUCCESS) {
        capture().core.commandBegin(d->id, number(command));
        observeBindings([&](auto& b) { b.beginCommand(d->id, command); });
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceEndCommandBuffer(VkCommandBuffer command) {
    auto* d = deviceFor(command);
    auto result = d->EndCommandBuffer(command);
    if (result == VK_SUCCESS) {
        capture().core.commandEnd(d->id, number(command));
        observeBindings([&](auto& b) { b.endCommand(d->id, command); });
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceResetCommandBuffer(VkCommandBuffer command, VkCommandBufferResetFlags flags) {
    auto* d = deviceFor(command);
    auto result = d->ResetCommandBuffer(command, flags);
    if (result == VK_SUCCESS) {
        capture().core.commandReset(d->id, number(command), false);
        observeBindings([&](auto& b) { b.resetCommand(d->id, command); });
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags) {
    auto* d = deviceFor(device);
    auto result = d->ResetCommandPool(device, pool, flags);
    if (result == VK_SUCCESS) {
        capture().core.poolReset(d->id, number(pool), false);
        observeBindings([&](auto& b) { b.resetCommandPool(d->id, pool); });
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    capture().core.poolReset(d->id, number(pool), true);
    observeBindings([&](auto& b) { b.destroyCommandPool(d->id, pool); });
    d->DestroyCommandPool(device, pool, allocator);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBindPipeline(VkCommandBuffer command, VkPipelineBindPoint point, VkPipeline pipeline) {
    auto* d = deviceFor(command);
    if (point == VK_PIPELINE_BIND_POINT_GRAPHICS) capture().core.bind(d->id, number(command), number(pipeline));
    d->CmdBindPipeline(command, point, pipeline);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBindShadersEXT(VkCommandBuffer command, std::uint32_t count,
    const VkShaderStageFlagBits* stages, const VkShaderEXT* shaders) {
    auto* d = deviceFor(command);
    // Shader objects can replace a previously bound graphics pipeline. Rather
    // than attribute their subsequent draw to stale pipeline state, taint the
    // capture and retain forwarding unchanged.
    for (std::uint32_t i = 0; i < limited(d, count); ++i)
        if (stages[i] & ~VK_SHADER_STAGE_COMPUTE_BIT) {
            capture().core.report(d->id, Core::UnsupportedCommand);
            break;
        }
    d->CmdBindShadersEXT(command, count, stages, shaders);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDraw(VkCommandBuffer command, std::uint32_t vertices,
    std::uint32_t instances, std::uint32_t firstVertex, std::uint32_t firstInstance) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 1);
    d->CmdDraw(command, vertices, instances, firstVertex, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndexed(VkCommandBuffer command, std::uint32_t indices,
    std::uint32_t instances, std::uint32_t firstIndex, std::int32_t offset, std::uint32_t firstInstance) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 2);
    d->CmdDrawIndexed(command, indices, instances, firstIndex, offset, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndirect(VkCommandBuffer command, VkBuffer buffer,
    VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 3);
    d->CmdDrawIndirect(command, buffer, offset, count, stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndexedIndirect(VkCommandBuffer command, VkBuffer buffer,
    VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 4);
    d->CmdDrawIndexedIndirect(command, buffer, offset, count, stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndirectCount(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer counts, VkDeviceSize countOffset, std::uint32_t maxCount, std::uint32_t stride) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 5);
    d->CmdDrawIndirectCount(command, buffer, offset, counts, countOffset, maxCount, stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndexedIndirectCount(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer counts, VkDeviceSize countOffset, std::uint32_t maxCount, std::uint32_t stride) {
    auto* d = deviceFor(command);
    recordedDraw(d->id, command, 6);
    d->CmdDrawIndexedIndirectCount(command, buffer, offset, counts, countOffset, maxCount, stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdExecuteCommands(VkCommandBuffer command, std::uint32_t count,
    const VkCommandBuffer* children) {
    auto* d = deviceFor(command);
    if (capture().drawsReady())
        for (std::uint32_t i = 0; i < limited(d, count); ++i) capture().core.execute(d->id, number(command), number(children[i]));
    if (count) observeBindings([&](auto& b) { b.executeCommands(d->id, command); });
    d->CmdExecuteCommands(command, count, children);
}
VKAPI_ATTR VkResult VKAPI_CALL traceQueueSubmit(VkQueue queue, std::uint32_t count,
    const VkSubmitInfo* info, VkFence fence) {
    auto* d = deviceFor(queue);
    if (!capture().drawsReady()) return d->QueueSubmit(queue, count, info, fence);
    std::array<SubmitTicket, CallRecordLimit> tickets{};
    std::uint32_t ticketCount = 0;
    for (std::uint32_t i = 0; i < limited(d, count); ++i) {
        const auto tracked = std::min(CallRecordLimit - ticketCount, info[i].commandBufferCount);
        for (std::uint32_t j = 0; j < tracked; ++j)
            tickets[ticketCount++] = capture().core.prepareSubmit(d->id, number(info[i].pCommandBuffers[j]));
        if (tracked != info[i].commandBufferCount) capture().core.report(d->id, Core::Capacity);
    }
    const auto result = d->QueueSubmit(queue, count, info, fence);
    if (result == VK_SUCCESS) {
        const auto serial = ++nextSubmission;
        for (std::uint32_t i = 0; i < ticketCount; ++i) capture().core.commitSubmit(tickets[i], serial, true);
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceQueueSubmit2(VkQueue queue, std::uint32_t count,
    const VkSubmitInfo2* info, VkFence fence) {
    auto* d = deviceFor(queue);
    if (!capture().drawsReady()) return d->QueueSubmit2(queue, count, info, fence);
    std::array<SubmitTicket, CallRecordLimit> tickets{};
    std::uint32_t ticketCount = 0;
    for (std::uint32_t i = 0; i < limited(d, count); ++i) {
        const auto tracked = std::min(CallRecordLimit - ticketCount, info[i].commandBufferInfoCount);
        for (std::uint32_t j = 0; j < tracked; ++j)
            tickets[ticketCount++] = capture().core.prepareSubmit(d->id, number(info[i].pCommandBufferInfos[j].commandBuffer));
        if (tracked != info[i].commandBufferInfoCount) capture().core.report(d->id, Core::Capacity);
    }
    const auto result = d->QueueSubmit2(queue, count, info, fence);
    if (result == VK_SUCCESS) {
        const auto serial = ++nextSubmission;
        for (std::uint32_t i = 0; i < ticketCount; ++i) capture().core.commitSubmit(tickets[i], serial, true);
    }
    return result;
}

TRACE_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    auto* d = deviceFor(device);
    if (!d || !name) return nullptr;
    auto native = d->gdpa(device, name);
    if (!native) return nullptr; // Do not manufacture support for extensions.
#define HOOK(name) if (!std::strcmp(nameString, "vk" #name)) return reinterpret_cast<PFN_vkVoidFunction>(trace##name);
    const char* nameString = name;
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    HOOK(DestroyDevice)
    if (capture().processAllowed) {
        HOOK(CreateShaderModule) HOOK(DestroyShaderModule) HOOK(CreateGraphicsPipelines) HOOK(DestroyPipeline)
        HOOK(AllocateCommandBuffers) HOOK(FreeCommandBuffers) HOOK(BeginCommandBuffer) HOOK(EndCommandBuffer)
        HOOK(ResetCommandBuffer) HOOK(ResetCommandPool) HOOK(DestroyCommandPool) HOOK(CmdBindPipeline)
        HOOK(CmdDraw) HOOK(CmdDrawIndexed) HOOK(CmdDrawIndirect) HOOK(CmdDrawIndexedIndirect)
        HOOK(CmdDrawIndirectCount) HOOK(CmdDrawIndexedIndirectCount)
        HOOK(CmdExecuteCommands) HOOK(QueueSubmit) HOOK(QueueSubmit2)
        HOOK(CmdBindShadersEXT)
        if (capture().bindingsEnabled) {
#define BINDING_FIELD(name) HOOK(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#define BINDING_ALIAS(alias, name) if (!std::strcmp(nameString, alias)) return reinterpret_cast<PFN_vkVoidFunction>(trace##name);
#include "passive_binding_aliases.inc"
#undef BINDING_ALIAS
        }
        if (!std::strcmp(name, "vkQueueSubmit2KHR")) return reinterpret_cast<PFN_vkVoidFunction>(traceQueueSubmit2);
        if (!std::strcmp(name, "vkCmdDrawIndirectCountKHR")) return reinterpret_cast<PFN_vkVoidFunction>(traceCmdDrawIndirectCount);
        if (!std::strcmp(name, "vkCmdDrawIndexedIndirectCountKHR")) return reinterpret_cast<PFN_vkVoidFunction>(traceCmdDrawIndexedIndirectCount);
    }
#undef HOOK
    return native;
}
TRACE_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (!std::strcmp(name, "vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (!std::strcmp(name, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(traceCreateInstance);
    auto* i = instanceFor(instance);
    if (!i) return nullptr;
    if (!std::strcmp(name, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(traceDestroyInstance);
    if (!std::strcmp(name, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(traceCreateDevice);
    // Vulkan permits device commands to be obtained through instance GPA. The
    // same dispatch wrappers must be returned or those paths bypass observation.
#define HOOK(name) if (capture().processAllowed && !std::strcmp(nameString, "vk" #name) && i->gipa(instance, nameString)) return reinterpret_cast<PFN_vkVoidFunction>(trace##name);
    const char* nameString = name;
    if (!std::strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(traceDestroyDevice);
    HOOK(CreateShaderModule) HOOK(DestroyShaderModule) HOOK(CreateGraphicsPipelines) HOOK(DestroyPipeline)
    HOOK(AllocateCommandBuffers) HOOK(FreeCommandBuffers) HOOK(BeginCommandBuffer) HOOK(EndCommandBuffer)
    HOOK(ResetCommandBuffer) HOOK(ResetCommandPool) HOOK(DestroyCommandPool) HOOK(CmdBindPipeline)
    HOOK(CmdDraw) HOOK(CmdDrawIndexed) HOOK(CmdDrawIndirect) HOOK(CmdDrawIndexedIndirect)
    HOOK(CmdDrawIndirectCount) HOOK(CmdDrawIndexedIndirectCount)
    HOOK(CmdExecuteCommands) HOOK(QueueSubmit) HOOK(QueueSubmit2)
    HOOK(CmdBindShadersEXT)
    if (capture().bindingsEnabled) {
#define BINDING_FIELD(name) HOOK(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#define BINDING_ALIAS(alias, name) if (!std::strcmp(nameString, alias) && i->gipa(instance, nameString)) return reinterpret_cast<PFN_vkVoidFunction>(trace##name);
#include "passive_binding_aliases.inc"
#undef BINDING_ALIAS
    }
#undef HOOK
    if (capture().processAllowed) {
        if (!std::strcmp(name, "vkQueueSubmit2KHR") && i->gipa(instance, name)) return reinterpret_cast<PFN_vkVoidFunction>(traceQueueSubmit2);
        if (!std::strcmp(name, "vkCmdDrawIndirectCountKHR") && i->gipa(instance, name)) return reinterpret_cast<PFN_vkVoidFunction>(traceCmdDrawIndirectCount);
        if (!std::strcmp(name, "vkCmdDrawIndexedIndirectCountKHR") && i->gipa(instance, name)) return reinterpret_cast<PFN_vkVoidFunction>(traceCmdDrawIndexedIndirectCount);
    }
    return i->gipa(instance, name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL traceGetPhysicalDeviceProcAddr(VkInstance instance, const char* name) {
    auto* i = instanceFor(instance);
    return i && i->gpdpa ? i->gpdpa(instance, name) : nullptr;
}
TRACE_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || info->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    info->loaderLayerInterfaceVersion = std::min(info->loaderLayerInterfaceVersion, 2u);
    info->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    info->pfnGetPhysicalDeviceProcAddr = traceGetPhysicalDeviceProcAddr;
    return VK_SUCCESS;
}
