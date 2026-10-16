// Actual owned cold-factory creation only. No draw, command buffer or queue wait.
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
#include "../../layer/pixel_ssbo_owned_clone.h"
namespace borrowed {
#include "../native-fragment-ssa-gpu/fixture.cpp"
}

namespace {
using borrowed::require;
using borrowed::check;
namespace contract=frame_pixel_ssbo_clone;
namespace factory=frame_pixel_ssbo_owned;
struct Context{
    VkInstance instance{};VkDebugUtilsMessengerEXT messenger{};VkPhysicalDevice physical{};VkDevice device{};
    VkPhysicalDeviceProperties properties{};VkPhysicalDeviceMemoryProperties memory{};
    VkDeviceCreateInfo deviceInfo{};VkDeviceQueueCreateInfo queueInfo{};float priority=1;
    const char* shaderLayerExtension=VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME;
    VkPhysicalDeviceFeatures enabled{};
    VkPhysicalDeviceVulkan12Features enabled12{};
    VkDescriptorSetLayout set0{};VkPipelineLayout layout{};VkRenderPass pass{};
    VkShaderModule vertex{},fragment{};VkPipeline pipeline{};
    factory::Clone clone;
    bool deviceSucceeded{};
    ~Context(){
        if(device){clone.release();retireOrigin();vkDestroyDevice(device,nullptr);}
        if(messenger){auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));if(destroy)destroy(instance,messenger,nullptr);}
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    void retireOrigin(){
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(fragment)vkDestroyShaderModule(device,fragment,nullptr);
        if(vertex)vkDestroyShaderModule(device,vertex,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(pass)vkDestroyRenderPass(device,pass,nullptr);
        if(set0)vkDestroyDescriptorSetLayout(device,set0,nullptr);
        pipeline=VK_NULL_HANDLE;fragment=vertex=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;pass=VK_NULL_HANDLE;set0=VK_NULL_HANDLE;
    }
    void init(){
        std::uint32_t count=0;check(vkEnumerateInstanceLayerProperties(&count,nullptr));
        std::vector<VkLayerProperties> layers(count);check(vkEnumerateInstanceLayerProperties(&count,layers.data()));
        const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        require(std::any_of(layers.begin(),layers.end(),[&](const auto& v){return std::strcmp(v.layerName,layer)==0;}),"Khronos validation required before instance creation");
        VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_2;
        VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
        info.enabledLayerCount=info.enabledExtensionCount=1;info.ppEnabledLayerNames=&layer;info.ppEnabledExtensionNames=&extension;
        VkInstance created{};auto result=vkCreateInstance(&info,nullptr,&created);if(result==VK_SUCCESS)instance=created;check(result);
        VkDebugUtilsMessengerCreateInfoEXT debug{};debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback=borrowed::validation;
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
        require(create!=nullptr,"debug messenger export");VkDebugUtilsMessengerEXT createdMessenger{};
        result=create(instance,&debug,nullptr,&createdMessenger);if(result==VK_SUCCESS)messenger=createdMessenger;check(result);
        check(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count>0,"actual physical device");
        std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
        std::uint32_t family=0;
        for(auto candidate:devices){
            VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(candidate,&p);if(p.apiVersion<VK_API_VERSION_1_2)continue;
            VkPhysicalDeviceVulkan12Features supported12{};supported12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceFeatures2 supported{};supported.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;supported.pNext=&supported12;
            vkGetPhysicalDeviceFeatures2(candidate,&supported);
            if(!supported.features.fragmentStoresAndAtomics || !supported.features.geometryShader || !supported12.shaderOutputLayer || !supported12.shaderOutputViewportIndex)continue;
            std::uint32_t extensionCount=0;check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,nullptr));
            std::vector<VkExtensionProperties> extensions(extensionCount);check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,extensions.data()));
            if(!std::any_of(extensions.begin(),extensions.end(),[&](const auto& value){return std::strcmp(value.extensionName,shaderLayerExtension)==0;}))continue;
            std::uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,nullptr);
            std::vector<VkQueueFamilyProperties> properties(queues);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,properties.data());
            for(unsigned i=0;i<queues;++i)if(properties[i].queueCount && (properties[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){physical=candidate;family=i;break;}
            if(physical)break;
        }
        require(physical!=VK_NULL_HANDLE,"queried actual native VS output-layer/viewport + geometry/fragmentStores support");
        enabled.fragmentStoresAndAtomics=enabled.geometryShader=VK_TRUE;
        enabled12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;enabled12.shaderOutputLayer=enabled12.shaderOutputViewportIndex=VK_TRUE;
        queueInfo.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
        deviceInfo.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;deviceInfo.pNext=&enabled12;deviceInfo.pEnabledFeatures=&enabled;
        // The pinned SPIR-V still declares its EXT capability. Keep the exact
        // original module and satisfy that declared extension explicitly.
        deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=&shaderLayerExtension;
        deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
        VkDevice createdDevice{};result=vkCreateDevice(physical,&deviceInfo,nullptr,&createdDevice);
        deviceSucceeded=result==VK_SUCCESS;if(deviceSucceeded)device=createdDevice;check(result);
        vkGetPhysicalDeviceProperties(physical,&properties);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    }
};
}

