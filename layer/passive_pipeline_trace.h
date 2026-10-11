#pragma once

// Metadata only. This core never reads descriptors, images, or mapped memory.
// A draw event describes recorded commands; a successful submit event links the
// command-buffer generation to a queue submission, not to GPU completion.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace frame_trace {

constexpr std::size_t ShaderCapacity = 256;
constexpr std::size_t PipelineCapacity = 256;
constexpr std::size_t CommandCapacity = 512;
constexpr std::size_t StageCapacity = 8;
constexpr std::size_t EventCapacity = 128;
constexpr std::size_t EventLimit = 2048;
constexpr std::size_t MaxShaderBytes = 1024 * 1024;
constexpr std::size_t EntryCapacity = 48;

inline std::uint64_t fingerprint(const void* data, std::size_t size) noexcept {
    // Standard FNV-1a 64. Size is emitted separately; this is a diagnostic
    // fingerprint, not a signature or a cryptographic authentication check.
    auto bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct Stage {
    std::uint32_t stage = 0;
    std::uint64_t module = 0;
    std::uint64_t generation = 0;
    std::uint64_t hash = 0;
    std::uint32_t bytes = 0;
    bool complete = false;
    char entry[EntryCapacity]{};
};

inline bool copyEntry(char (&target)[EntryCapacity], const char* source) noexcept {
    if (!source) return false;
    std::size_t i = 0;
    for (; i + 1 < EntryCapacity && source[i]; ++i) target[i] = source[i];
    target[i] = 0;
    return source[i] == 0;
}

enum class Kind : std::uint8_t {
    Shader, Pipeline, Draw, Execute, Submit, Incomplete
};

struct Event {
    Kind kind = Kind::Incomplete;
    std::uint64_t serial = 0;
    std::uint64_t device = 0;
    std::uint64_t command = 0;
    std::uint64_t commandGeneration = 0;
    std::uint64_t relatedCommand = 0;
    std::uint64_t relatedGeneration = 0;
    std::uint64_t pipeline = 0;
    std::uint64_t pipelineGeneration = 0;
    std::uint64_t layout = 0;
    std::uint64_t shader = 0;
    std::uint64_t shaderGeneration = 0;
    std::uint64_t hash = 0;
    std::uint64_t submission = 0;
    std::uint32_t bytes = 0;
    std::uint32_t drawKind = 0;
    std::uint32_t stageCount = 0;
    std::uint32_t reason = 0;
    bool complete = false;
    std::array<Stage, StageCapacity> stages{};
};

// Captured before the native queue call. A completed fence may allow another
// thread to reset/re-record the CB before that native call returns, so its
// generation must never be inferred retrospectively from current tracking state.
struct SubmitTicket {
    std::uint64_t device = 0;
    std::uint64_t command = 0;
    std::uint64_t generation = 0;
    bool eligible = false;
    bool known = false;
    bool executable = false;
    bool stateComplete = false;
};

// try_lock deliberately drops diagnostics rather than delaying a render thread.
// Producers share one bounded ring; the writer is its only consumer.
class EventRing {
    friend struct TestAccess;
public:
    // Admission covers both enqueue and Core bookkeeping. Only the background
    // writer waits for observers; rendering threads never wait for shutdown.
    class Producer {
        friend class EventRing;
        explicit Producer(EventRing* ring) noexcept : ring_(ring) {}
    public:
        Producer() noexcept = default;
        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;
        Producer(Producer&& other) noexcept : ring_(other.ring_) { other.ring_ = nullptr; }
        ~Producer() { if (ring_) ring_->producers_.fetch_sub(1, std::memory_order_release); }
        explicit operator bool() const noexcept { return ring_ != nullptr; }
    private:
        EventRing* ring_ = nullptr;
    };
    Producer admit() noexcept {
        auto state = producers_.load(std::memory_order_acquire);
        while (!(state & Closed)) {
            if (producers_.compare_exchange_weak(state, state + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) return Producer(this);
        }
        return Producer();
    }
    void close() noexcept {
        // Close admission first, then wait only on the writer thread for the
        // finite set of previously admitted observers. No later enqueue or
        // diagnostic loss counter update may race drain/footer serialization.
        producers_.fetch_or(Closed, std::memory_order_acq_rel);
        while (producers_.load(std::memory_order_acquire) != Closed) std::this_thread::yield();
    }
    bool closed() const noexcept { return (producers_.load(std::memory_order_acquire) & Closed) != 0; }
    bool push(Event event) noexcept {
        // The lifetime cap intentionally closes the observation window. A core
        // call can emit multiple records after its initial enabled() check, so
        // its trailing records must not turn normal closure into queue loss.
        if (finished()) return false;
        const auto producer = admit();
        if (!producer) return false;
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            // Conservatively retain contention observed after entering an open
            // window, even if another producer closes it during this attempt.
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        // Another producer may have accepted the last record between the first
        // check and this acquisition. Check closure before queue capacity.
        if (finished()) return false;
        if (count_ == EventCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        event.serial = ++emitted_;
        events_[write_] = event;
        write_ = (write_ + 1) % EventCapacity;
        ++count_;
        return true;
    }
    bool pop(Event& event) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !count_) return false;
        event = events_[read_];
        read_ = (read_ + 1) % EventCapacity;
        --count_;
        return true;
    }
    std::uint64_t dropped() const noexcept { return dropped_.load(); }
    std::uint64_t emitted() const noexcept { return emitted_.load(); }
    bool finished() const noexcept { return emitted() >= EventLimit; }
    bool empty() noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        return lock.owns_lock() && count_ == 0;
    }
private:
    static constexpr std::uint32_t Closed = 1u << 31;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "Diagnostic admission must not use a hidden producer lock");
    std::atomic<std::uint32_t> producers_{0};
    std::array<Event, EventCapacity> events_{};
    std::mutex mutex_;
    std::size_t read_ = 0, write_ = 0, count_ = 0;
    std::atomic<std::uint64_t> emitted_{0}, dropped_{0};
};

