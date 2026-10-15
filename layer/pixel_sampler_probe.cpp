#include "pixel_sampler_probe.h"
#include "pixel_sampler_probe_spv.h"

namespace frame_pixel_sampler {
namespace {
bool identity(const VkComponentMapping& v) noexcept {
    return v.r == VK_COMPONENT_SWIZZLE_IDENTITY && v.g == VK_COMPONENT_SWIZZLE_IDENTITY
        && v.b == VK_COMPONENT_SWIZZLE_IDENTITY && v.a == VK_COMPONENT_SWIZZLE_IDENTITY;
}
}

VkSamplerYcbcrConversionCreateInfo SamplerProbe::profile() noexcept {
    VkSamplerYcbcrConversionCreateInfo v{};
    v.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    v.format = P010;
    v.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    v.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    v.xChromaOffset = v.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
    v.chromaFilter = VK_FILTER_LINEAR;
    return v;
}

bool SamplerProbe::matchesProfile(const VkSamplerYcbcrConversionCreateInfo& v) noexcept {
    return v.sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO && !v.pNext
        && v.format == P010 && v.ycbcrModel == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY
        && v.ycbcrRange == VK_SAMPLER_YCBCR_RANGE_ITU_FULL && identity(v.components)
        && v.xChromaOffset == VK_CHROMA_LOCATION_MIDPOINT
        && v.yChromaOffset == VK_CHROMA_LOCATION_MIDPOINT && v.chromaFilter == VK_FILTER_LINEAR
        && v.forceExplicitReconstruction == VK_FALSE;
}

Status SamplerProbe::prepare(VkDevice device, PFN_vkGetDeviceProcAddr original,
                             const Preparation& preparation) noexcept {
    State expected = State::Empty;
    if (!state_.compare_exchange_strong(expected, State::Preparing, std::memory_order_acq_rel))
        return Status::Refused;
    if (!device || !original || !preparation.targetBuffer || preparation.capacity < 28
        || preparation.capacity > MaximumBytes || !preparation.samplerYcbcrConversionEnabled
        || !preparation.combinedImageSamplerDescriptorCount
        || preparation.combinedImageSamplerDescriptorCount > 16)
        return fail(VK_ERROR_FEATURE_NOT_PRESENT);
    device_ = device;
    buffer_ = preparation.targetBuffer;
    capacity_ = preparation.capacity;
#define PROC(member, name) p_.member = reinterpret_cast<PFN_##name>(original(device, #name)); if (!p_.member) return fail(VK_ERROR_INITIALIZATION_FAILED);
    p_.createConversion = reinterpret_cast<PFN_vkCreateSamplerYcbcrConversion>(original(device, "vkCreateSamplerYcbcrConversion"));
    if (!p_.createConversion) p_.createConversion = reinterpret_cast<PFN_vkCreateSamplerYcbcrConversion>(original(device, "vkCreateSamplerYcbcrConversionKHR"));
    p_.destroyConversion = reinterpret_cast<PFN_vkDestroySamplerYcbcrConversion>(original(device, "vkDestroySamplerYcbcrConversion"));
    if (!p_.destroyConversion) p_.destroyConversion = reinterpret_cast<PFN_vkDestroySamplerYcbcrConversion>(original(device, "vkDestroySamplerYcbcrConversionKHR"));
    if (!p_.createConversion || !p_.destroyConversion) return fail(VK_ERROR_INITIALIZATION_FAILED);
    PROC(createSampler, vkCreateSampler) PROC(destroySampler, vkDestroySampler)
    PROC(createLayout, vkCreateDescriptorSetLayout) PROC(destroyLayout, vkDestroyDescriptorSetLayout)
    PROC(createPool, vkCreateDescriptorPool) PROC(destroyPool, vkDestroyDescriptorPool)
    PROC(allocateSets, vkAllocateDescriptorSets) PROC(updateSets, vkUpdateDescriptorSets)
    PROC(createShader, vkCreateShaderModule) PROC(destroyShader, vkDestroyShaderModule)
    PROC(createPipelineLayout, vkCreatePipelineLayout) PROC(destroyPipelineLayout, vkDestroyPipelineLayout)
    PROC(createPipelines, vkCreateComputePipelines) PROC(destroyPipeline, vkDestroyPipeline)
    PROC(barrier, vkCmdPipelineBarrier) PROC(bindPipeline, vkCmdBindPipeline)
    PROC(bindSets, vkCmdBindDescriptorSets) PROC(dispatch, vkCmdDispatch)
#undef PROC
    auto conversionInfo = profile();
    VkResult r = p_.createConversion(device_, &conversionInfo, nullptr, &conversion_);
    if (r != VK_SUCCESS || !conversion_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkSamplerYcbcrConversionInfo attach{};
    attach.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    attach.conversion = conversion_;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext = &attach;
    samplerInfo.minFilter = samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    r = p_.createSampler(device_, &samplerInfo, nullptr, &sampler_);
    if (r != VK_SUCCESS || !sampler_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sampler_};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    r = p_.createLayout(device_, &layoutInfo, nullptr, &layout_);
    if (r != VK_SUCCESS || !layout_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, preparation.combinedImageSamplerDescriptorCount},
                                 {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    r = p_.createPool(device_, &poolInfo, nullptr, &pool_);
    if (r != VK_SUCCESS || !pool_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &layout_;
    r = p_.allocateSets(device_, &allocate, &set_);
    if (r != VK_SUCCESS || !set_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = sizeof(PixelSamplerProbeSpirv);
    shaderInfo.pCode = PixelSamplerProbeSpirv;
    r = p_.createShader(device_, &shaderInfo, nullptr, &shader_);
    if (r != VK_SUCCESS || !shader_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout_;
    r = p_.createPipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_);
    if (r != VK_SUCCESS || !pipelineLayout_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shader_;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    r = p_.createPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    if (r != VK_SUCCESS || !pipeline_) return fail(r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r);
    p_.destroyShader(device_, shader_, nullptr);
    shader_ = VK_NULL_HANDLE;
    state_.store(State::Ready, std::memory_order_release);
    return Status::Ok;
}

bool SamplerProbe::admit(const Source& source, Dispatch& out) const noexcept {
    out = {};
    const auto& range = source.subresource;
    if (state() != State::Ready || !source.complete || !source.view || !source.viewGeneration
        || source.format != P010 || source.viewType != VK_IMAGE_VIEW_TYPE_2D
        || !identity(source.components) || source.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        || range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || range.baseMipLevel != 0
        || range.levelCount != 1 || range.baseArrayLayer != 0 || range.layerCount != 1
        || !source.hasViewConversion || !source.conversionComplete || !matchesProfile(source.conversion)
        || source.extent.width < 2 || source.extent.height < 2
        || source.extent.width > 8192 || source.extent.height > 8192
        || (source.extent.width & 1u) || (source.extent.height & 1u)) return false;
    const VkDeviceSize pixels = VkDeviceSize(source.extent.width) * source.extent.height;
    if (pixels > capacity_ / 7u) return false;
    out.owner_ = this;
    out.view_ = source.view;
    out.generation_ = source.viewGeneration;
    out.extent_ = source.extent;
    out.offset_ = pixels * 3u;
    out.sampledBytes_ = pixels * 4u;
    out.totalBytes_ = pixels * 7u;
    return true;
}

Status SamplerProbe::record(VkCommandBuffer command, const Dispatch& dispatch) noexcept {
    if (!command || dispatch.owner_ != this || !dispatch.view_ || !dispatch.generation_
        || !dispatch.totalBytes_ || dispatch.totalBytes_ > capacity_) return Status::Refused;
    State expected = State::Ready;
    if (!state_.compare_exchange_strong(expected, State::Recording, std::memory_order_acq_rel))
        return Status::Refused;
    VkDescriptorImageInfo image{};
    image.imageView = dispatch.view_;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo buffer{buffer_, 0, dispatch.totalBytes_};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = writes[1].dstSet = set_;
    writes[0].dstBinding = 0;
    writes[1].dstBinding = 1;
    writes[0].descriptorCount = writes[1].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &buffer;
    p_.updateSets(device_, 2, writes, 0, nullptr);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer_;
    barrier.size = dispatch.totalBytes_;
    p_.barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               0, 0, nullptr, 1, &barrier, 0, nullptr);
    p_.bindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    p_.bindSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &set_, 0, nullptr);
    p_.dispatch(command, (dispatch.extent_.width + 7u) / 8u, (dispatch.extent_.height + 7u) / 8u, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    p_.barrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    state_.store(State::Recorded, std::memory_order_release);
    return Status::Ok;
}

Status SamplerProbe::fail(VkResult result) noexcept {
    lastResult_ = result;
    destroyOwned();
    state_.store(State::Failed, std::memory_order_release);
    return Status::VulkanFailure;
}

void SamplerProbe::destroyOwned() noexcept {
    if (pipeline_) p_.destroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_) p_.destroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (shader_) p_.destroyShader(device_, shader_, nullptr);
    if (pool_) p_.destroyPool(device_, pool_, nullptr);
    if (layout_) p_.destroyLayout(device_, layout_, nullptr);
    if (sampler_) p_.destroySampler(device_, sampler_, nullptr);
    if (conversion_) p_.destroyConversion(device_, conversion_, nullptr);
    pipeline_ = VK_NULL_HANDLE; pipelineLayout_ = VK_NULL_HANDLE; shader_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE; set_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE; conversion_ = VK_NULL_HANDLE;
}

Status SamplerProbe::release(bool deviceQuiescent) noexcept {
    const auto current = state();
    if (current == State::Preparing || current == State::Recording
        || (current == State::Recorded && !deviceQuiescent)) return Status::UnsafeToRelease;
    destroyOwned();
    state_.store(State::Released, std::memory_order_release);
    return Status::Ok;
}
}
