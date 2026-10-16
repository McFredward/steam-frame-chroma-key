// Inert owned-host cohort proof. No receiver hook, cross-queue guarantee or
// native pipeline admission. The TARGET command buffer is recorded exactly once.
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
std::vector<std::pair<VkBuffer,VkBufferUsageFlags>> actualBufferUsage;
std::vector<std::pair<VkImage,VkImageUsageFlags>> actualImageUsage;
VkResult cohortCreateBuffer(VkDevice device,const VkBufferCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkBuffer* result){
    auto info=*input;
    if(info.usage&(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        info.usage|=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    auto status=vkCreateBuffer(device,&info,allocator,result);
    if(status==VK_SUCCESS)actualBufferUsage.emplace_back(*result,info.usage);
    return status;
}
VkResult cohortCreateImage(VkDevice device,const VkImageCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkImage* result){
    auto info=*input;
    if(info.usage&VK_IMAGE_USAGE_SAMPLED_BIT)info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto status=vkCreateImage(device,&info,allocator,result);
    if(status==VK_SUCCESS)actualImageUsage.emplace_back(*result,info.usage);
    return status;
}
}
// Preinclude every borrowed standard header above. Its uncalled old main lives
// in this namespace; existing resource/lifecycle helpers are included unchanged.
#define vkCreateBuffer cohortCreateBuffer
#define vkCreateImage cohortCreateImage
namespace borrowed {
#include "../native-fragment-ssbo-gpu/fixture.cpp"
}
#undef vkCreateBuffer
#undef vkCreateImage

namespace {
using borrowed::Case;
using borrowed::Result;
using borrowed::bits;
using borrowed::check;
using borrowed::require;

constexpr unsigned Repeats=3;
constexpr VkDeviceSize ImageBytes=16*16*16;
using Record=std::array<std::uint32_t,32>;

Case cohort(unsigned epoch){
    const float step=float(epoch)*.015625f;
    return {epoch%2,0x02018018u|((epoch%4)<<17),{.3515625f,.4453125f,.625f,.875f},
        {.18f+step,.21f+step,.63f-step,.72f-step},
        {.04f+step,.08f+step,.94f-step,.92f-step},
        {.07f+2*step,.13f+step,0,0},{.73f-step,.59f+step,.91f-step,1}};
}
std::array<std::uint32_t,4> sourceTexel(unsigned epoch,unsigned x,unsigned y){
    return {bits(float(17+x*7+y*3+epoch*83)/1024),
        bits(float(31+x*2+y*8+epoch*61)/1024),bits(float(19+x*5+y*4+epoch*47)/1024),bits(1.f)};
}
std::array<std::uint32_t,4> sourceSample(unsigned epoch,float u,float v){
    const auto x=unsigned(std::max(0,std::min(15,int(std::floor(u*16)))));
    const auto y=unsigned(std::max(0,std::min(15,int(std::floor(v*16)))));
    return sourceTexel(epoch,x,y);
}
std::uint64_t fingerprint(const void* data,std::size_t bytes){
    auto* values=static_cast<const unsigned char*>(data);std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<bytes;++i){hash^=values[i];hash*=1099511628211ull;}return hash;
}

struct CohortGpu:borrowed::SsboGpu{
    VkCommandBuffer target{},update{};
    std::size_t stage4{},stage6{},sourceCopy{};
    unsigned targetRecordings{},updateRecordings{},targetSubmissions{};
    void initialize(){
        setup(false);
        auto usage=[&](std::size_t index){
            auto found=std::find_if(actualBufferUsage.begin(),actualBufferUsage.end(),[&](auto p){return p.first==buffers[index].value;});
            require(found!=actualBufferUsage.end(),"real buffer create flags observed");return found->second;
        };
        for(auto index:{output,b4,b6})require((usage(index)&VK_BUFFER_USAGE_TRANSFER_DST_BIT)!=0,"real source/record transfer-dst usage");
        auto found=std::find_if(actualImageUsage.begin(),actualImageUsage.end(),[&](auto p){return p.first==images[source].value;});
        require(found!=actualImageUsage.end() && (found->second&VK_IMAGE_USAGE_TRANSFER_SRC_BIT),"real sampled image transfer-src usage");
        stage4=buffer(272,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);stage6=buffer(2800,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        sourceCopy=buffer(ImageBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkCommandBufferAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool=commandPool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device,&allocate,&target));check(vkAllocateCommandBuffers(device,&allocate,&update));
    }
    static VkBufferMemoryBarrier barrier(VkBuffer buffer,VkAccessFlags sourceAccess,VkAccessFlags destinationAccess){
        VkBufferMemoryBarrier value{};value.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        value.srcAccessMask=sourceAccess;value.dstAccessMask=destinationAccess;value.buffer=buffer;value.size=VK_WHOLE_SIZE;
        value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;return value;
    }
    VkImageMemoryBarrier sourceBarrier(VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst){
        VkImageMemoryBarrier value{};value.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        value.oldLayout=before;value.newLayout=after;value.srcAccessMask=src;value.dstAccessMask=dst;
        value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        value.image=images[source].value;value.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return value;
    }
    void recordUpdate(){
        require(updateRecordings++==0,"update command buffer recorded only once");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=0;
        check(vkBeginCommandBuffer(update,&begin));
        VkBufferMemoryBarrier before[5]{barrier(buffers[upload].value,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT),
            barrier(buffers[stage4].value,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT),
            barrier(buffers[stage6].value,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT),
            barrier(buffers[b4].value,VK_ACCESS_UNIFORM_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT),
            barrier(buffers[b6].value,VK_ACCESS_UNIFORM_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)};
        auto imageBarrier=sourceBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdPipelineBarrier(update,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,5,before,1,&imageBarrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};
        vkCmdCopyBufferToImage(update,buffers[upload].value,images[source].value,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        for(auto pair:{std::pair<std::size_t,VkDeviceSize>{stage4,272},{stage6,2800}}){
            VkBufferCopy bufferCopy{0,0,pair.second};const auto destination=pair.first==stage4?b4:b6;
            vkCmdCopyBuffer(update,buffers[pair.first].value,buffers[destination].value,1,&bufferCopy);
        }
        VkBufferMemoryBarrier after[2]{barrier(buffers[b4].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_UNIFORM_READ_BIT),
            barrier(buffers[b6].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_UNIFORM_READ_BIT)};
        imageBarrier=sourceBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(update,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,2,after,1,&imageBarrier);check(vkEndCommandBuffer(update));
    }
    void recordTarget(bool reset){
        require(targetRecordings++==0,"TARGET command buffer recorded only once");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=0;
        check(vkBeginCommandBuffer(target,&begin));
        auto before=barrier(buffers[output].value,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
            reset?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            reset?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,1,&before,0,nullptr);
        VkBufferMemoryBarrier copies[2]{barrier(buffers[readback].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT),
            barrier(buffers[sourceCopy].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)};
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,2,copies,0,nullptr);
        if(reset){
            vkCmdFillBuffer(target,buffers[output].value,offset,128,0);
            auto filled=barrier(buffers[output].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,0,nullptr,1,&filled,0,nullptr);
        }
        // Order the previous replay's post-pass attachment read before this
        // replay writes/discards the color attachment again.
        VkMemoryBarrier reuse{};reuse.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        reuse.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;reuse.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,1,&reuse,0,nullptr,0,nullptr);
        VkClearValue clear{};for(auto& value:clear.color.float32)value=-99;
        VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;
        rp.framebuffer=framebuffer;rp.renderArea={{0,0},{1,1}};rp.clearValueCount=1;rp.pClearValues=&clear;
        vkCmdBeginRenderPass(target,&rp,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(target,VK_PIPELINE_BIND_POINT_GRAPHICS,probe);
        VkDescriptorSet sets[2]{set,exportSet};vkCmdBindDescriptorSets(target,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,2,sets,0,nullptr);
        const auto input=cohort(0).u;vkCmdPushConstants(target,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,input.data());
        vkCmdDraw(target,3,1,0,0);vkCmdEndRenderPass(target);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(target,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
        auto imageBarrier=sourceBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,0,nullptr,1,&imageBarrier);
        copy.imageExtent={16,16,1};vkCmdCopyImageToBuffer(target,images[source].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[sourceCopy].value,1,&copy);
        imageBarrier=sourceBarrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&imageBarrier);
        VkBufferMemoryBarrier host[3]{barrier(buffers[output].value,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT),
            barrier(buffers[readback].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT),
            barrier(buffers[sourceCopy].value,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)};
        vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,3,host,0,nullptr);
        check(vkEndCommandBuffer(target));
    }
    void initializeRecord(bool reset){
        auto* data=static_cast<std::uint32_t*>(buffers[output].mapped);
        std::fill_n(data,allocated/4,borrowed::Canary);
        // Positive starts with a NONZERO claim, so only the recorded GPU reset
        // can make even its first execution publish an observation.
        if(!reset)std::memset(static_cast<char*>(buffers[output].mapped)+offset,0,128);
        flush(output);
    }
    void prepare(unsigned epoch){
        auto c=cohort(epoch);
        std::memset(buffers[stage4].mapped,0,272);std::memset(buffers[stage6].mapped,0,2800);
        auto word=[&](std::size_t index,unsigned at,std::uint32_t value){std::memcpy(static_cast<char*>(buffers[index].mapped)+at,&value,4);};
        for(unsigned i=0;i<4;++i)word(stage4,160+i*4,bits(c.tint[i]));
        word(stage4,256,c.eye);word(stage4,260,epoch%3);
        for(unsigned eye=0;eye<2;++eye){const unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(stage6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(stage6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(stage6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(stage6,at+12,c.flags^(eye!=c.eye?0x00400000u:0u));
            word(stage6,at+44,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(stage6,at+60,bits(c.patch[1]+float(eye!=c.eye)*.25f));
        }
        auto* texture=static_cast<std::uint32_t*>(buffers[upload].mapped);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto texel=sourceTexel(epoch,x,y);std::copy(texel.begin(),texel.end(),texture+(y*16+x)*4);}
        for(auto index:{stage4,stage6,upload})flush(index);
    }
    void submit(VkCommandBuffer recorded){
        check(vkResetFences(device,1,&fence));VkSubmitInfo submitInfo{};submitInfo.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount=1;submitInfo.pCommandBuffers=&recorded;
        check(vkQueueSubmit(queue,1,&submitInfo,fence));check(vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ull));
        require(vkGetFenceStatus(device,fence)==VK_SUCCESS,"owned execution fence completed");
    }
    Result observe(unsigned epoch,std::uint64_t& imageFingerprint){
        submit(target);++targetSubmissions;
        for(auto index:{output,readback,sourceCopy})if(!buffers[index].coherent){
            VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=buffers[index].memory;
            range.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&range));
        }
        const auto* imageWords=static_cast<const std::uint32_t*>(buffers[sourceCopy].mapped);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto expected=sourceTexel(epoch,x,y);
            require(std::equal(expected.begin(),expected.end(),imageWords+(y*16+x)*4),"captured GPU source image is not current cohort");}
        imageFingerprint=fingerprint(imageWords,ImageBytes);
        const auto* words=static_cast<const std::uint32_t*>(buffers[output].mapped);
        for(unsigned i=0;i<allocated/4;++i)if(i<offset/4 || i>=(offset+128)/4)
            require(words[i]==borrowed::Canary,"GPU reset/export touched logical record canary");
        Result result{};std::memcpy(result.rgba.data(),buffers[readback].mapped,16);
        std::memcpy(result.record.data(),static_cast<const char*>(buffers[output].mapped)+offset,128);return result;
    }
};

