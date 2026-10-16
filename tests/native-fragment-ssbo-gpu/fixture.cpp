// Inert host-only fixture. Reuse the approved resource/queue/failure cleanup
// helpers. This compile-time device adapter calls the REAL vkCreateDevice with
// fragmentStoresAndAtomics explicitly enabled; it is not a receiver hook.
#include <vulkan/vulkan.h>
bool enableOwnedFragmentStores=true, actualOwnedFragmentStores=false;
VkResult createOwnedDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkDevice* device){
    if(!input || !input->pEnabledFeatures)return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPhysicalDeviceFeatures supported{};vkGetPhysicalDeviceFeatures(physical,&supported);
    if(enableOwnedFragmentStores && !supported.fragmentStoresAndAtomics)return VK_ERROR_FEATURE_NOT_PRESENT;
    auto features=*input->pEnabledFeatures;
    features.fragmentStoresAndAtomics=enableOwnedFragmentStores?VK_TRUE:VK_FALSE;
    auto info=*input;info.pEnabledFeatures=&features;
    const auto result=vkCreateDevice(physical,&info,allocator,device);
    actualOwnedFragmentStores=result==VK_SUCCESS && features.fragmentStoresAndAtomics==VK_TRUE;
    return result;
}
#define vkCreateDevice createOwnedDevice
#define main approved_ssa_fixture_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main
#undef vkCreateDevice

