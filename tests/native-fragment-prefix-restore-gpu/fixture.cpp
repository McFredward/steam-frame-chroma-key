// Owned offscreen proof only: no native pipeline or descriptor admission.
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

namespace {
struct LayoutFact{
    VkPipelineLayout handle;
    std::vector<VkDescriptorSetLayout> sets;
    std::vector<VkPushConstantRange> pushes;
};
std::vector<LayoutFact> actualLayouts;
std::vector<std::pair<VkPipeline,VkPipelineLayout>> actualPipelines;
struct DescriptorFact{
    VkDescriptorSetLayout handle;
    VkDescriptorSetLayoutCreateFlags flags;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};
std::vector<DescriptorFact> actualDescriptors;
VkResult prefixCreateDescriptorLayout(VkDevice device,const VkDescriptorSetLayoutCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkDescriptorSetLayout* result){
    // This narrow owned test has no immutable samplers or extension semantics.
    if(input->pNext || input->bindingCount>32)return VK_ERROR_INITIALIZATION_FAILED;
    for(unsigned i=0;i<input->bindingCount;++i)if(input->pBindings[i].pImmutableSamplers)return VK_ERROR_INITIALIZATION_FAILED;
    const auto status=vkCreateDescriptorSetLayout(device,input,allocator,result);
    if(status==VK_SUCCESS){
        DescriptorFact fact{*result,input->flags,{}};
        if(input->bindingCount)fact.bindings.assign(input->pBindings,input->pBindings+input->bindingCount);
        actualDescriptors.push_back(fact);
    }
    return status;
}
VkResult prefixCreateLayout(VkDevice device,const VkPipelineLayoutCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkPipelineLayout* result){
    const auto status=vkCreatePipelineLayout(device,input,allocator,result);
    if(status==VK_SUCCESS){
        LayoutFact fact{*result,{},{}};
        if(input->setLayoutCount)fact.sets.assign(input->pSetLayouts,input->pSetLayouts+input->setLayoutCount);
        if(input->pushConstantRangeCount)fact.pushes.assign(input->pPushConstantRanges,input->pPushConstantRanges+input->pushConstantRangeCount);
        actualLayouts.push_back(fact);
    }
    return status;
}
VkResult prefixCreatePipeline(VkDevice device,VkPipelineCache cache,std::uint32_t count,
        const VkGraphicsPipelineCreateInfo* input,const VkAllocationCallbacks* allocator,VkPipeline* result){
    if(count!=1)return VK_ERROR_INITIALIZATION_FAILED;
    auto info=*input;
    const VkDynamicState states[2]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;info.pDynamicState=&dynamic;
    const auto status=vkCreateGraphicsPipelines(device,cache,count,&info,allocator,result);
    if(status==VK_SUCCESS)actualPipelines.emplace_back(*result,info.layout);
    return status;
}
VkResult prefixCreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkRenderPass* result){
    if(input->attachmentCount!=1)return vkCreateRenderPass(device,input,allocator,result);
    auto info=*input;std::vector<VkSubpassDependency> dependencies;
    if(info.dependencyCount)dependencies.assign(info.pDependencies,info.pDependencies+info.dependencyCount);
    // Unlike the inherited single-pass test, this fixture copies then reuses
    // the same attachment within ONE recording. Order the implicit initial
    // layout transition through a real external->subpass dependency.
    VkSubpassDependency reuse{};reuse.srcSubpass=VK_SUBPASS_EXTERNAL;reuse.dstSubpass=0;
    reuse.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;reuse.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    reuse.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;reuse.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies.push_back(reuse);info.dependencyCount=static_cast<unsigned>(dependencies.size());info.pDependencies=dependencies.data();
    return vkCreateRenderPass(device,&info,allocator,result);
}
}
// All standard headers are already global. Keep the borrowed helper and its
// uncalled old main in a namespace, without editing or running that old test.
#define vkCreatePipelineLayout prefixCreateLayout
#define vkCreateGraphicsPipelines prefixCreatePipeline
#define vkCreateDescriptorSetLayout prefixCreateDescriptorLayout
#define vkCreateRenderPass prefixCreateRenderPass
namespace borrowed {
#include "../native-fragment-ssbo-gpu/fixture.cpp"
}
#undef vkCreateRenderPass
#undef vkCreateDescriptorSetLayout
#undef vkCreateGraphicsPipelines
#undef vkCreatePipelineLayout

