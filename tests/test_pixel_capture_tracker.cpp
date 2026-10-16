#include "layer/pixel_capture_tracker.h"

#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace frame_pixel_capture {
struct TestAccess {
    static void pinSyntheticModule(LiveTracker& tracker, std::uint64_t device, VkShaderModule module) {
        // The native SPIR-V is not redistributed. Positive state-machine
        // controls mark only this already observed synthetic shader as pinned;
        // the real public-code fingerprint and wrong-hash refusal are exercised
        // separately. No test-only admission API is exposed to production.
        auto* shader = tracker.shaders_.find(device, frame_bindings::handle(module));
        if (!shader || !shader->complete) std::abort();
        shader->hash = DistortionFragmentHash;
    }
    static std::uint64_t observedHash(LiveTracker& tracker, std::uint64_t device, VkShaderModule module) {
        const auto* shader = tracker.shaders_.find(device, frame_bindings::handle(module));
        return shader ? shader->hash : 0;
    }
    static void pinSyntheticNativePair(LiveTracker& tracker, std::uint64_t device,
                                       VkShaderModule vertex, VkShaderModule fragment) {
        auto* vs = tracker.shaders_.find(device, frame_bindings::handle(vertex));
        auto* fs = tracker.shaders_.find(device, frame_bindings::handle(fragment));
        if (!vs || !fs || !vs->complete || !fs->complete) std::abort();
        // Version-specific native code is private. Only this CPU state fixture
        // pins its observed synthetic code; production still fingerprints the
        // actual original public CreateShaderModule bytes and exact sizes.
        vs->hash = frame_pixel_uniform::NativeVertexFingerprint; vs->bytes = frame_pixel_uniform::NativeVertexBytes;
        fs->hash = frame_pixel_uniform::NativeFragmentFingerprint; fs->bytes = frame_pixel_uniform::NativeFragmentBytes;
    }
    static void wrongSyntheticVertexBytes(LiveTracker& tracker, std::uint64_t device, VkShaderModule module) {
        auto* shader = tracker.shaders_.find(device, frame_bindings::handle(module));
        if (!shader) std::abort();
        shader->bytes -= 4;
    }
    static void wrongSyntheticVertexHash(LiveTracker& tracker, std::uint64_t device, VkShaderModule module) {
        auto* shader = tracker.shaders_.find(device, frame_bindings::handle(module));
        if (!shader) std::abort();
        shader->hash ^= 1;
    }
    static void exhaustDrawOrdinal(LiveTracker& tracker, std::uint64_t device, VkCommandBuffer value) {
        auto* command = tracker.commands_.find(device, frame_bindings::handle(value));
        if (!command) std::abort();
        command->drawOrdinal = UINT64_MAX;
    }
    static void exhaustGeneration(LiveTracker& tracker) { tracker.serial_ = UINT64_MAX; }
    static bool ownerPresent(const LiveTracker& tracker) {
        return tracker.ownerPresent_.load(std::memory_order_acquire);
    }
};
}

#ifdef FRAME_PIXEL_SSBO_TRANSACTION
namespace frame_pixel_ssbo_transaction {
struct TransactionTestAccess {
    static bool recording(const Transaction::RecordingTicket& t, std::uint64_t device,
                          VkCommandBuffer command, VkCommandPool pool) {
        return t.tracker_ && t.device_ == device && t.command_ == command && t.pool_ == pool &&
            t.commandGeneration_ && t.poolGeneration_ && t.family_ == 3;
    }
    static std::uint64_t generation(const Transaction::RecordingTicket& t) { return t.commandGeneration_; }
    static bool target(const Transaction::TargetTicket& t, std::uint64_t device, std::uint64_t ordinal) {
        return t.tracker_ && t.device_ == device && t.ordinal_ == ordinal && t.commandGeneration_ &&
            t.poolGeneration_ && t.source_.image.device == device && t.source_.image.value == 70 &&
            t.source_.view.value == 90 && t.source_.memory.value == 80 && t.set0_.value == 40 &&
            t.pipeline_.value == 110 && t.pipeline_.generation == 905 &&
            t.layout_.generation == 901 && t.source_.extent.width == 1152 &&
            t.source_.extent.height == 4608;
    }
    static bool empty(const Transaction::TargetTicket& t) { return !t.tracker_ && !t.device_ && !t.ordinal_; }
};
}
#endif

namespace {
using namespace frame_pixel_capture;
std::atomic<unsigned> checks{0};
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

// Hold an actual production observe callback, rather than manually locking a
// test-only mutex. The timeout bounds a broken fixture; it is not an admission
// deadline, retry loop or automatic timer in the compositor implementation.
struct ObservationGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    void hold() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; changed.notify_all();
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; }),
                "test releases the observed metadata owner");
    }
    void waitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }),
                "actual metadata observation acquired tracker owner");
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true; changed.notify_all();
    }
};

struct CallerProgress {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false, returned = false;
    void start() {
        std::lock_guard<std::mutex> lock(mutex);
        started = true; changed.notify_all();
    }
    void finish() {
        std::lock_guard<std::mutex> lock(mutex);
        returned = true; changed.notify_all();
    }
    void requireWaiting() {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return started; }),
                "second native-observation caller started");
        require(!changed.wait_for(lock, std::chrono::milliseconds(25), [&] { return returned; }),
                "parallel metadata caller cannot return or drop before owner finishes");
    }
};
constexpr std::uint64_t device = 1;
const auto sl = h<VkDescriptorSetLayout>(10), uiSl = h<VkDescriptorSetLayout>(11);
const auto pl = h<VkPipelineLayout>(20), uiPl = h<VkPipelineLayout>(21);
const auto dp = h<VkDescriptorPool>(30);
const auto ds = h<VkDescriptorSet>(40), ds2 = h<VkDescriptorSet>(41), uiDs = h<VkDescriptorSet>(42);
const auto pool = h<VkCommandPool>(50);
const auto cb = h<VkCommandBuffer>(60);
const auto image = h<VkImage>(70);
const auto memory = h<VkDeviceMemory>(80);
const auto view = h<VkImageView>(90);
const auto vs = h<VkShaderModule>(100), fs = h<VkShaderModule>(101);
const auto pipeline = h<VkPipeline>(110), uiPipeline = h<VkPipeline>(111);
const std::uint32_t code[] = {0x07230203, 0x00010000, 0, 8, 0, 0x00010000};

