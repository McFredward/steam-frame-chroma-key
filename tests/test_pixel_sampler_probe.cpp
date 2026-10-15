// Production one-shot sampler preparation, sealed dispatch and lifetime controls.
#include "layer/pixel_sampler_probe.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace frame_pixel_sampler;
static unsigned checks = 0;
static void require(bool yes, const char* message) {
    ++checks;
    if (!yes) throw std::runtime_error(message);
}
template<class T> static T handle(std::uintptr_t n) { return (T)n; }

struct Fake {
    static Fake* active;
    unsigned step = 0, failAt = 0, updates = 0, commands = 0, dispatches = 0;
    unsigned live = 0, destroyed = 0, failedChecks = 0;
    bool aliasesOnly = false, conversionFunctionsMissing = false, partialPipelineFailure = false;
    std::uint32_t expectedCombinedCount = 3;
    const char* missing = nullptr;
    std::array<unsigned, 8> created{}, removed{};
    std::array<unsigned, 8> order{};
    unsigned orderCount = 0;
    VkDescriptorImageInfo writtenImage{};
    VkDescriptorBufferInfo writtenBuffer{};
    std::array<VkBufferMemoryBarrier, 2> bufferBarriers{};
    std::array<VkPipelineStageFlags, 2> srcStages{}, dstStages{};
    std::uint32_t groupsX = 0, groupsY = 0, groupsZ = 0;
    unsigned barriers = 0;
    Fake() { active = this; }
    void check(bool v) noexcept { if (!v) ++failedChecks; }
    template<class T> VkResult make(unsigned type, T* out) noexcept {
        ++step;
        if (step == failAt) {
            if (type == 6 && partialPipelineFailure) { *out = handle<T>(100 + type); ++created[type]; ++live; }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        *out = handle<T>(100 + type);
        ++created[type]; ++live;
        return VK_SUCCESS;
    }
    void remove(unsigned type) noexcept { ++removed[type]; ++destroyed; --live; }
    static VKAPI_ATTR VkResult VKAPI_CALL conversion(VkDevice, const VkSamplerYcbcrConversionCreateInfo* i,
        const VkAllocationCallbacks*, VkSamplerYcbcrConversion* out) {
        auto& f = *active; f.check(SamplerProbe::matchesProfile(*i)); return f.make(0, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyConversion(VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks*) { active->remove(0); }
    static VKAPI_ATTR VkResult VKAPI_CALL sampler(VkDevice, const VkSamplerCreateInfo* i,
        const VkAllocationCallbacks*, VkSampler* out) {
        auto& f = *active;
        const auto* a = static_cast<const VkSamplerYcbcrConversionInfo*>(i->pNext);
        f.check(a && a->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO && !a->pNext
            && a->conversion == handle<VkSamplerYcbcrConversion>(100));
        f.check(i->minFilter == VK_FILTER_LINEAR && i->magFilter == VK_FILTER_LINEAR
            && i->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST && i->minLod == 0 && i->maxLod == 0
            && !i->anisotropyEnable && !i->compareEnable && !i->unnormalizedCoordinates
            && i->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            && i->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        return f.make(1, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroySampler(VkDevice, VkSampler, const VkAllocationCallbacks*) { active->remove(1); }
    static VKAPI_ATTR VkResult VKAPI_CALL layout(VkDevice, const VkDescriptorSetLayoutCreateInfo* i,
        const VkAllocationCallbacks*, VkDescriptorSetLayout* out) {
        auto& f = *active;
        f.check(i->bindingCount == 2 && !i->pNext && i->flags == 0);
        if (i->bindingCount == 2) {
            const auto& a = i->pBindings[0]; const auto& b = i->pBindings[1];
            f.check(a.binding == 0 && a.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                && a.descriptorCount == 1 && a.stageFlags == VK_SHADER_STAGE_COMPUTE_BIT
                && a.pImmutableSamplers && *a.pImmutableSamplers == handle<VkSampler>(101));
            f.check(b.binding == 1 && b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                && b.descriptorCount == 1 && b.stageFlags == VK_SHADER_STAGE_COMPUTE_BIT && !b.pImmutableSamplers);
        }
        return f.make(2, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) { active->remove(2); }
    static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice, const VkDescriptorPoolCreateInfo* i,
        const VkAllocationCallbacks*, VkDescriptorPool* out) {
        auto& f = *active;
        f.check(i->maxSets == 1 && i->poolSizeCount == 2 && !i->flags && !i->pNext);
        if (i->poolSizeCount == 2) f.check(i->pPoolSizes[0].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            && i->pPoolSizes[0].descriptorCount == f.expectedCombinedCount && i->pPoolSizes[1].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            && i->pPoolSizes[1].descriptorCount == 1);
        return f.make(3, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) { active->remove(3); }
    static VKAPI_ATTR VkResult VKAPI_CALL sets(VkDevice, const VkDescriptorSetAllocateInfo* i, VkDescriptorSet* out) {
        auto& f = *active; f.check(i->descriptorSetCount == 1 && i->descriptorPool == handle<VkDescriptorPool>(103)
            && i->pSetLayouts && *i->pSetLayouts == handle<VkDescriptorSetLayout>(102));
        ++f.step; if (f.step == f.failAt) return VK_ERROR_OUT_OF_HOST_MEMORY;
        *out = handle<VkDescriptorSet>(901); return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL update(VkDevice, uint32_t n, const VkWriteDescriptorSet* w,
        uint32_t copies, const VkCopyDescriptorSet* copied) {
        auto& f = *active; ++f.updates;
        f.order[f.orderCount++] = 1;
        f.check(n == 2 && copies == 0 && !copied);
        if (n != 2) return;
        f.check(w[0].dstSet == handle<VkDescriptorSet>(901) && w[1].dstSet == handle<VkDescriptorSet>(901)
            && w[0].dstBinding == 0 && w[1].dstBinding == 1 && !w[0].dstArrayElement && !w[1].dstArrayElement
            && w[0].descriptorCount == 1 && w[1].descriptorCount == 1
            && w[0].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            && w[1].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        f.writtenImage = *w[0].pImageInfo; f.writtenBuffer = *w[1].pBufferInfo;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL shader(VkDevice, const VkShaderModuleCreateInfo* i,
        const VkAllocationCallbacks*, VkShaderModule* out) {
        auto& f = *active;
        f.check(i->codeSize >= 20 && i->codeSize <= 16384 && !(i->codeSize % 4)
            && i->pCode && i->pCode[0] == 0x07230203);
        return f.make(4, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice, VkShaderModule, const VkAllocationCallbacks*) { active->remove(4); }
    static VKAPI_ATTR VkResult VKAPI_CALL pipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo* i,
        const VkAllocationCallbacks*, VkPipelineLayout* out) {
        auto& f = *active;
        f.check(i->setLayoutCount == 1 && i->pSetLayouts && *i->pSetLayouts == handle<VkDescriptorSetLayout>(102)
            && i->pushConstantRangeCount == 0 && !i->pPushConstantRanges);
        return f.make(5, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) { active->remove(5); }
    static VKAPI_ATTR VkResult VKAPI_CALL pipelines(VkDevice, VkPipelineCache cache, uint32_t n,
        const VkComputePipelineCreateInfo* i, const VkAllocationCallbacks*, VkPipeline* out) {
        auto& f = *active;
        f.check(!cache && n == 1 && !i->pNext && i->layout == handle<VkPipelineLayout>(105)
            && i->stage.stage == VK_SHADER_STAGE_COMPUTE_BIT && i->stage.module == handle<VkShaderModule>(104)
            && !std::strcmp(i->stage.pName, "main") && !i->stage.pSpecializationInfo);
        return f.make(6, out);
    }
    static VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) { active->remove(6); }
    static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d,
        VkDependencyFlags flags, uint32_t mc, const VkMemoryBarrier* m, uint32_t bc,
        const VkBufferMemoryBarrier* b, uint32_t ic, const VkImageMemoryBarrier* image) {
        auto& f = *active; ++f.commands; f.order[f.orderCount++] = f.barriers ? 5 : 2;
        f.check(c == handle<VkCommandBuffer>(303) && flags == 0 && mc == 0 && !m && bc == 1 && ic == 0 && !image);
        if (f.barriers < 2) { f.bufferBarriers[f.barriers] = *b; f.srcStages[f.barriers] = s; f.dstStages[f.barriers] = d; }
        ++f.barriers;
    }
    static VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer c, VkPipelineBindPoint point, VkPipeline p) {
        auto& f = *active; ++f.commands; f.order[f.orderCount++] = 3;
        f.check(c == handle<VkCommandBuffer>(303) && point == VK_PIPELINE_BIND_POINT_COMPUTE && p == handle<VkPipeline>(106));
    }
    static VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
        uint32_t first, uint32_t n, const VkDescriptorSet* sets, uint32_t dc, const uint32_t* offsets) {
        auto& f = *active; ++f.commands; f.order[f.orderCount++] = 4;
        f.check(c == handle<VkCommandBuffer>(303) && point == VK_PIPELINE_BIND_POINT_COMPUTE
            && layout == handle<VkPipelineLayout>(105) && first == 0 && n == 1 && *sets == handle<VkDescriptorSet>(901)
            && dc == 0 && !offsets);
    }
    static VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) {
        auto& f = *active; ++f.commands; ++f.dispatches; f.order[f.orderCount++] = 6;
        f.check(c == handle<VkCommandBuffer>(303)); f.groupsX = x; f.groupsY = y; f.groupsZ = z;
    }
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice, const char* name) {
        auto& f = *active;
        if (f.missing && !std::strcmp(name, f.missing)) return nullptr;
        if (f.conversionFunctionsMissing && (std::strstr(name, "vkCreateSamplerYcbcrConversion")
            || std::strstr(name, "vkDestroySamplerYcbcrConversion"))) return nullptr;
        if (f.aliasesOnly && (!std::strcmp(name, "vkCreateSamplerYcbcrConversion")
            || !std::strcmp(name, "vkDestroySamplerYcbcrConversion"))) return nullptr;
#define PROC(n, fn) if (!std::strcmp(name, #n)) return reinterpret_cast<PFN_vkVoidFunction>(&fn);
        PROC(vkCreateSamplerYcbcrConversion, conversion) PROC(vkDestroySamplerYcbcrConversion, destroyConversion)
        PROC(vkCreateSamplerYcbcrConversionKHR, conversion) PROC(vkDestroySamplerYcbcrConversionKHR, destroyConversion)
        PROC(vkCreateSampler, sampler) PROC(vkDestroySampler, destroySampler)
        PROC(vkCreateDescriptorSetLayout, layout) PROC(vkDestroyDescriptorSetLayout, destroyLayout)
        PROC(vkCreateDescriptorPool, pool) PROC(vkDestroyDescriptorPool, destroyPool)
        PROC(vkAllocateDescriptorSets, sets) PROC(vkUpdateDescriptorSets, update)
        PROC(vkCreateShaderModule, shader) PROC(vkDestroyShaderModule, destroyShader)
        PROC(vkCreatePipelineLayout, pipelineLayout) PROC(vkDestroyPipelineLayout, destroyPipelineLayout)
        PROC(vkCreateComputePipelines, pipelines) PROC(vkDestroyPipeline, destroyPipeline)
        PROC(vkCmdPipelineBarrier, barrier) PROC(vkCmdBindPipeline, bindPipeline)
        PROC(vkCmdBindDescriptorSets, bindSets) PROC(vkCmdDispatch, dispatch)
#undef PROC
        return nullptr;
    }
};
Fake* Fake::active = nullptr;

static Preparation preparation() {
    Preparation p{}; p.targetBuffer = handle<VkBuffer>(202); p.capacity = MaximumBytes;
    p.samplerYcbcrConversionEnabled = true; p.combinedImageSamplerDescriptorCount = 3;
    return p;
}
static Source source() {
    Source s{}; s.complete = true; s.view = handle<VkImageView>(302); s.viewGeneration = 99;
    s.extent = {1152, 4608}; s.format = P010; s.subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    s.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; s.hasViewConversion = s.conversionComplete = true;
    s.conversion = SamplerProbe::profile(); return s;
}
static void prepareGood(SamplerProbe& p, Fake& f) {
    require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::Ok, "cold prepare succeeds");
    require(p.state() == State::Ready && f.step == 8 && f.live == 6 && f.removed[4] == 1,
        "all resources cold; shader released after successful pipeline creation");
    require(f.failedChecks == 0, "cold Vulkan calls match immutable profile/layout");
}

int main() {
    try {
        for (unsigned fail = 1; fail <= 8; ++fail) {
            Fake f; f.failAt = fail; SamplerProbe p;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::VulkanFailure, "cold stage failure propagated");
            require(p.state() == State::Failed && p.lastResult() == VK_ERROR_OUT_OF_HOST_MEMORY, "cold failure status retained");
            require(f.live == 0 && f.updates == 0 && f.commands == 0, "every partial cold failure frees only its objects");
            for (unsigned i = 0; i < f.created.size(); ++i) require(f.created[i] == f.removed[i], "failure object accounting exact");
            require(p.release() == Status::Ok && f.live == 0, "failed cold release idempotent");
        }
        {
            Fake f; f.failAt = 8; f.partialPipelineFailure = true; SamplerProbe p;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::VulkanFailure,
                "partial native pipeline failure propagated");
            require(f.created[6] == 1 && f.removed[6] == 1 && f.live == 0,
                "non-null partially returned pipeline is not leaked on failure");
        }
        {
            Fake f; f.conversionFunctionsMissing = true; SamplerProbe p;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::VulkanFailure
                && f.step == 0 && !f.live, "missing both core and KHR conversion paths fails before allocation");
        }
        const char* missing[] = {"vkCreateSampler", "vkDestroySampler", "vkCreateDescriptorSetLayout", "vkDestroyDescriptorSetLayout",
            "vkCreateDescriptorPool", "vkDestroyDescriptorPool", "vkAllocateDescriptorSets", "vkUpdateDescriptorSets",
            "vkCreateShaderModule", "vkDestroyShaderModule", "vkCreatePipelineLayout", "vkDestroyPipelineLayout",
            "vkCreateComputePipelines", "vkDestroyPipeline", "vkCmdPipelineBarrier", "vkCmdBindPipeline", "vkCmdBindDescriptorSets", "vkCmdDispatch"};
        for (const auto* name : missing) {
            Fake f; f.missing = name; SamplerProbe p;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::VulkanFailure, "missing native function refuses cold");
            require(f.step == 0 && f.live == 0, "all function resolution precedes allocation");
            require(p.release() == Status::Ok, "missing-function failure releases");
        }
        for (unsigned bad = 0; bad < 7; ++bad) {
            Fake f; SamplerProbe p; auto c = preparation();
            if (bad == 0) c.targetBuffer = VK_NULL_HANDLE;
            if (bad == 1) c.capacity = 27;
            if (bad == 2) c.capacity = MaximumBytes + 1;
            if (bad == 3) c.samplerYcbcrConversionEnabled = false;
            if (bad == 4) c.combinedImageSamplerDescriptorCount = 0;
            if (bad == 5) c.combinedImageSamplerDescriptorCount = 17;
            require(p.prepare(bad == 6 ? VK_NULL_HANDLE : handle<VkDevice>(201), Fake::resolve, c)
                == Status::VulkanFailure, "unsupported preparation refused");
            require(f.step == 0 && f.live == 0, "unsupported preparation never allocates");
        }
        {
            Fake f; f.aliasesOnly = true; SamplerProbe p; prepareGood(p, f);
            require(p.release() == Status::Ok && f.live == 0, "KHR conversion aliases resolve without native device mutation");
        }
        for (const auto count : {1u, 16u}) {
            Fake f; f.expectedCombinedCount = count; SamplerProbe p; auto c = preparation();
            c.combinedImageSamplerDescriptorCount = count;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, c) == Status::Ok && !f.failedChecks,
                "cold pool reserves the complete validated combined-descriptor bound");
            require(p.release() == Status::Ok && !f.live, "alternative valid descriptor counts release exactly");
        }
        for (const bool enough : {false, true}) {
            Fake f; SamplerProbe p; auto c = preparation(); c.capacity = 37158912 - (enough ? 0 : 1);
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, c) == Status::Ok, "bounded non-aligned capacity prepares cold");
            Dispatch d;
            require(p.admit(source(), d) == enough, "actual paired output extent must fit down to the final byte");
            require(f.updates == 0 && f.commands == 0 && p.release() == Status::Ok && !f.live,
                "capacity failure and successful pure admission can release before any GPU reference");
        }
        {
            Fake f; SamplerProbe p; auto c = preparation(); c.capacity = 28;
            require(p.prepare(handle<VkDevice>(201), Fake::resolve, c) == Status::Ok, "smallest valid paired allocation prepares");
            auto s = source(); s.extent = {2, 2}; Dispatch d;
            require(p.admit(s, d) && d.offset() == 12 && d.sampledBytes() == 16 && d.totalBytes() == 28,
                "small even source remains word-aligned without descriptor-offset assumptions");
            require(p.release() == Status::Ok && !f.live, "small pure dispatch releases cold");
        }
        Fake f; SamplerProbe p; prepareGood(p, f);
        require(p.prepare(handle<VkDevice>(201), Fake::resolve, preparation()) == Status::Refused, "second prepare never reuses resources");
        Dispatch d;
        for (unsigned bad = 0; bad < 33; ++bad) {
            auto s = source();
            switch (bad) {
                case 0: s.complete = false; break; case 1: s.view = VK_NULL_HANDLE; break;
                case 2: s.viewGeneration = 0; break; case 3: s.format = VK_FORMAT_R8G8B8A8_UNORM; break;
                case 4: s.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break; case 5: s.components.r = VK_COMPONENT_SWIZZLE_R; break;
                case 6: s.layout = VK_IMAGE_LAYOUT_GENERAL; break; case 7: s.subresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT; break;
                case 8: s.subresource.baseMipLevel = 1; break; case 9: s.subresource.levelCount = 2; break;
                case 10: s.subresource.baseArrayLayer = 1; break; case 11: s.subresource.layerCount = 2; break;
                case 12: s.hasViewConversion = false; break; case 13: s.conversionComplete = false; break;
                case 14: s.conversion.pNext = &s; break; case 15: s.conversion.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO; break;
                case 16: s.conversion.format = VK_FORMAT_R8G8B8A8_UNORM; break;
                case 17: s.conversion.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; break;
                case 18: s.conversion.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW; break;
                case 19: s.conversion.components.g = VK_COMPONENT_SWIZZLE_R; break;
                case 20: s.conversion.xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN; break;
                case 21: s.conversion.yChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN; break;
                case 22: s.conversion.chromaFilter = VK_FILTER_NEAREST; break;
                case 23: s.conversion.forceExplicitReconstruction = VK_TRUE; break;
                case 24: s.extent.width = 0; break; case 25: s.extent.height = 1; break;
                case 26: s.extent.width = 1151; break; case 27: s.extent.height = 4607; break;
                case 28: s.extent.width = 8194; break; case 29: s.extent.height = 8194; break;
                case 30: s.extent = {8192, 8192}; break;
                case 31: s.components.a = VK_COMPONENT_SWIZZLE_ONE; break;
                case 32: s.subresource.levelCount = VK_REMAINING_MIP_LEVELS; break;
            }
            require(!p.admit(s, d), "missing/inexact source profile refuses admission");
            require(d.sourceView() == VK_NULL_HANDLE && d.totalBytes() == 0, "refusal clears prior dispatch");
            require(p.state() == State::Ready && f.updates == 0 && f.commands == 0 && f.step == 8,
                "admission is pure and never spends GPU resources");
        }
        require(p.admit(source(), d), "exact actual-size source admitted");
        require(d.extent().width == 1152 && d.extent().height == 4608 && d.offset() == 15925248
            && d.sampledBytes() == 21233664 && d.totalBytes() == 37158912
            && d.sourceViewGeneration() == 99, "sealed P010+RGBA offsets and metadata exact");
        require(p.admit(source(), d) && f.updates == 0 && f.commands == 0, "repeat pure admission does not claim selection");
        require(p.record(VK_NULL_HANDLE, d) == Status::Refused && p.state() == State::Ready, "invalid command never spends selection");
        Dispatch blank;
        require(p.record(handle<VkCommandBuffer>(303), blank) == Status::Refused, "unsealed dispatch refuses");
        {
            Fake foreign; SamplerProbe q; prepareGood(q, foreign);
            require(q.record(handle<VkCommandBuffer>(303), d) == Status::Refused && !foreign.updates,
                "dispatch cannot authorize another object's resources");
            require(q.release() == Status::Ok && !foreign.live, "unrecorded foreign object releases cold");
        }
        Fake::active = &f;
        require(p.record(handle<VkCommandBuffer>(303), d) == Status::Ok, "winning native copy appends one own compute dispatch");
        require(p.state() == State::Recorded && f.updates == 1 && f.commands == 5 && f.dispatches == 1
            && f.step == 8 && f.failedChecks == 0, "record creates nothing and never touches native descriptor sets");
        require(f.writtenImage.imageView == source().view && !f.writtenImage.sampler
            && f.writtenImage.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "exact native view with immutable sampler");
        require(f.writtenBuffer.buffer == preparation().targetBuffer && f.writtenBuffer.offset == 0
            && f.writtenBuffer.range == 37158912, "storage descriptor stays aligned at0 and bounds both outputs");
        require(f.groupsX == 144 && f.groupsY == 576 && f.groupsZ == 1, "full actual extent covered with bounded8x8 groups");
        require(f.orderCount == 6 && f.order == std::array<unsigned, 8>{1,2,3,4,6,5,0,0}, "descriptor then transfer/compute/host order exact");
        require(f.srcStages[0] == VK_PIPELINE_STAGE_TRANSFER_BIT && f.dstStages[0] == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            && f.bufferBarriers[0].srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT
            && f.bufferBarriers[0].dstAccessMask == VK_ACCESS_SHADER_WRITE_BIT, "raw write completion precedes compute writes");
        require(f.srcStages[1] == (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
            && f.dstStages[1] == VK_PIPELINE_STAGE_HOST_BIT
            && f.bufferBarriers[1].srcAccessMask == (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)
            && f.bufferBarriers[1].dstAccessMask == VK_ACCESS_HOST_READ_BIT, "both raw and sampled writes become host visible");
        for (const auto& b : f.bufferBarriers) require(b.buffer == preparation().targetBuffer && b.offset == 0
            && b.size == d.totalBytes() && b.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED
            && b.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED, "barriers cover only borrowed owned-output buffer");
        require(p.record(handle<VkCommandBuffer>(303), d) == Status::Refused && f.updates == 1 && f.dispatches == 1,
            "replayed record never overwrites descriptor/output selection");
        require(!p.admit(source(), blank), "consumed one-shot does not admit later views");
        require(p.release() == Status::UnsafeToRelease && f.live == 6, "recorded resources retained without actual device proof");
        require(p.release(true) == Status::Ok && f.live == 0, "joined and quiesced release destroys own resources exactly once");
        require(p.release(true) == Status::Ok && f.live == 0, "quiesced release idempotent");
        require(!p.admit(source(), blank), "released probe stays spent");
        std::cout << "{\"sampler_probe_controls\":" << checks << ",\"failures\":0}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
