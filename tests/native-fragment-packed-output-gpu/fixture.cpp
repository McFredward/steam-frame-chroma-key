// The approved owned Vulkan harness supplies allocation, original descriptor
// layout, buffers, metadata and cleanup. Only this offscreen test submits/waits.
#define main frame_original_fixture_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main
#include <memory>

namespace {
struct OutputCase {
    Case native;
    Vec expected, colorU;
    float coverage, colorAperture, originalAperture;
    std::uint32_t kind; // 0/1/2 explicit, 3 overlap, 4 emission, 5 emission-over-glass, 6 glass-over-emission.
    std::array<float,1024> texels;
};
static_assert(sizeof(OutputCase)==4232,"fixed owned output case shape");
float floating(std::uint32_t v){float f;std::memcpy(&f,&v,4);return f;}
void near(float a,float b,const char* name){require(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=.000025f,std::string(name)+" differs");}
struct OutputGpu:Gpu {
    void singleDescriptors(unsigned count){
        // Count1 is an explicit validation-negative control for the declared
        // array[3]; valid composed output retains a count3 owned layout.
        vkDestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout={};
        vkDestroyDescriptorPool(device,descriptorPool,nullptr);descriptorPool={};
        vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);descriptorLayout={};
        VkDescriptorSetLayoutBinding bindings[4]{{4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {25,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo l{};l.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;l.bindingCount=4;l.pBindings=bindings;
        check(vkCreateDescriptorSetLayout(device,&l,nullptr,&descriptorLayout));
        VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count},{VK_DESCRIPTOR_TYPE_SAMPLER,1}};
        VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.poolSizeCount=3;pool.pPoolSizes=sizes;pool.maxSets=1;
        check(vkCreateDescriptorPool(device,&pool,nullptr,&descriptorPool));
        VkDescriptorSetAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;a.descriptorPool=descriptorPool;a.descriptorSetCount=1;a.pSetLayouts=&descriptorLayout;
        check(vkAllocateDescriptorSets(device,&a,&set));
        VkDescriptorBufferInfo buffersInfo[2]{{buffers[b4].value,0,272},{buffers[b6].value,0,2800}};
        VkDescriptorImageInfo imagesInfo[4]{{VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}};
        VkWriteDescriptorSet writes[4]{};
        for(unsigned i=0;i<4;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=bindings[i].binding;w.descriptorCount=bindings[i].descriptorCount;w.descriptorType=bindings[i].descriptorType;
            if(i<2)w.pBufferInfo=&buffersInfo[i];else w.pImageInfo=&imagesInfo[i==2?0:3];}
        vkUpdateDescriptorSets(device,4,writes,0,nullptr);
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;info.setLayoutCount=1;info.pSetLayouts=&descriptorLayout;info.pushConstantRangeCount=1;info.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device,&info,nullptr,&pipelineLayout));
    }
    void singleResources(unsigned descriptors){
        resources();
        singleDescriptors(descriptors);
        vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};
        vkDestroyRenderPass(device,renderPass,nullptr);renderPass={};
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;
        attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&reference;
        std::array<VkSubpassDependency,2> dep{};
        dep[0].srcSubpass=VK_SUBPASS_EXTERNAL;dep[0].dstSubpass=0;
        dep[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;dep[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].srcSubpass=0;dep[1].dstSubpass=VK_SUBPASS_EXTERNAL;
        dep[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;info.attachmentCount=1;info.pAttachments=&attachment;
        info.subpassCount=1;info.pSubpasses=&sub;info.dependencyCount=2;info.pDependencies=dep.data();
        check(vkCreateRenderPass(device,&info,nullptr,&renderPass));
        VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=renderPass;
        fb.attachmentCount=1;fb.pAttachments=&images[targets[0]].view;fb.width=fb.height=fb.layers=1;
        check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
    }
    VkPipeline singlePipeline(VkShaderModule vertex,VkShaderModule fragment,const char* entry="PS_DISTORT",const Vec* source=nullptr,bool additive=false){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName=entry;
        std::array<VkSpecializationMapEntry,4> entries{};VkSpecializationInfo specialization{};
        if(source){for(unsigned i=0;i<4;++i)entries[i]={i,i*4,4};specialization.mapEntryCount=4;specialization.pMapEntries=entries.data();specialization.dataSize=16;specialization.pData=source->data();stages[1].pSpecializationInfo=&specialization;}
        VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo rs{};rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;rs.polygonMode=VK_POLYGON_MODE_FILL;rs.lineWidth=1;rs.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
        if(source){blend.blendEnable=VK_TRUE;blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.colorBlendOp=VK_BLEND_OP_ADD;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.alphaBlendOp=VK_BLEND_OP_ADD;
            if(additive){blend.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE;
                blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;}}
        VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
        VkGraphicsPipelineCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;p.stageCount=2;p.pStages=stages;
        p.pVertexInputState=&vi;p.pInputAssemblyState=&ia;p.pViewportState=&vp;p.pRasterizationState=&rs;p.pMultisampleState=&ms;
        p.pColorBlendState=&cb;p.layout=pipelineLayout;p.renderPass=renderPass;
        VkPipeline result{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&p,nullptr,&result));return result;
    }
    void pipelinesSingle(const char* vertexPath,const char* oldPath,const char* outputPath,bool skipOriginal=false){
        VkShaderModule vertex{},old{},output{};auto release=[&]{for(auto m:{output,old,vertex})if(m)vkDestroyShaderModule(device,m,nullptr);};
        try{vertex=module(shader(vertexPath));if(!skipOriginal)old=module(shader(oldPath));output=module(shader(outputPath));if(!skipOriginal)original=singlePipeline(vertex,old);probe=singlePipeline(vertex,output);}catch(...){release();throw;}release();
    }
    void updateSource(const std::array<float,1024>& texels){
        std::memcpy(buffers[upload].mapped,texels.data(),4096);flush(upload);begin();
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=images[source].value;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};
        vkCmdCopyBufferToImage(command,buffers[upload].value,images[source].value,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);finish();
    }
    void metadata(const Case& c){
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        auto word=[&](std::size_t b,unsigned at,std::uint32_t value){std::memcpy(static_cast<char*>(buffers[b].mapped)+at,&value,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,bits(c.tint[i]));
        word(b4,256,c.eye);
        for(unsigned eye=0;eye<2;++eye){unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+3*4,c.flags^(eye!=c.eye?0x00400000u:0u));
            word(b6,at+11*4,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+15*4,bits(c.patch[1]+float(eye!=c.eye)*.25f));}
        flush(b4);flush(b6);
    }
    void startPass(){
        begin();VkClearValue clear{};
        VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;rp.framebuffer=framebuffer;
        rp.renderArea={{0,0},{1,1}};rp.clearValueCount=1;rp.pClearValues=&clear;
        vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);
    }
    void raster(VkPipeline pipeline,const Vec& u){
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);
        vkCmdPushConstants(command,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,u.data());vkCmdDraw(command,3,1,0,0);
    }
    std::array<std::uint32_t,4> readPass(){
        vkCmdEndRenderPass(command);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(command,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
        VkBufferMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=buffers[readback].value;b.size=16;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);finish();
        if(!buffers[readback].coherent){VkMappedMemoryRange r{};r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;r.memory=buffers[readback].memory;r.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&r));}
        std::array<std::uint32_t,4> result{};std::memcpy(result.data(),buffers[readback].mapped,16);return result;
    }
    std::array<std::uint32_t,4> drawSingle(const Case& c,VkPipeline pipeline){metadata(c);startPass();raster(pipeline,c.u);return readPass();}
    std::array<Vec,4> transparentSources(const char* vertexPath,const char* fragmentPath,bool premultiplied){
        VkShaderModule vertex{},fragment{};VkPipeline back{},front{},add{};
        auto release=[&]{for(auto p:{add,front,back})if(p)vkDestroyPipeline(device,p,nullptr);for(auto m:{fragment,vertex})if(m)vkDestroyShaderModule(device,m,nullptr);};
        std::array<Vec,4> result{};
        try{vertex=module(shader(vertexPath));fragment=module(shader(fragmentPath));
            const Vec behind{.8f,.2f,.1f,.25f},glass{.2f,.7f,.3f,.5f},emission{.12f,.18f,.08f,0.f};
            back=singlePipeline(vertex,fragment,"main",&behind);front=singlePipeline(vertex,fragment,"main",&glass);
            if(premultiplied)add=singlePipeline(vertex,fragment,"main",&emission,true);
            for(unsigned kind=0;kind<(premultiplied?4u:1u);++kind){
                startPass();
                if(kind==0||kind==2){raster(back,{0,0,0,0});raster(front,{0,0,0,0});}
                if(kind!=0)raster(add,{0,0,0,0});
                if(kind==3)raster(front,{0,0,0,0});
                const auto observed=readPass();
                for(unsigned i=0;i<3;++i){
                    const float overlap=glass[i]*glass[3]+behind[i]*behind[3]*(1-glass[3]);
                    const float expected=kind==0?overlap:kind==1?emission[i]:kind==2?overlap+emission[i]:glass[i]*glass[3]+emission[i]*(1-glass[3]);
                    result[kind][i]=floating(observed[i]);near(result[kind][i],expected,"true GPU source blend color");
                }
                result[kind][3]=floating(observed[3]);
                near(result[kind][3],kind==0||kind==2?.625f:kind==1?0.f:.5f,"true GPU source coverage");
                if(kind==1)require(result[kind][0]>0&&result[kind][1]>0&&result[kind][2]>0,"actual alpha0 additive emission retained");
            }
        }catch(...){release();throw;}release();return result;
    }
};
std::vector<OutputCase> readCases(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"explicit owned cases");auto bytes=file.tellg();
    require(bytes>=12&&bytes<=12+128*4232,"bounded case file");file.seekg(0);char magic[8]{};std::uint32_t count{};
    require(bool(file.read(magic,8))&&std::memcmp(magic,"PKOUT1\0",8)==0,"owned packed output schema");
    require(bool(file.read(reinterpret_cast<char*>(&count),4))&&count>0&&count<=128,"bounded output cases");
    require(bytes==std::streamoff(12+count*4232),"complete packed output case range");std::vector<OutputCase> result(count);
    require(bool(file.read(reinterpret_cast<char*>(result.data()),count*4232)),"complete output cases");
    for(const auto& c:result){require(c.native.eye<2&&c.kind<7,"owned eye and source kind");
        for(const auto* v:{&c.native.u,&c.native.r0,&c.native.r1,&c.native.patch,&c.native.tint,&c.expected,&c.colorU})for(float f:*v)require(std::isfinite(f),"finite owned input");
        require(std::isfinite(c.coverage)&&c.coverage>=0&&c.coverage<=1,"linear source coverage");
        require(std::isfinite(c.colorAperture)&&std::isfinite(c.originalAperture),"finite source aperture");
        for(float f:c.texels)require(std::isfinite(f)&&f>=0&&f<=1,"normalized owned source texel");}
    return result;
}
}
int main(int argc,char** argv){
    try{
        require(argc==9,"usage: output-fixture VERTEX ORIGINAL OUTPUT CASES SOURCE_FRAGMENT enabled|disabled 1|3|specialized1 straight|premultiplied");
        const bool premultiplied=std::string(argv[8])=="premultiplied";
        require(premultiplied||std::string(argv[8])=="straight","explicit source RGB convention");
        const bool specialized=std::string(argv[7])=="specialized1";
        const unsigned descriptors=std::string(argv[7])=="1"||specialized?1:3;
        require(std::string(argv[7])=="1"||std::string(argv[7])=="3"||specialized,"explicit descriptor count");
        const bool enabled=std::string(argv[6])=="enabled";require(enabled||std::string(argv[6])=="disabled","explicit mode");
        const auto cases=readCases(argv[4]);OutputGpu gpu;gpu.init();require(gpu.validationEnabled,"Khronos validation active");
        gpu.singleResources(descriptors);gpu.pipelinesSingle(argv[1],argv[2],argv[3],specialized);
        require(validationErrors.load()==0,"pipeline creation Vulkan validation errors");
        std::unique_ptr<OutputGpu> separate;
        if(specialized){separate=std::make_unique<OutputGpu>();separate->init();separate->singleResources(3);separate->pipelinesSingle(argv[1],argv[2],argv[2]);}
        OutputGpu& reference=separate?*separate:gpu;
        const auto sources=gpu.transparentSources(argv[1],argv[5],premultiplied);
        unsigned complete=0,partial=0,zero=0,full=0,overlap=0,emissive=0,emissiveGlass=0,gpuOracles=0;float maxError=0;
        for(auto c:cases){
            if(c.kind>=3){ // Feed ACTUAL GPU-produced RGB/coverage; never divide alpha0 emission.
                require(premultiplied||c.kind==3,"additive source requires premultiplied convention");
                const Vec& source=sources[c.kind-3];
                const float divisor=premultiplied?1.f:source[3];
                const float r=source[0]/divisor,g=source[1]/divisor,b=source[2]/divisor;
                const bool ycocg=(c.native.flags>>16)&4;Vec encoded{};
                if(ycocg){const float y=(r+b+2*g)/4;encoded={(r-b)/4+.5f,y,(g-y)/2+.5f,1};}
                else {const float y=(g+.187299997f*b/1.8556f+.468100011f*r/1.57480001f)/(1+.187299997f/1.8556f+.468100011f/1.57480001f);encoded={(r-y)/1.57480001f+.501960814f,y,(b-y)/1.8556f+.501960814f,1};}
                // Marker A=.5 identifies color pixels; A=.75 identifies coverage pixels.
                for(unsigned pixel=0;pixel<256;++pixel){float* t=c.texels.data()+4*pixel;if(t[3]==.5f)std::copy(encoded.begin(),encoded.end(),t);else if(t[3]==.75f)t[1]=source[3];}
                overlap+=c.kind==3;emissive+=c.kind==4;emissiveGlass+=c.kind==5||c.kind==6;
            }
            gpu.updateSource(c.texels);if(specialized)reference.updateSource(c.texels);
            const auto old=reference.drawSingle(c.native,reference.original);
            const auto output=gpu.drawSingle(c.native,gpu.probe),recovered=reference.drawSingle(c.native,reference.original);
            require(old==recovered,"original/off recovery bit identity");
            require(old[3]==bits(1.f),"original remains opaque");
            if(enabled){
                for(unsigned i=0;i<4;++i){float a=floating(output[i]);near(a,c.expected[i],i==3?"composed explicit alpha":"composed premultiplied color");maxError=std::max(maxError,std::abs(a-c.expected[i]));}
                Case color=c.native;color.u=c.colorU;const auto native=reference.drawSingle(color,reference.original);
                if(c.colorAperture>.0001f){for(unsigned i=0;i<3;++i)near(floating(output[i]),floating(native[i])/c.colorAperture*c.originalAperture*(premultiplied?1.f:c.coverage),"independent unchanged original color GPU oracle");++gpuOracles;}
            }else require(output==old,"disabled exact original GPU output");
            zero+=c.kind==0;partial+=c.kind==1;full+=c.kind==2;++complete;
        }
        require(zero&&partial&&full&&overlap,"zero partial full overlapping source exercised");
        require(!premultiplied||(emissive&&emissiveGlass),"actual alpha0 emission and glass source orders exercised");
        require(!enabled||gpuOracles>=8,"independent original-PS GPU oracle coverage");require(validationErrors.load()==0,"Vulkan validation errors");
        std::cout<<"{\"schema\":\"frame-native-packed-output-gpu-v1\",\"cases\":"<<complete<<",\"checks\":"<<checks
            <<",\"zero_cases\":"<<zero<<",\"partial_cases\":"<<partial<<",\"full_cases\":"<<full<<",\"overlapping_gpu_source_cases\":"<<overlap
            <<",\"alpha0_emission_gpu_cases\":"<<emissive<<",\"additive_glass_gpu_cases\":"<<emissiveGlass
            <<",\"original_color_gpu_oracles\":"<<gpuOracles<<",\"max_absolute_error\":"<<maxError
            <<",\"validation_enabled\":true,\"validation_errors\":"<<validationErrors.load()<<",\"color_attachment_count\":1,\"original_off_recovery_bit_identical\":true"
            <<",\"reference_sampled_image_descriptor_count\":3,\"sampled_image_descriptor_count\":"<<descriptors<<",\"device_name\":\""<<gpu.properties.deviceName<<"\",\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"packed output fixture failed: "<<e.what()<<"; checks="<<checks<<'\n';return 1;}
}