struct Fixture {
    LiveTracker tracker;
    bool uniforms = false;
    std::uint32_t dynamicOffset = 256;
    std::array<VkBuffer, 5> uniformBuffers{};
    std::array<VkDeviceMemory, 5> uniformMemories{};
    VkImageCreateInfo imageInfo = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    VkImageViewCreateInfo viewInfo = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    VkCommandBufferBeginInfo beginInfo = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    VkCommandPoolCreateInfo poolInfo = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    VkCommandBufferAllocateInfo allocate = info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);

    Fixture(bool pin = true, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, VkCommandPoolCreateFlags poolFlags = 0,
            bool withUniforms = false) : uniforms(withUniforms) {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        imageInfo.extent = {1152, 4608, 1}; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = imageInfo.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        require(tracker.observe([&](auto& core) {
            core.createImage(device, image, imageInfo);
            auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            allocation.allocationSize = 32 * 1024 * 1024; allocation.memoryTypeIndex = 0;
            core.allocateMemory(device, memory, allocation);
            core.bindImageMemory(device, image, memory, 0);
            core.createView(device, view, viewInfo);
            VkDescriptorSetLayoutBinding binding{9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            std::array<VkDescriptorSetLayoutBinding, 6> definitions{};
            if (uniforms) {
                for (unsigned i = 0; i < uniformBuffers.size(); ++i) {
                    uniformBuffers[i] = h<VkBuffer>(200 + i); uniformMemories[i] = h<VkDeviceMemory>(300 + i);
                    auto buffer = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
                    buffer.size = 8192; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    core.createBuffer(device, uniformBuffers[i], buffer);
                    auto memory = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
                    memory.allocationSize = 16384; core.allocateMemory(device, uniformMemories[i], memory);
                    core.bindBufferMemory(device, uniformBuffers[i], uniformMemories[i], 0);
                    definitions[i] = {frame_pixel_uniform::UniformBindings[i], i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                        : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
                }
                definitions.back() = binding;
            }
            auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
            layout.bindingCount = uniforms ? definitions.size() : 1; layout.pBindings = uniforms ? definitions.data() : &binding;
            core.createSetLayout(device, sl, layout);
            auto pi = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
            pi.setLayoutCount = 1; pi.pSetLayouts = &sl; core.createPipelineLayout(device, pl, pi);
            layout.bindingCount = 0; layout.pBindings = nullptr; core.createSetLayout(device, uiSl, layout);
            pi.pSetLayouts = &uiSl; core.createPipelineLayout(device, uiPl, pi);
            auto poolCreate = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            poolCreate.maxSets = 4; core.createPool(device, dp, poolCreate);
            auto sets = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            sets.descriptorPool = dp; sets.descriptorSetCount = 1; sets.pSetLayouts = &sl;
            core.allocateSets(device, sets, &ds); core.allocateSets(device, sets, &ds2);
            sets.pSetLayouts = &uiSl; core.allocateSets(device, sets, &uiDs);
        }), "seed production Core resource graph");
        write(ds); write(ds2);
        if (uniforms) { writeUniforms(ds); writeUniforms(ds2); }
        auto shader = info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        shader.codeSize = sizeof(code); shader.pCode = code;
        require(tracker.createShader(device, vs, shader) && tracker.createShader(device, fs, shader), "observe shader code");
        require(TestAccess::observedHash(tracker, device, fs) == frame_trace::fingerprint(code, sizeof(code)),
                "real public shader bytes use existing FNV implementation");
        if (pin) TestAccess::pinSyntheticModule(tracker, device, fs);
        if (uniforms) TestAccess::pinSyntheticNativePair(tracker, device, vs, fs);
        makePipeline(pipeline, pl, "PS_DISTORT");
        makePipeline(uiPipeline, uiPl, "PS_MAIN");
        poolInfo.queueFamilyIndex = 3; poolInfo.flags = poolFlags;
        require(tracker.createCommandPool(device, pool, poolInfo), "create nonprotected pool");
        allocate.commandPool = pool; allocate.commandBufferCount = 1; allocate.level = level;
        require(tracker.allocateCommands(device, allocate, &cb), "allocate command buffer");
        beginInfo.flags = flags;
        require(tracker.beginCommand(device, cb, beginInfo, true), "begin actual observed native recording");
        bind();
    }
    void writeUniforms(VkDescriptorSet output) {
        require(tracker.observe([&](auto& core) {
            constexpr std::array<VkDeviceSize, 5> ranges{{272, 2800, 400, 272, 2800}};
            for (unsigned i = 0; i < ranges.size(); ++i) {
                VkDescriptorBufferInfo buffer{uniformBuffers[i], 256, ranges[i]};
                auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                write.dstSet = output; write.dstBinding = frame_pixel_uniform::UniformBindings[i]; write.descriptorCount = 1;
                write.descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &buffer; core.updateSets(device, 1, &write, 0, nullptr);
            }
        }), "write five complete native uniform candidate descriptors");
    }
    void makePipeline(VkPipeline output, VkPipelineLayout layout, const char* entry,
                      const char* vertexEntry = "VS_DISTORT") {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = vertexEntry;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = entry;
        auto create = info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        create.stageCount = 2; create.pStages = stages; create.layout = layout;
        require(tracker.createPipeline(device, output, create), "observe ordinary pipeline");
    }
    void write(VkDescriptorSet output, VkImageView input = view) {
        require(tracker.observe([&](auto& core) {
            VkDescriptorImageInfo im{VK_NULL_HANDLE, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            write.dstSet = output; write.dstBinding = 9; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; write.pImageInfo = &im;
            core.updateSets(device, 1, &write, 0, nullptr);
        }), "write actual production descriptor");
    }
    void bind(VkDescriptorSet set = ds, VkPipeline pipe = pipeline, VkPipelineLayout layout = pl) {
        require(tracker.bindPipeline(device, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe), "bind graphics pipeline");
        require(tracker.observe([&](auto& core) {
            const auto count = uniforms && layout == pl ? 1u : 0u;
            core.bindSets(device, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, count, count ? &dynamicOffset : nullptr);
        }), "bind actual production descriptor set");
    }
    Candidate finishPass(bool select = true, const frame_pixel_uniform::Limits* limits = nullptr) {
        Candidate candidate;
        require(tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "begin inline primary pass");
        const bool selected = tracker.draw(device, cb, limits);
        require(selected == select, "draw source admission result");
        const bool issued = tracker.endRenderPass(device, cb, candidate);
        require(issued == select, "end after native pass emits only selected candidate");
        return candidate;
    }
    void recreateImage() {
        require(tracker.observe([&](auto& core) {
            core.destroyView(device, view); core.destroyImage(device, image);
            core.createImage(device, image, imageInfo); core.bindImageMemory(device, image, memory, 0);
            core.createView(device, view, viewInfo);
        }), "recycle source image/view with real new generations");
        write(ds);
    }
};

void baselineAndImmutableDraw() {
    auto f = std::make_unique<Fixture>();
    require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "begin pass for immutable draw");
    require(f->tracker.draw(device, cb), "select live P010 source");
    // Later native UI changes the full pipeline layout/set. End-scope must use
    // the exact prior draw resource, not a fresh snapshot of this UI binding.
    f->bind(uiDs, uiPipeline, uiPl);
    require(!f->tracker.draw(device, cb), "later UI draw cannot replace selected source");
    Candidate out;
    require(f->tracker.endRenderPass(device, cb, out), "original selected resource survives UI rebind");
    require(out.command == cb && out.device == device && out.queueFamily == 3 && out.serial && out.commandGeneration,
            "candidate has original command generation and queue family");
    require(out.beginFlags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT && out.oneTimeSubmit && out.noSimultaneousUse,
            "candidate carries exact one-time non-simultaneous contract");
    require(out.binding.image.identity.handle == frame_bindings::handle(image) &&
            out.binding.view.identity.handle == frame_bindings::handle(view) &&
            out.binding.image.flags == 0 && out.binding.image.sharingMode == VK_SHARING_MODE_EXCLUSIVE,
            "candidate owns full original live binding metadata");
    require(!f->tracker.prepareSubmit(device, cb).eligible, "recording command cannot submit");
    require(f->tracker.endCommand(device, cb, true), "native end command success");
    const auto ticket = f->tracker.prepareSubmit(device, cb);
    require(ticket.eligible && ticket.serial == out.serial && ticket.commandGeneration == out.commandGeneration,
            "pre-submit ticket identifies exact candidate recording");
    require(!f->tracker.acceptSubmit(ticket, false), "failed native submit cannot authorize snapshot");
    require(!f->tracker.acceptSubmit(ticket, true), "failed call's consumed ticket cannot authorize a later call");
    const auto retried = f->tracker.prepareSubmit(device, cb);
    require(retried.eligible && retried.admissionGeneration != ticket.admissionGeneration,
            "explicit retry prepares its own pre-call admission");
    require(f->tracker.acceptSubmit(retried, true), "native success accepts its original ticket");
    require(!f->tracker.acceptSubmit(retried, true) && !f->tracker.prepareSubmit(device, cb).eligible,
            "one-time command cannot authorize a replayed capture");
    require(!f->tracker.poisoned(), "ordinary UI rebinding/failed submit is not lost state");
}

void beginAndPipelineGuards() {
    for (auto flags : {VkCommandBufferUsageFlags(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT),
                       VkCommandBufferUsageFlags(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT),
                       VkCommandBufferUsageFlags(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)}) {
        auto f = std::make_unique<Fixture>(true, flags);
        require(!f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "reject simultaneous/continuation begin before acquisition");
        Candidate out; require(!f->tracker.endRenderPass(device, cb, out) && !out.serial, "unsupported begin emits no GPU candidate");
    }
    for (auto level : {VK_COMMAND_BUFFER_LEVEL_SECONDARY}) {
        auto f = std::make_unique<Fixture>(true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, level);
        require(!f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "secondary recording never selected");
    }
    auto protectedPool = std::make_unique<Fixture>(true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY, VK_COMMAND_POOL_CREATE_PROTECTED_BIT);
    require(!protectedPool->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "protected pool cannot acquire pixels");
    protectedPool.reset();
    auto wrongHash = std::make_unique<Fixture>(false);
    wrongHash->finishPass(false);
    auto wrongEntry = std::make_unique<Fixture>();
    wrongEntry->bind(ds, uiPipeline, uiPl);
    wrongEntry->finishPass(false);
}

void sourceCurrentness() {
    for (unsigned retirement = 0; retirement < 6; ++retirement) {
        auto f = std::make_unique<Fixture>();
        require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE) && f->tracker.draw(device, cb),
                "select source before resource lifecycle adversary");
        require(f->tracker.observe([&](auto& core) {
            if (retirement == 0) core.destroyView(device, view);
            if (retirement == 1) core.destroyImage(device, image);
            if (retirement == 2) core.freeMemory(device, memory);
            if (retirement == 3) {
                core.destroyView(device, view); core.createView(device, view, f->viewInfo);
            }
            if (retirement == 4) core.bindImageMemory(device, image, memory, 16);
        }), "observe retirement/rebind synchronously");
        if (retirement == 5) require(f->tracker.destroyPipeline(device, pipeline), "retire actual selected pipeline");
        Candidate out;
        require(!f->tracker.endRenderPass(device, cb, out) && !out.serial,
                "stale view/image/memory generation or memory bind never emits candidate");
    }
    auto copy = std::make_unique<Fixture>();
    require(copy->tracker.observe([&](auto& core) {
        auto from = info<VkCopyDescriptorSet>(VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET);
        from.srcSet = ds; from.srcBinding = 9; from.dstSet = ds2; from.dstBinding = 9; from.descriptorCount = 1;
        core.updateSets(device, 0, nullptr, 1, &from);
    }), "copy descriptor via production Core");
    copy->bind(ds2);
    const auto selected = copy->finishPass();
    require(selected.binding.complete, "copied production descriptor preserves source graph");
    copy.reset();
    auto conversion = std::make_unique<Fixture>();
    const auto handle = h<VkSamplerYcbcrConversion>(120);
    auto conversionInfo = info<VkSamplerYcbcrConversionCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);
    conversionInfo.format = conversion->imageInfo.format;
    auto conversionChain = info<VkSamplerYcbcrConversionInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO);
    conversionChain.conversion = handle;
    require(conversion->tracker.observe([&](auto& core) {
        core.destroyView(device, view); core.createConversion(device, handle, conversionInfo);
        auto create = conversion->viewInfo; create.pNext = &conversionChain;
        core.createView(device, view, create);
    }), "observe real conversion-backed source view");
    conversion->write(ds);
    require(conversion->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE) && conversion->tracker.draw(device, cb),
            "conversion-backed source initially current");
    require(conversion->tracker.observe([&](auto& core) { core.destroyConversion(device, handle); }), "retire source conversion");
    Candidate absent;
    require(!conversion->tracker.endRenderPass(device, cb, absent), "retired conversion cannot retain complete source view");
    conversion.reset();
    auto retiredModules = std::make_unique<Fixture>();
    require(retiredModules->tracker.destroyShader(device, vs) && retiredModules->tracker.destroyShader(device, fs),
            "native modules may be destroyed after pipeline creation");
    require(retiredModules->finishPass().serial != 0, "pipeline owns original shader fingerprint after module retirement");
}