class Core {
    friend struct TestAccess;
    struct Shader {
        bool used = false, complete = false;
        std::uint64_t device = 0, handle = 0, generation = 0, hash = 0;
        std::uint32_t bytes = 0;
    };
    struct Pipeline {
        bool used = false, complete = false;
        std::uint64_t device = 0, handle = 0, generation = 0, layout = 0;
        std::uint32_t count = 0;
        std::array<Stage, StageCapacity> stages{};
    };
    struct Command {
        bool used = false, recording = false, executable = false;
        bool matchingDraw = false, submitReported = false;
        std::uint64_t device = 0, handle = 0, pool = 0, generation = 0;
        std::uint64_t pipeline = 0, pipelineGeneration = 0;
        std::array<std::uint64_t, 8> recordedPipelines{};
    };
public:
    enum Reason : std::uint32_t {
        Capacity = 1, UnknownShader = 2, UnknownPipeline = 3,
        UnknownCommand = 4, Contention = 5, StageLimit = 6,
        PartialCreation = 7, UnsupportedCommand = 8,
        UnknownSecondary = 9, OversizeShader = 10, ChangedGeneration = 11
    };
    explicit Core(EventRing& events, bool enabled = false) noexcept
        : events_(events), enabled_(enabled) {}
    void enable(bool enabled) noexcept { enabled_.store(enabled); }
    void filterFragment(std::uint64_t hash) noexcept { fragmentHash_ = hash; }
    bool enabled() const noexcept { return enabled_.load() && !events_.finished(); }
    std::uint64_t lostState() const noexcept { return stateDrops_.load(); }