namespace {
constexpr std::uint32_t Canary=0xdecafbad;
struct Result {std::array<std::uint32_t,4> rgba;std::array<std::uint32_t,32> record;};
struct SsboGpu:Gpu{
    VkDescriptorSetLayout exportLayout{};VkDescriptorPool exportPool{};VkDescriptorSet exportSet{};
    std::size_t output{};VkDeviceSize offset{},allocated{};bool extended{};
    ~SsboGpu(){
        if(device){vkDeviceWaitIdle(device);if(exportPool)vkDestroyDescriptorPool(device,exportPool,nullptr);if(exportLayout)vkDestroyDescriptorSetLayout(device,exportLayout,nullptr);}
    }
    void setup(bool adversarial){
        require(actualOwnedFragmentStores,"actually enabled fragmentStoresAndAtomics required BEFORE pipeline creation");
        require(properties.limits.maxBoundDescriptorSets>=2 && properties.limits.maxPerStageDescriptorStorageBuffers>=1
                && properties.limits.maxStorageBufferRange>=256,"actual owned SSBO descriptor limits");
        resources();extended=adversarial;
        const auto alignment=properties.limits.minStorageBufferOffsetAlignment;
        require(alignment>0 && alignment<=65536,"bounded real storage offset alignment");
        offset=((64+alignment-1)/alignment)*alignment;allocated=offset+256+64;
        output=buffer(allocated,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;layout.bindingCount=1;layout.pBindings=&binding;
        check(vkCreateDescriptorSetLayout(device,&layout,nullptr,&exportLayout));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.poolSizeCount=1;pool.pPoolSizes=&size;pool.maxSets=1;
        check(vkCreateDescriptorPool(device,&pool,nullptr,&exportPool));
        VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate.descriptorPool=exportPool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&exportLayout;
        check(vkAllocateDescriptorSets(device,&allocate,&exportSet));
        // Normal descriptor is exactly128B. The sole overflow MUTANT run uses
        //256B so its canary corruption is a legal access, not descriptor OOB.
        VkDescriptorBufferInfo bufferInfo{buffers[output].value,offset,adversarial?256u:128u};
        VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=exportSet;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bufferInfo;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        vkDestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout=VK_NULL_HANDLE;
        VkDescriptorSetLayout layouts[2]{descriptorLayout,exportLayout};VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=2;pl.pSetLayouts=layouts;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device,&pl,nullptr,&pipelineLayout));
        vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer=VK_NULL_HANDLE;
        vkDestroyRenderPass(device,renderPass,nullptr);renderPass=VK_NULL_HANDLE;
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dep;
        check(vkCreateRenderPass(device,&rp,nullptr,&renderPass));
        const auto view=images[targets[0]].view;VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=renderPass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=fb.layers=1;
        check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
    }
    VkPipeline singlePipeline(VkShaderModule vertex,VkShaderModule fragment){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="PS_DISTORT";
        VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
        VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vi;info.pInputAssemblyState=&ia;info.pViewportState=&vp;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pColorBlendState=&cb;info.layout=pipelineLayout;info.renderPass=renderPass;
        VkPipeline result{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&result));return result;
    }
    void createPipelines(const char* vertexPath,const char* oldPath,const char* newPath){
        VkShaderModule vertex{},old{},instrumented{};
        auto release=[&]{for(auto value:{instrumented,old,vertex})if(value)vkDestroyShaderModule(device,value,nullptr);};
        try{vertex=module(shader(vertexPath));old=module(shader(oldPath));instrumented=module(shader(newPath));original=singlePipeline(vertex,old);probe=singlePipeline(vertex,instrumented);}catch(...){release();throw;}
        release();
    }
    Result run(const Case& c,VkPipeline pipeline,bool reset=true,unsigned instances=1){
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        auto word=[&](std::size_t b,unsigned at,std::uint32_t v){std::memcpy(static_cast<char*>(buffers[b].mapped)+at,&v,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,bits(c.tint[i]));
        word(b4,256,c.eye);
        for(unsigned eye=0;eye<2;++eye){unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+12,c.flags^(eye!=c.eye?0x00400000u:0u));word(b6,at+44,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+60,bits(c.patch[1]+float(eye!=c.eye)*.25f));}
        if(reset){auto* words=static_cast<std::uint32_t*>(buffers[output].mapped);std::fill_n(words,allocated/4,Canary);std::memset(static_cast<char*>(buffers[output].mapped)+offset,0,128);flush(output);}
        flush(b4);flush(b6);begin();
        VkBufferMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;before.buffer=buffers[output].value;before.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&before,0,nullptr);
        VkClearValue clear{};for(auto& f:clear.color.float32)f=-99;
        VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;rp.framebuffer=framebuffer;rp.renderArea={{0,0},{1,1}};rp.clearValueCount=1;rp.pClearValues=&clear;
        vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkDescriptorSet sets[2]{set,exportSet};vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,2,sets,0,nullptr);vkCmdPushConstants(command,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,c.u.data());vkCmdDraw(command,3,instances,0,0);vkCmdEndRenderPass(command);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};vkCmdCopyImageToBuffer(command,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
        VkBufferMemoryBarrier after[2]{};for(auto& b:after){b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.size=VK_WHOLE_SIZE;}
        after[0].buffer=buffers[output].value;after[0].srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;after[1].buffer=buffers[readback].value;after[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,2,after,0,nullptr);finish();
        for(auto index:{output,readback})if(!buffers[index].coherent){VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=buffers[index].memory;range.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&range));}
        const auto* words=static_cast<const std::uint32_t*>(buffers[output].mapped);
        for(unsigned i=0;i<allocated/4;++i)if(i<offset/4 || i>=(offset+128)/4)require(words[i]==Canary,"SSBO logical record canary overwritten");
        Result result{};std::memcpy(result.rgba.data(),buffers[readback].mapped,16);std::memcpy(result.record.data(),static_cast<const char*>(buffers[output].mapped)+offset,128);return result;
    }
};
float number(std::uint32_t v){float result;std::memcpy(&result,&v,4);return result;}
}

