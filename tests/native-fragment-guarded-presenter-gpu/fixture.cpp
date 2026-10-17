// Owned offscreen guard and timing fixture. No native shader body is embedded.
#define main frame_original_fixture_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main
#include <chrono>
#include <memory>
namespace {
float floating(std::uint32_t v){float f;std::memcpy(&f,&v,4);return f;}
void near(float a,float b,const char* name){require(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=.000025f,std::string(name)+" differs");}
struct OutputGpu:Gpu {
    unsigned outputWidth{1},outputHeight{1};
    void singleDescriptors(unsigned count){
        // The explicitly specialized candidate uses count1; unchanged-original
        // comparison instances retain their declared count3 layout separately.
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
    VkPipeline singlePipeline(VkShaderModule vertex,VkShaderModule fragment,const char* entry="PS_DISTORT",const Vec* source=nullptr,bool additive=false,bool premultiplied=false){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName=entry;
        std::array<VkSpecializationMapEntry,4> entries{};VkSpecializationInfo specialization{};
        if(source){for(unsigned i=0;i<4;++i)entries[i]={i,i*4,4};specialization.mapEntryCount=4;specialization.pMapEntries=entries.data();specialization.dataSize=16;specialization.pData=source->data();stages[1].pSpecializationInfo=&specialization;}
        VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,float(outputWidth),float(outputHeight),0,1};VkRect2D scissor{{0,0},{outputWidth,outputHeight}};
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo rs{};rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;rs.polygonMode=VK_POLYGON_MODE_FILL;rs.lineWidth=1;rs.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
        if(source){blend.blendEnable=VK_TRUE;blend.srcColorBlendFactor=premultiplied?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_SRC_ALPHA;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.colorBlendOp=VK_BLEND_OP_ADD;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.alphaBlendOp=VK_BLEND_OP_ADD;
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
};
constexpr unsigned SourceWidth=688,SourceHeight=548;
constexpr std::array<unsigned,12> GuardCodes{{111,153,155,51,111,82,92,219,102,227,121,189}};
constexpr Vec Charts[4]{{0,.25f,1,.5f},{0,0,1,1},{0,.75f,1,1},{.13f,.29f,.81f,.73f}};
constexpr Vec Background{.11f,.17f,.23f,1};
struct GuardCase {
    Case native;
    Vec expected,body,background;
    std::uint32_t mode,cell;
    std::int32_t delta;
    std::uint32_t sourceChart,valid,slot;
};
static_assert(sizeof(GuardCase)==160,"fixed owned guard case");
std::vector<GuardCase> readGuardCases(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"explicit guard cases");
    const auto bytes=file.tellg();require(bytes>=12&&bytes<=12+256*160,"bounded guard cases");
    file.seekg(0);char magic[8]{};std::uint32_t count{};
    require(bool(file.read(magic,8))&&std::memcmp(magic,"GVRGUAR2",8)==0,"owned guard case schema");
    require(bool(file.read(reinterpret_cast<char*>(&count),4))&&count>0&&count<=256,"bounded guard count");
    require(bytes==std::streamoff(12+count*160),"complete guard case range");
    std::vector<GuardCase> out(count);require(bool(file.read(reinterpret_cast<char*>(out.data()),count*160)),"complete guard cases");
    for(const auto& c:out){
        require(c.native.eye<2&&c.mode<=10&&c.cell<12&&c.sourceChart<4&&c.valid<2&&c.slot<2,"bounded owned case fields");
        // u/R1 intentionally include invalid finite and NaN controls. They are
        // supplied to the unchanged original comparison too, never dereferenced.
        for(const auto* v:{&c.expected,&c.body,&c.background})for(float f:*v)require(std::isfinite(f),"finite expected source/output");
        require(c.body[3]>=0&&c.body[3]<=1,"owned linear partial coverage");
        require(c.background==Background,"fixed source-over background");
    }
    return out;
}
Vec encodeColor(const Vec& rgb,std::uint32_t flags){
    const float r=rgb[0],g=rgb[1],b=rgb[2];
    if((flags>>16)&4){const float y=(r+b+2*g)/4;return {(r-b)/4+.5f,y,(g-y)/2+.5f,1};}
    const float y=(g+.187299997f*b/1.8556f+.468100011f*r/1.57480001f)/(1+.187299997f/1.8556f+.468100011f/1.57480001f);
    return {(r-y)/1.57480001f+.501960814f,y,(b-y)/1.8556f+.501960814f,1};
}
std::vector<float> sourceFor(const GuardCase& c){
    std::vector<float> out(SourceWidth*SourceHeight*4);const Vec color=encodeColor(c.body,c.native.flags);
    const Vec& chart=Charts[c.sourceChart];
    for(unsigned y=0;y<SourceHeight;++y)for(unsigned x=0;x<SourceWidth;++x){
        const double qx=((double(x)+.5)/SourceWidth-chart[0])/(chart[2]-chart[0]);
        const double qy=((double(y)+.5)/SourceHeight-chart[1])/(chart[3]-chart[1]);
        const double px=qx*688,py=qy*548;Vec v{.24f,.53f,.69f,1};
        if(px>=16&&px<336&&py>=244&&py<436)v=color;
        if(px>=352&&px<672&&py>=244&&py<436)v={.31f,c.body[3],.68f,1};
        if(c.mode==7)v={.21f,.5f,.72f,1};
        const bool strip=c.mode!=5&&c.mode!=6&&c.mode!=7;
        if(strip){
            double gx=px,gy=py;
            if(c.mode==2)gx-=26;
            if(c.mode==3)gx=688-px;
            if(c.mode==4){gx=16+(px-16)*2;gy=464+(py-464)*2;}
            if(gy>=464&&gy<496)for(unsigned cell=0;cell<12;++cell)
                if(gx>=16+52*cell&&gx<48+52*cell){
                    int code=static_cast<int>(GuardCodes[cell]);
                    if(c.mode==1)code^=0x5a;
                    if(c.mode==8&&cell==c.cell)code+=c.delta;
                    require(code>=0&&code<=255,"normalized guard mutation");
                    v={.31f,float(code)/255,.68f,1};break;
                }
        }
        if(c.mode==6&&py>=16&&py<20&&px>=16&&px<272){
            // Owned old-style magic band without the new strip; this is not a
            // claim that the former full app-payload packet is authenticated.
            constexpr unsigned char magic[8]{'F','A','P','P','R','O','B','1'};
            const unsigned bit=static_cast<unsigned>((px-16)/4);
            const bool one=(magic[bit/8]>>(7-bit%8))&1;
            v={.31f,one?1.f:0.f,.68f,1};
        }
        std::copy(v.begin(),v.end(),out.begin()+4*(y*SourceWidth+x));
    }
    return out;
}
struct GuardGpu:OutputGpu {
    std::array<std::size_t,2> sourceImages{};
    std::array<VkDescriptorSet,2> sourceSets{};
    std::array<bool,2> initialized{};
    std::array<std::array<VkCommandBuffer,3>,2> commands{};
    VkPipeline over{};VkQueryPool queryPool{};std::uint32_t timestampBits{};
    unsigned recordings{};
    ~GuardGpu(){if(device){vkDeviceWaitIdle(device);if(over)vkDestroyPipeline(device,over,nullptr);if(queryPool)vkDestroyQueryPool(device,queryPool,nullptr);}}
    void guardResources(unsigned width=1,unsigned height=1){
        outputWidth=width;outputHeight=height;singleResources(3);
        if(width!=1||height!=1){
            vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};
            targets[0]=image(VK_FORMAT_R32G32B32A32_SFLOAT,width,height,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            VkFramebufferCreateInfo f{};f.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;f.renderPass=renderPass;f.attachmentCount=1;
            f.pAttachments=&images[targets[0]].view;f.width=width;f.height=height;f.layers=1;check(vkCreateFramebuffer(device,&f,nullptr,&framebuffer));
        }
        upload=buffer(SourceWidth*SourceHeight*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        for(auto& i:sourceImages)i=image(VK_FORMAT_R32G32B32A32_SFLOAT,SourceWidth,SourceHeight,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        vkDestroyDescriptorPool(device,descriptorPool,nullptr);descriptorPool={};set={};
        VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,6},{VK_DESCRIPTOR_TYPE_SAMPLER,2}};
        VkDescriptorPoolCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;p.poolSizeCount=3;p.pPoolSizes=sizes;p.maxSets=2;
        check(vkCreateDescriptorPool(device,&p,nullptr,&descriptorPool));
        std::array<VkDescriptorSetLayout,2> layouts{descriptorLayout,descriptorLayout};
        VkDescriptorSetAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;a.descriptorPool=descriptorPool;a.descriptorSetCount=2;a.pSetLayouts=layouts.data();
        check(vkAllocateDescriptorSets(device,&a,sourceSets.data()));
        VkDescriptorBufferInfo bi[2]{{buffers[b4].value,0,272},{buffers[b6].value,0,2800}};
        const unsigned binding[4]{4,6,9,25};
        for(unsigned slot=0;slot<2;++slot){
            VkDescriptorImageInfo ii[4]{{VK_NULL_HANDLE,images[sourceImages[slot]].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE,images[sourceImages[slot]].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE,images[sourceImages[slot]].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}};
            VkWriteDescriptorSet w[4]{};
            for(unsigned i=0;i<4;++i){w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].dstSet=sourceSets[slot];w[i].dstBinding=binding[i];w[i].descriptorCount=i==2?3:1;
                w[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;
                if(i<2)w[i].pBufferInfo=&bi[i];else w[i].pImageInfo=&ii[i==2?0:3];}
            vkUpdateDescriptorSets(device,4,w,0,nullptr);
        }
        std::array<VkCommandBuffer,6> allocated{};a={};
        VkCommandBufferAllocateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;cb.commandPool=commandPool;cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb.commandBufferCount=6;
        check(vkAllocateCommandBuffers(device,&cb,allocated.data()));
        for(unsigned slot=0;slot<2;++slot)std::copy_n(allocated.begin()+slot*3,3,commands[slot].begin());
        std::uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> qp(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,qp.data());timestampBits=qp[family].timestampValidBits;
        if(timestampBits){VkQueryPoolCreateInfo q{};q.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=2;check(vkCreateQueryPool(device,&q,nullptr,&queryPool));}
        require(sourceImages[0]!=sourceImages[1]&&images[sourceImages[0]].value!=images[sourceImages[1]].value,"two distinct owned source images");
    }
    VkPipeline premultipliedPipeline(VkShaderModule vertex,VkShaderModule fragment){
        // Reuse the exact same single-output state and enable source-over only.
        const Vec dummy{0,0,0,0};return singlePipeline(vertex,fragment,"PS_DISTORT",&dummy,false,true);
    }
    void guardPipelines(const char* vertexPath,const char* originalPath,const char* guardedPath,bool makeOver){
        VkShaderModule vertex{},old{},guarded{};
        auto release=[&]{for(auto m:{guarded,old,vertex})if(m)vkDestroyShaderModule(device,m,nullptr);};
        try{vertex=module(shader(vertexPath));old=module(shader(originalPath));guarded=module(shader(guardedPath));
            original=singlePipeline(vertex,old);probe=singlePipeline(vertex,guarded);
            if(makeOver)over=premultipliedPipeline(vertex,guarded);
        }catch(...){release();throw;}release();
    }
    void update(unsigned slot,const std::vector<float>& texels){
        require(slot<2&&texels.size()==SourceWidth*SourceHeight*4,"complete owned texture update");
        std::memcpy(buffers[upload].mapped,texels.data(),texels.size()*4);flush(upload);begin();
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=images[sourceImages[slot]].value;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        b.oldLayout=initialized[slot]?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask=initialized[slot]?VK_ACCESS_SHADER_READ_BIT:0;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,initialized[slot]?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={SourceWidth,SourceHeight,1};
        vkCmdCopyBufferToImage(command,buffers[upload].value,images[sourceImages[slot]].value,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);finish();initialized[slot]=true;
    }
    void record(const Vec& u){
        const VkPipeline pipes[3]{original,probe,over};
        for(unsigned slot=0;slot<2;++slot)for(unsigned mode=0;mode<3;++mode){
            if(!pipes[mode])continue;
            const auto cb=commands[slot][mode];check(vkResetCommandBuffer(cb,0));
            VkCommandBufferBeginInfo b{};b.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;check(vkBeginCommandBuffer(cb,&b));
            if(queryPool){vkCmdResetQueryPool(cb,queryPool,0,2);vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queryPool,0);}
            VkClearValue clear{};std::copy(Background.begin(),Background.end(),clear.color.float32);
            VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;rp.framebuffer=framebuffer;rp.renderArea={{0,0},{outputWidth,outputHeight}};rp.clearValueCount=1;rp.pClearValues=&clear;
            vkCmdBeginRenderPass(cb,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes[mode]);
            vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&sourceSets[slot],0,nullptr);
            vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,u.data());vkCmdDraw(cb,3,1,0,0);vkCmdEndRenderPass(cb);
            if(queryPool)vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queryPool,1);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
            vkCmdCopyImageToBuffer(cb,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
            VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffers[readback].value;barrier.size=16;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);check(vkEndCommandBuffer(cb));
        }++recordings;
    }
    struct Result{std::array<std::uint32_t,4> rgba{};double cpuMs{},gpuMs{};};
    Result execute(unsigned slot,unsigned mode){
        require(slot<2&&mode<3&&initialized[slot],"recorded draw requires initialized source");
        const auto start=std::chrono::steady_clock::now();check(vkResetFences(device,1,&fence));
        VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&commands[slot][mode];
        check(vkQueueSubmit(queue,1,&s,fence));check(vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ull));
        Result out;out.cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(!buffers[readback].coherent){VkMappedMemoryRange r{};r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;r.memory=buffers[readback].memory;r.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&r));}
        std::memcpy(out.rgba.data(),buffers[readback].mapped,16);
        if(queryPool){std::uint64_t q[2]{};check(vkGetQueryPoolResults(device,queryPool,0,2,sizeof(q),q,8,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
            const auto delta=timestampBits==64?q[1]-q[0]:(q[1]-q[0])&((1ull<<timestampBits)-1);
            out.gpuMs=double(delta)*properties.limits.timestampPeriod/1e6;}
        return out;
    }
};
}
int main(int argc,char** argv){
    try{
        require(argc==6||argc==8,"usage: guard-fixture VERTEX ORIGINAL GUARDED CASES correctness|benchmark [WIDTH HEIGHT]");
        const bool timing=std::string(argv[5])=="benchmark";
        require(timing||std::string(argv[5])=="correctness","explicit fixture mode");
        require(timing?argc==8:argc==6,"bounded mode arguments");
        const auto input=readGuardCases(argv[4]);GuardGpu gpu;gpu.init();
        if(timing){
            require(!gpu.validationEnabled,"timings exclude validation layer overhead");
            const auto width=static_cast<unsigned>(std::stoul(argv[6])),height=static_cast<unsigned>(std::stoul(argv[7]));
            require(width>0&&height>0&&std::uint64_t(width)*height<=1152ull*4608,"bounded timing extent");
            gpu.guardResources(width,height);gpu.guardPipelines(argv[1],argv[2],argv[3],false);
            auto c=input.front();require(c.valid&&c.sourceChart==0,"timing uses explicit actual-quarter source chart");
            gpu.metadata(c.native);gpu.record({0,.25f,1,.25f});
            std::cout<<"{\"schema\":\"frame-native-guard-cost-v1\",\"width\":"<<width<<",\"height\":"<<height
                <<",\"pixels\":"<<std::uint64_t(width)*height<<",\"validation_enabled\":false,\"timestamp_bits\":"<<gpu.timestampBits
                <<",\"timestamp_period_ns\":"<<gpu.properties.limits.timestampPeriod<<",\"device_name\":\""<<gpu.properties.deviceName<<"\",\"runs\":{";
            const char* names[3]{"original","guard_valid","guard_off"};
            for(unsigned mode=0;mode<3;++mode){
                c.mode=mode==2?7:0;gpu.update(0,sourceFor(c));
                const auto pipeline=mode==0?0:1;const auto warm=gpu.execute(0,pipeline);
                require(warm.cpuMs<9000,"bounded warmed timing command");
                if(mode)std::cout<<',';
                std::cout<<'\"'<<names[mode]<<"\":{\"cpu_ms\":[";
                std::array<GuardGpu::Result,3> samples{};
                for(auto& s:samples)s=gpu.execute(0,pipeline);
                for(unsigned i=0;i<3;++i){if(i)std::cout<<',';std::cout<<samples[i].cpuMs;}
                std::cout<<"],\"gpu_ms\":[";for(unsigned i=0;i<3;++i){if(i)std::cout<<',';std::cout<<samples[i].gpuMs;}
                std::cout<<"],\"warmed_cpu_ms\":"<<warm.cpuMs<<",\"warmed_gpu_ms\":"<<warm.gpuMs<<'}';
            }
            std::cout<<"},\"native_frame_timing_proven\":false,\"passthrough_authorized\":false}\n";return 0;
        }
        require(gpu.validationEnabled,"Khronos validation active from initialization");
        gpu.guardResources();gpu.guardPipelines(argv[1],argv[2],argv[3],true);
        unsigned valid=0,invalid=0,sourceOver=0,sameCommandTransitions=0,cellCases=0,bodyChanged=0;
        Vec recorded{};bool recordedOnce=false;unsigned firstRecording=0;float maxError=0;
        for(unsigned index=0;index<input.size();++index){
            const auto& c=input[index];
            if(!recordedOnce||std::memcmp(recorded.data(),c.native.u.data(),16)!=0){gpu.record(c.native.u);recorded=c.native.u;recordedOnce=true;}
            if(index==0)firstRecording=gpu.recordings;
            if(index<3){require(gpu.recordings==firstRecording,"ON OFF ON uses same already-recorded commands");++sameCommandTransitions;}
            gpu.metadata(c.native);gpu.update(c.slot,sourceFor(c));
            const auto original=gpu.execute(c.slot,0).rgba;
            const auto guarded=gpu.execute(c.slot,1).rgba;
            const auto restored=gpu.execute(c.slot,0).rgba;
            require(original==restored,"unchanged original command restores bit-identically");
            if(c.valid){
                require(original[3]==bits(1.f),"valid source comparison original is opaque");
                for(unsigned i=0;i<4;++i){near(floating(guarded[i]),c.expected[i],"valid guarded contribution");maxError=std::max(maxError,std::abs(floating(guarded[i])-c.expected[i]));}
                const auto over=gpu.execute(c.slot,2).rgba;
                for(unsigned i=0;i<4;++i)near(floating(over[i]),c.expected[i]+c.background[i]*(1-c.expected[3]),"actual partial-alpha source over");
                ++valid;++sourceOver;
            }else{require(guarded==original,"invalid or OFF must output exact ORIGINAL994 bits");++invalid;}
            cellCases+=c.mode==8;bodyChanged+=c.mode==9;
        }
        require(valid&&invalid&&sourceOver&&sameCommandTransitions==3&&cellCases>=48&&bodyChanged,"guard acceptance refusal and recognition limitation covered");
        require(validationErrors.load()==0,"Vulkan or synchronization validation error");
        std::cout<<"{\"schema\":\"frame-native-guard-gpu-v1\",\"cases\":"<<input.size()<<",\"checks\":"<<checks
            <<",\"valid\":"<<valid<<",\"invalid_exact_original\":"<<invalid<<",\"source_over_cases\":"<<sourceOver
            <<",\"same_recorded_on_off_on\":true,\"single_cell_tolerance_cases\":"<<cellCases
            <<",\"copied_valid_strip_changed_body_accepted\":"<<bodyChanged<<",\"different_source_images\":2,\"recording_generations\":"<<gpu.recordings
            <<",\"max_absolute_error\":"<<maxError<<",\"validation_enabled\":true,\"validation_errors\":"<<validationErrors.load()
            <<",\"color_attachment_count\":1,\"layout_b9_descriptor_count\":3,\"device_name\":\""<<gpu.properties.deviceName
            <<"\",\"native_frame_association_proven\":false,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"guard fixture failed: "<<e.what()<<"; checks="<<checks<<'\n';return 1;}
}