namespace {
using borrowed::bits;
using borrowed::check;
using borrowed::require;

bool samePushRanges(const std::vector<VkPushConstantRange>& a,const std::vector<VkPushConstantRange>& b){
    if(a.size()!=b.size())return false;
    for(unsigned i=0;i<a.size();++i)if(a[i].stageFlags!=b[i].stageFlags || a[i].offset!=b[i].offset || a[i].size!=b[i].size)return false;
    return true;
}
DescriptorFact descriptorFact(VkDescriptorSetLayout handle){
    const auto found=std::find_if(actualDescriptors.rbegin(),actualDescriptors.rend(),[&](const auto& fact){return fact.handle==handle;});
    require(found!=actualDescriptors.rend(),"real descriptor layout creation observed");return *found;
}
bool sameDescriptorDefinition(VkDescriptorSetLayout first,VkDescriptorSetLayout second){
    const auto a=descriptorFact(first),b=descriptorFact(second);
    if(a.flags!=b.flags || a.bindings.size()!=b.bindings.size())return false;
    for(unsigned i=0;i<a.bindings.size();++i){const auto& x=a.bindings[i];const auto& y=b.bindings[i];
        if(x.binding!=y.binding || x.descriptorType!=y.descriptorType || x.descriptorCount!=y.descriptorCount
            || x.stageFlags!=y.stageFlags || x.pImmutableSamplers || y.pImmutableSamplers)return false;
    }
    return true;
}
struct PrefixGuard{
    bool higherSetSeen{};
    void observeSetBind(unsigned first,unsigned count){
        if(first>0 || count>1)higherSetSeen=true;
    }
    bool permits(const LayoutFact& original,const LayoutFact& clone,VkDescriptorSetLayout owned)const{
        return !higherSetSeen && original.sets.size()==1 && clone.sets.size()==2
            && sameDescriptorDefinition(original.sets[0],clone.sets[0]) && clone.sets[1]==owned
            && samePushRanges(original.pushes,clone.pushes);
    }
};
LayoutFact layoutFact(VkPipelineLayout handle){
    const auto found=std::find_if(actualLayouts.rbegin(),actualLayouts.rend(),[&](const auto& fact){return fact.handle==handle;});
    require(found!=actualLayouts.rend(),"real original/clone layout creation observed");return *found;
}
struct Observations{
    std::array<std::uint32_t,4> original,probe,restored;
    std::array<std::uint32_t,32> record;
};
struct PrefixGpu:borrowed::SsboGpu{
    VkPipelineLayout originalLayout{};
    VkDescriptorSetLayout compatibleSet0{};
    unsigned initialSet0Binds{},initialPushWrites{},initialViewportWrites{},initialScissorWrites{};
    unsigned ownedSet1Binds{},restorations{},guardRefusals{};
    ~PrefixGpu(){if(device){vkDeviceWaitIdle(device);if(originalLayout)vkDestroyPipelineLayout(device,originalLayout,nullptr);if(compatibleSet0)vkDestroyDescriptorSetLayout(device,compatibleSet0,nullptr);}}
    void preparePipelines(const char* vertexPath,const char* originalPath,const char* probePath){
        setup(false);
        const auto definition=descriptorFact(descriptorLayout);
        VkDescriptorSetLayoutCreateInfo setInfo{};setInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.flags=definition.flags;setInfo.bindingCount=static_cast<unsigned>(definition.bindings.size());setInfo.pBindings=definition.bindings.data();
        check(prefixCreateDescriptorLayout(device,&setInfo,nullptr,&compatibleSet0));
        require(compatibleSet0!=descriptorLayout && sameDescriptorDefinition(descriptorLayout,compatibleSet0),"actual DISTINCT identically-defined Set0 layouts");
        vkDestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout=VK_NULL_HANDLE;
        const VkDescriptorSetLayout sets[2]{compatibleSet0,exportLayout};
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount=2;info.pSetLayouts=sets;info.pushConstantRangeCount=1;info.pPushConstantRanges=&push;
        check(prefixCreateLayout(device,&info,nullptr,&pipelineLayout));
        info.setLayoutCount=1;info.pSetLayouts=&descriptorLayout;info.pushConstantRangeCount=1;info.pPushConstantRanges=&push;
        check(prefixCreateLayout(device,&info,nullptr,&originalLayout));
        const auto cloneLayout=pipelineLayout;
        VkShaderModule vertex{},stock{},instrumented{};
        auto release=[&]{for(auto module:{instrumented,stock,vertex})if(module)vkDestroyShaderModule(device,module,nullptr);};
        try{
            vertex=module(borrowed::shader(vertexPath));stock=module(borrowed::shader(originalPath));instrumented=module(borrowed::shader(probePath));
            pipelineLayout=originalLayout;original=singlePipeline(vertex,stock);
            pipelineLayout=cloneLayout;probe=singlePipeline(vertex,instrumented);
        }catch(...){pipelineLayout=cloneLayout;release();throw;}
        release();
        const auto prefix=layoutFact(originalLayout),clone=layoutFact(pipelineLayout);
        require(prefix.handle!=clone.handle,"actual different original/clone layout objects");
        require(prefix.sets.size()==1 && clone.sets.size()==2 && prefix.sets[0]!=clone.sets[0]
            && sameDescriptorDefinition(prefix.sets[0],clone.sets[0]) && samePushRanges(prefix.pushes,clone.pushes),"actual Set0-only vs Set0+Set1 complete definition/push compatibility");
        require(prefix.pushes.size()==1 && prefix.pushes[0].stageFlags==VK_SHADER_STAGE_VERTEX_BIT
            && prefix.pushes[0].offset==0 && prefix.pushes[0].size==16,"actual full owned vertex push range observed");
        require(actualPipelines.size()==2 && actualPipelines[0]==std::make_pair(original,originalLayout)
            && actualPipelines[1]==std::make_pair(probe,pipelineLayout),"actual original and probe use distinct observed layouts");
        PrefixGuard priorHigher;priorHigher.observeSetBind(1,1);priorHigher.observeSetBind(0,1);
        require(!priorHigher.permits(prefix,clone,exportLayout),"prior Set1 history was erased by Set0 bind");++guardRefusals;
        auto differentPush=clone;differentPush.pushes[0].size+=4;
        require(!PrefixGuard{}.permits(prefix,differentPush,exportLayout),"different complete push range admitted");++guardRefusals;
        // Both negative decisions occur before recording/issuing any intervention
        // commands. No invalid mismatched-layout Vulkan call is a test oracle.
    }
    void prepare(const borrowed::Case& c){
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        auto word=[&](std::size_t index,unsigned at,std::uint32_t value){std::memcpy(static_cast<char*>(buffers[index].mapped)+at,&value,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,bits(c.tint[i]));
        word(b4,256,c.eye);
        for(unsigned eye=0;eye<2;++eye){unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+12,c.flags^(eye!=c.eye?0x00400000u:0u));word(b6,at+44,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+60,bits(c.patch[1]+float(eye!=c.eye)*.25f));
        }
        std::fill_n(static_cast<std::uint32_t*>(buffers[output].mapped),allocated/4,borrowed::Canary);
        std::memset(static_cast<char*>(buffers[output].mapped)+offset,0,128);
        for(auto index:{b4,b6,output})flush(index);
    }
    Observations runPrefix(const borrowed::Case& c){
        prepare(c);begin();PrefixGuard guard;
        VkBufferMemoryBarrier before[2]{};
        for(auto& b:before){b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.size=VK_WHOLE_SIZE;}
        before[0].buffer=buffers[output].value;before[0].srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;before[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        before[1].buffer=buffers[readback].value;before[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;before[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,2,before,0,nullptr);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,originalLayout,0,1,&set,0,nullptr);
        guard.observeSetBind(0,1);++initialSet0Binds;
        vkCmdPushConstants(command,originalLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,c.u.data());++initialPushWrites;
        const VkViewport viewport{0,0,1,1,0,1};const VkRect2D scissor{{0,0},{1,1}};
        vkCmdSetViewport(command,0,1,&viewport);++initialViewportWrites;
        vkCmdSetScissor(command,0,1,&scissor);++initialScissorWrites;
        const auto prefix=layoutFact(originalLayout),clone=layoutFact(pipelineLayout);
        for(unsigned stage=0;stage<3;++stage){
            VkMemoryBarrier reuse{};reuse.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;reuse.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;reuse.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,1,&reuse,0,nullptr,0,nullptr);
            VkClearValue clear{};for(auto& value:clear.color.float32)value=-99;
            VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;
            rp.framebuffer=framebuffer;rp.renderArea={{0,0},{1,1}};rp.clearValueCount=1;rp.pClearValues=&clear;
            vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);
            if(stage==1){
                require(guard.permits(prefix,clone,exportLayout),"compatible untouched Set0 prefix required before driver intervention");
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,probe);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&exportSet,0,nullptr);
                guard.observeSetBind(1,1);++ownedSet1Binds;
            }
            vkCmdDraw(command,3,1,0,0);
            if(stage==1){
                // This is the IMMEDIATE next Vulkan command after the probe draw.
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);++restorations;
                require(!guard.permits(prefix,clone,exportLayout),"higher-set history must remain monotonic after own bind");
            }
            vkCmdEndRenderPass(command);
            VkBufferImageCopy copy{};copy.bufferOffset=stage*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
            vkCmdCopyImageToBuffer(command,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
        }
        VkBufferMemoryBarrier after[2]{};
        for(auto& b:after){b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.size=VK_WHOLE_SIZE;}
        after[0].buffer=buffers[output].value;after[0].srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        after[1].buffer=buffers[readback].value;after[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,2,after,0,nullptr);finish();
        for(auto index:{output,readback})if(!buffers[index].coherent){VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=buffers[index].memory;range.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&range));}
        const auto* words=static_cast<const std::uint32_t*>(buffers[output].mapped);
        for(unsigned i=0;i<allocated/4;++i)if(i<offset/4 || i>=(offset+128)/4)require(words[i]==borrowed::Canary,"prefix observer overwrote bounded record canary");
        Observations result{};std::memcpy(result.original.data(),buffers[readback].mapped,16);
        std::memcpy(result.probe.data(),static_cast<const char*>(buffers[readback].mapped)+16,16);
        std::memcpy(result.restored.data(),static_cast<const char*>(buffers[readback].mapped)+32,16);
        std::memcpy(result.record.data(),static_cast<const char*>(buffers[output].mapped)+offset,128);return result;
    }
};
}