    void shaderCreated(std::uint64_t device, std::uint64_t handle,
                       const void* code, std::size_t size) noexcept {
        if (!enabled() || !handle) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        auto* shader = find(shaders_, device, handle);
        if (!shader) shader = free(shaders_);
        if (!shader) { incomplete(device, Capacity); return; }
        *shader = Shader{};
        shader->used = true;
        shader->device = device;
        shader->handle = handle;
        shader->generation = ++generation_;
        shader->complete = code && size && size <= MaxShaderBytes;
        shader->bytes = size <= UINT32_MAX ? static_cast<std::uint32_t>(size) : 0;
        if (shader->complete) shader->hash = fingerprint(code, size);
        Event event;
        event.kind = Kind::Shader;
        event.device = device;
        event.shader = handle;
        event.shaderGeneration = shader->generation;
        event.hash = shader->hash;
        event.bytes = shader->bytes;
        event.complete = shader->complete;
        events_.push(event);
    }
    void shaderDestroyed(std::uint64_t device, std::uint64_t handle) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        if (auto* record = find(shaders_, device, handle)) record->used = false;
    }
    void pipelineCreated(std::uint64_t device, std::uint64_t handle,
                         std::uint64_t layout, const Stage* input,
                         std::uint32_t count, bool createSucceeded,
                         bool ordinaryPipeline = true) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        if (!createSucceeded) incomplete(device, PartialCreation);
        // Vulkan may return valid non-null handles even for an aggregate error.
        if (!handle) return;
        auto* pipeline = find(pipelines_, device, handle);
        if (!pipeline) pipeline = free(pipelines_);
        if (!pipeline) { incomplete(device, Capacity); return; }
        *pipeline = Pipeline{};
        pipeline->used = true;
        pipeline->device = device;
        pipeline->handle = handle;
        pipeline->generation = ++generation_;
        pipeline->layout = layout;
        pipeline->count = count <= StageCapacity ? count : StageCapacity;
        pipeline->complete = ordinaryPipeline && input && count > 0 && count <= StageCapacity;
        for (std::uint32_t i = 0; input && i < pipeline->count; ++i) {
            auto& stage = pipeline->stages[i];
            stage = input[i];
            auto* shader = find(shaders_, device, stage.module);
            if (shader) {
                stage.generation = shader->generation;
                stage.hash = shader->hash;
                stage.bytes = shader->bytes;
                stage.complete = stage.complete && shader->complete;
            } else {
                stage.complete = false;
            }
            pipeline->complete = pipeline->complete && stage.complete;
        }
        Event event;
        event.kind = Kind::Pipeline;
        snapshot(event, *pipeline);
        events_.push(event);
    }
    void pipelineDestroyed(std::uint64_t device, std::uint64_t handle) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        if (auto* record = find(pipelines_, device, handle)) record->used = false;
    }
    void commandAllocated(std::uint64_t device, std::uint64_t handle,
                          std::uint64_t pool) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        auto* command = find(commands_, device, handle);
        if (!command) command = free(commands_);
        if (!command) { incomplete(device, Capacity); return; }
        *command = Command{};
        command->used = true;
        command->device = device;
        command->handle = handle;
        command->pool = pool;
        command->generation = ++generation_;
    }
    void commandBegin(std::uint64_t device, std::uint64_t handle) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        auto* command = find(commands_, device, handle);
        if (!command) { incomplete(device, UnknownCommand); return; }
        command->generation = ++generation_;
        command->pipeline = command->pipelineGeneration = 0;
        command->matchingDraw = command->submitReported = false;
        command->recordedPipelines.fill(0);
        command->recording = true;
        command->executable = false;
    }
    void commandEnd(std::uint64_t device, std::uint64_t handle) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        if (auto* command = find(commands_, device, handle)) {
            command->executable = command->recording;
            command->recording = false;
        }
    }
    void commandReset(std::uint64_t device, std::uint64_t handle, bool removed) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        if (auto* command = find(commands_, device, handle)) invalidate(*command, removed);
    }
    void poolReset(std::uint64_t device, std::uint64_t pool, bool removed) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        for (auto& command : commands_)
            if (command.used && command.device == device && command.pool == pool)
                invalidate(command, removed);
    }
    void deviceDestroyed(std::uint64_t device) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        for (auto& value : commands_) if (value.device == device) value.used = false;
        for (auto& value : pipelines_) if (value.device == device) value.used = false;
        for (auto& value : shaders_) if (value.device == device) value.used = false;
    }
    void bind(std::uint64_t device, std::uint64_t handle, std::uint64_t pipeline) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        auto* command = find(commands_, device, handle);
        if (!command || !command->recording) { incomplete(device, UnknownCommand); return; }
        auto* record = find(pipelines_, device, pipeline);
        command->pipeline = pipeline;
        command->pipelineGeneration = record ? record->generation : 0;
    }
    void draw(std::uint64_t device, std::uint64_t handle, std::uint32_t drawKind) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        Event event;
        event.kind = Kind::Draw;
        event.device = device;
        event.command = handle;
        event.drawKind = drawKind;
        auto* command = find(commands_, device, handle);
        if (!command || !command->recording) {
            event.reason = UnknownCommand;
        } else {
            event.commandGeneration = command->generation;
            auto* record = find(pipelines_, device, command->pipeline);
            if (record && record->generation == command->pipelineGeneration) {
                if (fragmentHash_) {
                    bool matches = false;
                    for (std::uint32_t i = 0; i < record->count; ++i)
                        if (record->stages[i].stage == 0x10 && record->stages[i].hash == fragmentHash_) matches = true;
                    if (!matches) return;
                }
                command->matchingDraw = true;
                for (auto generation : command->recordedPipelines) if (generation == record->generation) return;
                bool remembered = false;
                for (auto& generation : command->recordedPipelines) if (!generation) {
                    generation = record->generation;
                    remembered = true;
                    break;
                }
                if (!remembered) { incomplete(device, Capacity); return; }
                snapshot(event, *record);
            } else {
                if (fragmentHash_) return;
                event.reason = UnknownPipeline;
            }
        }
        events_.push(event);
    }
    void execute(std::uint64_t device, std::uint64_t parent, std::uint64_t child) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        Event event;
        event.kind = Kind::Execute;
        event.device = device;
        event.command = parent;
        event.relatedCommand = child;
        auto* a = find(commands_, device, parent);
        auto* b = find(commands_, device, child);
        event.complete = a && a->recording && b && b->executable && !b->recording;
        if (a && b && b->matchingDraw) a->matchingDraw = true;
        if (fragmentHash_ && event.complete && !b->matchingDraw) return;
        event.complete = event.complete && !tainted_ && stateDrops_.load() == 0;
        event.commandGeneration = a ? a->generation : 0;
        event.relatedGeneration = b ? b->generation : 0;
        if (!event.complete) event.reason = UnknownSecondary;
        events_.push(event);
    }
    SubmitTicket prepareSubmit(std::uint64_t device, std::uint64_t commandHandle) noexcept {
        SubmitTicket ticket;
        ticket.device = device;
        ticket.command = commandHandle;
        if (!enabled()) return ticket;
        ticket.eligible = true;
        auto lock = acquire();
        if (!lock.owns_lock()) return ticket;
        auto* command = find(commands_, device, commandHandle);
        if (!command) return ticket;
        ticket.known = true;
        ticket.generation = command->generation;
        ticket.executable = command->executable && !command->recording;
        ticket.stateComplete = !tainted_ && stateDrops_.load() == 0;
        ticket.eligible = !command->submitReported && (!fragmentHash_ || command->matchingDraw);
        return ticket;
    }
    void commitSubmit(const SubmitTicket& ticket, std::uint64_t submission, bool accepted) noexcept {
        if (!accepted || !ticket.eligible || !enabled()) return;
        auto lock = acquire();
        if (!lock.owns_lock()) return;
        Event event;
        event.kind = Kind::Submit;
        event.device = ticket.device;
        event.command = ticket.command;
        event.commandGeneration = ticket.generation;
        event.submission = submission;
        auto* command = find(commands_, ticket.device, ticket.command);
        const bool sameGeneration = command && ticket.known && command->generation == ticket.generation;
        if (sameGeneration && command->submitReported) return;
        if (sameGeneration) command->submitReported = true;
        event.complete = sameGeneration && ticket.executable && ticket.stateComplete &&
            command->executable && !command->recording && !tainted_ && stateDrops_.load() == 0;
        if (!event.complete) event.reason = !sameGeneration && ticket.known ? ChangedGeneration : UnknownCommand;
        events_.push(event);
    }
    void report(std::uint64_t device, Reason reason) noexcept {
        if (!enabled()) return;
        auto lock = acquire();
        if (lock.owns_lock()) incomplete(device, reason);
    }