bool matches(const Result& result,unsigned epoch){
    const auto c=cohort(epoch);const auto& r=result.record;
    if(r[0]!=1 || r[1]!=0x5353424fu || r[2]!=1 || r[3]!=32)return false;
    for(unsigned i=0;i<2;++i)if(r[4+i]!=bits(c.u[i]) || r[14+i]!=bits(c.patch[i]))return false;
    for(unsigned i=0;i<4;++i)if(r[6+i]!=bits(c.r1[i]) || r[10+i]!=bits(c.r0[i]) || r[28+i]!=result.rgba[i])return false;
    if(r[16]!=c.flags)return false;
    for(unsigned i=0;i<3;++i)if(r[17+i]!=bits(c.tint[i]))return false;
    const auto a=sourceSample(epoch,c.u[0],c.u[1]);
    const auto b=sourceSample(epoch,c.patch[0]+(c.u[0]-c.r0[0])/(c.r0[2]-c.r0[0])*(c.r1[2]-c.r1[0]),
        c.patch[1]+(c.u[1]-c.r0[1])/(c.r0[3]-c.r0[1])*(c.r1[3]-c.r1[1]));
    return std::equal(a.begin(),a.end(),r.begin()+20) && std::equal(b.begin(),b.end(),r.begin()+24);
}
void printWords(const Record& record){for(unsigned i=0;i<record.size();++i){if(i)std::cerr<<',';std::cerr<<record[i];}}
}

