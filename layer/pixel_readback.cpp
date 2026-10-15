#include "pixel_readback.h"

namespace frame_pixels {
bool Readback::describe(const Config& c, Layout& out) noexcept {
    out = {};
    if (!c.extent.width || !c.extent.height) return false;
    Layout value{};
    value.format = c.format;
    value.extent = c.extent;
    const VkDeviceSize pixels = VkDeviceSize(c.extent.width) * c.extent.height;
    if (c.mode == Mode::NativeP010 && c.format == NativeP010) {
        if ((c.extent.width & 1u) || (c.extent.height & 1u)
            || pixels > MaximumBytes / 3u) return false;
        value.planeCount = 2;
        value.bytes = pixels * 3u;
        value.planes[0].imageSubresource = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1};
        value.planes[0].imageExtent = {c.extent.width, c.extent.height, 1};
        // Compatible plane0 texel size is 2, plane1 R10X6G10X6 is 4 bytes.
        // Even WxH makes offset0+plane0bytes a multiple of both 4 and 2.
        value.planes[1].bufferOffset = pixels * 2u;
        value.planes[1].imageSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1};
        value.planes[1].imageExtent = {c.extent.width / 2u, c.extent.height / 2u, 1};
    } else if (c.mode == Mode::OwnedRgbaFixture && c.format == VK_FORMAT_R8G8B8A8_UNORM) {
        if (pixels > MaximumBytes / 4u) return false;
        value.planeCount = 1;
        value.bytes = pixels * 4u;
        value.planes[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        value.planes[0].imageExtent = {c.extent.width, c.extent.height, 1};
    } else return false;
    out = value;
    return true;
}