private:
    class ObservationLock {
    public:
        explicit ObservationLock(EventRing::Producer producer) noexcept
            : producer_(std::move(producer)) {}
        ObservationLock(EventRing::Producer producer, std::mutex& mutex) noexcept
            : producer_(std::move(producer)), lock_(mutex, std::try_to_lock) {}
        bool owns_lock() const noexcept { return lock_.owns_lock(); }
    private:
        // The bookkeeping lock is released before the admission scope, so
        // writer quiescence includes all updates made by this observation.
        EventRing::Producer producer_;
        std::unique_lock<std::mutex> lock_;
    };
    ObservationLock acquire() noexcept {
        auto producer = events_.admit();
        if (!producer || !enabled_.load()) return ObservationLock(std::move(producer));
        ObservationLock lock(std::move(producer), mutex_);
        if (!lock.owns_lock()) {
            stateDrops_.fetch_add(1, std::memory_order_release);
        } else {
            const auto drops = stateDrops_.load(std::memory_order_acquire);
            if (drops != observedDrops_) {
                // A missed destruction/reset/bind must never yield a false
                // exact-stage claim. Clear stale state and mark the trace gap.
                for (auto& value : shaders_) value.used = false;
                for (auto& value : pipelines_) value.used = false;
                for (auto& value : commands_) value.used = false;
                observedDrops_ = drops;
                incomplete(0, Contention);
            }
        }
        return lock;
    }
    template <typename T, std::size_t N>
    static T* find(std::array<T, N>& values, std::uint64_t device, std::uint64_t handle) noexcept {
        for (auto& value : values)
            if (value.used && value.device == device && value.handle == handle) return &value;
        return nullptr;
    }
    template <typename T, std::size_t N>
    static T* free(std::array<T, N>& values) noexcept {
        for (auto& value : values) if (!value.used) return &value;
        return nullptr;
    }
    void invalidate(Command& command, bool removed) noexcept {
        command.generation = ++generation_;
        command.pipeline = command.pipelineGeneration = 0;
        command.matchingDraw = command.submitReported = false;
        command.recordedPipelines.fill(0);
        command.recording = false;
        command.executable = false;
        if (removed) command.used = false;
    }
    void incomplete(std::uint64_t device, Reason reason) noexcept {
        if (reason == Contention || reason == Capacity || reason == UnsupportedCommand) tainted_ = true;
        Event event;
        event.device = device;
        event.reason = reason;
        events_.push(event);
    }
    void snapshot(Event& event, const Pipeline& pipeline) noexcept {
        event.device = pipeline.device;
        event.pipeline = pipeline.handle;
        event.pipelineGeneration = pipeline.generation;
        event.layout = pipeline.layout;
        event.complete = pipeline.complete && !tainted_ && stateDrops_.load() == 0;
        event.stageCount = pipeline.count;
        event.stages = pipeline.stages;
    }
    EventRing& events_;
    std::atomic<bool> enabled_{false};
    std::mutex mutex_;
    std::array<Shader, ShaderCapacity> shaders_{};
    std::array<Pipeline, PipelineCapacity> pipelines_{};
    std::array<Command, CommandCapacity> commands_{};
    std::uint64_t generation_ = 0, observedDrops_ = 0;
    std::uint64_t fragmentHash_ = 0;
    bool tainted_ = false;
    std::atomic<std::uint64_t> stateDrops_{0};
};

} // namespace frame_trace
