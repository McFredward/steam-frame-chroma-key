#pragma once

// Live public Vulkan API state only. The caller forwards every supported
// resource mutation synchronously, never via the passive journal replay.
// A candidate describes one valid native draw's sampled resource at the end
// of its primary rendering scope. It is not a cross-process/decoder lease,
// source-video association, GPU completion, or permission to issue GPU work.
// Reusable (flags0) recordings require exact successful native retirement as
// well as owned GPU completion before CPU readback: the embedded copy may run
// again until retirement. The owner never changes native recording flags.
#include "passive_pipeline_trace.h"

#include <utility>

namespace frame_pixel_capture {

constexpr std::uint64_t DistortionFragmentHash = UINT64_C(0xf333a137b4e79ced);
constexpr std::size_t PoolCapacity = 128;
constexpr std::size_t CommandCapacity = 512;
constexpr std::size_t ShaderCapacity = 1024;
constexpr std::size_t PipelineCapacity = 256;
constexpr std::size_t SubmitCapacity = 64;

struct Candidate {
    std::uint64_t device = 0;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::uint64_t commandGeneration = 0, serial = 0;
    std::uint64_t poolGeneration = 0;
    std::uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
    VkCommandBufferUsageFlags beginFlags = 0;
    bool oneTimeSubmit = false, noSimultaneousUse = false, retirementRequired = true;
    frame_bindings::Binding binding{};
};
static_assert(std::is_trivially_copyable_v<Candidate>, "Pixel candidates must own bounded metadata");

struct SubmitTicket {
    std::uint64_t device = 0;
    VkCommandBuffer command = VK_NULL_HANDLE;
    std::uint64_t commandGeneration = 0, serial = 0, admissionGeneration = 0;
    std::uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
    bool eligible = false;
};

// Saved BEFORE successful native reset/begin/free/pool retirement. The owner
// must match the original candidate generation and report retirement only
// AFTER native success; querying the replacement CB afterwards loses identity.
struct RetirementTicket {
    std::uint64_t device = 0;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::uint64_t commandGeneration = 0, poolGeneration = 0;
    bool eligible = false;
};

enum class Failure : std::uint8_t { None, Contention, Capacity, CoreLoss, Unknown, GenerationWrap };
constexpr std::size_t CoreReasonCount = static_cast<std::size_t>(frame_bindings::Reason::LayoutMismatch) + 1;
constexpr std::size_t CoreTableCount = static_cast<std::size_t>(frame_bindings::Table::Count);
static_assert(CoreReasonCount == 8, "Audit telemetry when the Core reason enumeration changes");

// The first refusal is published exactly once. Counters are atomic observations
// taken by its winner: a contention refusal can overlap the lock owner's final
// Core mutation, so these counts are not claimed to be a transactional Core dump.
struct FailureSnapshot {
    bool complete = false;
    Failure failure = Failure::None;
    const char* site = nullptr;
    std::uint64_t coreLostState = 0;
    std::array<std::uint64_t, CoreReasonCount> coreReasonCounts{};
    std::array<std::uint64_t, CoreTableCount> coreCapacityCounts{};
};

class LiveTracker {
    friend struct TestAccess;
    using Identity = frame_bindings::Identity;
    struct Base { std::uint64_t device = 0; Identity id{}; };
    struct Pool : Base { std::uint32_t family = VK_QUEUE_FAMILY_IGNORED; bool supported = false; };
    struct Shader : Base { std::uint64_t hash = 0; bool complete = false; };
    struct Pipeline : Base { VkPipelineLayout layout = VK_NULL_HANDLE; bool supported = false; };
    struct PendingSubmit : Base { SubmitTicket ticket{}; };
    enum class Scope : std::uint8_t { None, RenderPass, Dynamic };
    struct Command : Base {
        Identity pool{}, pipeline{}, selectedPipeline{};
        std::uint32_t family = VK_QUEUE_FAMILY_IGNORED;
        VkCommandBufferUsageFlags beginFlags = 0;
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        Scope scope = Scope::None;
        bool supported = false, recording = false, executable = false;
        bool selected = false, issued = false, submitted = false, prepared = false;
        Candidate candidate{};
    };

public:
    bool poisoned() const noexcept { return failure_.load(std::memory_order_acquire) != Failure::None; }
    Failure failure() const noexcept { return failure_.load(std::memory_order_acquire); }