void sourceShapeGuards() {
    for (unsigned change = 0; change < 9; ++change) {
        auto f = std::make_unique<Fixture>();
        if (change == 0) f->imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (change == 1) f->imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        if (change == 2) f->imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        if (change == 3) f->imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        if (change == 4) f->imageInfo.arrayLayers = 2;
        if (change == 5) f->imageInfo.samples = VK_SAMPLE_COUNT_2_BIT;
        if (change == 6) f->viewInfo.subresourceRange.layerCount = 2;
        if (change == 7) f->imageInfo.extent.width = 1153;
        if (change == 8) f->viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        if (change == 3) f->viewInfo.format = f->imageInfo.format;
        f->recreateImage(); f->finishPass(false);
    }
}

void scopesAndUnknownPaths() {
    auto f = std::make_unique<Fixture>();
    require(!f->tracker.draw(device, cb), "draw outside rendering has no end-scope boundary");
    require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "begin primary pass");
    require(f->tracker.draw(device, cb), "select within primary pass");
    require(f->tracker.nextSubpass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "inline next subpass preserves scope");
    Candidate out;
    require(f->tracker.endRenderPass(device, cb, out), "inline subpass closes at matching native end");
    f.reset();
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto nested = std::make_unique<Fixture>();
        require(nested->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE) && nested->tracker.draw(device, cb),
                "select before invalid scope/path");
        if (invalid == 0) require(!nested->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "nested scope rejected");
        if (invalid == 1) require(!nested->tracker.nextSubpass(device, cb, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS), "secondary subpass rejected");
        if (invalid == 2) require(!nested->tracker.unknownCommand(device, cb), "unknown command invalidates recording");
        if (invalid == 3) require(!nested->tracker.endRendering(device, cb, out), "mismatched end operation rejected");
        require(!nested->tracker.endRenderPass(device, cb, out) && !out.serial && !out.noPriorComputeState,
                "unknown/secondary/invalid recording cannot fabricate raw or sampler admission by closing pass");
    }
    for (auto flags : {VkRenderingFlags(0), VkRenderingFlags(VK_RENDERING_SUSPENDING_BIT),
                       VkRenderingFlags(VK_RENDERING_RESUMING_BIT), VkRenderingFlags(VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT)}) {
        auto dynamic = std::make_unique<Fixture>();
        auto rendering = info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.layerCount = 1; rendering.flags = flags;
        if (flags) require(!dynamic->tracker.beginRendering(device, cb, rendering), "suspended/resumed/secondary dynamic rendering rejected");
        else {
            require(dynamic->tracker.beginRendering(device, cb, rendering) && dynamic->tracker.draw(device, cb), "ordinary dynamic rendering selects");
            require(dynamic->tracker.endRendering(device, cb, out), "matching native dynamic end emits candidate");
        }
    }
}

void commandReuseAndLoss() {
    for (unsigned change = 0; change < 4; ++change) {
        auto f = std::make_unique<Fixture>();
        f->finishPass();
        require(f->tracker.endCommand(device, cb, true), "end original command before ticket");
        const auto ticket = f->tracker.prepareSubmit(device, cb);
        require(ticket.eligible, "pre-native exact recording ticket");
        if (change == 0) require(f->tracker.resetCommand(device, cb), "command reset");
        if (change == 1) require(f->tracker.resetCommandPool(device, pool), "pool reset");
        if (change == 2) require(f->tracker.freeCommands(device, pool, 1, &cb), "free command");
        if (change == 3) require(f->tracker.destroyCommandPool(device, pool), "destroy pool");
        auto forged = ticket; ++forged.commandGeneration;
        require(!f->tracker.acceptSubmit(forged, true), "changed generation cannot borrow an old pre-call admission");
        require(f->tracker.acceptSubmit(ticket, true),
                "original accepted native submit survives legal completion/reset/free before native return");
        require(!f->tracker.acceptSubmit(ticket, true), "completion-return ticket cannot be reused");
        if (change <= 1) {
            require(f->tracker.beginCommand(device, cb, f->beginInfo, true), "explicit native re-record allowed");
            f->bind(); const auto newer = f->finishPass();
            require(newer.commandGeneration != ticket.commandGeneration && newer.serial != ticket.serial,
                    "fresh recording/candidate has new generations");
        }
    }
    auto loss = std::make_unique<Fixture>();
    require(!loss->tracker.observe([](auto& core) { core.reportLoss(frame_bindings::Reason::Capacity); }), "Core loss poisons capture");
    require(loss->tracker.poisoned() && loss->tracker.failure() == Failure::CoreLoss, "Core loss remains sticky");
    loss.reset();
    auto exhausted = std::make_unique<LiveTracker>();
    TestAccess::exhaustGeneration(*exhausted);
    auto create = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    require(!exhausted->createCommandPool(device, pool, create) && exhausted->failure() == Failure::GenerationWrap,
            "generation exhaustion does not recycle a recording identity");
    exhausted.reset();
    auto capacity = std::make_unique<LiveTracker>();
    for (std::size_t i = 0; i < PoolCapacity; ++i)
        require(capacity->createCommandPool(device, h<VkCommandPool>(1000 + i), create), "bounded pool capacity entry");
    require(!capacity->createCommandPool(device, h<VkCommandPool>(9999), create) && capacity->failure() == Failure::Capacity,
            "capacity loss poisons rather than selecting untracked objects");
}


