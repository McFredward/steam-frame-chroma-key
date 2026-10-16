#include "pixel_uniform_copy.h"

namespace frame_uniform_copy {

Status CopyRecorder::prepare(VkDevice device, PFN_vkGetDeviceProcAddr original,
                             const Preparation& info) noexcept {
    State expected = State::Empty;
    if (!state_.compare_exchange_strong(expected, State::Preparing, std::memory_order_acq_rel))
        return Status::Refused;
    if (!device || !original || !info.target || info.capacity < 4 || info.capacity > MaximumCapacity
        || !(info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        state_.store(State::Failed, std::memory_order_release);
        return Status::Refused;
    }
    const auto barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(original(device, "vkCmdPipelineBarrier"));
    const auto copy = reinterpret_cast<PFN_vkCmdCopyBuffer>(original(device, "vkCmdCopyBuffer"));
    if (!barrier || !copy) {
        state_.store(State::Failed, std::memory_order_release);
        return Status::Refused;
    }
    barrier_ = barrier; copy_ = copy; target_ = info.target; capacity_ = info.capacity;
    state_.store(State::Ready, std::memory_order_release);
    return Status::Ok;
}

bool CopyRecorder::admit(const Slice* slices, std::uint32_t count,
                         VkDeviceSize tailOffset, Dispatch& out) const noexcept {
    out = {};
    if (state() != State::Ready || !slices || !count || count > MaximumSlices
        || (tailOffset & 3u) || tailOffset > capacity_)
        return false;
    Dispatch candidate;
    VkDeviceSize total = 0;
    constexpr VkBufferUsageFlags required = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& source = slices[index];
        if (!source.buffer || source.buffer == target_ || (source.usage & required) != required
            || !source.size || !source.bytes || (source.effectiveOffset & 3u) || (source.bytes & 3u)
            || source.effectiveOffset > source.size || source.bytes > source.size - source.effectiveOffset
            || source.bytes > MaximumCopyBytes - total)
            return false;
        total += source.bytes; // Bounded by MaximumCopyBytes before addition.
        candidate.sources_[index] = source;
    }
    if (total > capacity_ - tailOffset) return false;
    VkDeviceSize cursor = tailOffset;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& source = candidate.sources_[index];
        candidate.copies_[index] = {source.effectiveOffset, cursor, source.bytes};
        cursor += source.bytes; // Entire disjoint tail already proved <= capacity.
    }
    candidate.owner_ = this; candidate.target_ = target_; candidate.capacity_ = capacity_;
    candidate.offset_ = tailOffset; candidate.bytes_ = total; candidate.count_ = count;
    out = candidate;
    return true;
}

Status CopyRecorder::record(VkCommandBuffer command, const Dispatch& dispatch) noexcept {
    if (!command || dispatch.owner_ != this || dispatch.target_ != target_ || dispatch.capacity_ != capacity_)
        return Status::Refused;
    State expected = State::Ready;
    if (!state_.compare_exchange_strong(expected, State::Recording, std::memory_order_acq_rel))
        return Status::Refused;
    std::array<VkBufferMemoryBarrier, MaximumSlices> sources{};
    for (std::uint32_t index = 0; index < dispatch.count_; ++index) {
        const auto& source = dispatch.sources_[index];
        auto& barrier = sources[index];
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = source.buffer; barrier.offset = source.effectiveOffset; barrier.size = source.bytes;
    }
    barrier_(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, dispatch.count_, sources.data(), 0, nullptr);
    for (std::uint32_t index = 0; index < dispatch.count_; ++index)
        copy_(command, dispatch.sources_[index].buffer, target_, 1, &dispatch.copies_[index]);
    VkBufferMemoryBarrier output{};
    output.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    output.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    output.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    output.srcQueueFamilyIndex = output.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    output.buffer = target_; output.offset = dispatch.offset_; output.size = dispatch.bytes_;
    // The execution dependency covers ALL prior transfer accesses, including
    // source reads before later native writes. HOST_READ visibility applies
    // only to our output tail, whose publication still needs the caller's fence
    // and exact native-generation retirement. No source buffer is modified.
    barrier_(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
             0, 0, nullptr, 1, &output, 0, nullptr);
    state_.store(State::Recorded, std::memory_order_release);
    return Status::Ok;
}

} // namespace frame_uniform_copy