    FailureSnapshot failureSnapshot() const noexcept {
        FailureSnapshot out;
        // Never read partially published fields, or wait on the render thread.
        // A cold observer may retry if poison won but publication is unfinished.
        if (!failurePublished_.load(std::memory_order_acquire)) {
            out.failure = failure();
            return out;
        }
        out.complete = true;
        out.failure = failure();
        out.site = firstSite_.load(std::memory_order_relaxed);
        out.coreLostState = firstCoreLost_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < out.coreReasonCounts.size(); ++i)
            out.coreReasonCounts[i] = firstCoreReasons_[i].load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < out.coreCapacityCounts.size(); ++i)
            out.coreCapacityCounts[i] = firstCoreCapacities_[i].load(std::memory_order_relaxed);
        return out;
    }

    // This is the sole external access to the live Core. Forward updates,
    // copies, creations and retirements under this owner, after native success
    // (or before native void destruction). Do not expose Core to another owner.
    // A non-default site must have static storage duration (for example __func__
    // or a string literal). It is retained without allocation or string copying.
    template<class F> bool observe(F&& mutation, const char* site = "observe") noexcept {
        return locked(site, [&] {
            mutation(bindings_);
            return coreHealthy();
        });
    }

    bool createShader(std::uint64_t device, VkShaderModule value,
                      const VkShaderModuleCreateInfo& info) noexcept {
        return locked("createShader", [&] {
            auto* shader = insert(shaders_, device, frame_bindings::handle(value));
            if (!shader) return false;
            shader->complete = info.sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO && !info.pNext &&
                info.flags == 0 && info.pCode && info.codeSize >= 5 * sizeof(std::uint32_t) &&
                info.codeSize <= frame_trace::MaxShaderBytes && info.codeSize % sizeof(std::uint32_t) == 0 &&
                info.pCode[0] == UINT32_C(0x07230203);
            if (shader->complete) shader->hash = frame_trace::fingerprint(info.pCode, info.codeSize);
            return true;
        });
    }
    bool destroyShader(std::uint64_t device, VkShaderModule value) noexcept {
        return locked("destroyShader", [&] { return erase(shaders_, device, frame_bindings::handle(value)); });
    }
    bool createPipeline(std::uint64_t device, VkPipeline value,
                        const VkGraphicsPipelineCreateInfo& info) noexcept {
        return locked("createPipeline", [&] {
            auto* pipeline = insert(pipelines_, device, frame_bindings::handle(value));
            if (!pipeline) return false;
            pipeline->layout = info.layout;
            pipeline->supported = ordinaryPipeline(info);
            bool vertex = false, fragment = false;
            if (pipeline->supported) for (std::uint32_t i = 0; i < info.stageCount; ++i) {
                const auto& stage = info.pStages[i];
                const auto* shader = shaders_.find(device, frame_bindings::handle(stage.module));
                if (stage.sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO || stage.pNext ||
                    stage.flags || stage.pSpecializationInfo || !shader || !shader->complete) {
                    pipeline->supported = false; break;
                }
                if (stage.stage == VK_SHADER_STAGE_VERTEX_BIT && !vertex) vertex = true;
                else if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && !fragment &&
                         shader->hash == DistortionFragmentHash && exactEntry(stage.pName, "PS_DISTORT")) fragment = true;
                else { pipeline->supported = false; break; }
            }
            pipeline->supported = pipeline->supported && vertex && fragment;
            return true;
        });
    }
    bool destroyPipeline(std::uint64_t device, VkPipeline value) noexcept {
        return locked("destroyPipeline", [&] { return erase(pipelines_, device, frame_bindings::handle(value)); });
    }
    bool bindPipeline(std::uint64_t device, VkCommandBuffer value, VkPipelineBindPoint point,
                      VkPipeline pipeline) noexcept {
        if (point != VK_PIPELINE_BIND_POINT_GRAPHICS) return !poisoned();
        return locked("bindPipeline", [&] {
            auto* command = recording(device, value); if (!command) return false;
            const auto* current = pipelines_.find(device, frame_bindings::handle(pipeline));
            command->pipeline = current ? current->id : Identity{};
            return true; // An ordinary non-distortion pipeline is not a loss.
        });
    }

    bool createCommandPool(std::uint64_t device, VkCommandPool value,
                           const VkCommandPoolCreateInfo& info) noexcept {
        return locked("createCommandPool", [&] {
            auto* pool = insert(pools_, device, frame_bindings::handle(value)); if (!pool) return false;
            pool->family = info.queueFamilyIndex;
            constexpr auto allowed = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool->supported = info.sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO && !info.pNext &&
                !(info.flags & ~allowed) && info.queueFamilyIndex < VK_QUEUE_FAMILY_EXTERNAL;
            return true;
        });
    }
    bool destroyCommandPool(std::uint64_t device, VkCommandPool value) noexcept {
        return locked("destroyCommandPool", [&] {
            const auto key = frame_bindings::handle(value);
            const auto* pool = pools_.find(device, key); if (!pool) return unknown();
            const auto id = pool->id;
            commands_.eraseIf([&](const Command& c) { return c.device == device && frame_bindings::same(c.pool, id); });
            bindings_.destroyCommandPool(device, value);
            return erase(pools_, device, key) && coreHealthy();
        });
    }
    bool resetCommandPool(std::uint64_t device, VkCommandPool value) noexcept {
        return locked("resetCommandPool", [&] {
            const auto* pool = pools_.find(device, frame_bindings::handle(value)); if (!pool) return unknown();
            for (auto& command : commands_) if (command.device == device && frame_bindings::same(command.pool, pool->id))
                clearRecording(command);
            bindings_.resetCommandPool(device, value);
            return coreHealthy();
        });
    }
    bool allocateCommands(std::uint64_t device, const VkCommandBufferAllocateInfo& info,
                          const VkCommandBuffer* values) noexcept {
        return locked("allocateCommands", [&] {
            if (info.sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO || info.pNext ||
                info.commandBufferCount > CommandCapacity || (info.commandBufferCount && !values)) return unknown();
            const auto* pool = pools_.find(device, frame_bindings::handle(info.commandPool));
            if (!pool) return unknown();
            for (std::uint32_t i = 0; i < info.commandBufferCount; ++i) {
                auto* command = insert(commands_, device, frame_bindings::handle(values[i])); if (!command) return false;
                command->pool = pool->id; command->family = pool->family; command->level = info.level;
                command->supported = pool->supported && info.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            }
            bindings_.allocateCommands(device, info.commandPool, info.commandBufferCount, values);
            return coreHealthy();
        });
    }
    bool freeCommands(std::uint64_t device, VkCommandPool pool, std::uint32_t count,
                      const VkCommandBuffer* values) noexcept {
        return locked("freeCommands", [&] {
            if (count > CommandCapacity || (count && !values)) return unknown();
            const auto* knownPool = pools_.find(device, frame_bindings::handle(pool)); if (!knownPool) return unknown();
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto* command = commands_.find(device, frame_bindings::handle(values[i]));
                if (!command || !frame_bindings::same(command->pool, knownPool->id)) return unknown();
                if (!erase(commands_, device, frame_bindings::handle(values[i]))) return false;
            }
            bindings_.freeCommands(device, pool, count, values);
            return coreHealthy();
        });
    }
    bool beginCommand(std::uint64_t device, VkCommandBuffer value,
                      const VkCommandBufferBeginInfo& info, bool nativeSuccess) noexcept {
        return locked("beginCommand", [&] {
            auto* command = commands_.find(device, frame_bindings::handle(value)); if (!command) return unknown();
            clearRecording(*command);
            if (!nativeSuccess) return false;
            const auto* pool = pools_.find(device, command->pool.handle);
            command->supported = pool && frame_bindings::same(pool->id, command->pool) && pool->supported &&
                command->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
                info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO && !info.pNext && !info.pInheritanceInfo &&
                (info.flags == 0 || info.flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            command->beginFlags = info.flags;
            command->recording = true;
            bindings_.beginCommand(device, value);
            return coreHealthy();
        });
    }
    bool endCommand(std::uint64_t device, VkCommandBuffer value, bool nativeSuccess) noexcept {
        return locked("endCommand", [&] {
            auto* command = commands_.find(device, frame_bindings::handle(value)); if (!command) return unknown();
            if (!command->recording) return invalidate(*command);
            command->executable = nativeSuccess && command->supported && command->scope == Scope::None;
            command->recording = false;
            if (!nativeSuccess) { command->supported = false; command->issued = false; }
            bindings_.endCommand(device, value);
            return coreHealthy();
        });
    }
    bool resetCommand(std::uint64_t device, VkCommandBuffer value) noexcept {
        return locked("resetCommand", [&] {
            auto* command = commands_.find(device, frame_bindings::handle(value)); if (!command) return unknown();
            clearRecording(*command); bindings_.resetCommand(device, value); return coreHealthy();
        });
    }
    bool unknownCommand(std::uint64_t device, VkCommandBuffer value) noexcept {
        return locked("unknownCommand", [&] {
            auto* command = commands_.find(device, frame_bindings::handle(value)); if (!command) return unknown();
            return invalidate(*command);
        });
    }

    bool beginRenderPass(std::uint64_t device, VkCommandBuffer value, VkSubpassContents contents) noexcept {
        return beginScope(device, value, Scope::RenderPass, contents == VK_SUBPASS_CONTENTS_INLINE, "beginRenderPass");
    }
    bool nextSubpass(std::uint64_t device, VkCommandBuffer value, VkSubpassContents contents) noexcept {
        return locked("nextSubpass", [&] {
            auto* command = recording(device, value); if (!command) return false;
            if (command->scope != Scope::RenderPass || contents != VK_SUBPASS_CONTENTS_INLINE)
                return invalidate(*command);
            return true;
        });
    }
    // Call only AFTER the corresponding original native end operation.
    bool endRenderPass(std::uint64_t device, VkCommandBuffer value, Candidate& out) noexcept {
        return endScope(device, value, Scope::RenderPass, out, "endRenderPass");
    }
    bool beginRendering(std::uint64_t device, VkCommandBuffer value, const VkRenderingInfo& info) noexcept {
        // Suspended/resumed rendering can cross command buffers/submits; that
        // is explicitly outside this single-primary end-scope acquisition.
        return beginScope(device, value, Scope::Dynamic, info.sType == VK_STRUCTURE_TYPE_RENDERING_INFO &&
            !info.pNext && info.flags == 0 && info.viewMask == 0 && info.layerCount == 1, "beginRendering");
    }
    bool endRendering(std::uint64_t device, VkCommandBuffer value, Candidate& out) noexcept {
        return endScope(device, value, Scope::Dynamic, out, "endRendering");
    }

    bool draw(std::uint64_t device, VkCommandBuffer value) noexcept {
        return locked("draw", [&] {
            auto* command = recording(device, value); if (!command) return false;
            if (!command->supported || command->scope == Scope::None || command->selected || command->issued) return false;
            const auto* pipeline = pipelines_.find(device, command->pipeline.handle);
            if (!pipeline || !frame_bindings::same(pipeline->id, command->pipeline) || !pipeline->supported) return false;
            const auto state = bindings_.snapshot(device, value, pipeline->layout);
            if (!coreHealthy() || !state.sourceComplete) return false;
            const frame_bindings::Binding* source = nullptr;
            for (const auto& binding : state.bindings) if (binding.binding == 9) { source = &binding; break; }
            if (!source || !eligible(*source)) return false;
            command->candidate = {};
            command->candidate.device = device; command->candidate.command = value;
            command->candidate.pool = native<VkCommandPool>(command->pool.handle);
            command->candidate.poolGeneration = command->pool.generation;
            command->candidate.commandGeneration = command->id.generation;
            command->candidate.beginFlags = command->beginFlags;
            command->candidate.oneTimeSubmit = (command->beginFlags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0;
            command->candidate.noSimultaneousUse = (command->beginFlags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0;
            command->candidate.retirementRequired = !command->candidate.oneTimeSubmit;
            command->candidate.queueFamily = command->family; command->candidate.binding = *source;
            command->selectedPipeline = pipeline->id; command->selected = true;
            return true;
        });
    }

    SubmitTicket prepareSubmit(std::uint64_t device, VkCommandBuffer value) noexcept {
        SubmitTicket out;
        const bool ready = locked("prepareSubmit", [&] {
            auto* command = commands_.find(device, frame_bindings::handle(value));
            if (!command || !command->supported || !command->executable || command->recording ||
                command->scope != Scope::None || !command->issued || command->submitted || command->prepared) return false;
            auto* pending = insert(pendingSubmits_, device, command->candidate.serial);
            if (!pending) return false;
            out.device = device; out.command = value; out.commandGeneration = command->id.generation;
            out.queueFamily = command->family; out.serial = command->candidate.serial;
            out.admissionGeneration = pending->id.generation; out.eligible = true;
            pending->ticket = out; command->prepared = true;
            return true;
        });
        return ready ? out : SubmitTicket{};
    }
    // Captured before native QueueSubmit; native success associates this exact
    // recording, never the newest recording after the native call returned.
    bool acceptSubmit(const SubmitTicket& ticket, bool nativeSuccess) noexcept {
        return locked("acceptSubmit", [&] {
            if (!ticket.eligible || !ticket.serial || !ticket.admissionGeneration) return false;
            auto* pending = pendingSubmits_.find(ticket.device, ticket.serial);
            if (!pending || pending->id.generation != ticket.admissionGeneration ||
                !sameTicket(pending->ticket, ticket)) return false;
            pendingSubmits_.eraseEntry(*pending);
            // Completion of the original work can let another thread legally
            // reset/free/re-record the CB before QueueSubmit returns. Preserve
            // the admitted pre-call identity, not that later current state.
            auto* command = commands_.find(ticket.device, frame_bindings::handle(ticket.command));
            if (command && command->id.generation == ticket.commandGeneration && command->candidate.serial == ticket.serial) {
                command->prepared = false; command->submitted = nativeSuccess;
            }
            return nativeSuccess;
        });
    }

    RetirementTicket prepareRetirement(std::uint64_t device, VkCommandBuffer value) noexcept {
        RetirementTicket out;
        const bool ready = locked("prepareRetirement", [&] {
            const auto* command = commands_.find(device, frame_bindings::handle(value));
            if (!command) return false;
            const auto* pool = pools_.find(device, command->pool.handle);
            if (!pool || !frame_bindings::same(pool->id, command->pool)) return false;
            out.device = device; out.command = value; out.commandGeneration = command->id.generation;
            out.pool = native<VkCommandPool>(pool->id.handle); out.poolGeneration = pool->id.generation;
            out.eligible = true;
            return true;
        });
        return ready ? out : RetirementTicket{};
    }
    bool ownsCommandPool(std::uint64_t device, VkCommandBuffer value, VkCommandPool poolValue) noexcept {
        return locked("ownsCommandPool", [&] {
            const auto* command = commands_.find(device, frame_bindings::handle(value));
            const auto* pool = pools_.find(device, frame_bindings::handle(poolValue));
            return command && pool && frame_bindings::same(command->pool, pool->id);
        });
    }

private:
    template<class F> bool locked(const char* site, F&& action) noexcept {
        const auto* origin = site ? site : "unspecified";
        if (poisoned()) return false;
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) { poison(Failure::Contention, origin); return false; }
        if (poisoned()) return false;
        activeSite_.store(origin, std::memory_order_relaxed);
        if (!coreHealthy()) return false;
        const bool result = action();
        return !poisoned() && coreHealthy() && result;
    }
    void poison(Failure reason, const char* site = nullptr) noexcept {
        auto expected = Failure::None;
        if (!failure_.compare_exchange_strong(expected, reason, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) return;
        // Only the winning refusal writes these bounded fields. A contender
        // supplies its own site instead of borrowing the current lock owner's.
        firstSite_.store(site ? site : activeSite_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        firstCoreLost_.store(bindings_.lostState(), std::memory_order_relaxed);
        for (std::size_t i = 0; i < firstCoreReasons_.size(); ++i)
            firstCoreReasons_[i].store(bindings_.lossCount(static_cast<frame_bindings::Reason>(i)),
                                       std::memory_order_relaxed);
        for (std::size_t i = 0; i < firstCoreCapacities_.size(); ++i)
            firstCoreCapacities_[i].store(bindings_.capacityLossCount(static_cast<frame_bindings::Table>(i)),
                                          std::memory_order_relaxed);
        failurePublished_.store(true, std::memory_order_release);
    }
    bool unknown() noexcept { poison(Failure::Unknown); return false; }
    bool coreHealthy() noexcept {
        if (bindings_.lostState()) { poison(Failure::CoreLoss); return false; }
        return true;
    }
    std::uint64_t next() noexcept {
        if (serial_ == UINT64_MAX) { poison(Failure::GenerationWrap); return 0; }
        return ++serial_;
    }
    template<class T, std::size_t N> T* insert(frame_bindings::ObjectTable<T, N>& table,
                                             std::uint64_t device, std::uint64_t value) noexcept {
        if (!device || !value || table.find(device, value)) { unknown(); return nullptr; }
        if (!table.hasRoom()) { poison(Failure::Capacity); return nullptr; }
        const auto generation = next(); if (!generation) return nullptr;
        auto* entry = table.insert(device, value, generation);
        if (!entry) unknown();
        return entry;
    }
    template<class T, std::size_t N> bool erase(frame_bindings::ObjectTable<T, N>& table,
                                               std::uint64_t device, std::uint64_t value) noexcept {
        return table.erase(device, value) || (value == 0) || unknown();
    }
    Command* recording(std::uint64_t device, VkCommandBuffer value) noexcept {
        auto* command = commands_.find(device, frame_bindings::handle(value));
        if (!command) { unknown(); return nullptr; }
        if (!command->recording) { invalidate(*command); return nullptr; }
        return command;
    }
    bool invalidate(Command& command) noexcept {
        command.supported = false; command.executable = false;
        command.selected = false; command.issued = false;
        return false;
    }
    void clearRecording(Command& command) noexcept {
        const auto device = command.device; const auto handle = command.id.handle;
        const auto pool = command.pool; const auto family = command.family; const auto level = command.level;
        command = {}; command.device = device; command.id = {handle, next()};
        command.pool = pool; command.family = family; command.level = level;
    }
    bool beginScope(std::uint64_t device, VkCommandBuffer value, Scope scope, bool supported, const char* site) noexcept {
        return locked(site, [&] {
            auto* command = recording(device, value); if (!command) return false;
            if (!command->supported || !supported || command->scope != Scope::None) return invalidate(*command);
            command->scope = scope; command->selected = false;
            return true;
        });
    }
    bool endScope(std::uint64_t device, VkCommandBuffer value, Scope scope, Candidate& out, const char* site) noexcept {
        out = {};
        return locked(site, [&] {
            auto* command = recording(device, value); if (!command) return false;
            if (command->scope != scope || !command->supported) return invalidate(*command);
            command->scope = Scope::None;
            if (!command->selected || command->issued) return false;
            command->selected = false;
            const auto* pipeline = pipelines_.find(device, command->selectedPipeline.handle);
            if (!pipeline || !frame_bindings::same(pipeline->id, command->selectedPipeline) ||
                !bindings_.validateBindingResources(command->candidate.binding) || !coreHealthy()) return false;
            const auto serial = next(); if (!serial) return false;
            command->candidate.serial = serial; command->issued = true;
            out = command->candidate;
            return true;
        });
    }
    static bool exactEntry(const char* entry, const char* expected) noexcept {
        if (!entry) return false;
        for (std::size_t i = 0; i < frame_trace::EntryCapacity; ++i) {
            if (entry[i] != expected[i]) return false;
            if (!expected[i]) return true;
        }
        return false;
    }
    static bool sameTicket(const SubmitTicket& a, const SubmitTicket& b) noexcept {
        return a.device == b.device && a.command == b.command && a.commandGeneration == b.commandGeneration &&
            a.queueFamily == b.queueFamily && a.serial == b.serial &&
            a.admissionGeneration == b.admissionGeneration && a.eligible && b.eligible;
    }
    template<class T> static T native(std::uint64_t value) noexcept {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
        else return static_cast<T>(value);
    }
    static bool ordinaryPipeline(const VkGraphicsPipelineCreateInfo& info) noexcept {
        if (info.sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO || info.flags ||
            info.stageCount != 2 || !info.pStages || info.layout == VK_NULL_HANDLE) return false;
        if (!info.pNext) return true;
        const auto* rendering = static_cast<const VkPipelineRenderingCreateInfo*>(info.pNext);
        return rendering->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO &&
            !rendering->pNext && rendering->viewMask == 0;
    }
    static bool eligible(const frame_bindings::Binding& binding) noexcept {
        const auto& image = binding.image; const auto& view = binding.view;
        const auto& range = view.subresource;
        constexpr auto usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return binding.complete && binding.state == frame_bindings::BindingState::Written &&
            binding.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && binding.descriptorCount == 1 &&
            binding.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            image.complete && image.flags == 0 && image.usage == usage &&
            image.sharingMode == VK_SHARING_MODE_EXCLUSIVE && image.type == VK_IMAGE_TYPE_2D &&
            image.format == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 &&
            image.extent.width && image.extent.height && image.extent.width <= 8192 && image.extent.height <= 8192 &&
            !(image.extent.width % 2) && !(image.extent.height % 2) && image.extent.depth == 1 &&
            image.mipLevels == 1 && image.arrayLayers == 1 && image.samples == VK_SAMPLE_COUNT_1_BIT &&
            view.complete && view.type == VK_IMAGE_VIEW_TYPE_2D && view.format == image.format &&
            range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && range.baseMipLevel == 0 && range.levelCount == 1 &&
            range.baseArrayLayer == 0 && range.layerCount == 1;
    }

    std::mutex mutex_;
    std::atomic<Failure> failure_{Failure::None};
    std::atomic<const char*> activeSite_{"unentered"}, firstSite_{nullptr};
    std::atomic<bool> failurePublished_{false};
    std::atomic<std::uint64_t> firstCoreLost_{0};
    std::array<std::atomic<std::uint64_t>, CoreReasonCount> firstCoreReasons_{};
    std::array<std::atomic<std::uint64_t>, CoreTableCount> firstCoreCapacities_{};
    std::uint64_t serial_ = 0;
    frame_bindings::Core bindings_{};
    frame_bindings::ObjectTable<Pool, PoolCapacity> pools_{};
    frame_bindings::ObjectTable<Shader, ShaderCapacity> shaders_{};
    frame_bindings::ObjectTable<Pipeline, PipelineCapacity> pipelines_{};
    frame_bindings::ObjectTable<Command, CommandCapacity> commands_{};
    frame_bindings::ObjectTable<PendingSubmit, SubmitCapacity> pendingSubmits_{};
};

} // namespace frame_pixel_capture