Status Readback::prepare(VkDevice device, PFN_vkGetDeviceProcAddr next,
                         const VkPhysicalDeviceMemoryProperties& memory, const Config& config) noexcept {
    State expected = State::Empty;
    if (!state_.compare_exchange_strong(expected, State::Preparing, std::memory_order_acq_rel)) return Status::Refused;
    dynamicExtent_ = config.extent.width == 0 && config.extent.height == 0;
    config_ = config;
    if (dynamicExtent_ && ((config_.mode == Mode::NativeP010 && config.format == NativeP010)
        || (config_.mode == Mode::OwnedRgbaFixture && config.format == VK_FORMAT_R8G8B8A8_UNORM))) {
        layout_.format = config.format;
        layout_.bytes = MaximumBytes;
    } else if (!describe(config, layout_)) {
        state_.store(State::Failed, std::memory_order_release);
        return Status::Refused;
    }
    if (!device || !next) {
        state_.store(State::Failed, std::memory_order_release);
        return Status::Refused;
    }
    device_ = device;
#define RESOLVE(member, name) p_.member = reinterpret_cast<PFN_##name>(next(device, #name)); if (!p_.member) { fail(VK_ERROR_INITIALIZATION_FAILED); return Status::Refused; }
    RESOLVE(createBuffer, vkCreateBuffer)
    RESOLVE(destroyBuffer, vkDestroyBuffer)
    RESOLVE(getBufferMemoryRequirements, vkGetBufferMemoryRequirements)
    RESOLVE(allocateMemory, vkAllocateMemory)
    RESOLVE(freeMemory, vkFreeMemory)
    RESOLVE(bindBufferMemory, vkBindBufferMemory)
    RESOLVE(mapMemory, vkMapMemory)
    RESOLVE(unmapMemory, vkUnmapMemory)
    RESOLVE(createFence, vkCreateFence)
    RESOLVE(destroyFence, vkDestroyFence)
    RESOLVE(barrier, vkCmdPipelineBarrier)
    RESOLVE(copy, vkCmdCopyImageToBuffer)
    RESOLVE(submit, vkQueueSubmit)
    RESOLVE(fenceStatus, vkGetFenceStatus)
    RESOLVE(invalidate, vkInvalidateMappedMemoryRanges)
#undef RESOLVE
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = layout_.bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = p_.createBuffer(device_, &info, nullptr, &buffer_);
    if (r != VK_SUCCESS) { fail(r); return Status::VulkanFailure; }
    VkMemoryRequirements requirements{};
    p_.getBufferMemoryRequirements(device_, buffer_, &requirements);
    if (!requirements.size || requirements.size > MaximumBytes) {
        destroyOwned(); fail(VK_ERROR_OUT_OF_DEVICE_MEMORY); return Status::Refused;
    }
    std::uint32_t selected = VK_MAX_MEMORY_TYPES;
    // Coherent is preferred; a bounded noncoherent whole-allocation invalidate
    // is permitted only on the background polling path after fence success.
    for (unsigned pass = 0; pass < 2 && selected == VK_MAX_MEMORY_TYPES; ++pass)
        for (std::uint32_t i = 0; i < memory.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i) {
            const auto flags = memory.memoryTypes[i].propertyFlags;
            if ((requirements.memoryTypeBits & (1u << i)) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                && (pass || (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) { selected = i; break; }
        }
    if (selected == VK_MAX_MEMORY_TYPES) {
        destroyOwned(); fail(VK_ERROR_FEATURE_NOT_PRESENT); return Status::Refused;
    }
    coherent_ = (memory.memoryTypes[selected].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = selected;
    r = p_.allocateMemory(device_, &allocation, nullptr, &memory_);
    if (r == VK_SUCCESS) r = p_.bindBufferMemory(device_, buffer_, memory_, 0);
    if (r == VK_SUCCESS) r = p_.mapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped_);
    if (r == VK_SUCCESS) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; // deliberately unsignaled
        r = p_.createFence(device_, &fenceInfo, nullptr, &fence_);
    }
    if (r != VK_SUCCESS) { destroyOwned(); fail(r); return Status::VulkanFailure; }
    state_.store(State::Ready, std::memory_order_release);
    return Status::Ok;
}

Status Readback::record(VkCommandBuffer command, const Source& s, const Admission& a) noexcept {
    // Check every fact before emitting ANY command. Source flags=0 excludes
    // protected/disjoint/sparse/aliasing cases this bounded experiment has not
    // established. Source mip/layer zero are the only selected subresources.
    if (state() != State::Ready) return Status::Refused;
    Layout selected{};
    Config candidate = config_;
    candidate.extent = {s.extent.width, s.extent.height};
    if (!describe(candidate, selected)) return Status::Refused;
    if (!command || !s.image || !s.generation || !a.commandGeneration
        || !a.complete || !a.primary || !a.outsideRenderPass || !a.afterMatchingDistortDraw
        || !a.queueOwnsImage || !a.beforeExternalRelease || !a.ordinaryUnprotected
        || (!a.oneTimeSubmit && !a.retirementRequired) || !a.noSimultaneousUse
        || s.format != config_.format
        || (!dynamicExtent_ && (s.extent.width != config_.extent.width || s.extent.height != config_.extent.height))
        || s.extent.depth != 1
        || s.type != VK_IMAGE_TYPE_2D || s.mipLevels != 1 || s.arrayLayers != 1
        || s.samples != VK_SAMPLE_COUNT_1_BIT || s.flags != 0
        || (s.usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
             != (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        || s.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return Status::Refused;
    State expected = State::Ready;
    if (!state_.compare_exchange_strong(expected, State::Recording, std::memory_order_acq_rel)) return Status::Refused;
    layout_ = selected;
    command_ = command; commandGeneration_ = a.commandGeneration;
    source_ = s.image; sourceGeneration_ = s.generation;
    oneTimeSubmit_ = a.oneTimeSubmit; retirementRequired_ = a.retirementRequired;
    recorded_ = true;
    VkImageMemoryBarrier image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image.image = s.image;
    image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    p_.barrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               0, 0, nullptr, 0, nullptr, 1, &image);
    p_.copy(command, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_,
            layout_.planeCount, layout_.planes.data());
    image.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkBufferMemoryBarrier buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer.srcQueueFamilyIndex = buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer.buffer = buffer_; buffer.size = layout_.bytes;
    p_.barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
               0, 0, nullptr, 1, &buffer, 1, &image);
    state_.store(State::Recorded, std::memory_order_release);
    return Status::Ok;
}

Status Readback::submitCompletion(VkQueue queue, VkCommandBuffer command,
                                  std::uint64_t generation, VkResult nativeResult) noexcept {
    if (state() != State::Recorded || !queue || command != command_ || generation != commandGeneration_)
        return Status::Refused;
    State expected = State::Recorded;
    if (!state_.compare_exchange_strong(expected, State::Submitting, std::memory_order_acq_rel)) return Status::Refused;
    if (nativeResult != VK_SUCCESS) { fail(nativeResult); return Status::VulkanFailure; }
    // Queue operations are externally synchronized by the existing native queue
    // hook. A zero-submit fence follows all earlier work on THIS queue.
    const auto r = p_.submit(queue, 0, nullptr, fence_);
    if (r != VK_SUCCESS) { fail(r); return Status::VulkanFailure; }
    state_.store(State::Pending, std::memory_order_release);
    return Status::Pending;
}

Status Readback::retireCommand(VkCommandBuffer command, std::uint64_t generation,
                               bool nativeSuccessfulReset) noexcept {
    const auto s = state();
    if (s != State::Recorded && s != State::Submitting && s != State::Pending
        && s != State::Polling && s != State::AwaitingRetirement && s != State::Complete
        && s != State::Failed) return Status::Refused;
    if (!nativeSuccessfulReset || !command || command != command_ || !generation
        || generation != commandGeneration_) return Status::Refused;
    retired_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status Readback::poll(View& out) noexcept {
    out = {};
    auto expected = state();
    if (expected != State::Complete) {
        if (expected != State::Pending && expected != State::AwaitingRetirement)
            return expected == State::Failed ? Status::VulkanFailure : Status::NotReady;
        const bool fenceAlreadyReady = expected == State::AwaitingRetirement;
        if (!state_.compare_exchange_strong(expected, State::Polling, std::memory_order_acq_rel))
            return Status::NotReady;
        if (!fenceAlreadyReady) {
            const auto r = p_.fenceStatus(device_, fence_);
            if (r == VK_NOT_READY) { state_.store(State::Pending, std::memory_order_release); return Status::Pending; }
            if (r != VK_SUCCESS) { fail(r); return Status::VulkanFailure; }
        }
        // A first-submission fence alone cannot protect CPU reads from later
        // native CB replay. Native successful retirement supplies the separate
        // NOT-pending/no-future-recorded-copy proof; never read before BOTH.
        if (retirementRequired_ && !retired_.load(std::memory_order_acquire)) {
            state_.store(State::AwaitingRetirement, std::memory_order_release);
            return Status::Pending;
        }
        if (!coherent_) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = memory_; range.size = VK_WHOLE_SIZE;
            const auto r = p_.invalidate(device_, 1, &range);
            if (r != VK_SUCCESS) { fail(r); return Status::VulkanFailure; }
        }
        state_.store(State::Complete, std::memory_order_release);
    }
    out.data = static_cast<const std::uint8_t*>(mapped_); out.bytes = layout_.bytes;
    out.layout = layout_; out.source = source_; out.sourceGeneration = sourceGeneration_;
    out.commandGeneration = commandGeneration_;
    out.oneTimeSubmit = oneTimeSubmit_; out.retirementRequired = retirementRequired_;
    out.generationRetired = retired_.load(std::memory_order_acquire);
    return Status::Complete;
}

void Readback::fail(VkResult r) noexcept {
    lastResult_.store(r, std::memory_order_relaxed);
    state_.store(State::Failed, std::memory_order_release);
}
void Readback::destroyOwned() noexcept {
    if (mapped_ && p_.unmapMemory) p_.unmapMemory(device_, memory_);
    if (fence_ && p_.destroyFence) p_.destroyFence(device_, fence_, nullptr);
    if (buffer_ && p_.destroyBuffer) p_.destroyBuffer(device_, buffer_, nullptr);
    if (memory_ && p_.freeMemory) p_.freeMemory(device_, memory_, nullptr);
    mapped_ = nullptr; fence_ = VK_NULL_HANDLE; buffer_ = VK_NULL_HANDLE; memory_ = VK_NULL_HANDLE;
}
Status Readback::release(bool deviceQuiescent) noexcept {
    const auto s = state();
    if (s == State::Preparing || s == State::Recording || s == State::Submitting || s == State::Polling)
        return Status::UnsafeToRelease;
    if (recorded_ && (s != State::Complete || retirementRequired_) && !deviceQuiescent) return Status::UnsafeToRelease;
    destroyOwned();
    state_.store(State::Released, std::memory_order_release);
    return Status::Ok;
}
}