bool sameFailure(const FailureSnapshot& a, const FailureSnapshot& b) {
    return a.complete == b.complete && a.failure == b.failure && a.site == b.site &&
        a.coreLostState == b.coreLostState && a.coreReasonCounts == b.coreReasonCounts &&
        a.coreCapacityCounts == b.coreCapacityCounts;
}
void firstFailureTelemetry() {
    auto healthy = std::make_unique<LiveTracker>();
    const auto empty = healthy->failureSnapshot();
    require(!empty.complete && empty.failure == Failure::None && !empty.site && !empty.coreLostState,
            "healthy tracker reports no invented refusal or API site");
    require(healthy->observe([](auto&) {}, "vkHealthyObservation") && !healthy->failureSnapshot().complete,
            "successful observation does not publish a first failure");
    healthy.reset();
    auto unspecified = std::make_unique<LiveTracker>();
    require(!unspecified->observe([](auto& core) { core.reportUnsupported(device); }, nullptr),
            "null external site does not weaken refusal");
    const auto unnamed = unspecified->failureSnapshot();
    require(unnamed.complete && std::strcmp(unnamed.site, "unspecified") == 0,
            "null external label has a stable bounded fallback");
    unspecified.reset();

    // Every existing Core counter is copied, including zero categories. The
    // report is a bounded atomic observation, not an independently guessed cause.
    auto all = std::make_unique<LiveTracker>();
    require(!all->observe([](auto& core) {
        for (std::size_t i = 1; i < CoreReasonCount; ++i)
            for (std::size_t n = 0; n < i; ++n) core.reportLoss(static_cast<frame_bindings::Reason>(i));
    }, "vkAllCoreReasons"), "existing Core reason counters retain capture refusal");
    const auto allReasons = all->failureSnapshot();
    require(allReasons.complete && allReasons.failure == Failure::CoreLoss &&
            std::strcmp(allReasons.site, "vkAllCoreReasons") == 0,
            "Core-loss winner keeps exact external API site");
    require(allReasons.coreLostState == CoreReasonCount * (CoreReasonCount - 1) / 2,
            "first Core report retains observed total loss count");
    for (std::size_t i = 0; i < CoreReasonCount; ++i)
        require(allReasons.coreReasonCounts[i] == i, "each Core loss category is independently retained");
    for (auto count : allReasons.coreCapacityCounts) require(!count, "reported loss alone invents no table exhaustion");
    bool invoked = false;
    require(!all->observe([&](auto& core) { invoked = true; core.reportUnsupported(device); }, "vkLaterFailure"),
            "poisoned owner never mutates Core for later observations");
    require(!all->destroyCommandPool(device, pool) && !invoked && sameFailure(allReasons, all->failureSnapshot()),
            "later calls cannot overwrite first cause, site or counters");
    all.reset();

    auto capacity = std::make_unique<LiveTracker>();
    require(!capacity->observe([](auto& core) {
        auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        for (std::size_t i = 0; i <= frame_bindings::ImageCapacity; ++i)
            core.createImage(device, h<VkImage>(10000 + i), create);
    }, "vkCreateImage"), "actual Core image table exhaustion remains a global refusal");
    const auto full = capacity->failureSnapshot();
    require(full.complete && full.failure == Failure::CoreLoss && std::strcmp(full.site, "vkCreateImage") == 0 &&
            full.coreLostState == 1 && full.coreReasonCounts[static_cast<std::size_t>(frame_bindings::Reason::Capacity)] == 1 &&
            full.coreCapacityCounts[static_cast<std::size_t>(frame_bindings::Table::Image)] == 1,
            "capacity telemetry identifies actual failing Core table and API");
    for (std::size_t i = 0; i < CoreTableCount; ++i)
        require(full.coreCapacityCounts[i] == (i == static_cast<std::size_t>(frame_bindings::Table::Image) ? 1u : 0u),
                "unrelated table counters stay zero");
    capacity.reset();

    for (unsigned cause = 0; cause < 3; ++cause) {
        auto tracker = std::make_unique<LiveTracker>();
        const auto reason = cause == 0 ? frame_bindings::Reason::Unsupported :
                            cause == 1 ? frame_bindings::Reason::Stale : frame_bindings::Reason::Unknown;
        require(!tracker->observe([&](auto& core) {
            if (cause == 0) core.reportUnsupported(device);
            else {
                auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
                if (cause == 1) {
                    core.createImage(device, image, create);
                    core.createImage(device, image, create); // Actual duplicate native identity.
                } else core.createImage(device, VK_NULL_HANDLE, create);
            }
        }, "vkExternalCoreMutation"), "unsupported/stale/unknown Core mutations remain refusals");
        const auto snapshot = tracker->failureSnapshot();
        require(snapshot.complete && snapshot.failure == Failure::CoreLoss &&
                std::strcmp(snapshot.site, "vkExternalCoreMutation") == 0 && snapshot.coreLostState == 1 &&
                snapshot.coreReasonCounts[static_cast<std::size_t>(reason)] == 1,
                "unsupported/stale/unknown reason and API are published without recovery");
    }
    auto unknown = std::make_unique<LiveTracker>();
    require(!unknown->destroyShader(device, fs), "untracked shader retirement refuses capture");
    const auto unknownSnapshot = unknown->failureSnapshot();
    require(unknownSnapshot.complete && unknownSnapshot.failure == Failure::Unknown &&
            std::strcmp(unknownSnapshot.site, "destroyShader") == 0 && !unknownSnapshot.coreLostState,
            "tracker unknown failure is distinct from an invented Core unknown");
    unknown.reset();
    auto exhausted = std::make_unique<LiveTracker>();
    TestAccess::exhaustGeneration(*exhausted);
    auto create = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    require(!exhausted->createCommandPool(device, pool, create), "generation exhaustion is still refused");
    const auto wrap = exhausted->failureSnapshot();
    require(wrap.complete && wrap.failure == Failure::GenerationWrap &&
            std::strcmp(wrap.site, "createCommandPool") == 0 && !wrap.coreLostState,
            "internal helper failure retains invoking method site");
    exhausted.reset();
    auto ownCapacity = std::make_unique<LiveTracker>();
    for (std::size_t i = 0; i < PoolCapacity; ++i)
        require(ownCapacity->createCommandPool(device, h<VkCommandPool>(20000 + i), create), "fill tracker pool inventory");
    require(!ownCapacity->createCommandPool(device, h<VkCommandPool>(30000), create), "tracker capacity is still refused");
    const auto ownFull = ownCapacity->failureSnapshot();
    require(ownFull.complete && ownFull.failure == Failure::Capacity &&
            std::strcmp(ownFull.site, "createCommandPool") == 0 && !ownFull.coreLostState,
            "tracker capacity remains distinct from Core table exhaustion");
}
void overlappingImageBinding() {
    auto f = std::make_unique<Fixture>();
    const auto extraImage = h<VkImage>(71);
    const auto extraView = h<VkImageView>(91);
    const auto extraMemory = h<VkDeviceMemory>(81);
    require(f->tracker.observe([&](auto& core) {
        core.createImage(device, extraImage, f->imageInfo);
    }, "traceCreateImage"), "observe actual image before parallel allocation/bind");
    ObservationGate owner;
    CallerProgress other;
    bool allocated = false, bound = false;
    std::thread allocator([&] {
        allocated = f->tracker.observe([&](auto& core) {
            auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            allocation.allocationSize = 32 * 1024 * 1024;
            core.allocateMemory(device, extraMemory, allocation);
            owner.hold();
        }, "traceAllocateMemory");
    });
    owner.waitEntered();
    std::thread binder([&] {
        other.start();
        bound = f->tracker.observe([&](auto& core) {
            core.bindImageMemory(device, extraImage, extraMemory, 128);
            auto create = f->viewInfo; create.image = extraImage;
            core.createView(device, extraView, create);
        }, "traceBindImageMemory");
        other.finish();
    });
    other.requireWaiting();
    require(!f->tracker.poisoned() && TestAccess::ownerPresent(f->tracker),
            "real parallel native metadata overlap is not a global tracking failure");
    owner.release(); allocator.join(); binder.join();
    require(allocated && bound && !f->tracker.poisoned(),
            "both overlapping resource observations finish without loss");
    f->write(ds, extraView);
    const auto candidate = f->finishPass();
    require(candidate.binding.image.identity.handle == frame_bindings::handle(extraImage) &&
            candidate.binding.view.identity.handle == frame_bindings::handle(extraView) &&
            candidate.binding.image.memory.handle == frame_bindings::handle(extraMemory) &&
            candidate.binding.image.memory.generation && candidate.binding.image.memoryOffset == 128 &&
            candidate.binding.image.allocation.complete,
            "parallel traceBindImageMemory retains exact current image, view, allocation generation and offset");
    require(!f->tracker.failureSnapshot().complete && !TestAccess::ownerPresent(f->tracker),
            "successful contention produces no refusal record and releases the metadata owner");
}

void reentryAndExceptionRefusal() {
    for (unsigned kind = 0; kind < 2; ++kind) {
        auto tracker = std::make_unique<LiveTracker>();
        bool innerRan = false, innerResult = true;
        require(!tracker->observe([&](auto&) {
            if (!kind) innerResult = tracker->observe([&](auto&) { innerRan = true; }, "recursiveObservation");
            else innerResult = tracker->destroyShader(device, fs);
        }, "outerObservation"), "same-thread reentry refuses without deadlocking");
        const auto failure = tracker->failureSnapshot();
        require(!innerResult && !innerRan && failure.complete && failure.failure == Failure::Contention &&
                std::strcmp(failure.site, kind ? "destroyShader" : "recursiveObservation") == 0 && !failure.coreLostState,
                "true recursive caller publishes its own site and never enters Core");
        require(!TestAccess::ownerPresent(*tracker), "recursive failure clears the outer owner before unlocking");
        require(!tracker->observe([](auto&) { std::abort(); }, "afterReentry") &&
                sameFailure(failure, tracker->failureSnapshot()), "recursive failure remains sticky");
    }
    auto tracker = std::make_unique<LiveTracker>();
    require(!tracker->observe([](auto&) { throw std::runtime_error("malformed metadata callback"); }, "throwingObservation"),
            "callback exception is contained before crossing native API boundary");
    const auto failure = tracker->failureSnapshot();
    require(failure.complete && failure.failure == Failure::Unknown &&
            std::strcmp(failure.site, "throwingObservation") == 0 && !TestAccess::ownerPresent(*tracker),
            "exception publishes exact first site and releases metadata ownership");
}

void concurrentFirstRealFailure() {
    for (unsigned repeat = 0; repeat < 8; ++repeat) {
        auto tracker = std::make_unique<LiveTracker>();
        ObservationGate owner;
        CallerProgress other;
        bool leftResult = true, rightResult = true, laterRan = false;
        std::thread left([&] {
            leftResult = tracker->observe([&](auto& core) {
                auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
                core.createImage(device, VK_NULL_HANDLE, create); // Actual Core unknown handle.
                owner.hold();
            }, "traceFirstInvalidImage");
        });
        owner.waitEntered();
        std::thread right([&] {
            other.start();
            rightResult = tracker->observe([&](auto&) { laterRan = true; }, "traceLaterObservation");
            other.finish();
        });
        other.requireWaiting();
        require(!tracker->failureSnapshot().complete, "cold readers do not see a partial first failure");
        owner.release(); left.join(); right.join();
        const auto failure = tracker->failureSnapshot();
        require(!leftResult && !rightResult && !laterRan && failure.complete && failure.failure == Failure::CoreLoss &&
                std::strcmp(failure.site, "traceFirstInvalidImage") == 0 && failure.coreLostState == 1 &&
                failure.coreReasonCounts[static_cast<std::size_t>(frame_bindings::Reason::Unknown)] == 1,
                "real Core failure wins over waiting caller with exact cause and API site");
        require(!tracker->observe([](auto&) { std::abort(); }, "traceAfterFailure") &&
                sameFailure(failure, tracker->failureSnapshot()), "waiting and later callers cannot rewrite first failure");
    }
}

