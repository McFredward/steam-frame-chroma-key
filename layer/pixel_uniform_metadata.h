#pragma once

// Background-only companion metadata. These are producer observations, not an
// independent native eye/UV/video-frame association or permission for alpha.
#include "pixel_capture_tracker.h"
#include "pixel_uniform_copy.h"
#include "pixel_readback.h"
#include "pixel_snapshot_io.h"
#include <iomanip>
#include <ostream>

namespace frame_uniform_metadata {

inline bool compatible(const frame_pixel_capture::Candidate& candidate,
                       const frame_uniform_copy::Dispatch& dispatch,
                       const frame_pixels::View& view, VkDeviceSize sampledEnd) noexcept {
    const auto& plan = candidate.uniformPlan;
    if (!candidate.uniformFinalDraw || !plan.metadataCandidate || !plan.totalBytes ||
        plan.totalBytes > frame_pixel_uniform::MaximumTotalBytes ||
        !candidate.uniformPipeline.handle || !candidate.uniformPipeline.generation ||
        !candidate.uniformDrawOrdinal || !frame_pixel_uniform::nativeStagesMatch(plan.stages) ||
        dispatch.sliceCount() != plan.slices.size() || dispatch.bytes() != plan.totalBytes ||
        !view.data || !view.commandGeneration || !view.sourceGeneration ||
        view.commandGeneration != candidate.commandGeneration ||
        view.sourceGeneration != candidate.binding.image.identity.generation ||
        frame_bindings::handle(view.source) != candidate.binding.image.identity.handle ||
        !frame_bindings::same(candidate.binding.view.image,candidate.binding.image.identity) ||
        !candidate.binding.view.identity.handle || !candidate.binding.view.identity.generation ||
        plan.device != candidate.device ||
        plan.command.handle != frame_bindings::handle(candidate.command) ||
        !((view.oneTimeSubmit && !view.retirementRequired) ||
          (!view.oneTimeSubmit && view.retirementRequired && view.generationRetired)) ||
        (dispatch.offset() & 3u) || view.capacity > frame_uniform_copy::MaximumCapacity ||
        dispatch.offset() < sampledEnd || dispatch.offset() > view.capacity ||
        dispatch.bytes() > view.capacity - dispatch.offset()) return false;
    VkDeviceSize cursor = 0;
    for (std::size_t i = 0; i < plan.slices.size(); ++i) {
        const auto& slice = plan.slices[i]; const auto source = dispatch.source(i);
        VkBufferCopy copy{};
        if (slice.binding != frame_pixel_uniform::UniformBindings[i] ||
            slice.destinationOffset != cursor || !slice.effectiveRange ||
            slice.effectiveRange > frame_pixel_uniform::MaximumSliceBytes ||
            slice.effectiveRange > plan.totalBytes - cursor || !dispatch.region(i, copy) ||
            frame_bindings::handle(source.buffer) != slice.buffer.identity.handle ||
            source.size != slice.buffer.size || source.usage != slice.buffer.usage ||
            source.effectiveOffset != slice.effectiveOffset || source.bytes != slice.effectiveRange ||
            copy.srcOffset != slice.effectiveOffset || copy.dstOffset != dispatch.offset() + cursor ||
            copy.size != slice.effectiveRange) return false;
        cursor += slice.effectiveRange;
    }
    return cursor == plan.totalBytes;
}

inline void hex(std::ostream& out, std::uint64_t value) {
    out << '"' << std::hex << value << std::dec << '"';
}
inline void identity(std::ostream& out, frame_bindings::Identity value) {
    out << "\"handle\":"; hex(out, value.handle);
    out << ",\"generation\":" << value.generation;
}
inline bool append(std::ostream& out, const frame_pixel_capture::Candidate& candidate,
                   const frame_uniform_copy::Dispatch& dispatch,
                   const frame_pixels::View& view, VkDeviceSize sampledEnd,
                   const std::string& captureId) {
    if (!compatible(candidate, dispatch, view, sampledEnd) || captureId.size() != 32) return false;
    bool nonzero = false;
    for (const char c : captureId) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        nonzero = nonzero || c != '0';
    }
    if (!nonzero) return false;
    const auto& plan = candidate.uniformPlan;
    const auto* data = view.data + dispatch.offset();
    out << ",\"native_uniform_control\":{\"schema\":\"frame-native-uniform-control-v1\","
           "\"file\":\"uniform-bytes.bin\",\"capture_id\":\"" << captureId
        << "\",\"command_generation\":" << view.commandGeneration
        << ",\"image_generation\":" << view.sourceGeneration
        << ",\"view_generation\":" << candidate.binding.view.identity.generation
        << ",\"pipeline\":{";
    identity(out, candidate.uniformPipeline);
    out << ",\"layout_handle\":"; hex(out, plan.pipelineLayout.handle);
    out << ",\"layout_generation\":" << plan.pipelineLayout.generation << "},\"stages\":[";
    const frame_trace::Stage* stages[]{&plan.stages.vertex, &plan.stages.fragment};
    for (unsigned i = 0; i < 2; ++i) {
        if (i) out << ',';
        const auto& stage = *stages[i];
        out << "{\"stage\":" << stage.stage << ",\"module\":"; hex(out, stage.module);
        out << ",\"generation\":" << stage.generation << ",\"fnv1a64\":\""
            << std::hex << std::setfill('0') << std::setw(16) << stage.hash << std::dec
            << "\",\"bytes\":" << stage.bytes << ",\"entry\":\"" << stage.entry << "\"}";
    }
    out << "],\"selected_draw_ordinal\":" << candidate.uniformDrawOrdinal << ",\"set\":{";
    identity(out, plan.set);
    out << ",\"layout_handle\":"; hex(out, plan.setLayout.handle);
    out << ",\"layout_generation\":" << plan.setLayout.generation
        << ",\"revision\":" << plan.setRevision
        << ",\"command_binding_revision\":" << plan.commandBindingRevision
        << "},\"reobserved_set_revision\":" << plan.setRevision
        << ",\"reobserved_command_binding_revision\":" << plan.commandBindingRevision
        << ",\"byte_length\":" << dispatch.bytes() << ",\"sha256\":\""
        << frame_pixel_io::digest(data, dispatch.bytes())
        << "\",\"staging_offset\":" << dispatch.offset()
        << ",\"shared_storage_capacity\":" << view.capacity
        << ",\"guards\":{\"same_primary_after_native_render_scope\":true,"
           "\"selected_draw_is_last\":true,\"no_intervening_draw\":true,"
           "\"no_intervening_buffer_write\":true,\"buffer_generations_revalidated\":true,"
           "\"descriptor_revisions_revalidated\":true,\"no_known_overlapping_allocation\":true,"
           "\"queue_ownership_revalidated\":true},\"slices\":[";
    for (std::size_t i = 0; i < plan.slices.size(); ++i) {
        if (i) out << ',';
        const auto& slice = plan.slices[i]; const auto& buffer = slice.buffer;
        out << "{\"binding\":" << slice.binding << ",\"descriptor_type\":" << int(slice.descriptorType)
            << ",\"descriptor_stages\":" << slice.descriptorStages
            << ",\"descriptor_revision\":" << slice.descriptorRevision << ",\"buffer_handle\":";
        hex(out, buffer.identity.handle);
        out << ",\"buffer_generation\":" << buffer.identity.generation
            << ",\"buffer_size\":" << buffer.size << ",\"buffer_usage\":" << buffer.usage
            << ",\"allocation_handle\":"; hex(out, buffer.allocation.identity.handle);
        out << ",\"allocation_generation\":" << buffer.allocation.identity.generation
            << ",\"allocation_size\":" << buffer.allocation.allocationSize
            << ",\"memory_offset\":" << buffer.memoryOffset
            << ",\"descriptor_offset\":" << slice.descriptorOffset
            << ",\"descriptor_range\":" << slice.descriptorRange
            << ",\"dynamic_offset\":" << slice.dynamicOffset
            << ",\"effective_offset\":" << slice.effectiveOffset
            << ",\"effective_range\":" << slice.effectiveRange
            << ",\"file_offset\":" << slice.destinationOffset
            << ",\"byte_length\":" << slice.effectiveRange << ",\"sha256\":\""
            << frame_pixel_io::digest(data + slice.destinationOffset, slice.effectiveRange) << "\"}";
    }
    out << "]}";
    return bool(out);
}

} // namespace frame_uniform_metadata
