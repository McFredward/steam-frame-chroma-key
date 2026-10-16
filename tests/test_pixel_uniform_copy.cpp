// Local, inert helper controls and actual Vulkan buffer sequencing. No draw,
// native UBO discovery, Frame resource, image mapping or passthrough is tested.
#include "layer/pixel_uniform_copy.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace frame_uniform_copy;
namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class T> T handle(std::uintptr_t value) { return (T)value; }
Preparation preparation() {
    return {handle<VkBuffer>(70), 65536, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
}
Slice slice() {
    return {handle<VkBuffer>(80), 512, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 16, 64};
}
struct Fake {
    static Fake* active;
    unsigned resolutions = 0, unexpected = 0, barriers = 0, copies = 0, eventCount = 0;
    const char* missing = nullptr;
    std::array<char, MaximumSlices + 2> events{};
    std::array<VkPipelineStageFlags, 2> srcStages{}, dstStages{};
    std::array<std::uint32_t, 2> barrierCounts{};
    std::array<std::array<VkBufferMemoryBarrier, MaximumSlices>, 2> bufferBarriers{};
    std::array<VkBufferCopy, MaximumSlices> regions{};
    std::array<VkBuffer, MaximumSlices> sourceBuffers{}, targetBuffers{};
    VkCommandBuffer command = VK_NULL_HANDLE;
    Fake() { active = this; }
    void check(bool condition) noexcept { if (!condition) ++unexpected; }
    void commandEvent(VkCommandBuffer value, char type) noexcept {
        if (!command) command = value;
        check(command == value && eventCount < events.size());
        if (eventCount < events.size()) events[eventCount++] = type;
    }
    static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer command,
        VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags,
        std::uint32_t memoryCount, const VkMemoryBarrier* memory,
        std::uint32_t count, const VkBufferMemoryBarrier* buffers,
        std::uint32_t imageCount, const VkImageMemoryBarrier* images) {
        auto& f = *active;
        f.commandEvent(command, 'B');
        f.check(!flags && !memoryCount && !memory && !imageCount && !images
            && f.barriers < 2 && count > 0 && count <= MaximumSlices && buffers);
        if (f.barriers < 2) {
            f.srcStages[f.barriers] = src; f.dstStages[f.barriers] = dst;
            f.barrierCounts[f.barriers] = count;
            if (buffers && count <= MaximumSlices)
                std::copy_n(buffers, count, f.bufferBarriers[f.barriers].begin());
        }
        ++f.barriers;
    }
    static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer command, VkBuffer source,
        VkBuffer target, std::uint32_t count, const VkBufferCopy* region) {
        auto& f = *active;
        f.commandEvent(command, 'C');
        f.check(f.copies < MaximumSlices && count == 1 && region);
        if (f.copies < MaximumSlices && count == 1 && region) {
            f.sourceBuffers[f.copies] = source; f.targetBuffers[f.copies] = target;
            f.regions[f.copies] = *region;
        }
        ++f.copies;
    }
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name) {
        auto& f = *active; ++f.resolutions; f.check(device == handle<VkDevice>(1));
        if (f.missing && !std::strcmp(name, f.missing)) return nullptr;
        if (!std::strcmp(name, "vkCmdPipelineBarrier")) return reinterpret_cast<PFN_vkVoidFunction>(barrier);
        if (!std::strcmp(name, "vkCmdCopyBuffer")) return reinterpret_cast<PFN_vkVoidFunction>(copy);
        ++f.unexpected; return nullptr;
    }
};
Fake* Fake::active = nullptr;