void concurrentDrawAndRetirement() {
    auto f = std::make_unique<Fixture>(true, 0);
    // A second independent primary recording is legal on a different externally
    // synchronized native pool. Retiring it must never replace the source CB.
    const auto otherPool = h<VkCommandPool>(51);
    const auto otherCb = h<VkCommandBuffer>(61);
    require(f->tracker.createCommandPool(device, otherPool, f->poolInfo), "create independent concurrent pool");
    auto allocate = f->allocate; allocate.commandPool = otherPool;
    require(f->tracker.allocateCommands(device, allocate, &otherCb) &&
            f->tracker.beginCommand(device, otherCb, f->beginInfo, true), "begin independent concurrent recording");
    const auto independentOld = f->tracker.prepareRetirement(device, otherCb);
    require(independentOld.eligible, "save exact other command generation before native retirement");
    require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "begin source scope before concurrent draw");
    ObservationGate owner;
    CallerProgress drawProgress, resetProgress;
    bool ownerResult = false, drawn = false, reset = false;
    std::thread metadata([&] {
        ownerResult = f->tracker.observe([&](auto&) { owner.hold(); }, "traceIndependentMetadata");
    });
    owner.waitEntered();
    std::thread renderer([&] { drawProgress.start(); drawn = f->tracker.draw(device, cb); drawProgress.finish(); });
    std::thread retire([&] { resetProgress.start(); reset = f->tracker.resetCommand(device, otherCb); resetProgress.finish(); });
    drawProgress.requireWaiting(); resetProgress.requireWaiting();
    owner.release(); metadata.join(); renderer.join(); retire.join();
    const auto independentNew = f->tracker.prepareRetirement(device, otherCb);
    Candidate candidate;
    require(ownerResult && drawn && reset && f->tracker.endRenderPass(device, cb, candidate) &&
            candidate.command == cb && candidate.commandGeneration != independentOld.commandGeneration &&
            independentNew.eligible && independentNew.commandGeneration != independentOld.commandGeneration &&
            independentOld.pool == otherPool && candidate.pool == pool,
            "serialized parallel draw and independent reset retain both exact command/pool generations");
    require(f->tracker.endCommand(device, cb, true), "end source command for concurrent delayed return");
    const auto submitted = f->tracker.prepareSubmit(device, cb);
    const auto oldRetirement = f->tracker.prepareRetirement(device, cb);
    require(submitted.eligible && oldRetirement.commandGeneration == candidate.commandGeneration,
            "pre-native submit and retirement refer to selected original generation");
    ObservationGate resetOwner;
    CallerProgress acceptProgress;
    bool resetMetadata = false, nativeAccepted = false;
    std::thread replacement([&] {
        require(f->tracker.beginCommand(device, cb, f->beginInfo, true), "legal native completion precedes delayed QueueSubmit return");
        resetMetadata = f->tracker.observe([&](auto&) { resetOwner.hold(); }, "tracePostResetMetadata");
    });
    resetOwner.waitEntered();
    std::thread delayedSubmit([&] {
        acceptProgress.start(); nativeAccepted = f->tracker.acceptSubmit(submitted, true); acceptProgress.finish();
    });
    acceptProgress.requireWaiting();
    resetOwner.release(); replacement.join(); delayedSubmit.join();
    const auto current = f->tracker.prepareRetirement(device, cb);
    require(resetMetadata && nativeAccepted && current.eligible &&
            current.commandGeneration != oldRetirement.commandGeneration && !f->tracker.poisoned(),
            "parallel delayed submit accepts only saved native recording while replacement identity stays current");
    require(!f->tracker.acceptSubmit(submitted, true), "concurrent return ticket still cannot be replayed");
}

void threadedResourceLifecycle() {
    auto tracker = std::make_unique<LiveTracker>();
    constexpr unsigned Threads = 4, Iterations = 256;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::array<std::uint64_t, Threads> lastGeneration{};
    std::vector<std::thread> callers;
    for (unsigned thread = 0; thread < Threads; ++thread) callers.emplace_back([&, thread] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        const auto im = h<VkImage>(10000 + thread);
        const auto vw = h<VkImageView>(20000 + thread);
        const auto mem = h<VkDeviceMemory>(30000 + thread);
        for (unsigned iteration = 0; iteration < Iterations; ++iteration) {
            require(tracker->observe([&](auto& core) {
                auto create = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
                create.imageType = VK_IMAGE_TYPE_2D; create.format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
                create.extent = {1152, 4608, 1}; create.mipLevels = create.arrayLayers = 1;
                create.samples = VK_SAMPLE_COUNT_1_BIT; create.tiling = VK_IMAGE_TILING_OPTIMAL;
                create.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                core.createImage(device, im, create);
                auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
                allocation.allocationSize = 32 * 1024 * 1024; core.allocateMemory(device, mem, allocation);
            }, "traceStressCreate"), "parallel finite native resource births retained");
            require(tracker->observe([&](auto& core) {
                core.bindImageMemory(device, im, mem, 0);
                auto create = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
                create.image = im; create.viewType = VK_IMAGE_VIEW_TYPE_2D;
                create.format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
                create.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                core.createView(device, vw, create);
                const auto current = core.trackedResourceIdentity(frame_bindings::TrackedResourceKind::Image, device,
                                                                  frame_bindings::handle(im));
                require(current.handle == frame_bindings::handle(im) && current.generation > lastGeneration[thread],
                        "same native handle receives a fresh generation on every observed lifetime");
                lastGeneration[thread] = current.generation;
            }, "traceStressBind"), "parallel finite native binds retained");
            require(tracker->observe([&](auto& core) {
                core.destroyView(device, vw); core.destroyImage(device, im); core.freeMemory(device, mem);
            }, "traceStressDestroy"), "parallel finite native resource retirements retained");
        }
    });
    while (ready.load(std::memory_order_acquire) != Threads) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& caller : callers) caller.join();
    require(!tracker->poisoned() && !tracker->failureSnapshot().complete && !TestAccess::ownerPresent(*tracker),
            "all interleaved native resource lifetimes finish with no lost metadata or leaked owner");
}

void reusableRetirementIdentity() {
    auto f = std::make_unique<Fixture>(true, 0);
    const auto candidate = f->finishPass();
    require(candidate.beginFlags == 0 && !candidate.oneTimeSubmit && candidate.noSimultaneousUse && candidate.retirementRequired,
            "native reusable recording explicitly requires retirement before CPU readback");
    require(candidate.pool == pool && candidate.poolGeneration,
            "candidate retains exact original pool generation");
    require(f->tracker.ownsCommandPool(device, cb, pool) &&
            !f->tracker.ownsCommandPool(device, cb, h<VkCommandPool>(999)) &&
            !f->tracker.ownsCommandPool(2, cb, pool), "pool ownership is device and generation checked");
    const auto old = f->tracker.prepareRetirement(device, cb);
    require(old.eligible && old.commandGeneration == candidate.commandGeneration && old.pool == candidate.pool &&
            old.poolGeneration == candidate.poolGeneration, "pre-native retirement ticket identifies exact copied recording");
    require(f->tracker.endCommand(device, cb, true), "reusable native command ends");
    const auto submit = f->tracker.prepareSubmit(device, cb);
    require(submit.eligible, "reusable command records an initial exact submit ticket");
    // Native completion and implicit reset can precede a delayed original
    // QueueSubmit return. The saved retirement and submit identities remain old.
    require(f->tracker.beginCommand(device, cb, f->beginInfo, true), "successful native implicit reset begins new recording");
    const auto replacement = f->tracker.prepareRetirement(device, cb);
    require(replacement.eligible && replacement.commandGeneration != old.commandGeneration &&
            old.commandGeneration == candidate.commandGeneration, "post-reset query cannot stand in for pre-call retirement ticket");
    require(f->tracker.acceptSubmit(submit, true), "native-success ticket survives valid reusable retirement before return");
    require(!f->tracker.prepareRetirement(device, h<VkCommandBuffer>(999)).eligible,
            "unknown recording provides no retirement proof");
}

