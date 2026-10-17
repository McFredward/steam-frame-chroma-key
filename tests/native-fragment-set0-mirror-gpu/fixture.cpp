// Owned raster oracle for compatible Set0 substitution. Private pinned PS input;
// the raster vertex, higher-set consumers and descriptor values are fixture-owned.
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

namespace borrowed {
#include "../native-fragment-ssbo-gpu/fixture.cpp"
}

namespace {
using borrowed::check;
using borrowed::require;
using Words=std::array<std::uint32_t,4>;
struct MirrorGpu:borrowed::SsboGpu {
    std::array<VkDescriptorSetLayout,4> definitions{};
    std::array<VkPipelineLayout,3> layouts{};
    VkDescriptorPool pool{};
    VkDescriptorSet native{},mirror{},higher1{},higher2{};
    VkPipeline consumer{};
    std::size_t dynamic0{},dynamic2{},regular3{},sentinel{},high1{},high2{};
    VkDeviceSize stride{};
    std::array<std::uint32_t,2> offsets{};
    unsigned copies{},restores{},laterDraws{},submissions{};
    ~MirrorGpu(){if(device){vkDeviceWaitIdle(device);if(consumer)vkDestroyPipeline(device,consumer,nullptr);
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        for(auto p:layouts)if(p)vkDestroyPipelineLayout(device,p,nullptr);
        for(auto d:definitions)if(d)vkDestroyDescriptorSetLayout(device,d,nullptr);}}
    void setupMirror(const char* vertex,const char* control,const char* probeFile,const char* consumerFile){
        setup(false);
        stride=std::max<VkDeviceSize>(16,properties.limits.minUniformBufferOffsetAlignment);
        require(stride<=65536,"bounded real dynamic-uniform alignment");
        dynamic0=buffer(stride*4,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        dynamic2=buffer(stride*4,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        regular3=buffer(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        sentinel=buffer(128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        high1=buffer(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);high2=buffer(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        // Finite owned shape: all seven statically used original bindings and
        // the separately attested unused storage binding8; not a native29 census.
        const VkDescriptorSetLayoutBinding binding[8]{
            {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {8,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {25,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo d{};d.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        d.bindingCount=8;d.pBindings=binding;
        for(unsigned i=0;i<2;++i)check(vkCreateDescriptorSetLayout(device,&d,nullptr,&definitions[i]));
        require(definitions[0]!=definitions[1],"distinct identical full Set0 definitions");
        const VkDescriptorSetLayoutBinding high{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        d.bindingCount=1;d.pBindings=&high;
        for(unsigned i=2;i<4;++i)check(vkCreateDescriptorSetLayout(device,&d,nullptr,&definitions[i]));
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo l{};l.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;l.pushConstantRangeCount=1;l.pPushConstantRanges=&push;
        l.setLayoutCount=1;l.pSetLayouts=&definitions[0];check(vkCreatePipelineLayout(device,&l,nullptr,&layouts[0]));
        l.pSetLayouts=&definitions[1];check(vkCreatePipelineLayout(device,&l,nullptr,&layouts[1]));
        const VkDescriptorSetLayout full[]{definitions[0],definitions[2],definitions[3]};
        l.setLayoutCount=3;l.pSetLayouts=full;check(vkCreatePipelineLayout(device,&l,nullptr,&layouts[2]));
        const VkDescriptorPoolSize sizes[]{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_SAMPLER,2}};
        VkDescriptorPoolCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;p.maxSets=4;
        p.poolSizeCount=5;p.pPoolSizes=sizes;check(vkCreateDescriptorPool(device,&p,nullptr,&pool));
        VkDescriptorSetAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;a.descriptorPool=pool;
        a.descriptorSetCount=4;a.pSetLayouts=definitions.data();VkDescriptorSet sets[4]{};
        check(vkAllocateDescriptorSets(device,&a,sets));native=sets[0];mirror=sets[1];higher1=sets[2];higher2=sets[3];
        VkDescriptorBufferInfo b[]{
            {buffers[dynamic0].value,0,16},{buffers[dynamic2].value,0,16},{buffers[regular3].value,0,16},
            {buffers[b4].value,0,272},{buffers[b6].value,0,2800},{buffers[sentinel].value,0,128}};
        VkDescriptorImageInfo image{VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo sample{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
        VkWriteDescriptorSet writes[8]{};
        for(unsigned i=0;i<8;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=native;w.dstBinding=binding[i].binding;w.descriptorType=binding[i].descriptorType;w.descriptorCount=1;
            if(i<6)w.pBufferInfo=&b[i];else w.pImageInfo=i==6?&image:&sample;}
        vkUpdateDescriptorSets(device,8,writes,0,nullptr);
        VkDescriptorBufferInfo record{buffers[output].value,offset,128};VkWriteDescriptorSet recordWrite{};
        recordWrite.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;recordWrite.dstSet=mirror;recordWrite.dstBinding=8;
        recordWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;recordWrite.descriptorCount=1;recordWrite.pBufferInfo=&record;
        vkUpdateDescriptorSets(device,1,&recordWrite,0,nullptr);
        const unsigned copyBindings[]{0,2,3,4,6,9,25};VkCopyDescriptorSet copy[7]{};
        for(unsigned i=0;i<7;++i){copy[i].sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
            copy[i].srcSet=native;copy[i].dstSet=mirror;copy[i].srcBinding=copy[i].dstBinding=copyBindings[i];copy[i].descriptorCount=1;}
        vkUpdateDescriptorSets(device,0,nullptr,7,copy);copies=7;
        for(unsigned i=0;i<2;++i){VkDescriptorBufferInfo info{buffers[i?high2:high1].value,0,16};
            VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=i?higher2:higher1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.descriptorCount=1;w.pBufferInfo=&info;vkUpdateDescriptorSets(device,1,&w,0,nullptr);}
        // Descriptor content is now immutable through all recordings/replays.
        // Cover transfer readback before the next pass reuses the attachment.
        vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};vkDestroyRenderPass(device,renderPass,nullptr);renderPass={};
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};
        sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        VkSubpassDependency dep[2]{};dep[0].srcSubpass=VK_SUBPASS_EXTERNAL;dep[0].dstSubpass=0;
        dep[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;dep[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].srcSubpass=0;dep[1].dstSubpass=VK_SUBPASS_EXTERNAL;dep[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=1;pass.pAttachments=&attachment;
        pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=2;pass.pDependencies=dep;check(vkCreateRenderPass(device,&pass,nullptr,&renderPass));
        const auto view=images[targets[0]].view;VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass=renderPass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=fb.layers=1;check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        auto vs=module(borrowed::shader(vertex)),ps=module(borrowed::shader(control)),probePs=module(borrowed::shader(probeFile)),later=module(borrowed::shader(consumerFile));
        auto saved=pipelineLayout;
        pipelineLayout=layouts[0];original=singlePipeline(vs,ps);
        pipelineLayout=layouts[1];probe=singlePipeline(vs,probePs);
        pipelineLayout=layouts[2];consumer=singlePipeline(vs,later);
        pipelineLayout=saved;
        for(auto s:{vs,ps,probePs,later})vkDestroyShaderModule(device,s,nullptr);

    }
    void prepare(const borrowed::Case& c){
        // Same bounded input fields as the approved original-PS oracle; no draw
        // is issued while initializing these host-visible fixture resources.
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        auto word=[&](std::size_t b,unsigned at,std::uint32_t v){std::memcpy(static_cast<char*>(buffers[b].mapped)+at,&v,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,borrowed::bits(c.tint[i]));
        word(b4,256,c.eye);
        for(unsigned eye=0;eye<2;++eye){unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,borrowed::bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,borrowed::bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,borrowed::bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+12,c.flags^(eye!=c.eye?0x00400000u:0u));word(b6,at+44,borrowed::bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+60,borrowed::bits(c.patch[1]+float(eye!=c.eye)*.25f));}
        for(auto index:{dynamic0,dynamic2}){std::memset(buffers[index].mapped,0,stride*4);
            for(unsigned slot=0;slot<4;++slot)for(unsigned j=0;j<4;++j)word(index,unsigned(slot*stride)+j*4,borrowed::bits(float(1+slot*4+j)));}
        offsets={std::uint32_t(stride),std::uint32_t(stride*2)};
        for(unsigned i=0;i<32;++i)word(sentinel,i*4,0x1234abcd);
        for(unsigned i=0;i<4;++i){word(high1,i*4,borrowed::bits(float(20+i)));word(high2,i*4,borrowed::bits(float(30+i)));word(regular3,i*4,borrowed::bits(float(40+i)));}
        std::fill_n(static_cast<std::uint32_t*>(buffers[output].mapped),allocated/4,borrowed::Canary);
        std::memset(static_cast<char*>(buffers[output].mapped)+offset,0,128);
        for(auto i:{b4,b6,dynamic0,dynamic2,regular3,sentinel,high1,high2,output})flush(i);
    }
    void draw(VkPipeline p,unsigned readbackSlot){
        VkClearValue clear{};VkRenderPassBeginInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass=renderPass;pass.framebuffer=framebuffer;pass.renderArea={{0,0},{1,1}};pass.clearValueCount=1;pass.pClearValues=&clear;
        vkCmdBeginRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,p);
        vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
        VkBufferImageCopy copy{};copy.bufferOffset=readbackSlot*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(command,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
    }
    std::array<std::uint32_t,32> run(const borrowed::Case& c,bool omitRestore=false,bool wrongOffsets=false){
        prepare(c);begin();
        VkBufferMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;before.buffer=buffers[output].value;
        before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;before.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
        before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;before.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&before,0,nullptr);
        VkDescriptorSet sets[]{native,higher1,higher2};
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layouts[2],0,3,sets,2,offsets.data());
        vkCmdPushConstants(command,layouts[0],VK_SHADER_STAGE_VERTEX_BIT,0,16,c.u.data());
        draw(original,0);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layouts[1],0,1,&mirror,2,offsets.data());draw(probe,1);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
        const std::uint32_t zeroOffsets[2]{};
        if(!omitRestore){vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layouts[0],0,1,&native,2,wrongOffsets?zeroOffsets:offsets.data());++restores;}
        draw(original,2);++laterDraws;
        before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&before,0,nullptr);
        // Both higher sets must survive the one-set mirror and restoration.
        // Native storage8 must still designate the original sentinel buffer.
        draw(consumer,3);++laterDraws;
        // A later native one-set rebind must likewise preserve the already-bound
        // higher sets. Exercise a different (three-set) bind-layout object too.
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layouts[2],0,1,&native,2,offsets.data());
        draw(consumer,4);++laterDraws;
        VkBufferMemoryBarrier after[3]{};
        const std::size_t indices[]{output,sentinel,readback};
        for(unsigned i=0;i<3;++i){auto& b=after[i];b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.buffer=buffers[indices[i]].value;
            b.srcAccessMask=i==2?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.size=VK_WHOLE_SIZE;}
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,3,after,0,nullptr);
        finish();++submissions;
        for(auto i:indices)if(!buffers[i].coherent){VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=buffers[i].memory;range.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&range));}
        const auto* colors=static_cast<const Words*>(buffers[readback].mapped);
        require(colors[0]==colors[1] && colors[0]==colors[2],"control/probe/later native color must be bit-identical");
        const Words expected{borrowed::bits(5.f),borrowed::bits(10.f),borrowed::bits(22.f),borrowed::bits(33.f)};
        // The owned consumer maps a native-binding8 sentinel match to +0, a
        // wrong mirror binding to +100; both mutations remain legal GPU reads.
        Words causal=expected;
        if(omitRestore)for(auto& value:causal)value=borrowed::bits(borrowed::number(value)+100.f);
        if(wrongOffsets){causal[0]=borrowed::bits(1.f);causal[1]=borrowed::bits(2.f);}
        require(colors[3]==causal,"exact causal descriptor/dynamic-offset restoration oracle");
        require(colors[4]==expected,"later native rebind must always recover both higher sets and full offsets");
        for(unsigned i=0;i<32;++i)require(static_cast<const std::uint32_t*>(buffers[sentinel].mapped)[i]==0x1234abcd,"native unused binding8 storage was changed");
        const auto* words=static_cast<const std::uint32_t*>(buffers[output].mapped);
        for(unsigned i=0;i<allocated/4;++i)if(i<offset/4||i>=(offset+128)/4)require(words[i]==borrowed::Canary,"owned record canary");
        std::array<std::uint32_t,32> record{};std::memcpy(record.data(),static_cast<const char*>(buffers[output].mapped)+offset,128);
        require(record[0]==1 && record[1]==0x5353424fu && record[2]==1 && record[3]==32,"same-invocation128B claimed record");
        for(unsigned i=0;i<4;++i)require(record[28+i]==colors[1][i],"record color equals exact observed fragment");
        return record;
    }
};
}
int main(int argc,char** argv){try{
    require(argc==5,"usage: set0-mirror-gpu VERTEX COUNT1_CONTROL MIRROR_PROBE CONSUMER");
    MirrorGpu gpu;gpu.init();require(gpu.validationEnabled,"actual Khronos validation required");gpu.setupMirror(argv[1],argv[2],argv[3],argv[4]);
    unsigned cases=0;for(const auto& c:borrowed::cases()){gpu.run(c);++cases;}
    const auto c=borrowed::cases().front();gpu.run(c,true,false);gpu.run(c,false,true);
    require(borrowed::validationErrors.load()==0,"actual validation must remain clean including causal mutations");
    std::cout<<"{\"schema\":\"owned-compatible-set0-mirror-gpu-v1\",\"cases\":"<<cases<<",\"causal_mutations\":2,\"checks\":"<<borrowed::checks
        <<",\"descriptor_copies_once\":"<<gpu.copies<<",\"later_native_draws\":"<<gpu.laterDraws<<",\"submissions\":"<<gpu.submissions
        <<",\"higher_sets_preserved\":true,\"full_dynamic_offsets_restored\":true,\"native_storage_unchanged\":true,\"original_vertex_replayed\":false,\"production_clone_drawn\":false,\"validation_errors\":0,\"passthrough_authorized\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"Set0 mirror GPU oracle failed: "<<error.what()<<'\n';return 1;}}