void empty(const Dispatch& dispatch) {
    require(dispatch.sliceCount() == 0 && !dispatch.target() && !dispatch.bytes() && !dispatch.offset(),
            "refusal clears every published dispatch field");
    VkBufferCopy copy{1, 2, 3};
    require(!dispatch.region(0, copy) && !copy.srcOffset && !copy.dstOffset && !copy.size,
            "out-of-range region lookup clears output");
    require(!dispatch.source(0).buffer, "out-of-range source lookup exposes no handle");
}
void coldControls() {
    for (unsigned invalid = 0; invalid < 9; ++invalid) {
        Fake fake; CopyRecorder recorder; auto info = preparation();
        VkDevice device = handle<VkDevice>(1); auto original = Fake::resolve;
        switch (invalid) {
        case 0: device = VK_NULL_HANDLE; break;
        case 1: original = nullptr; break;
        case 2: info.target = VK_NULL_HANDLE; break;
        case 3: info.capacity = 0; break;
        case 4: info.capacity = 3; break;
        case 5: info.capacity = MaximumCapacity + 1; break;
        case 6: info.capacity = std::numeric_limits<VkDeviceSize>::max(); break;
        case 7: info.usage = 0; break;
        case 8: info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; break;
        }
        require(recorder.prepare(device, original, info) == Status::Refused
                && recorder.state() == State::Failed && !fake.resolutions && !fake.eventCount,
                "malformed cold bounds/target/usage refuse before resolving or recording");
        auto source = slice(); Dispatch dispatch;
        require(!recorder.admit(&source, 1, 0, dispatch), "failed preparation provides no source capability");
        empty(dispatch);
        require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, preparation()) == Status::Refused
                && !fake.resolutions, "failed helper cannot be silently rearmed");
    }
    for (const char* name : {"vkCmdPipelineBarrier", "vkCmdCopyBuffer"}) {
        Fake fake; fake.missing = name; CopyRecorder recorder;
        require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, preparation()) == Status::Refused
                && recorder.state() == State::Failed && fake.resolutions == 2 && !fake.eventCount && !fake.unexpected,
                "both required original functions must resolve without creating GPU resources");
    }
    Fake fake; CopyRecorder recorder;
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, preparation()) == Status::Ok
            && recorder.state() == State::Ready && fake.resolutions == 2 && !fake.eventCount,
            "cold setup resolves exactly two original command functions and emits no commands");
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, preparation()) == Status::Refused
            && fake.resolutions == 2, "prepared helper cannot change its target or functions");
}
void rangeControls() {
    Fake fake; CopyRecorder recorder; const auto original = preparation();
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, original) == Status::Ok, "range fixture preparation");
    const auto good = slice(); Dispatch dispatch;
    require(recorder.admit(&good, 1, 12, dispatch), "positive bounded slice can start at aligned tail");
    for (unsigned invalid = 0; invalid < 12; ++invalid) {
        auto source = good;
        switch (invalid) {
        case 0: source.buffer = VK_NULL_HANDLE; break;
        case 1: source.buffer = original.target; break;
        case 2: source.size = 0; break;
        case 3: source.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; break;
        case 4: source.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
        case 5: source.bytes = 0; break;
        case 6: source.effectiveOffset += 1; break;
        case 7: source.bytes += 1; break;
        case 8: source.effectiveOffset = 516; break;
        case 9: source.effectiveOffset = 512; source.bytes = 4; break;
        case 10: source.effectiveOffset = 508; source.bytes = 8; break;
        case 11: source.size = std::numeric_limits<VkDeviceSize>::max();
            source.effectiveOffset = source.size - 3; source.bytes = 4; break;
        }
        require(!recorder.admit(&source, 1, 12, dispatch) && recorder.state() == State::Ready,
                "invalid source handle/usage/alignment/range refuses without consuming the one shot");
        empty(dispatch);
        require(!fake.eventCount, "pure refusal records no native operation");
    }
    for (VkDeviceSize offset : {VkDeviceSize(1), VkDeviceSize(2), VkDeviceSize(3), original.capacity,
                               original.capacity + 4, std::numeric_limits<VkDeviceSize>::max() - 3}) {
        require(!recorder.admit(&good, 1, offset, dispatch), "unaligned, exhausted or oversized output tail refuses");
        empty(dispatch);
    }
    require(!recorder.admit(nullptr, 1, 0, dispatch), "missing source array refuses"); empty(dispatch);
    require(!recorder.admit(&good, 0, 0, dispatch), "zero slices refuse"); empty(dispatch);
    require(!recorder.admit(reinterpret_cast<const Slice*>(1), MaximumSlices + 1, 0, dispatch),
            "over-limit count refuses before dereferencing the array"); empty(dispatch);
    auto large = good; large.size = MaximumCopyBytes + 4; large.effectiveOffset = 0; large.bytes = large.size;
    require(!recorder.admit(&large, 1, 0, dispatch), "single copy cannot exceed the complete byte budget");
    large.bytes = MaximumCopyBytes;
    std::array<Slice, 2> overflow{large, good}; overflow[1].bytes = 4;
    require(!recorder.admit(overflow.data(), overflow.size(), 0, dispatch), "sum over byte budget refuses before overflow");
    require(recorder.admit(&large, 1, original.capacity - MaximumCopyBytes, dispatch)
            && dispatch.bytes() == MaximumCopyBytes, "exact byte and capacity boundary is accepted");
    auto end = good; end.effectiveOffset = good.size - 4; end.bytes = 4;
    require(recorder.admit(&end, 1, original.capacity - 4, dispatch), "last aligned source and destination word is valid");
    end.effectiveOffset = 0;
    require(recorder.admit(&end, 1, 0, dispatch), "zero offsets remain legal; only byte length must be positive");
    require(!fake.eventCount && !fake.unexpected, "all range admission stays pure");
}
void recordingControls() {
    Fake fake; CopyRecorder recorder, other;
    auto info = preparation(); info.capacity = MaximumCapacity;
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, info) == Status::Ok, "maximum cold capacity accepted");
    auto otherInfo = info; otherInfo.target = handle<VkBuffer>(71);
    require(other.prepare(handle<VkDevice>(1), Fake::resolve, otherInfo) == Status::Ok, "independent owner prepared");
    std::array<Slice, MaximumSlices> sources{};
    for (unsigned i = 0; i < sources.size(); ++i) {
        sources[i] = slice(); sources[i].buffer = handle<VkBuffer>(80 + i);
        sources[i].effectiveOffset = i * 12; sources[i].bytes = 4; sources[i].size = MaximumCopyBytes + i * 12;
    }
    sources.back().bytes = MaximumCopyBytes - 4 * (MaximumSlices - 1);
    // Repeated source handles/overlapping READ ranges are legal. Destination
    // ranges are generated separately and cannot overlap each other.
    sources[1].buffer = sources[0].buffer;
    sources[1].effectiveOffset = sources[0].effectiveOffset;
    Dispatch dispatch;
    const auto tail = MaximumCapacity - MaximumCopyBytes;
    require(recorder.admit(sources.data(), sources.size(), tail, dispatch)
            && dispatch.sliceCount() == MaximumSlices && dispatch.bytes() == MaximumCopyBytes
            && dispatch.offset() == tail && dispatch.target() == info.target && !fake.eventCount,
            "maximum count and total seal disjoint copies without a native call");
    const auto admitted = sources;
    sources.fill({}); // Later caller mutation must not change a sealed dispatch.
    auto exposed = dispatch.source(0); exposed.effectiveOffset += 100;
    require(exposed.effectiveOffset != dispatch.source(0).effectiveOffset
            && dispatch.source(0).effectiveOffset == admitted[0].effectiveOffset,
            "a public source accessor cannot mutate admitted metadata");
    require(other.record(handle<VkCommandBuffer>(99), dispatch) == Status::Refused
            && other.state() == State::Ready && !fake.eventCount, "a dispatch cannot cross owners/targets");
    require(recorder.record(handle<VkCommandBuffer>(99), Dispatch{}) == Status::Refused && !fake.eventCount,
            "invented dispatch cannot record");
    require(recorder.record(VK_NULL_HANDLE, dispatch) == Status::Refused && !fake.eventCount,
            "null command refuses without consuming admitted work");
    require(recorder.record(handle<VkCommandBuffer>(99), dispatch) == Status::Ok
            && recorder.state() == State::Recorded && fake.barriers == 2 && fake.copies == MaximumSlices
            && fake.eventCount == MaximumSlices + 2 && !fake.unexpected,
            "one record emits only two barriers and one buffer copy per slice");
    require(fake.events[0] == 'B' && fake.events[MaximumSlices + 1] == 'B'
            && std::all_of(fake.events.begin() + 1, fake.events.end() - 1, [](char e) { return e == 'C'; }),
            "exact native order is source barriers, all copies, output/execution barrier");
    require(fake.srcStages[0] == (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT)
            && fake.dstStages[0] == VK_PIPELINE_STAGE_TRANSFER_BIT && fake.barrierCounts[0] == MaximumSlices,
            "all native/host writes and preceding uniform reads precede every copy");
    require(fake.srcStages[1] == VK_PIPELINE_STAGE_TRANSFER_BIT
            && fake.dstStages[1] == (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT),
            "copy source reads precede later native GPU work, not only host reads");
    VkDeviceSize cursor = tail;
    for (unsigned i = 0; i < MaximumSlices; ++i) {
        const auto& barrier = fake.bufferBarriers[0][i];
        require(barrier.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER && !barrier.pNext
                && barrier.srcAccessMask == (VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT)
                && barrier.dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT
                && barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED
                && barrier.buffer == admitted[i].buffer && barrier.offset == admitted[i].effectiveOffset
                && barrier.size == admitted[i].bytes, "source barriers cover exact ranges without an ownership transfer");
        VkBufferCopy region{};
        require(dispatch.region(i, region) && fake.sourceBuffers[i] == admitted[i].buffer
                && fake.targetBuffers[i] == info.target && region.srcOffset == admitted[i].effectiveOffset
                && region.dstOffset == cursor && region.size == admitted[i].bytes
                && !std::memcmp(&fake.regions[i], &region, sizeof(region)), "each copied region preserves its effective source offset and disjoint tail");
        cursor += region.size;
    }
    const auto& output = fake.bufferBarriers[1][0];
    require(cursor == MaximumCapacity && fake.barrierCounts[1] == 1
            && output.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER && !output.pNext
            && output.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT && output.dstAccessMask == VK_ACCESS_HOST_READ_BIT
            && output.buffer == info.target && output.offset == tail && output.size == MaximumCopyBytes
            && output.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && output.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED,
            "host visibility applies only to the borrowed owned output; no CPU read/fence is invented");
    require(recorder.record(handle<VkCommandBuffer>(99), dispatch) == Status::Refused
            && fake.eventCount == MaximumSlices + 2, "recorded copy cannot be replayed through the helper");
    require(!recorder.admit(admitted.data(), admitted.size(), 0, dispatch), "recorded owner cannot issue a second dispatch"); empty(dispatch);
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, info) == Status::Refused
            && fake.resolutions == 4, "recorded owner never re-resolves or rearms");
}
void concurrentControls() {
    Fake fake; CopyRecorder recorder;
    require(recorder.prepare(handle<VkDevice>(1), Fake::resolve, preparation()) == Status::Ok, "concurrent fixture cold preparation");
    auto source = slice(); Dispatch dispatch;
    require(recorder.admit(&source, 1, 4, dispatch), "concurrent fixture sealed source");
    std::atomic<bool> go{false}; std::atomic<unsigned> ready{0};
    std::array<Status, 8> outcomes{}; std::array<std::thread, 8> threads;
    for (unsigned i = 0; i < threads.size(); ++i) threads[i] = std::thread([&, i] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield(); // Fixture only.
        outcomes[i] = recorder.record(handle<VkCommandBuffer>(100 + i), dispatch);
    });
    while (ready.load(std::memory_order_acquire) != threads.size()) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    require(std::count(outcomes.begin(), outcomes.end(), Status::Ok) == 1
            && recorder.state() == State::Recorded && fake.copies == 1 && fake.barriers == 2 && !fake.unexpected,
            "one CAS winner records; losing candidate threads issue no native commands");
}