void computeStateAdmissionHistory() {
    require(!Candidate{}.noPriorComputeState, "invented candidate cannot assume clean native compute state");
    auto clean = std::make_unique<Fixture>();
    require(clean->finishPass().noPriorComputeState, "observed successful primary Begin admits clean compute history");
    clean.reset();
    for (unsigned route = 0; route < 4; ++route) {
        auto f = std::make_unique<Fixture>();
        if (route == 0)
            require(f->tracker.bindPipeline(device, cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline),
                    "native compute pipeline marks only sampler history dirty");
        else if (route == 1)
            require(f->tracker.touchComputeState(device, cb), "descriptor/dispatch wrapper marks sampler history dirty");
        else if (route == 2)
            require(f->tracker.bindPipeline(device, cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline),
                    "other non-graphics pipeline state is refused conservatively for sampler insertion");
        else {
            require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE) && f->tracker.draw(device, cb),
                    "select original draw before later compute-state observation");
            require(f->tracker.touchComputeState(device, cb), "later compute observation cannot borrow earlier clean draw flag");
            Candidate candidate;
            require(f->tracker.endRenderPass(device, cb, candidate) && !candidate.noPriorComputeState,
                    "end-scope candidate reflects complete native compute history at actual injection");
        }
        if (route < 3) require(!f->finishPass().noPriorComputeState,
                               "prior compute state disables sampler insertion without suppressing independent raw copy");
        require(!f->tracker.poisoned(), "valid native compute use is not global metadata loss");
        const auto old = f->tracker.prepareRetirement(device, cb);
        require(f->tracker.resetCommand(device, cb), "retire compute-dirty command before new recording");
        Candidate unbegun;
        require(!f->tracker.endRenderPass(device, cb, unbegun) && !unbegun.noPriorComputeState,
                "reset alone cannot invent clean recording admission");
        require(!f->tracker.beginCommand(device, cb, f->beginInfo, false), "failed native Begin provides no clean admission");
        require(f->tracker.beginCommand(device, cb, f->beginInfo, true), "actual successful Begin initializes new clean history");
        f->bind();
        const auto next = f->finishPass();
        require(next.noPriorComputeState && next.commandGeneration != old.commandGeneration,
                "only actual new native generation can restore clean compute admission");
    }
    auto reused = std::make_unique<Fixture>();
    require(reused->tracker.touchComputeState(device, cb), "dirty original command before handle recycling");
    const auto old = reused->tracker.prepareRetirement(device, cb);
    require(reused->tracker.freeCommands(device, pool, 1, &cb) &&
            reused->tracker.allocateCommands(device, reused->allocate, &cb), "recycle exact native command handle");
    require(reused->tracker.beginCommand(device, cb, reused->beginInfo, true), "observe recycled handle's new Begin");
    reused->bind(); const auto candidate = reused->finishPass();
    require(candidate.noPriorComputeState && candidate.commandGeneration != old.commandGeneration,
            "recycled handle has independent generation and fresh observed compute history");
    reused.reset();
    auto independent = std::make_unique<Fixture>();
    const auto otherPool = h<VkCommandPool>(51);
    const auto otherCb = h<VkCommandBuffer>(61);
    require(independent->tracker.createCommandPool(device, otherPool, independent->poolInfo), "create separate native pool");
    auto allocation = independent->allocate; allocation.commandPool = otherPool;
    require(independent->tracker.allocateCommands(device, allocation, &otherCb) &&
            independent->tracker.beginCommand(device, otherCb, independent->beginInfo, true) &&
            independent->tracker.touchComputeState(device, otherCb), "observe dirty compute history in another command generation");
    require(independent->finishPass().noPriorComputeState,
            "other command's compute state never dirties the selected primary generation");
}

