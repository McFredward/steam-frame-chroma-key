// Owned offscreen RP2 compatibility proof. No receiver or Frame code is loaded.
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

namespace {
VkResult createV12Instance(const VkInstanceCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkInstance* output){
    if(!input || !input->pApplicationInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto app=*input->pApplicationInfo;app.apiVersion=VK_API_VERSION_1_2;
    auto info=*input;info.pApplicationInfo=&app;
    return vkCreateInstance(&info,allocator,output);
}
}
// Reuse allocation/upload/fence/validation/failure cleanup only. The borrowed
// old main and execution paths are never called. Every test command is below.
#define vkCreateInstance createV12Instance
namespace borrowed {
#include "../native-fragment-ssbo-gpu/fixture.cpp"
}
#undef vkCreateInstance

namespace {
namespace contract=frame_pixel_ssbo_clone;
using borrowed::check;
using borrowed::require;
using borrowed::bits;
struct Result {
    std::array<std::uint32_t,4> original{},observed{},restored{};
    std::array<std::uint32_t,32> record{};
};
struct RenderPass2Gpu:borrowed::SsboGpu {
    VkRenderPass legacyPass{};
    VkPipelineLayout originalLayout{};
    VkDescriptorSetLayout cloneSet0{};
    std::size_t indexBuffer{};
    unsigned begins2{},ends2{},indexedDraws{},set0Binds{},pushWrites{},viewportWrites{},scissorWrites{},indexBinds{},set1Binds{},restores{},gpuResets{};
    ~RenderPass2Gpu(){
        if(device){
            // Fixture-only teardown: retire the actual reusable recording first.
            vkDeviceWaitIdle(device);vkResetCommandBuffer(command,0);
            if(originalLayout)vkDestroyPipelineLayout(device,originalLayout,nullptr);
            if(cloneSet0)vkDestroyDescriptorSetLayout(device,cloneSet0,nullptr);
            if(legacyPass)vkDestroyRenderPass(device,legacyPass,nullptr);
        }
    }
    VkPipeline createPipeline(VkShaderModule vertex,VkShaderModule fragment,VkPipelineLayout layout,VkRenderPass pass){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="PS_DISTORT";
        VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
        const VkDynamicState states[2]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
        VkGraphicsPipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;ci.stageCount=2;ci.pStages=stages;
        ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&raster;ci.pMultisampleState=&ms;ci.pColorBlendState=&cb;ci.pDynamicState=&dynamic;ci.layout=layout;ci.renderPass=pass;
        VkPipeline result{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&result));return result;
    }
    void prepare(const char* vertexPath,const char* originalPath,const char* probePath){
        require(properties.apiVersion>=VK_API_VERSION_1_2,"actual Vulkan1.2 core RP2 support");setup(false);
        vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};
        vkDestroyRenderPass(device,renderPass,nullptr);renderPass={};
        vkDestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout={};
        VkAttachmentDescription2 attachment{};attachment.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference2 reference{};reference.sType=VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        reference.attachment=0;reference.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;reference.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        VkSubpassDescription2 subpass{};subpass.sType=VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
        std::array<VkSubpassDependency2,2> dependencies{};
        for(auto& dep:dependencies)dep.sType=VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo2 ci{};ci.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;ci.attachmentCount=1;ci.pAttachments=&attachment;
        ci.subpassCount=1;ci.pSubpasses=&subpass;ci.dependencyCount=2;ci.pDependencies=dependencies.data();
        // Real RP2 creation, followed by the same checked lowering and legacy
        // view that the production cold Clone uses. No reinterpret cast.
        check(vkCreateRenderPass2(device,&ci,nullptr,&renderPass));
        contract::RenderPassDefinition definition;contract::Refusal why;
        require(contract::captureRenderPass2(&ci,definition,why),"production ordinary RP2 capture");
        contract::RenderPassView lowered;
        require(definition.view(lowered),"production legacy owned renderpass view");
        require(lowered.info.sType==VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO && lowered.info.subpassCount==1
            && lowered.info.dependencyCount==2 && lowered.subpass.colorAttachmentCount==1,"checked RP2 lowering retains shape");
        check(vkCreateRenderPass(device,&lowered.info,nullptr,&legacyPass));
        require(renderPass!=legacyPass,"actual different RP2 and legacy renderpass objects");
        const auto view=images[targets[0]].view;
        VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=renderPass;
        fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=fb.layers=1;check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        const VkDescriptorSetLayoutBinding bindings[4]{{4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {25,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo setInfo{};setInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;setInfo.bindingCount=4;setInfo.pBindings=bindings;
        contract::Set0Definition setDefinition;
        require(contract::captureSet0(setInfo,setDefinition,why) && setDefinition.view(setInfo),"production compatible Set0 definition");
        check(vkCreateDescriptorSetLayout(device,&setInfo,nullptr,&cloneSet0));require(cloneSet0!=descriptorLayout,"distinct compatible Set0 objects");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo layoutInfo{};layoutInfo.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount=1;layoutInfo.pSetLayouts=&descriptorLayout;layoutInfo.pushConstantRangeCount=1;layoutInfo.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device,&layoutInfo,nullptr,&originalLayout));
        contract::LayoutDefinition layoutDefinition;
        require(contract::captureLayout(layoutInfo,descriptorLayout,layoutDefinition,why),"complete original push/prefix capture");
        const VkDescriptorSetLayout sets[2]{cloneSet0,exportLayout};
        layoutInfo.setLayoutCount=2;layoutInfo.pSetLayouts=sets;layoutInfo.pushConstantRangeCount=layoutDefinition.count;layoutInfo.pPushConstantRanges=layoutDefinition.ranges.data();
        check(vkCreatePipelineLayout(device,&layoutInfo,nullptr,&pipelineLayout));
        // Preserve inherited logical128-byte canaries while exercising a real
        // GPU reset before RP2, on every execution rather than cold host zero.
        output=buffer(allocated,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkDescriptorBufferInfo record{buffers[output].value,offset,128};
        VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=exportSet;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&record;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        indexBuffer=buffer(3*sizeof(std::uint16_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        const std::uint16_t indices[3]{0,1,2};std::memcpy(buffers[indexBuffer].mapped,indices,sizeof(indices));flush(indexBuffer);
        VkShaderModule vertex{},stock{},instrumented{};
        const auto release=[&]{for(auto module:{instrumented,stock,vertex})if(module)vkDestroyShaderModule(device,module,nullptr);};
        try{
            vertex=module(borrowed::shader(vertexPath));stock=module(borrowed::shader(originalPath));instrumented=module(borrowed::shader(probePath));
            original=createPipeline(vertex,stock,originalLayout,renderPass);
            probe=createPipeline(vertex,instrumented,pipelineLayout,legacyPass);
        }catch(...){release();throw;}
        release();
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
        vkCmdBindIndexBuffer(command,buffers[indexBuffer].value,0,VK_INDEX_TYPE_UINT16);++indexBinds;
        for(unsigned stage=0;stage<3;++stage){
            VkClearValue clear{};for(auto& value:clear.color.float32)value=-99;
            VkRenderPassBeginInfo beginInfo{};beginInfo.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            beginInfo.renderPass=renderPass;beginInfo.framebuffer=framebuffer;beginInfo.renderArea={{0,0},{1,1}};beginInfo.clearValueCount=1;beginInfo.pClearValues=&clear;
            VkSubpassBeginInfo subBegin{};subBegin.sType=VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;
            vkCmdBeginRenderPass2(command,&beginInfo,&subBegin);++begins2;
            if(stage==1){
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,probe);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&exportSet,0,nullptr);++set1Binds;
            }
            vkCmdDrawIndexed(command,3,1,0,0,0);++indexedDraws;
            if(stage==1){
                // Immediate next Vulkan command; no duplicate original draw.
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);++restores;
            }
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
int main(int argc,char** argv){
    try{
        require(argc==4,"usage: rp2-fixture VERTEX ORIGINAL PROBE");
        unsigned cases=0,bitIdentical=0;float maximumError=0;std::string deviceName;
        {
            RenderPass2Gpu gpu;gpu.init();require(gpu.validationEnabled,"actual validation required");gpu.prepare(argv[1],argv[2],argv[3]);deviceName=gpu.properties.deviceName;
            const auto all=borrowed::cases();
            for(auto index:{0u,17u}){
                const auto& c=all[index];const auto result=gpu.execute(c);
                require(result.original==result.restored,"original pipeline restore lost Set0/push/dynamic/index state");
                require(result.original[3]==bits(1.f),"actual indexed output coverage required");
                bitIdentical+=result.original==result.observed;
                for(unsigned i=0;i<4;++i){const auto error=std::abs(borrowed::number(result.original[i])-borrowed::number(result.observed[i]));
                    require(std::isfinite(error) && error<=.000002f,"observed color exceeds inherited SSBO tolerance");maximumError=std::max(maximumError,error);
                    require(result.record[28+i]==result.observed[i],"same-invocation RP2 record RGBA mismatch");
                    require(result.record[6+i]==bits(c.r1[i]) && result.record[10+i]==bits(c.r0[i]),"original Set0 UBO state changed");}
                require(result.record[0]==1 && result.record[1]==0x5353424fu && result.record[2]==1 && result.record[3]==32,"full128-byte GPU-reset record");
                for(unsigned i=0;i<2;++i)require(result.record[4+i]==bits(c.u[i]) && result.record[14+i]==bits(c.patch[i]),"push-derived raster input or patch changed");
                require(result.record[16]==c.flags,"original consumed flags changed");++cases;
            }
            require(gpu.begins2==cases*3 && gpu.ends2==cases*3 && gpu.indexedDraws==cases*3,"exact original/observe/restore RP2 indexed command counts");
            require(gpu.set0Binds==cases && gpu.pushWrites==cases && gpu.viewportWrites==cases && gpu.scissorWrites==cases && gpu.indexBinds==cases
                && gpu.set1Binds==cases && gpu.restores==cases && gpu.gpuResets==cases,"native state was unexpectedly rebound or original draw duplicated");
        }
        require(borrowed::validationErrors.load()==0,"actual Vulkan/synchronization validation errors including teardown");
        std::cout<<"{\"schema\":\"owned-renderpass2-equivalence-v1\",\"cases\":"<<cases<<",\"checks\":"<<borrowed::checks
            <<",\"device_name\":\""<<deviceName<<"\",\"original_rp2_creation\":true,\"original_begin_end2\":true,\"production_checked_legacy_lowering\":true"
            <<",\"indexed_draws\":"<<cases*3<<",\"immediate_original_restorations\":"<<cases<<",\"original_restored_bit_identical\":true,\"observed_bit_identical_cases\":"<<bitIdentical
            <<",\"observed_max_absolute_error\":"<<maximumError<<",\"logical_record_bytes\":128,\"original_state_rebinds_around_probe\":0"
            <<",\"validation_enabled\":true,\"validation_errors\":0,\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& error){std::cerr<<"RP2 fixture failed: "<<error.what()<<"; checks="<<borrowed::checks<<'\n';return 1;}
}