struct Skip { std::string reason; };
void success(VkResult result, const char* message) {
    require(result == VK_SUCCESS, message);
}
struct Gpu {
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        bool coherent = false;
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memory{};
    std::array<Buffer, 3> buffers{};
    std::string name;
    ~Gpu() {
        if (device) {
            vkDeviceWaitIdle(device); // Fixture teardown only, never helper/render code.
            for (auto& b : buffers) {
                if (b.mapped) vkUnmapMemory(device, b.memory);
                if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
                if (b.memory) vkFreeMemory(device, b.memory, nullptr);
            }
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    void init() {
        VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "inert-uniform-copy-fixture"; app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{}; create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; create.pApplicationInfo = &app;
        const auto result = vkCreateInstance(&create, nullptr, &instance);
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER) throw Skip{"Vulkan driver unavailable"};
        success(result, "actual Vulkan instance creation");
        std::uint32_t count = 0; success(vkEnumeratePhysicalDevices(instance, &count, nullptr), "actual device count");
        if (!count) throw Skip{"No Vulkan physical device"};
        require(count <= 32, "bounded actual device count");
        std::vector<VkPhysicalDevice> devices(count);
        success(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "actual physical devices");
        int best = -1;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(candidate, &properties);
            std::uint32_t families = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            require(families <= 64, "bounded actual queue count");
            std::vector<VkQueueFamilyProperties> queues(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
            for (std::uint32_t i = 0; i < families; ++i) if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                const int preference = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                    : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (preference > best) { physical = candidate; family = i; best = preference; name = properties.deviceName; }
                break;
            }
        }
        if (!physical) throw Skip{"No ordinary graphics-capable Vulkan queue"};
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{}; queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{}; deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        success(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "actual Vulkan device creation");
        vkGetDeviceQueue(device, family, 0, &queue); require(queue != VK_NULL_HANDLE, "actual Vulkan queue");
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        VkCommandPoolCreateInfo poolInfo{}; poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = family;
        success(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "actual command pool");
        VkCommandBufferAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        success(vkAllocateCommandBuffers(device, &allocate, &command), "actual primary allocation");
        VkFenceCreateInfo fenceInfo{}; fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        success(vkCreateFence(device, &fenceInfo, nullptr, &fence), "actual owned fixture fence");
    }
    void make(unsigned index, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        auto& b = buffers[index]; b.size = bytes; b.usage = usage;
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        success(vkCreateBuffer(device, &info, nullptr, &b.buffer), "actual buffer creation");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, b.buffer, &requirements);
        std::uint32_t type = memory.memoryTypeCount;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                if (type == memory.memoryTypeCount || (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) type = i;
                if (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
            }
        if (type == memory.memoryTypeCount) throw Skip{"No host-visible memory for the bounded fixture"};
        b.coherent = (memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        success(vkAllocateMemory(device, &allocation, nullptr, &b.memory), "actual buffer allocation");
        success(vkBindBufferMemory(device, b.buffer, b.memory, 0), "actual buffer binding");
        success(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "actual fixture memory mapping");
    }
    void visibility(Buffer& b, bool hostWrite) {
        if (!b.coherent) {
            VkMappedMemoryRange range{}; range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = b.memory; range.size = VK_WHOLE_SIZE;
            success(hostWrite ? vkFlushMappedMemoryRanges(device, 1, &range) : vkInvalidateMappedMemoryRanges(device, 1, &range),
                    "actual noncoherent fixture visibility");
        }
    }
};
std::string jsonString(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c >= 32 ? static_cast<char>(c) : '?';
    }
    return out + '"';
}
void gpuControls() {
    Gpu gpu; gpu.init();
    constexpr VkBufferUsageFlags sourceUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    gpu.make(0, 4096, sourceUsage); gpu.make(1, 2048, sourceUsage);
    gpu.make(2, 1024, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::array<std::vector<std::uint8_t>, 2> initial;
    for (unsigned i = 0; i < 2; ++i) {
        auto& b = gpu.buffers[i]; initial[i].resize(static_cast<std::size_t>(b.size));
        for (std::size_t j = 0; j < initial[i].size(); ++j) initial[i][j] = static_cast<std::uint8_t>(j * 73 + (j >> 3) * 19 + i * 111);
        std::memcpy(b.mapped, initial[i].data(), initial[i].size()); gpu.visibility(b, true);
    }
    auto& target = gpu.buffers[2]; std::vector<std::uint8_t> expected(static_cast<std::size_t>(target.size), 0xCD);
    std::memcpy(target.mapped, expected.data(), expected.size()); gpu.visibility(target, true);
    const auto source = [&](unsigned index, VkDeviceSize base, VkDeviceSize dynamic, VkDeviceSize bytes) {
        const auto& b = gpu.buffers[index]; return Slice{b.buffer, b.size, b.usage, base + dynamic, bytes};
    };
    const std::array<Slice, MaximumSlices> sources{{source(0, 16, 256, 68), source(1, 32, 512, 124),
        source(0, 2048, 64, 512), source(1, 0, 0, 4), source(0, 4092, 0, 4)}};
    constexpr VkDeviceSize tail = 76;
    VkDeviceSize cursor = tail;
    for (const auto& s : sources) {
        const unsigned i = s.buffer == gpu.buffers[0].buffer ? 0 : 1;
        std::copy_n(initial[i].begin() + static_cast<std::ptrdiff_t>(s.effectiveOffset), static_cast<std::size_t>(s.bytes),
                    expected.begin() + static_cast<std::ptrdiff_t>(cursor));
        cursor += s.bytes;
    }
    CopyRecorder recorder; Dispatch dispatch;
    require(recorder.prepare(gpu.device, vkGetDeviceProcAddr, {target.buffer, target.size, target.usage}) == Status::Ok,
            "production recorder prepares with actual original Vulkan functions and borrowed target");
    require(recorder.admit(sources.data(), sources.size(), tail, dispatch) && dispatch.bytes() == 712,
            "actual multiple buffers and varied effective base/dynamic offsets admitted");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    success(vkBeginCommandBuffer(gpu.command, &begin), "actual primary Begin");
    require(recorder.record(gpu.command, dispatch) == Status::Ok, "production helper records actual copies");
    const std::array<std::uint32_t, 2> later{{0xA53C0FD1u, 0x16924BE7u}};
    std::array<VkBufferMemoryBarrier, 2> host{};
    for (unsigned i = 0; i < 2; ++i) {
        // Causal ordering: output must retain A even though subsequent native
        // GPU commands overwrite both source buffers with B in this SAME CB.
        vkCmdFillBuffer(gpu.command, gpu.buffers[i].buffer, 0, gpu.buffers[i].size, later[i]);
        host[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host[i].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host[i].srcQueueFamilyIndex = host[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host[i].buffer = gpu.buffers[i].buffer; host[i].size = gpu.buffers[i].size;
    }
    vkCmdPipelineBarrier(gpu.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, host.size(), host.data(), 0, nullptr);
    success(vkEndCommandBuffer(gpu.command), "actual primary End");
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &gpu.command;
    success(vkQueueSubmit(gpu.queue, 1, &submit, gpu.fence), "actual native fixture submission");
    success(vkWaitForFences(gpu.device, 1, &gpu.fence, VK_TRUE, 5000000000ULL), "actual owned completion fence");
    for (auto& b : gpu.buffers) gpu.visibility(b, false);
    require(!std::memcmp(target.mapped, expected.data(), expected.size()),
            "fenced output equals exact A slices and preserves every untouched prefix/suffix byte");
    for (unsigned i = 0; i < 2; ++i) {
        std::vector<std::uint8_t> changed(static_cast<std::size_t>(gpu.buffers[i].size));
        for (std::size_t j = 0; j < changed.size(); j += 4) std::memcpy(changed.data() + j, &later[i], 4);
        require(!std::memcmp(gpu.buffers[i].mapped, changed.data(), changed.size()),
                "later actual GPU source B writes completed; output is not a deferred source read");
    }
    std::cout << "{\"uniform_copy_gpu_checks\":" << checks << ",\"driver\":" << jsonString(gpu.name)
        << ",\"slices\":5,\"copied_bytes\":712,\"target_bytes_checked\":1024,\"later_source_bytes_checked\":6144,"
        "\"effective_offsets\":[272,544,2112,0,4092],\"copied_A_before_later_native_B\":true,"
        "\"draw_coherence_proven\":false,\"native_source_association_proven\":false,\"frame_mapping_proven\":false,\"passthrough_authorized\":false}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 || (std::strcmp(argv[1], "controls") && std::strcmp(argv[1], "gpu")))
            throw std::runtime_error("Use controls or gpu");
        if (!std::strcmp(argv[1], "gpu")) gpuControls();
        else {
            coldControls(); rangeControls(); recordingControls(); concurrentControls();
            std::cout << "{\"uniform_copy_controls\":" << checks
                << ",\"one_shot_winner\":true,\"gpu_objects_created\":false,\"native_association_proven\":false,\"passthrough_authorized\":false}\n";
        }
        return 0;
    } catch (const Skip& skipped) {
        std::cout << "{\"gpu_skipped\":true,\"reason\":" << jsonString(skipped.reason)
                  << ",\"native_association_proven\":false,\"passthrough_authorized\":false}\n";
        return 77;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (checks=" << checks << ")\n";
        return 1;
    }
}