constexpr frame_pixel_uniform::Limits uniformLimits{256, 65536};
std::unique_ptr<Fixture> uniformFixture() {
    return std::make_unique<Fixture>(true, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                    VK_COMMAND_BUFFER_LEVEL_PRIMARY, 0, true);
}
void noUniform(const Candidate& candidate, UniformRefusal reason) {
    require(candidate.serial && !candidate.uniformFinalDraw && !candidate.uniformPlan.metadataCandidate
            && candidate.uniformPlan.totalBytes == 0 && candidate.uniformRefusal == reason,
            "refused companion preserves the ordinary raw candidate and exposes no partial uniform copy");
}
void uniformMetadataPositive() {
    auto f = uniformFixture(); const auto candidate = f->finishPass(true, &uniformLimits);
    require(candidate.uniformFinalDraw && candidate.uniformRefusal == UniformRefusal::None
            && candidate.uniformPlan.metadataCandidate && candidate.uniformPlan.totalBytes == 6544,
            "only explicit opt-in publishes five full final-draw uniform metadata slices");
    require(candidate.uniformDrawOrdinal == 1 && candidate.uniformPipeline.handle == frame_bindings::handle(pipeline)
            && candidate.uniformPipeline.generation && candidate.uniformPlan.set.handle == frame_bindings::handle(ds)
            && candidate.uniformPlan.command.handle == frame_bindings::handle(cb),
            "metadata retains exact last native pipeline/descriptor and observed draw ordinal");
    require(candidate.uniformStages.vertex.hash == frame_pixel_uniform::NativeVertexFingerprint
            && candidate.uniformStages.vertex.bytes == frame_pixel_uniform::NativeVertexBytes
            && candidate.uniformStages.fragment.hash == frame_pixel_uniform::NativeFragmentFingerprint
            && candidate.uniformStages.fragment.bytes == frame_pixel_uniform::NativeFragmentBytes
            && candidate.uniformPlan.stages.vertex.generation == candidate.uniformStages.vertex.generation,
            "pipeline creation retains both exact compiled stage fingerprints, sizes and original generations");
    require(candidate.uniformPlan.slices[1].dynamicOffset == 256
            && candidate.uniformPlan.slices[1].effectiveOffset == 512
            && candidate.uniformPlan.slices[1].effectiveRange == 2800
            && candidate.uniformOffendingBinding == UINT32_MAX
            && candidate.uniformResourceRefusal.reason == frame_bindings::UniformResourceReason::None,
            "effective dynamic source offset and successful exact resource guard are retained");
    f.reset();
    auto defaultPath = uniformFixture();
    noUniform(defaultPath->finishPass(), UniformRefusal::Disabled);
    // Compiled pipeline code does not require its source shader module to stay
    // live; destroying it is legal Vulkan and never changes that pipeline.
    auto retained = uniformFixture();
    require(retained->tracker.destroyShader(device, vs) && retained->tracker.destroyShader(device, fs),
            "legal original module retirement after pipeline compilation");
    require(retained->finishPass(true, &uniformLimits).uniformFinalDraw,
            "compiled stage facts survive legal shader module destruction without guessing a replacement module");
    auto dynamic = uniformFixture(); auto rendering = info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.layerCount = 1;
    require(dynamic->tracker.beginRendering(device, cb, rendering) && dynamic->tracker.draw(device, cb, &uniformLimits),
            "ordinary non-suspended dynamic scope observes its own exact direct target");
    Candidate dynamicResult;
    require(dynamic->tracker.endRendering(device, cb, dynamicResult) && dynamicResult.uniformFinalDraw,
            "matching dynamic end uses the same fresh final-draw/resource validation");
}
void uniformFinalDrawReplacement() {
    auto f = uniformFixture();
    require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
            && f->tracker.draw(device, cb, &uniformLimits), "first target selects original raw source");
    f->dynamicOffset = 512; f->bind(ds2);
    require(!f->tracker.draw(device, cb, &uniformLimits), "later exact target does not replace legacy first raw selection");
    Candidate result;
    require(f->tracker.endRenderPass(device, cb, result) && result.uniformFinalDraw
            && result.uniformDrawOrdinal == 2 && result.uniformPlan.set.handle == frame_bindings::handle(ds2)
            && result.uniformPlan.slices[1].dynamicOffset == 512 && result.uniformPlan.slices[1].effectiveOffset == 768,
            "later final target captures its own descriptor revision and dynamic ranges rather than projecting first draw data");
    require(result.binding.image.identity.handle == frame_bindings::handle(image), "raw source remains the original matching image");
    f.reset();
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto current = uniformFixture();
        require(current->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
                && current->tracker.draw(device, cb, &uniformLimits), "select companion before later draw event");
        auto expected = UniformRefusal::NoFinalTarget;
        if (invalid == 0) current->bind(uiDs, uiPipeline, uiPl);
        if (invalid == 1) expected = UniformRefusal::NotDirectNonempty;
        if (invalid == 2) {
            require(current->tracker.nextSubpass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "inline subpass preserves raw policy");
            expected = UniformRefusal::Subpass;
        } else {
            require(!current->tracker.draw(device, cb, invalid == 3 ? nullptr : &uniformLimits, invalid != 1),
                    "foreign, zero or unobserved later direct call is still observed before selected fast return");
        }
        Candidate out; require(current->tracker.endRenderPass(device, cb, out), "ordinary raw source survives companion-only refusal");
        noUniform(out, expected);
    }
    auto afterSubpass = uniformFixture();
    require(afterSubpass->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
            && afterSubpass->tracker.draw(device, cb, &uniformLimits)
            && afterSubpass->tracker.nextSubpass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "select then invalidate at subpass edge");
    require(!afterSubpass->tracker.draw(device, cb, &uniformLimits), "new final target in the next inline subpass is observed independently");
    Candidate out; require(afterSubpass->tracker.endRenderPass(device, cb, out) && out.uniformFinalDraw
            && out.uniformDrawOrdinal == 2, "a newly observed final target may restore the companion after a subpass");
    auto unknown = uniformFixture();
    require(unknown->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
            && unknown->tracker.draw(device, cb, &uniformLimits), "target before existing unknown/indirect/secondary gate");
    require(!unknown->tracker.unknownCommand(device, cb) && !unknown->tracker.draw(device, cb, &uniformLimits)
            && !unknown->tracker.endRenderPass(device, cb, out) && !out.uniformFinalDraw,
            "unknown/indirect/secondary paths keep the original whole-recording refusal and cannot reselect");
}
void uniformProfileAndCurrentness() {
    for (unsigned wrong = 0; wrong < 4; ++wrong) {
        auto f = uniformFixture();
        require(f->tracker.destroyPipeline(device, pipeline), "retire prior test pipeline before new observed creation");
        if (wrong == 0) TestAccess::wrongSyntheticVertexBytes(f->tracker, device, vs);
        if (wrong == 1) TestAccess::wrongSyntheticVertexHash(f->tracker, device, vs);
        if (wrong == 2) TestAccess::wrongSyntheticVertexBytes(f->tracker, device, fs);
        f->makePipeline(pipeline, pl, "PS_DISTORT", wrong == 3 ? "VS_OTHER" : "VS_DISTORT"); f->bind();
        noUniform(f->finishPass(true, &uniformLimits), UniformRefusal::NativeStages);
    }
    auto invalidLimits = uniformLimits; invalidLimits.minUniformBufferOffsetAlignment = 0;
    auto f = uniformFixture(); noUniform(f->finishPass(true, &invalidLimits), UniformRefusal::Limits); f.reset();
    auto incomplete = std::make_unique<Fixture>();
    const auto missing = incomplete->finishPass(true, &uniformLimits);
    // This fixture also has a non-native vertex stage; no complete raw-only
    // fixture may accidentally inherit the new profile/range assumptions.
    noUniform(missing, UniformRefusal::NativeStages); incomplete.reset();
    auto noBuffers = std::make_unique<Fixture>();
    require(noBuffers->tracker.destroyPipeline(device, pipeline), "replace raw-only pipeline with exact paired stage facts");
    TestAccess::pinSyntheticNativePair(noBuffers->tracker, device, vs, fs);
    noBuffers->makePipeline(pipeline, pl, "PS_DISTORT"); noBuffers->bind();
    const auto noUniformBindings = noBuffers->finishPass(true, &uniformLimits);
    noUniform(noUniformBindings, UniformRefusal::Metadata);
    require(noUniformBindings.uniformContractRefusal == frame_pixel_uniform::Refusal::Descriptor
            && noUniformBindings.uniformOffendingBinding == 0,
            "known stage pair still requires every complete native UBO binding and retains the exact typed failure");
    noBuffers.reset();
    for (bool rebind : {false, true}) {
        auto current = uniformFixture();
        require(current->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
                && current->tracker.draw(device, cb, &uniformLimits), "select before later binding-only change");
        if (rebind) current->bind(ds2);
        else {
            const auto another = h<VkPipeline>(112); current->makePipeline(another, pl, "PS_DISTORT");
            current->bind(ds, another);
        }
        Candidate out; require(current->tracker.endRenderPass(device, cb, out), "binding-only change leaves raw source intact");
        noUniform(out, rebind ? UniformRefusal::Metadata : UniformRefusal::PipelineChanged);
        if (rebind) require(out.uniformContractRefusal == frame_pixel_uniform::Refusal::SnapshotChanged,
                            "typed contract refusal identifies descriptor-binding revision change");
    }
    auto stale = uniformFixture();
    require(stale->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
            && stale->tracker.draw(device, cb, &uniformLimits), "select before current whole metadata becomes incomplete");
    require(stale->tracker.observe([&](auto& core) { core.destroyBuffer(device, stale->uniformBuffers[0]); }),
            "adversarial source retirement is observed without changing the raw sampled image");
    Candidate staleOut; require(stale->tracker.endRenderPass(device, cb, staleOut), "raw image remains independently current");
    noUniform(staleOut, UniformRefusal::Metadata);
    require(staleOut.uniformContractRefusal == frame_pixel_uniform::Refusal::SnapshotIncomplete
            && staleOut.uniformOffendingBinding == UINT32_MAX,
            "incomplete whole-state metadata is not falsely attributed to sampled-image binding9");
    auto overflow = uniformFixture(); TestAccess::exhaustDrawOrdinal(overflow->tracker, device, cb);
    noUniform(overflow->finishPass(true, &uniformLimits), UniformRefusal::DrawOrdinalOverflow);
}
void uniformSourceAndAliasGuards() {
    auto changed = uniformFixture();
    require(changed->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
            && changed->tracker.draw(device, cb, &uniformLimits), "select original image before later target uses another source");
    const auto otherImage = h<VkImage>(71); const auto otherMemory = h<VkDeviceMemory>(81); const auto otherView = h<VkImageView>(91);
    require(changed->tracker.observe([&](auto& core) {
        core.createImage(device, otherImage, changed->imageInfo);
        auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO); allocation.allocationSize = 32 * 1024 * 1024;
        core.allocateMemory(device, otherMemory, allocation); core.bindImageMemory(device, otherImage, otherMemory, 0);
        auto create = changed->viewInfo; create.image = otherImage; core.createView(device, otherView, create);
    }), "observe second complete native sampled resource without aliasing UBO memory");
    changed->write(ds2, otherView); changed->bind(ds2);
    require(!changed->tracker.draw(device, cb, &uniformLimits), "later target has an independent source snapshot");
    Candidate out; require(changed->tracker.endRenderPass(device, cb, out), "legacy raw first image remains independently valid");
    noUniform(out, UniformRefusal::ChangedSource);
    require(out.uniformOffendingBinding == 9, "source mismatch telemetry identifies the sampled-image binding");
    changed.reset();
    for (unsigned where = 0; where < 3; ++where) {
        auto f = uniformFixture();
        if (where == 1) require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE)
                && f->tracker.draw(device, cb, &uniformLimits), "select before later alias appears");
        require(f->tracker.observe([&](auto& core) {
            auto alias = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            alias.size = 128; alias.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; alias.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            const auto buffer = h<VkBuffer>(900); core.createBuffer(device, buffer, alias);
            core.bindBufferMemory(device, buffer, f->uniformMemories[0], 256);
        }), "create actual observed overlapping writable buffer on first source allocation");
        Candidate result;
        if (where != 1) result = f->finishPass(true, where == 2 ? nullptr : &uniformLimits);
        else require(f->tracker.endRenderPass(device, cb, result), "fresh end-scope guard checks aliases introduced after selection");
        noUniform(result, where == 2 ? UniformRefusal::Disabled : UniformRefusal::Resources);
        if (where != 2) require(result.uniformResourceRefusal.reason == frame_bindings::UniformResourceReason::BufferAlias
                && result.uniformResourceRefusal.binding == 0 && result.uniformOffendingBinding == 0,
                "exact Core alias reason/binding survives without inferred coherence or partial data");
    }
    auto generation = uniformFixture(); require(generation->finishPass(true, &uniformLimits).uniformFinalDraw,
                                                "first recording has explicit uniform candidate");
    require(generation->tracker.resetCommand(device, cb) && generation->tracker.beginCommand(device, cb, generation->beginInfo, true),
            "successful native recording replacement clears optional snapshot and draw ordinal");
    generation->bind(); noUniform(generation->finishPass(), UniformRefusal::Disabled);
    const std::array<const char*, 13> names{{"disabled", "none", "no_final_target", "not_direct_nonempty", "native_stages", "limits",
        "metadata", "changed_source", "pipeline_changed", "resources", "subpass", "unknown_path", "draw_ordinal_overflow"}};
    for (unsigned i = 0; i < names.size(); ++i)
        require(!std::strcmp(uniformRefusalName(static_cast<UniformRefusal>(i)), names[i]), "each bounded uniform refusal has a fixed literal status name");
    require(!std::strcmp(uniformRefusalName(static_cast<UniformRefusal>(255)), "unknown_uniform_refusal"),
            "unexpected status values do not borrow or format native data");
}
#ifdef FRAME_PIXEL_SSBO_TRANSACTION
void ssboSelectedPipelineIdentity() {
    constexpr std::uint64_t nativeId = 0x510;
    for (bool alternate : {false, true}) {
        auto f = uniformFixture();
        const auto another = h<VkPipeline>(112);
        const frame_pixel_ssbo_clone::Identity original{nativeId,frame_bindings::handle(pipeline),905};
        const frame_pixel_ssbo_clone::Identity second{nativeId,frame_bindings::handle(another),999};
        require(f->tracker.ssboDeviceCreated(device,h<VkDevice>(nativeId)) &&
                f->tracker.ssboPipelineCreated(device,pipeline,original),
                "associate actual original target for cold selected-draw lookup");
        f->makePipeline(another,pl,"PS_DISTORT");
        require(f->tracker.ssboPipelineCreated(device,another,second),
                "separate live native variant has its own exact creation identity");
        f->bind(ds,alternate?another:pipeline);
        require(f->tracker.beginRenderPass(device,cb,VK_SUBPASS_CONTENTS_INLINE) && f->tracker.draw(device,cb),
                "observe selected variant with uniform companion disabled");
        f->bind(ds,alternate?pipeline:another);
        Candidate candidate;
        require(f->tracker.endRenderPass(device,cb,candidate),"later binding-only change preserves original raw candidate");
        const auto expected=alternate?second:original;
        require(candidate.ssboPipeline.device==expected.device && candidate.ssboPipeline.value==expected.value &&
                candidate.ssboPipeline.generation==expected.generation,
                "cold selector gets actual drawn pipeline, never the last binding or first created variant");
    }
    auto missing=uniformFixture();
    const auto unassociated=missing->finishPass();
    require(!unassociated.ssboPipeline.device && !unassociated.ssboPipeline.value && !unassociated.ssboPipeline.generation,
            "raw-only candidate never invents an SSBO pipeline creation association");
}
void ssboNativeTickets() {
    using Transaction = frame_pixel_ssbo_transaction::Transaction;
    using Access = frame_pixel_ssbo_transaction::TransactionTestAccess;
    constexpr std::uint64_t nativeId = 0x510;
    const auto nativeDevice = h<VkDevice>(nativeId);
    const auto recipe = [] {
        frame_pixel_ssbo_clone::Recipe r;
        r.definitionComplete = true;
        const std::array<std::uint64_t, 6> values{{10, 20, 2000, 100, 101, 110}};
        for (unsigned i = 0; i < values.size(); ++i) r.provenance[i] = {nativeId, values[i], 900 + i};
        return r;
    }();
    auto f = uniformFixture();
    bool contains = false;
    require(f->tracker.ssboPoolContainsSet(device, dp, ds, contains) && contains,
            "selected descriptor set retains exact actual native pool ownership");
    const auto otherPool = h<VkDescriptorPool>(31);
    require(f->tracker.observe([&](auto& core) {
        auto pool = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool.maxSets = 1; core.createPool(device, otherPool, pool);
    }), "observe an unrelated native pool");
    require(f->tracker.ssboPoolContainsSet(device, otherPool, ds, contains) && !contains,
            "retiring an unrelated pool cannot cancel the selected observer set");
    require(!f->tracker.ssboPoolContainsSet(device, h<VkDescriptorPool>(9999), ds, contains) && !contains,
            "unknown pool never becomes affirmative unrelated ownership evidence");
    Transaction::RecordingTicket recording, next;
    Transaction::TargetTicket target;
    require(!f->tracker.ssboRecordingTicket(device, cb, recording), "unmapped device mints no recording proof");
    require(f->tracker.ssboDeviceCreated(device, nativeDevice), "retain actual original device mapping");
    require(!f->tracker.ssboDeviceCreated(device, h<VkDevice>(nativeId + 1)) &&
            !f->tracker.ssboDeviceCreated(device + 1, nativeDevice), "duplicate native/tag mappings never overwrite identity");
    require(f->tracker.ssboPipelineCreated(device, pipeline, recipe.provenance[5]),
            "same original pipeline-create observation associates two generation namespaces");
    require(!f->tracker.ssboPipelineCreated(device, pipeline, recipe.provenance[5]),
            "a later caller cannot overwrite the original creation association");
    require(f->tracker.ssboRecordingTicket(device, cb, recording) &&
            Access::recording(recording, nativeId, cb, pool), "actual observed primary begin mints exact recording token");
    require(!f->tracker.ssboTargetTicket(device, cb, recipe, target) && Access::empty(target),
            "no target token outside original rendering scope");
    require(f->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "start observed native scope");
    require(!f->tracker.ssboRecordingTicket(device, cb, next), "cannot adopt a recording after entering its pass");
    // Actual archived compositor recordings contain two preceding UI draws and
    // an indexed target; no invented one-draw command-buffer restriction.
    f->tracker.bindPipeline(device, cb, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);
    f->tracker.draw(device, cb);
    require(!f->tracker.ssboTargetTicket(device, cb, recipe, target), "ordinary UI draw is not native distortion");
    f->tracker.draw(device, cb);
    f->bind();
    require(f->tracker.draw(device, cb), "observe actual target after preceding UI draws");
    require(f->tracker.ssboTargetTicket(device, cb, recipe, target) && Access::target(target, nativeId, 3),
            "live target uses actual Core source/set generations and separate owned recipe generations");
    for (unsigned changed = 0; changed < 5; ++changed) {
        auto wrong = recipe;
        if (changed < 3) ++wrong.provenance[std::array<unsigned, 3>{{0, 1, 5}}[changed]].value;
        else if (changed == 3) ++wrong.provenance[5].device;
        else ++wrong.provenance[5].generation;
        require(!f->tracker.ssboTargetTicket(device, cb, wrong, target) && Access::empty(target),
                "wrong exact set layout/pipeline layout/pipeline/device never produces a partial target");
    }
    require(f->tracker.observe([&](auto& core) { core.destroyView(device, view); }), "observe source destruction before draw boundary");
    require(!f->tracker.ssboTargetTicket(device, cb, recipe, target), "stale current resource cannot mint a target");
    require(f->tracker.resetCommand(device, cb) && f->tracker.beginCommand(device, cb, f->beginInfo, true),
            "observe native reusable handle replacement");
    require(f->tracker.ssboRecordingTicket(device, cb, next) &&
            Access::generation(next) > Access::generation(recording), "equal raw CB handles carry distinct original recording generations");
    require(!f->tracker.beginCommand(device, cb, f->beginInfo, false) &&
            !f->tracker.ssboRecordingTicket(device, cb, next), "failed native begin never revives its prior token");
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        const VkCommandBufferUsageFlags flags = invalid == 0 ? VkCommandBufferUsageFlags(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) : 0u;
        auto bad = std::make_unique<Fixture>(true, flags,
            invalid == 1 ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            invalid == 2 ? VkCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_PROTECTED_BIT) : 0u, true);
        bad->tracker.ssboDeviceCreated(device, nativeDevice);
        if (invalid == 3) bad->tracker.freeCommands(device, pool, 1, &cb);
        require(!bad->tracker.ssboRecordingTicket(device, cb, next),
                "simultaneous, secondary, protected and retired original recordings cannot be adopted");
        require(!bad->tracker.poisoned(), "optional refusal does not poison ordinary state tracking");
    }
    for (unsigned changed = 0; changed < 2; ++changed) {
        auto reused = uniformFixture();
        require(reused->tracker.ssboDeviceCreated(device, nativeDevice) &&
                reused->tracker.ssboPipelineCreated(device, pipeline, recipe.provenance[5]),
                "associate original native object before handle reuse");
        if (!changed) {
            require(reused->tracker.destroyPipeline(device, pipeline), "retire original native pipeline generation");
            reused->makePipeline(pipeline, pl, "PS_DISTORT");
        } else {
            require(reused->tracker.observe([&](auto& core) {
                core.destroyPipelineLayout(device, pl);
                auto layout = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
                layout.setLayoutCount = 1; layout.pSetLayouts = &sl;
                core.createPipelineLayout(device, pl, layout);
            }), "retire and reuse original layout handle while pipeline remains alive");
        }
        reused->bind();
        require(reused->tracker.beginRenderPass(device, cb, VK_SUBPASS_CONTENTS_INLINE), "enter original render scope after handle reuse");
        reused->tracker.draw(device, cb);
        require(!reused->tracker.ssboTargetTicket(device, cb, recipe, target) && Access::empty(target),
                "same raw native pipeline/layout cannot substitute a new creation for owned clone provenance");
    }
    auto retiredDevice = uniformFixture();
    require(retiredDevice->tracker.ssboDeviceCreated(device, nativeDevice) &&
            retiredDevice->tracker.ssboDeviceDestroyed(device) &&
            !retiredDevice->tracker.ssboRecordingTicket(device, cb, next),
            "native device destruction removes its original mapping before reuse");
}
#endif
}