int main(int argc,char** argv){
    try{
        require(argc==5,"usage: cohort-fixture VERTEX ORIGINAL PROBE reset|no-reset");
        const bool reset=std::string(argv[4])=="reset";
        require(reset || std::string(argv[4])=="no-reset","known cohort mode");
        CohortGpu gpu;gpu.init();require(gpu.validationEnabled,"Khronos validation active");gpu.initialize();
        gpu.createPipelines(argv[1],argv[2],argv[3]);gpu.initializeRecord(reset);gpu.recordUpdate();gpu.recordTarget(reset);
        Result first{};std::uint64_t firstImage=0,lastImage=0;unsigned completed=0;
        for(unsigned epoch=0;epoch<Repeats;++epoch){
            gpu.prepare(epoch);gpu.submit(gpu.update);const auto result=gpu.observe(epoch,lastImage);
            if(epoch==0){first=result;firstImage=lastImage;}
            else{
                require(lastImage!=firstImage,"post-pass captured GPU source must actually change");
                require(result.rgba!=first.rgba,"actual native original fragment must rerun with changed source/UBO");
            }
            if(!matches(result,epoch)){
                std::cerr<<"{\"first_mismatch_epoch\":"<<epoch<<",\"observed_record\":[";printWords(result.record);std::cerr<<"]}\n";
                require(!reset && epoch==1 && result.record==first.record,"unexpected cohort mismatch shape");
                require(borrowed::validationErrors.load()==0,"causal control Vulkan errors");
                std::cerr<<"{\"causal_no_reset\":true,\"target_recordings\":"<<gpu.targetRecordings
                    <<",\"target_submissions\":"<<gpu.targetSubmissions<<",\"source_fingerprint_first\":"<<firstImage
                    <<",\"source_fingerprint_current\":"<<lastImage<<",\"first_record\":[";printWords(first.record);
                std::cerr<<"],\"current_record\":[";printWords(result.record);std::cerr<<"],\"validation_errors\":0}\n";
                require(false,"cohort mismatch: FIRST SSBO record with LAST sampled image");
            }
            ++completed;
        }
        require(reset,"no-reset causal control unexpectedly passed latest-cohort gate");
        require(gpu.targetRecordings==1 && gpu.updateRecordings==1 && gpu.targetSubmissions==Repeats,"actual same recording reused across all serial submissions");
        require(borrowed::validationErrors.load()==0,"cohort Vulkan validation errors");
        std::cout<<"{\"schema\":\"owned-ssbo-replay-cohort-v1\",\"status\":\"PASS\",\"cohorts\":"<<completed
            <<",\"checks\":"<<borrowed::checks<<",\"target_recordings\":1,\"target_resets\":0,\"target_begin_flags\":0,\"target_submissions\":"<<gpu.targetSubmissions
            <<",\"update_recordings\":1,\"per_execution_gpu_reset\":true,\"reset_bytes\":128,\"active_color_attachments\":1,\"source_image_bytes_checked_per_cohort\":4096"
            <<",\"source_fingerprint_first\":"<<firstImage<<",\"source_fingerprint_last\":"<<lastImage
            <<",\"actual_feature_enabled\":"<<(borrowed::actualOwnedFragmentStores?"true":"false")
            <<",\"validation_enabled\":true,\"validation_errors\":0,\"readback_memory_coherent\":"<<(gpu.buffers[gpu.sourceCopy].coherent?"true":"false")
            <<",\"device_name\":\""<<gpu.properties.deviceName<<"\",\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"cohort fixture failed: "<<error.what()<<"\n";return 1;}
}
