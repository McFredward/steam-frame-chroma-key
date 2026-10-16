// Actual owned multi-subpass GPU proof. All borrowed helpers are read-only.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../layer/pixel_ssbo_clone_contract.h"
namespace previous {
bool dynamicCullEnabled=false;
VkResult createDynamicDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* input,const VkAllocationCallbacks* alloc,VkDevice* out){
    std::uint32_t count=0;if(::vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)!=VK_SUCCESS)return VK_ERROR_EXTENSION_NOT_PRESENT;
    std::vector<VkExtensionProperties> supported(count);if(::vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,supported.data())!=VK_SUCCESS)return VK_ERROR_EXTENSION_NOT_PRESENT;
    if(std::none_of(supported.begin(),supported.end(),[](const auto& e){return !std::strcmp(e.extensionName,VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);}))return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    VkPhysicalDeviceFeatures2 features{};features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&dynamic;::vkGetPhysicalDeviceFeatures2(physical,&features);
    if(!dynamic.extendedDynamicState)return VK_ERROR_FEATURE_NOT_PRESENT;
    std::vector<const char*> names;if(input->enabledExtensionCount)names.assign(input->ppEnabledExtensionNames,input->ppEnabledExtensionNames+input->enabledExtensionCount);
    if(std::none_of(names.begin(),names.end(),[](const char* name){return !std::strcmp(name,VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);}))names.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    auto ci=*input;ci.enabledExtensionCount=names.size();ci.ppEnabledExtensionNames=names.data();dynamic.pNext=const_cast<void*>(ci.pNext);ci.pNext=&dynamic;
    const auto result=::vkCreateDevice(physical,&ci,alloc,out);dynamicCullEnabled=result==VK_SUCCESS;return result;
}
constexpr PFN_vkCreateDevice vkCreateDevice=createDynamicDevice; // Pointer lookup suppresses global-handle ADL.
#include "../native-fragment-renderpass2-gpu/fixture.cpp"
}
namespace {
namespace borrowed=previous::borrowed;namespace contract=frame_pixel_ssbo_clone;
using previous::RenderPass2Gpu;using previous::Result;using borrowed::check;using borrowed::require;using borrowed::bits;
struct CohortGpu:RenderPass2Gpu {
    unsigned cohortCount=0,nexts=0,cullWrites=0,graphicsCaptures=0;bool originalRoute2=false;
    contract::RenderPassDefinition retained{};PFN_vkCmdSetCullModeEXT setCull{};
    static std::vector<std::uint32_t> vertexEntry(std::vector<std::uint32_t> words){
        // Rename only the tracked, fixture-owned vertex entry. No Valve shader
        // is parsed or changed here; the original fragment remains a private pin.
        for(std::size_t at=5;at<words.size();){const auto length=words[at]>>16,op=words[at]&65535u;require(length&&length<=words.size()-at,"owned vertex instruction bounds");
            if(op==15 && words[at+1]==0){require(length>=5 && words[at+3]==0x6e69616du && words[at+4]==0,"canonical owned vertex main entry");
                words.insert(words.begin()+static_cast<std::ptrdiff_t>(at+5),0);words[at]=((length+1)<<16)|op;
                std::memset(words.data()+at+3,0,12);std::memcpy(words.data()+at+3,"VS_DISTORT",11);return words;}
            at+=length;
        }throw std::runtime_error("owned vertex entry missing");
    }
    std::size_t extra(VkFormat format,VkSampleCountFlagBits samples,VkImageUsageFlags usage,VkImageAspectFlags aspect){
        VkImageFormatProperties supported{};check(vkGetPhysicalDeviceImageFormatProperties(physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&supported));
        require((supported.sampleCounts&samples)!=0,"actual attachment format/sample support");const auto index=images.size();images.emplace_back();auto& out=images.back();
        VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={1,1,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=samples;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;
        check(vkCreateImage(device,&ci,nullptr,&out.value));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,out.value,&req);VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=memoryType(req.memoryTypeBits,0);
        check(vkAllocateMemory(device,&a,nullptr,&out.memory));check(vkBindImageMemory(device,out.value,out.memory,0));VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=out.value;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={aspect,0,1,0,1};check(vkCreateImageView(device,&view,nullptr,&out.view));return index;
    }
    VkPipeline cohortPipeline(VkShaderModule vertex,VkShaderModule fragment,VkPipelineLayout layout,VkRenderPass pass){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="VS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="PS_DISTORT";
        VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
        const VkDynamicState states[3]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_CULL_MODE_EXT};
        VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=3;dynamic.pDynamicStates=states;
        VkGraphicsPipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;ci.stageCount=2;ci.pStages=stages;
        ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&raster;ci.pMultisampleState=&ms;ci.pColorBlendState=&cb;ci.pDynamicState=&dynamic;ci.layout=layout;ci.renderPass=pass;ci.subpass=1;
        VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthCompareOp=VK_COMPARE_OP_ALWAYS;ci.pDepthStencilState=&depth;
        contract::GraphicsDefinition definition;contract::GraphicsView owned;contract::Refusal why;
        require(contract::captureGraphics(ci,layout,pass,vertex,fragment,retained,definition,why),"production graphics captures selected1 and enabled EDS1 cull state");
        require(definition.view({vertex,fragment,layout,pass,true},owned,why) && owned.info.subpass==1 && owned.dynamic.dynamicStateCount==3,"production owned graphics preserves exact selected subpass and EDS1 declaration");++graphicsCaptures;
        VkPipeline result{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&owned.info,nullptr,&result));return result;
    }
    void cohort(const char* vertexPath,const char* originalPath,const char* probePath,unsigned count,bool route2){
        RenderPass2Gpu::prepare(vertexPath,originalPath,probePath);require(previous::dynamicCullEnabled,"EDS1 feature advertised and enabled on this actual device");
        setCull=reinterpret_cast<PFN_vkCmdSetCullModeEXT>(vkGetDeviceProcAddr(device,"vkCmdSetCullModeEXT"));require(setCull!=nullptr,"actual enabled native dynamic cull setter");cohortCount=count;originalRoute2=route2;
        for(auto* pipeline:{&original,&probe}){vkDestroyPipeline(device,*pipeline,nullptr);*pipeline={};}
        vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};vkDestroyRenderPass(device,renderPass,nullptr);renderPass={};vkDestroyRenderPass(device,legacyPass,nullptr);legacyPass={};
        const auto ms=extra(VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_4_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        const auto resolved=extra(VK_FORMAT_R32G32B32A32_SFLOAT,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        const auto depth=extra(VK_FORMAT_D32_SFLOAT,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        std::array<VkAttachmentDescription2,4> attachments{};for(auto& a:attachments){a.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;a.format=VK_FORMAT_R32G32B32A32_SFLOAT;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
        attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[0].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        attachments[1].samples=VK_SAMPLE_COUNT_4_BIT;attachments[2].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[2].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[3].format=VK_FORMAT_D32_SFLOAT;attachments[3].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference2 output{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT},multisample{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT},resolve{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT},input{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT},depthRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,nullptr,3,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT};
        std::array<VkSubpassDescription2,3> subs{};for(auto& sub:subs){sub.sType=VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;}
        subs[0].colorAttachmentCount=1;subs[0].pColorAttachments=&multisample;subs[0].pResolveAttachments=&resolve;
        subs[1].colorAttachmentCount=1;subs[1].pColorAttachments=&output;subs[1].inputAttachmentCount=1;subs[1].pInputAttachments=&input;subs[1].pDepthStencilAttachment=&depthRef;
        const std::uint32_t preserved[]{0,2};subs[2].preserveAttachmentCount=2;subs[2].pPreserveAttachments=preserved;
        std::array<VkSubpassDependency2,5> deps{};for(auto& d:deps){d.sType=VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;d.dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;}
        deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=VK_SUBPASS_EXTERNAL;deps[1].dstSubpass=1;deps[1].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[1].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;deps[1].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;deps[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[2].srcSubpass=0;deps[2].dstSubpass=1;deps[2].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[2].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[2].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[2].dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[3].srcSubpass=1;deps[3].dstSubpass=VK_SUBPASS_EXTERNAL;deps[3].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[3].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[3].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[3].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        deps[4].srcSubpass=1;deps[4].dstSubpass=2;deps[4].srcStageMask=deps[4].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[4].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo2 ci{};ci.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;ci.attachmentCount=attachments.size();ci.pAttachments=attachments.data();ci.subpassCount=count;ci.pSubpasses=subs.data();ci.dependencyCount=count==3?5:4;ci.pDependencies=deps.data();
        contract::RenderPassDefinition captured;contract::Refusal why;
        if(route2){check(vkCreateRenderPass2(device,&ci,nullptr,&renderPass));require(contract::captureRenderPass2(&ci,captured,why),"production whole RP2 cohort capture");contract::RenderPass2View own;require(captured.view2(own),"production same-route owned RP2 view");check(vkCreateRenderPass2(device,&own.info,nullptr,&legacyPass));}
        else{
            std::array<VkAttachmentDescription,4> a{};for(unsigned i=0;i<a.size();++i){const auto& v=attachments[i];a[i]={v.flags,v.format,v.samples,v.loadOp,v.storeOp,v.stencilLoadOp,v.stencilStoreOp,v.initialLayout,v.finalLayout};}
            const VkAttachmentReference o{output.attachment,output.layout},m{multisample.attachment,multisample.layout},r{resolve.attachment,resolve.layout},in{input.attachment,input.layout},d{depthRef.attachment,depthRef.layout};
            std::array<VkSubpassDescription,3> s{};for(auto& sub:s)sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;s[0].colorAttachmentCount=1;s[0].pColorAttachments=&m;s[0].pResolveAttachments=&r;s[1].colorAttachmentCount=1;s[1].pColorAttachments=&o;s[1].inputAttachmentCount=1;s[1].pInputAttachments=&in;s[1].pDepthStencilAttachment=&d;s[2].preserveAttachmentCount=2;s[2].pPreserveAttachments=preserved;
            std::array<VkSubpassDependency,5> dependencies{};for(unsigned i=0;i<dependencies.size();++i){const auto& v=deps[i];dependencies[i]={v.srcSubpass,v.dstSubpass,v.srcStageMask,v.dstStageMask,v.srcAccessMask,v.dstAccessMask,v.dependencyFlags};}
            VkRenderPassCreateInfo legacy{};legacy.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;legacy.attachmentCount=a.size();legacy.pAttachments=a.data();legacy.subpassCount=count;legacy.pSubpasses=s.data();legacy.dependencyCount=ci.dependencyCount;legacy.pDependencies=dependencies.data();check(vkCreateRenderPass(device,&legacy,nullptr,&renderPass));require(contract::captureRenderPass(&legacy,captured,why),"production whole legacy cohort capture");contract::RenderPassView own;require(captured.view(own),"production same-route owned legacy view");check(vkCreateRenderPass(device,&own.info,nullptr,&legacyPass));
        }
        retained=captured;
        require(captured.subpassCount==count&&captured.subpass(1)->hasDepth&&captured.subpass(0)->hasResolves&&captured.subpass(1)->inputCount==1,"complete cohort and exact target1 roles retained");
        const VkImageView views[]{images[targets[0]].view,images[ms].view,images[resolved].view,images[depth].view};VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=renderPass;fb.attachmentCount=4;fb.pAttachments=views;fb.width=fb.height=fb.layers=1;check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        VkShaderModule vertex{},stock{},instrumented{};const auto release=[&]{for(auto m:{vertex,stock,instrumented})if(m)vkDestroyShaderModule(device,m,nullptr);};
        try{vertex=module(vertexEntry(borrowed::shader(vertexPath)));stock=module(borrowed::shader(originalPath));instrumented=module(borrowed::shader(probePath));original=cohortPipeline(vertex,stock,originalLayout,renderPass);probe=cohortPipeline(vertex,instrumented,pipelineLayout,legacyPass);}catch(...){release();throw;}release();
    }
    Result execute(const borrowed::Case& c){
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        const auto word=[&](std::size_t index,unsigned at,std::uint32_t value){std::memcpy(static_cast<char*>(buffers[index].mapped)+at,&value,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,bits(c.tint[i]));
        word(b4,256,c.eye);
        for(unsigned eye=0;eye<2;++eye){const unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+12,c.flags^(eye!=c.eye?0x00400000u:0u));word(b6,at+44,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+60,bits(c.patch[1]+float(eye!=c.eye)*.25f));
        }
        std::fill_n(static_cast<std::uint32_t*>(buffers[output].mapped),allocated/4,borrowed::Canary);
        for(auto index:{b4,b6,output})flush(index);
        begin();
        std::array<VkBufferMemoryBarrier,2> before{};
        for(auto& barrier:before){barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.size=VK_WHOLE_SIZE;}
        before[0].buffer=buffers[output].value;before[0].srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;before[0].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        before[1].buffer=buffers[readback].value;before[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;before[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,2,before.data(),0,nullptr);
        vkCmdFillBuffer(command,buffers[output].value,offset,128,0);++gpuResets;
        auto reset=before[0];reset.offset=offset;reset.size=128;reset.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;reset.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&reset,0,nullptr);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,originalLayout,0,1,&set,0,nullptr);++set0Binds;
        vkCmdPushConstants(command,originalLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,c.u.data());++pushWrites;
        const VkViewport viewport{0,0,1,1,0,1};const VkRect2D scissor{{0,0},{1,1}};
        vkCmdSetViewport(command,0,1,&viewport);++viewportWrites;vkCmdSetScissor(command,0,1,&scissor);++scissorWrites;
        setCull(command,VK_CULL_MODE_NONE);++cullWrites;
        vkCmdBindIndexBuffer(command,buffers[indexBuffer].value,0,VK_INDEX_TYPE_UINT16);++indexBinds;
        for(unsigned stage=0;stage<3;++stage){
            // Independent owned render-pass executions reuse all four images.
            // Order prior implicit stores/layout writes, including DONT_CARE.
            VkMemoryBarrier reuse{};reuse.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;reuse.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;reuse.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&reuse,0,nullptr,0,nullptr);
            std::array<VkClearValue,4> clear{};for(unsigned i=0;i<3;++i)for(auto& value:clear[i].color.float32)value=-99;clear[3].depthStencil={1,0};
            VkRenderPassBeginInfo beginInfo{};beginInfo.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            beginInfo.renderPass=renderPass;beginInfo.framebuffer=framebuffer;beginInfo.renderArea={{0,0},{1,1}};beginInfo.clearValueCount=clear.size();beginInfo.pClearValues=clear.data();
            VkSubpassBeginInfo subBegin{};subBegin.sType=VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;
            vkCmdBeginRenderPass2(command,&beginInfo,&subBegin);++begins2;
            VkSubpassEndInfo advance{};advance.sType=VK_STRUCTURE_TYPE_SUBPASS_END_INFO;
            vkCmdNextSubpass2(command,&subBegin,&advance);++nexts; // Target is exact original subpass1.
            if(stage==1){
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,probe);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&exportSet,0,nullptr);++set1Binds;
            }
            vkCmdDrawIndexed(command,3,1,0,0,0);++indexedDraws;
            if(stage==1){
                // Immediate next Vulkan command; no duplicate original draw.
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);++restores;
            }
            if(cohortCount==3){vkCmdNextSubpass2(command,&subBegin,&advance);++nexts;}
            VkSubpassEndInfo subEnd{};subEnd.sType=VK_STRUCTURE_TYPE_SUBPASS_END_INFO;
            vkCmdEndRenderPass2(command,&subEnd);++ends2;
            VkBufferImageCopy copy{};copy.bufferOffset=stage*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
            vkCmdCopyImageToBuffer(command,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
        }
        auto after=before;
        after[0].srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;after[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        for(auto& barrier:after)barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,2,after.data(),0,nullptr);finish();
        for(auto index:{output,readback})if(!buffers[index].coherent){VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=buffers[index].memory;range.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&range));}
        const auto* words=static_cast<const std::uint32_t*>(buffers[output].mapped);
        for(unsigned i=0;i<allocated/4;++i)if(i<offset/4 || i>=(offset+128)/4)require(words[i]==borrowed::Canary,"RP2 record canary changed");
        Result result{};std::memcpy(result.original.data(),buffers[readback].mapped,16);
        std::memcpy(result.observed.data(),static_cast<const char*>(buffers[readback].mapped)+16,16);
        std::memcpy(result.restored.data(),static_cast<const char*>(buffers[readback].mapped)+32,16);
        std::memcpy(result.record.data(),static_cast<const char*>(buffers[output].mapped)+offset,128);return result;
    }
};
}
int main(int argc,char** argv){try{
    require(argc==4,"usage: cohort VERTEX ORIGINAL PROBE");unsigned cases=0,cohorts=0;std::string name;
    for(bool route2:{false,true})for(unsigned count:{2u,3u}){
        CohortGpu gpu;gpu.init();require(gpu.validationEnabled,"actual validation required");gpu.cohort(argv[1],argv[2],argv[3],count,route2);name=gpu.properties.deviceName;
        const auto all=borrowed::cases();for(unsigned index:{0u,17u}){const auto& c=all[index];const auto out=gpu.execute(c);
            require(out.original==out.observed&&out.original==out.restored,"whole-cohort original/observer/restored output must be bit identical");require(out.original[3]==bits(1.f),"target1 actual raster coverage");
            require(out.record[0]==1&&out.record[1]==0x5353424fu&&out.record[2]==1&&out.record[3]==32,"exact128byte SSBO record header");
            for(unsigned i=0;i<4;++i){require(out.record[28+i]==out.observed[i],"same target1 invocation exact RGBA record");require(out.record[6+i]==bits(c.r1[i])&&out.record[10+i]==bits(c.r0[i]),"unchanged original UBO access");}
            require(out.record[16]==c.flags,"unchanged consumed flags");for(unsigned i=0;i<2;++i)require(out.record[4+i]==bits(c.u[i])&&out.record[14+i]==bits(c.patch[i]),"unchanged push input and patch");++cases;
        }
        require(gpu.graphicsCaptures==2&&gpu.cullWrites==2,"enabled native EDS1 setter and production graphics decoder executed");
        require(gpu.nexts==6*(count-1)&&gpu.begins2==6&&gpu.ends2==6&&gpu.indexedDraws==6&&gpu.restores==2,"only target1 observer replacement; remaining empty subpasses complete before HOST export");++cohorts;
    }
    require(borrowed::validationErrors.load()==0,"validation/synchronization clean including teardown");std::cout<<"{\"schema\":\"owned-renderpass-cohort-gpu-v1\",\"cohorts\":"<<cohorts<<",\"cases\":"<<cases<<",\"checks\":"<<borrowed::checks<<",\"device_name\":\""<<name<<"\",\"selected_subpass\":1,\"eds1_cull_advertised_enabled_set_and_captured\":true,\"legacy_and_rp2\":true,\"depth_resolve_input_preserve\":true,\"bit_exact_original_observer_restore\":true,\"record_bytes\":128,\"validation_enabled\":true,\"validation_errors\":0,\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"cohort GPU: "<<e.what()<<"; checks="<<borrowed::checks<<'\n';return 1;}}