int main(int argc, char** argv) {
#ifdef FRAME_PIXEL_SSBO_TRANSACTION
    if (argc == 2 && !std::strcmp(argv[1], "ssbo")) {
        ssboSelectedPipelineIdentity();
        ssboNativeTickets();
        std::cout << "pixel SSBO live tracker bridge: " << checks << " checks passed; no GPU/frame/eye authority\n";
        return 0;
    }
#endif
    if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "legacy") && std::strcmp(argv[1], "uniform"))) {
        std::cerr << "Use legacy or uniform (no arguments runs both)\n"; return 2;
    }
    if (argc == 1 || !std::strcmp(argv[1], "legacy")) {
        baselineAndImmutableDraw(); beginAndPipelineGuards(); sourceCurrentness();
        sourceShapeGuards(); scopesAndUnknownPaths(); commandReuseAndLoss(); reusableRetirementIdentity();
        firstFailureTelemetry(); overlappingImageBinding(); reentryAndExceptionRefusal();
        concurrentFirstRealFailure(); concurrentDrawAndRetirement(); threadedResourceLifecycle();
        computeStateAdmissionHistory();
    }
    if (argc == 1 || !std::strcmp(argv[1], "uniform")) {
        uniformMetadataPositive(); uniformFinalDrawReplacement(); uniformProfileAndCurrentness(); uniformSourceAndAliasGuards();
    }
    std::cout << "pixel live tracker: " << checks << " checks passed; no GPU/native video association claimed\n";
}
