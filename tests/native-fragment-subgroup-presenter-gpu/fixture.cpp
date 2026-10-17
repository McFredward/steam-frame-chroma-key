// Owned subgroup equivalence/cost fixture. Borrowed implementation is generated
// privately from pinned existing fixture source; no native shader is embedded.
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
static VkPhysicalDeviceSubgroupProperties subgroupFacts{};
static VkResult subgroupCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* input,
        const VkAllocationCallbacks* allocator,VkDevice* out){
    subgroupFacts.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 props{};props.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;props.pNext=&subgroupFacts;
    vkGetPhysicalDeviceProperties2(physical,&props);
    std::uint32_t count=0;vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr);
    std::vector<VkExtensionProperties> extensions(count);vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data());
    bool extension=false;for(const auto& e:extensions)extension|=std::strcmp(e.extensionName,VK_KHR_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_EXTENSION_NAME)==0;
    VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR flow{};flow.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 features{};features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&flow;vkGetPhysicalDeviceFeatures2(physical,&features);
    const auto required=VK_SUBGROUP_FEATURE_BASIC_BIT|VK_SUBGROUP_FEATURE_BALLOT_BIT|VK_SUBGROUP_FEATURE_SHUFFLE_BIT;
    if(!extension||!flow.shaderSubgroupUniformControlFlow||!(subgroupFacts.supportedStages&VK_SHADER_STAGE_FRAGMENT_BIT)||
            (subgroupFacts.supportedOperations&required)!=required)throw std::runtime_error("UNSUPPORTED: Fragment BASIC/BALLOT/SHUFFLE and enabled subgroup-uniform-control-flow required");
    if(input->pNext||input->enabledExtensionCount)throw std::runtime_error("owned fixture device chain must initially be empty");
    const char* name=VK_KHR_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_EXTENSION_NAME;
    VkDeviceCreateInfo create=*input;create.pNext=&flow;create.enabledExtensionCount=1;create.ppEnabledExtensionNames=&name;
    return vkCreateDevice(physical,&create,allocator,out);
}
#define vkCreateDevice subgroupCreateDevice
#include "borrowed_guard_fixture.h"
#undef vkCreateDevice

namespace {
struct SubgroupGpu:GuardGpu {
    void pipelines(const char* vertexPath,const char* originalPath,const char* oldPath,const char* subgroupPath,bool helperWitness=false){
        VkShaderModule vertex{},old{},baseline{},subgroup{};
        auto release=[&]{for(auto m:{subgroup,baseline,old,vertex})if(m)vkDestroyShaderModule(device,m,nullptr);};
        try{vertex=module(shader(vertexPath));old=module(shader(originalPath));baseline=module(shader(oldPath));subgroup=module(shader(subgroupPath));
            original=singlePipeline(vertex,old);probe=singlePipeline(vertex,baseline);over=singlePipeline(vertex,subgroup,helperWitness?"main":"PS_DISTORT");
        }catch(...){release();throw;}release();
    }
    void fullRecord(const Vec& u){
        const VkPipeline pipes[3]{original,probe,over};
        for(unsigned slot=0;slot<2;++slot)for(unsigned mode=0;mode<3;++mode){
            const auto cb=commands[slot][mode];check(vkResetCommandBuffer(cb,0));
            VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;check(vkBeginCommandBuffer(cb,&begin));
            if(queryPool){vkCmdResetQueryPool(cb,queryPool,0,2);vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queryPool,0);}
            VkClearValue clear{};std::copy(Background.begin(),Background.end(),clear.color.float32);
            VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;rp.framebuffer=framebuffer;
            rp.renderArea={{0,0},{outputWidth,outputHeight}};rp.clearValueCount=1;rp.pClearValues=&clear;
            vkCmdBeginRenderPass(cb,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes[mode]);
            vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&sourceSets[slot],0,nullptr);
            vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,u.data());vkCmdDraw(cb,3,1,0,0);vkCmdEndRenderPass(cb);
            if(queryPool)vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queryPool,1);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={outputWidth,outputHeight,1};
            vkCmdCopyImageToBuffer(cb,images[targets[0]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);
            VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer=buffers[readback].value;barrier.size=VkDeviceSize(outputWidth)*outputHeight*16;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);check(vkEndCommandBuffer(cb));
        }++recordings;
    }
    std::vector<std::uint32_t> pixels(unsigned slot,unsigned mode){
        execute(slot,mode);const auto count=std::size_t(outputWidth)*outputHeight*4;
        std::vector<std::uint32_t> out(count);std::memcpy(out.data(),buffers[readback].mapped,count*4);return out;
    }
};
}