int main(int argc,char** argv){
    try{
        require(argc==5,"usage: owned-clone-fixture ORIGINAL_VS ORIGINAL_PS PROBE_PS PROBE_SHA256");
        auto vs=borrowed::shader(argv[1]),ps=borrowed::shader(argv[2]),probe=borrowed::shader(argv[3]);
        require(std::strlen(argv[4])==64,"explicit independently pinned probe digest");
        frame_receiver::Sha256 hash;hash.update(probe.data(),probe.size()*4);const auto probeDigest=hash.finish();
        require(contract::digestMatches(probeDigest,argv[4]),"actual private probe transport digest");
        std::string name;VkDeviceSize allocation=0;bool coherent=false;
        {
            Context c;c.init();name=c.properties.deviceName;
            std::array<VkDescriptorSetLayoutBinding,7> bindings{};
            unsigned at=0;for(auto binding:{0u,2u,3u,4u,6u})bindings[at++]={binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
            bindings[5]={9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};bindings[6]={25,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
            VkDescriptorSetLayoutCreateInfo setInfo{};setInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;setInfo.bindingCount=7;setInfo.pBindings=bindings.data();
            VkDescriptorSetLayout createdSet{};auto status=vkCreateDescriptorSetLayout(c.device,&setInfo,nullptr,&createdSet);if(status==VK_SUCCESS)c.set0=createdSet;check(status);
            VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&c.set0;
            VkPipelineLayout createdLayout{};status=vkCreatePipelineLayout(c.device,&layout,nullptr,&createdLayout);if(status==VK_SUCCESS)c.layout=createdLayout;check(status);
            VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
            VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&sub;
            VkRenderPass createdPass{};status=vkCreateRenderPass(c.device,&pass,nullptr,&createdPass);if(status==VK_SUCCESS)c.pass=createdPass;check(status);
            VkShaderModuleCreateInfo vertex{};vertex.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;vertex.codeSize=vs.size()*4;vertex.pCode=vs.data();
            VkShaderModuleCreateInfo fragment=vertex;fragment.codeSize=ps.size()*4;fragment.pCode=ps.data();
            VkShaderModule createdShader{};status=vkCreateShaderModule(c.device,&vertex,nullptr,&createdShader);if(status==VK_SUCCESS)c.vertex=createdShader;check(status);
            createdShader=VK_NULL_HANDLE;status=vkCreateShaderModule(c.device,&fragment,nullptr,&createdShader);if(status==VK_SUCCESS)c.fragment=createdShader;check(status);
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};for(auto& value:stages)value.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            char vertexEntry[]="VS_DISTORT",fragmentEntry[]="PS_DISTORT";
            stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=c.vertex;stages[0].pName=vertexEntry;
            stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=c.fragment;stages[1].pName=fragmentEntry;
            VkVertexInputBindingDescription binding{0,40,VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription,4> attributes{{{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,16},{2,0,VK_FORMAT_R32G32_SFLOAT,24},{3,0,VK_FORMAT_R32G32_SFLOAT,32}}};
            VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&binding;vi.vertexAttributeDescriptionCount=4;vi.pVertexAttributeDescriptions=attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};
            VkPipelineViewportStateCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;view.viewportCount=view.scissorCount=1;view.pViewports=&viewport;view.pScissors=&scissor;
            VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
            VkPipelineMultisampleStateCreateInfo samples{};samples.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
            VkPipelineColorBlendStateCreateInfo blends{};blends.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blends.attachmentCount=1;blends.pAttachments=&blend;
            VkGraphicsPipelineCreateInfo graphics{};graphics.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;graphics.stageCount=2;graphics.pStages=stages.data();
            graphics.pVertexInputState=&vi;graphics.pInputAssemblyState=&assembly;graphics.pViewportState=&view;graphics.pRasterizationState=&raster;
            graphics.pMultisampleState=&samples;graphics.pColorBlendState=&blends;graphics.layout=c.layout;graphics.renderPass=c.pass;
            // Graphics creation alone may legally return a partial pipeline on
            // failure. Ordinary failed creates above never publish undefined outputs.
            VkPipeline createdPipeline{};status=vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&graphics,nullptr,&createdPipeline);c.pipeline=createdPipeline;check(status);
            require(borrowed::validationErrors.load()==0,"actual original native VS/PS objects must be valid BEFORE capture");
            contract::Inputs inputs{};inputs.device=contract::handle(c.device);inputs.deviceCreateSucceeded=c.deviceSucceeded;inputs.deviceCreate=&c.deviceInfo;
            inputs.set0=&setInfo;inputs.layout=&layout;inputs.renderPass=&pass;inputs.vertex=&vertex;inputs.fragment=&fragment;inputs.graphics=&graphics;
            auto identity=[&](auto object,std::uint64_t generation){return contract::Identity{inputs.device,contract::handle(object),generation};};
            inputs.set0Identity=identity(c.set0,1);inputs.layoutIdentity=identity(c.layout,2);inputs.renderPassIdentity=identity(c.pass,3);
            inputs.vertexIdentity=identity(c.vertex,4);inputs.fragmentIdentity=identity(c.fragment,5);inputs.pipelineIdentity=identity(c.pipeline,6);
            contract::Recipe recipe{};contract::Refusal why{};
            const bool captured=contract::capture(inputs,recipe,why);
            require(captured,std::string("actual recipe capture refused: ")+contract::reasonName(why.reason));
            c.retireOrigin();require(!c.pipeline && !c.vertex && !c.fragment && !c.layout && !c.pass && !c.set0,"all original source objects retired before factory");
            // Eliminate every original call-duration input before prepare.
            std::fill(vs.begin(),vs.end(),0);std::fill(ps.begin(),ps.end(),0);bindings={};attributes={};stages={};binding={};
            vertexEntry[0]=fragmentEntry[0]='\0';setInfo={};layout={};pass={};sub={};color={};attachment={};vertex={};fragment={};
            vi={};assembly={};view={};viewport={};scissor={};raster={};samples={};blend={};blends={};graphics={};inputs={};
            c.deviceInfo={};c.queueInfo={};c.enabled={};c.enabled12={};
            factory::Preparation preparation{};preparation.device=c.device;preparation.downstream=vkGetDeviceProcAddr;
            preparation.limits=c.properties.limits;preparation.memory=c.memory;preparation.recipe=&recipe;
            preparation.fragment=probe.data();preparation.fragmentBytes=probe.size()*4;preparation.fragmentSha256=probeDigest;
            const auto prepared=c.clone.prepare(preparation);
            require(prepared.status==factory::Status::Ok,"actual owned factory creation failed stage="+std::to_string(unsigned(prepared.stage))+" VkResult="+std::to_string(prepared.driver));
            recipe={};std::fill(probe.begin(),probe.end(),0);preparation={};
            require(c.clone.ready() && !c.clone.referenced() && c.clone.pipeline() && c.clone.layout() && c.clone.set1()
                && c.clone.recordBuffer() && c.clone.recordMemory() && c.clone.mappedRecord(),"factory publishes complete independently owned objects only");
            const auto* data=static_cast<const std::uint8_t*>(c.clone.mappedRecord());for(unsigned i=0;i<128;++i)require(data[i]==0,"actual mapped record initialized to zero");
            allocation=c.clone.allocationBytes();coherent=c.clone.hostCoherent();
            require(c.clone.release().status==factory::Status::Ok && !c.clone.ready() && !c.clone.pipeline() && !c.clone.mappedRecord(),"never-recorded factory releases without fabricated retirement/wait proof");
        }
        require(borrowed::validationErrors.load()==0,"actual owned factory/cleanup Vulkan validation error");
        std::cout<<"{\"schema\":\"owned-real-cold-clone-v1\",\"cases\":1,\"checks\":"<<borrowed::checks
            <<",\"device_name\":\""<<name<<"\",\"original_pipeline_created\":true,\"recipe_captured_from_actual_successful_objects\":true"
            <<",\"original_objects_destroyed_and_inputs_overwritten_before_prepare\":true,\"native_vertex_bytes\":9460,\"native_fragment_bytes\":6620"
            <<",\"shader_output_layer_enabled\":true,\"shader_output_viewport_index_enabled\":true,\"fragment_stores_enabled\":true,\"geometry_enabled\":true"
            <<",\"shader_viewport_index_layer_extension_enabled\":true"
            <<",\"factory_prepare_succeeded\":true,\"record_zero_bytes\":128,\"allocation_bytes\":"<<allocation<<",\"host_coherent\":"<<(coherent?"true":"false")
            <<",\"never_referenced_release\":true,\"queue_submissions\":0,\"queue_waits\":0,\"command_buffers\":0,\"validation_enabled\":true,\"validation_errors\":0"
            <<",\"native_admission_proven\":false,\"native_pixel_equivalence_proven\":false,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& error){std::cerr<<"owned clone fixture failed: "<<error.what()<<"\n";return 1;}
}