int main(int argc,char** argv){
    try{
        require(argc==4,"usage: prefix-fixture VERTEX ORIGINAL PROBE");
        PrefixGpu gpu;gpu.init();require(gpu.validationEnabled,"Khronos validation active before creation");
        gpu.preparePipelines(argv[1],argv[2],argv[3]);float maximumError=0;unsigned completed=0;
        const auto all=borrowed::cases();
        for(auto index:{0u,17u}){
            const auto& c=all[index];const auto result=gpu.runPrefix(c);
            require(result.original==result.restored,"restoring original pipeline lost Set0/push/dynamic state");
            require(result.original[3]==bits(1.f),"baseline original draw actually covered output");
            for(unsigned i=0;i<4;++i){const auto error=std::abs(borrowed::number(result.original[i])-borrowed::number(result.probe[i]));
                require(std::isfinite(error) && error<=.000002f,"observer original color exceeds inherited SSBO2e-6 bound");maximumError=std::max(maximumError,error);
                require(result.record[28+i]==result.probe[i],"same-invocation SSBO RGBA differs from actual probe attachment");
                require(result.record[6+i]==bits(c.r1[i]) && result.record[10+i]==bits(c.r0[i]),"original Set0 UBO prefix changed during observer");}
            require(result.record[0]==1 && result.record[1]==0x5353424fu && result.record[2]==1 && result.record[3]==32,"complete bounded same-invocation record");
            for(unsigned i=0;i<2;++i)require(result.record[4+i]==bits(c.u[i]) && result.record[14+i]==bits(c.patch[i]),"push-derived original input or UBO patch lost");
            require(result.record[16]==c.flags,"Set0 consumed flags changed");
            for(unsigned i=0;i<3;++i)require(result.record[17+i]==bits(c.tint[i]),"Set0 tint changed");
            ++completed;
        }
        require(gpu.initialSet0Binds==completed && gpu.initialPushWrites==completed && gpu.initialViewportWrites==completed
            && gpu.initialScissorWrites==completed && gpu.ownedSet1Binds==completed && gpu.restorations==completed,"unexpected original-state rebind around intervention");
        require(borrowed::validationErrors.load()==0,"prefix Vulkan validation errors");
        std::cout<<"{\"schema\":\"owned-native-prefix-restore-v1\",\"cases\":"<<completed<<",\"checks\":"<<borrowed::checks
            <<",\"original_layout_set_count\":1,\"clone_layout_set_count\":2,\"distinct_set0_handles\":true,\"identical_set0_definitions\":true,\"complete_push_ranges_equal\":true,\"dynamic_viewport_scissor\":true"
            <<",\"initial_set0_binds\":"<<gpu.initialSet0Binds<<",\"original_state_rebinds_around_probe\":0,\"owned_set1_binds\":"<<gpu.ownedSet1Binds
            <<",\"immediate_original_pipeline_restorations\":"<<gpu.restorations<<",\"set1_remains_bound_unused\":true,\"guard_preissue_refusals\":"<<gpu.guardRefusals
            <<",\"original_restored_bit_identical\":true,\"probe_stock_max_error\":"<<maximumError<<",\"probe_stock_absolute_tolerance\":0.000002"
            <<",\"actual_feature_enabled\":"<<(borrowed::actualOwnedFragmentStores?"true":"false")
            <<",\"validation_enabled\":true,\"validation_errors\":0,\"device_name\":\""<<gpu.properties.deviceName
            <<"\",\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& error){std::cerr<<"prefix fixture failed: "<<error.what()<<"\n";return 1;}
}
