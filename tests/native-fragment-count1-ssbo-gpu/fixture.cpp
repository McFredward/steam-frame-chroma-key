// Owned-only integration fixture. run.py supplies an exact reusable prefix of
// the approved SSBO fixture as a temporary include, excluding its old main.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace {
bool count1Layout=false;
VkResult ownedSetLayout(VkDevice device,const VkDescriptorSetLayoutCreateInfo* ci,
        const VkAllocationCallbacks* allocator,VkDescriptorSetLayout* result){
    if(!count1Layout)return vkCreateDescriptorSetLayout(device,ci,allocator,result);
    if(!ci || ci->bindingCount>32)return VK_ERROR_INITIALIZATION_FAILED;
    auto info=*ci;std::array<VkDescriptorSetLayoutBinding,32> bindings{};
    for(unsigned i=0;i<ci->bindingCount;++i){bindings[i]=ci->pBindings[i];if(bindings[i].binding==9)bindings[i].descriptorCount=1;}
    info.pBindings=bindings.data();return vkCreateDescriptorSetLayout(device,&info,allocator,result);
}
void ownedSetWrite(VkDevice device,std::uint32_t count,const VkWriteDescriptorSet* writes,
        std::uint32_t copies,const VkCopyDescriptorSet* copy){
    if(!count1Layout){vkUpdateDescriptorSets(device,count,writes,copies,copy);return;}
    if(count>32)std::abort();
    std::array<VkWriteDescriptorSet,32> changed{};
    for(unsigned i=0;i<count;++i){changed[i]=writes[i];if(changed[i].dstBinding==9)changed[i].descriptorCount=1;}
    vkUpdateDescriptorSets(device,count,changed.data(),copies,copy);
}
}
#define vkCreateDescriptorSetLayout ownedSetLayout
#define vkUpdateDescriptorSets ownedSetWrite
#include "owned_ssbo_fixture_helpers.inc"
#undef vkCreateDescriptorSetLayout
#undef vkUpdateDescriptorSets

namespace {
void transferSafePass(SsboGpu& gpu){
    // Explicitly cover the previous readback before another clear/write too.
    vkDestroyFramebuffer(gpu.device,gpu.framebuffer,nullptr);gpu.framebuffer={};
    vkDestroyRenderPass(gpu.device,gpu.renderPass,nullptr);gpu.renderPass={};
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};
    sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&reference;
    std::array<VkSubpassDependency,2> dependencies{};
    dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
    dependencies[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=1;pass.pAttachments=&attachment;
    pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=dependencies.data();
    check(vkCreateRenderPass(gpu.device,&pass,nullptr,&gpu.renderPass));
    const auto view=gpu.images[gpu.targets[0]].view;VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass=gpu.renderPass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=fb.layers=1;
    check(vkCreateFramebuffer(gpu.device,&fb,nullptr,&gpu.framebuffer));
}
}
int main(int argc,char** argv){
    try{
        require(argc==5,"usage: count1-ssbo-fixture VERTEX ORIGINAL ARRAY3_SSBO COUNT1_SSBO");
        count1Layout=false;SsboGpu oracle;oracle.init();require(oracle.validationEnabled,"actual validation required");
        oracle.setup(false);transferSafePass(oracle);oracle.createPipelines(argv[1],argv[2],argv[3]);
        count1Layout=true;SsboGpu gpu;gpu.init();require(gpu.validationEnabled,"actual count1 validation required");
        gpu.setup(false);transferSafePass(gpu);
        // Unchanged original3 is compiled/run only with its own count3 layout.
        // Both candidate stages here use the verified specialized artifact.
        gpu.createPipelines(argv[1],argv[4],argv[4]);
        unsigned completed=0,bitIdentical=0;
        for(const auto& c:cases()){
            const auto original=oracle.run(c,oracle.original),reference=oracle.run(c,oracle.probe,true,2),observed=gpu.run(c,gpu.probe,true,2);
            require(number(original.rgba[3])==1.f,"unchanged original3 output remains opaque");
            require(original.rgba==reference.rgba && original.rgba==observed.rgba,"specialized count1 retains bit-exact unchanged original3 color output");++bitIdentical;
            require(reference.record==observed.record,"specialized count1 exports exact original-invocation record and sampled components");
            require(observed.record[0]==1 && observed.record[1]==0x5353424fu && observed.record[2]==1 && observed.record[3]==32,"full single-attachment128-byte actual invocation record");
            auto next=c;next.u[0]+=.03125f;const auto retained=gpu.run(next,gpu.probe,false,2);
            require(retained.record==observed.record,"existing atomic first claim prevents later overwrite");++completed;
        }
        require(validationErrors.load()==0,"count1 shader/layout Vulkan validation errors");
        std::cout<<"{\"schema\":\"count1-original-invocation-ssbo-gpu-v1\",\"cases\":"<<completed<<",\"checks\":"<<checks
            <<",\"original3_bit_identical_cases\":"<<bitIdentical<<",\"active_color_attachments\":1,\"count1_layout_created\":true,\"logical_record_bytes\":128"
            <<",\"validation_enabled\":true,\"validation_errors\":0,\"device_name\":\""<<gpu.properties.deviceName<<"\",\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& error){std::cerr<<"count1 SSBO fixture failed: "<<error.what()<<"; checks="<<checks<<'\n';return 1;}
}