int main(int argc,char** argv){
    try{
        require(argc==9||argc==10,"usage: subgroup-fixture VERTEX ORIGINAL OLD NEW CASES correctness|benchmark WIDTH HEIGHT [mixed]");
        const bool mixed=argc==10&&std::string(argv[9])=="mixed";
        require(argc==9||mixed,"explicit mixed-selector control");
        const bool timing=std::string(argv[6])=="benchmark";
        const bool routing=std::string(argv[6])=="routing",helperWitness=std::string(argv[6])=="helper";
        require(timing||routing||helperWitness||std::string(argv[6])=="correctness","explicit mode");
        auto input=readGuardCases(argv[5]);SubgroupGpu gpu;gpu.init();
        if(helperWitness)require(subgroupFacts.supportedOperations&VK_SUBGROUP_FEATURE_QUAD_BIT,"test-only helper witness needs QUAD support");
        const unsigned width=unsigned(std::stoul(argv[7])),height=unsigned(std::stoul(argv[8]));
        require(width&&height&&std::uint64_t(width)*height<=1152ull*4608,"bounded extent");
        gpu.guardResources(width,height);
        gpu.readback=gpu.buffer(VkDeviceSize(width)*height*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        gpu.pipelines(argv[1],argv[2],argv[3],argv[4],helperWitness);
        auto c=input.front();gpu.metadata(c.native);gpu.fullRecord(timing?Vec{0,.25f,1,.25f}:c.native.u);
        if(routing||helperWitness){
            require(gpu.validationEnabled,"witness validation required");
            gpu.update(0,sourceFor(c));const auto output=gpu.pixels(0,2);
            unsigned shared=0,fallback=0,witnesses=0;
            for(std::size_t i=0;i<output.size();i+=4){
                const auto red=floating(output[i]);
                if(helperWitness){if(red>=1.f)++witnesses;}
                else if(output[i]==bits(1.f))++shared;
                else if(output[i]==bits(0.f))++fallback;
            }
            if(helperWitness)require(witnesses>0,"actual quad witness must observe helper at raster edge");
            else{require(shared>0,"covered invocations use shared path");require(mixed?fallback>0:fallback==0,"mixed R1 must actually take per-lane fallback");}
            require(validationErrors.load()==0,"witness validation clean");
            std::cout<<"{\"mode\":\""<<(helperWitness?"helper":"routing")<<"\",\"shared_pixels\":"<<shared<<",\"fallback_pixels\":"<<fallback
                <<",\"helper_boundary_witness_pixels\":"<<witnesses<<",\"validation_errors\":"<<validationErrors.load()<<"}\n";return 0;
        }
        if(timing){
            require(!gpu.validationEnabled,"timing validation must be disabled");
            std::cout<<"{\"mode\":\"benchmark\",\"pixels\":"<<std::uint64_t(width)*height<<",\"device\":\""<<gpu.properties.deviceName
                <<"\",\"subgroup_size\":"<<subgroupFacts.subgroupSize<<",\"uniform_control_flow_enabled\":true,\"runs\":{";
            bool comma=false;
            for(unsigned content=0;content<2;++content){c.mode=content?7:0;gpu.update(0,sourceFor(c));
                for(unsigned mode=0;mode<3;++mode){const auto warm=gpu.execute(0,mode);require(warm.cpuMs<9000,"bounded warm draw");
                    if(comma)std::cout<<',';
                    comma=true;
                    std::cout<<'"'<<(content?"off_":"valid_")<<mode<<"\":{\"gpu_ms\":[";
                    std::array<GuardGpu::Result,3> samples;for(auto& s:samples)s=gpu.execute(0,mode);
                    for(unsigned i=0;i<3;++i){if(i)std::cout<<',';std::cout<<samples[i].gpuMs;}
                    std::cout<<"],\"cpu_ms\":[";for(unsigned i=0;i<3;++i){if(i)std::cout<<',';std::cout<<samples[i].cpuMs;}std::cout<<"]}";
                }
            }std::cout<<"},\"frame_gpu_proven\":false}\n";return 0;
        }
        require(gpu.validationEnabled,"Khronos validation required");
        unsigned exact=0,originalExact=0,changed=0;unsigned firstRecord=gpu.recordings;
        Vec recorded=c.native.u;
        for(unsigned n=0;n<input.size();++n){
            c=input[n];if(c.native.u!=recorded){gpu.fullRecord(c.native.u);recorded=c.native.u;}
            if(n<3)require(firstRecord==gpu.recordings,"same CB ON OFF ON");
            gpu.metadata(c.native);gpu.update(c.slot,sourceFor(c));
            const auto old=gpu.pixels(c.slot,1),fresh=gpu.pixels(c.slot,2),original=gpu.pixels(c.slot,0);
            require(old==fresh,"subgroup matches complete old guard bit for bit: case "+std::to_string(n));++exact;
            if(!c.valid&&!mixed){require(fresh==original,"invalid uniform source retains original994 bits");++originalExact;}
            changed+=old!=original;
        }
        require(changed>0,"valid partial contribution must differ from opaque original");
        require(validationErrors.load()==0,"Vulkan/sync validation clean");
        std::cout<<"{\"mode\":\"correctness\",\"cases\":"<<input.size()<<",\"exact_old_guard\":"<<exact
            <<",\"invalid_exact_original\":"<<originalExact<<",\"changed_from_original\":"<<changed<<",\"checked_pixels_per_case\":"<<width*height
            <<",\"checks\":"<<checks<<",\"mixed_selector\":"<<(mixed?"true":"false")<<",\"subgroup_size\":"<<subgroupFacts.subgroupSize<<",\"uniform_control_flow_enabled\":true,\"validation_errors\":"<<validationErrors.load()<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"; checks="<<checks<<'\n';return 1;}
}