int main(int argc,char** argv){
    try{
        require(argc==5,"usage: ssbo-fixture VERTEX ORIGINAL PROBE normal|no-feature|overflow|no-pixel");
        const std::string mode=argv[4];require(mode=="normal" || mode=="no-feature" || mode=="overflow" || mode=="no-pixel","known fixture mode");
        enableOwnedFragmentStores=mode!="no-feature";SsboGpu gpu;gpu.init();require(gpu.validationEnabled,"Khronos validation active from initialization");
        if(!actualOwnedFragmentStores){require(mode=="no-feature" && validationErrors.load()==0,"feature refusal validation clean");std::cout<<"{\"admitted\":false,\"actual_feature_enabled\":false,\"pipeline_created\":false,\"validation_enabled\":true,\"validation_errors\":0}\n";return 0;}
        gpu.setup(mode=="overflow");gpu.createPipelines(argv[1],argv[2],argv[3]);
        unsigned completed=0,bitIdentical=0;float maxError=0;
        for(const auto& c:cases()){
            const auto old=gpu.run(c,gpu.original),observed=gpu.run(c,gpu.probe,true,2);
            for(unsigned i=0;i<4;++i){const float error=std::abs(number(old.rgba[i])-number(observed.rgba[i]));require(std::isfinite(error) && error<=.000002f,"SSBO original output0 exceeds explicit2e-6 FP bound");maxError=std::max(maxError,error);}
            bitIdentical+=old.rgba==observed.rgba;require(old.rgba[3]==bits(1.f) && observed.rgba[3]==old.rgba[3],"SSBO original alpha unchanged");
            if(mode=="no-pixel"){require(std::all_of(observed.record.begin(),observed.record.end(),[](auto v){return v==0;}),"unselected fragment wrote record");++completed;continue;}
            const auto& r=observed.record;require(r[0]==1 && r[1]==0x5353424fu && r[2]==1 && r[3]==32,"complete claimed SSBO header after fence");
            for(unsigned i=0;i<2;++i)require(r[4+i]==bits(c.u[i]) && r[14+i]==bits(c.patch[i]),"SSBO u or patch raw bits differ");
            for(unsigned i=0;i<4;++i){require(r[6+i]==bits(c.r1[i]),"SSBO R1 raw bits differ");require(r[10+i]==bits(c.r0[i]),"SSBO R0 raw bits differ");require(r[28+i]==observed.rgba[i],"SSBO original same-invocation RGBA bits differ");}
            require(r[16]==c.flags,"SSBO consumed flag bits differ");for(unsigned i=0;i<3;++i)require(r[17+i]==bits(c.tint[i]),"SSBO tint bits differ");
            auto sample=[&](float u,float v,unsigned at){unsigned x=static_cast<unsigned>(std::max(0,std::min(15,static_cast<int>(std::floor(u*16)))));unsigned y=static_cast<unsigned>(std::max(0,std::min(15,static_cast<int>(std::floor(v*16)))));const float expected[4]{float(17+x*7+y*3)/255,float(31+x*2+y*8)/255,float(19+x*5+y*4)/255,1};for(unsigned i=0;i<4;++i)require(r[at+i]==bits(expected[i]),"SSBO original sampled-component bits differ");};
            sample(c.u[0],c.u[1],20);sample(c.patch[0]+(c.u[0]-c.r0[0])/(c.r0[2]-c.r0[0])*(c.r1[2]-c.r1[0]),c.patch[1]+(c.u[1]-c.r0[1])/(c.r0[3]-c.r0[1])*(c.r1[3]-c.r1[1]),24);
            auto next=c;next.u[0]+=.03125f;const auto repeated=gpu.run(next,gpu.probe,false,2);require(repeated.record==r,"atomic claim allowed a later invocation to overwrite completed record");++completed;
        }
        require(validationErrors.load()==0,"SSBO Vulkan validation errors");
        std::cout<<"{\"schema\":\"frame-native-ssbo-gpu-proof-v1\",\"cases\":"<<completed<<",\"checks\":"<<checks<<",\"actual_feature_enabled\":true,\"validation_enabled\":true,\"validation_errors\":0,\"active_color_attachments\":1,\"logical_record_bytes\":128,\"descriptor_range\":"<<(gpu.extended?256:128)<<",\"output0_bit_identical_cases\":"<<bitIdentical<<",\"output0_max_absolute_error\":"<<maxError<<",\"output0_absolute_tolerance\":0.000002,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& error){std::cerr<<"ssbo fixture failed: "<<error.what()<<"; checks="<<checks<<'\n';return 1;}
}
