// Inert, owned Vulkan fixture. Original/probe modules are private explicit
// inputs, never embedded or redistributed. Only this fixture allocates, waits,
// and reads GPU data. It does not load the native receiver or claim live alpha.
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
unsigned checks = 0;
std::atomic<unsigned> validationErrors{0};
void require(bool value, const std::string& message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result));
}
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        if (validationErrors.fetch_add(1) < 8) std::cerr << "Validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}
std::vector<std::uint32_t> shader(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(bool(file), "explicit shader input unavailable");
    const auto bytes = file.tellg();
    require(bytes >= 20 && bytes <= 65536 && bytes % 4 == 0, "bounded SPIR-V input required");
    std::vector<std::uint32_t> result(static_cast<std::size_t>(bytes) / 4);
    file.seekg(0); require(bool(file.read(reinterpret_cast<char*>(result.data()), bytes)), "complete shader read");
    require(result[0] == 0x07230203u, "SPIR-V magic"); return result;
}
using Vec = std::array<float, 4>;
std::uint32_t bits(float value) { std::uint32_t result; std::memcpy(&result, &value, 4); return result; }
struct Buffer { VkBuffer value{}; VkDeviceMemory memory{}; void* mapped{}; bool coherent{}; };
struct Image { VkImage value{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct Case {
    unsigned eye; std::uint32_t flags; Vec u, r0, r1, patch, tint;
};
struct Gpu {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkDebugUtilsMessengerEXT messenger{}; bool validationEnabled{};
    VkPhysicalDeviceMemoryProperties memory{}; VkPhysicalDeviceProperties properties{};
    std::uint32_t family{};
    VkCommandPool commandPool{}; VkCommandBuffer command{}; VkFence fence{};
    VkDescriptorSetLayout descriptorLayout{}; VkDescriptorPool descriptorPool{}; VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{}; VkRenderPass renderPass{}; VkFramebuffer framebuffer{};
    VkPipeline original{}, probe{}; VkSampler sampler{};
    std::vector<Buffer> buffers; std::vector<Image> images;
    std::size_t source{}, upload{}, b4{}, b6{}, readback{};
    std::array<std::size_t,5> targets{};
    ~Gpu() {
        if (device) {
            vkDeviceWaitIdle(device); // Test-only failure cleanup after reaping GPU work.
            if (original) vkDestroyPipeline(device, original, nullptr);
            if (probe) vkDestroyPipeline(device, probe, nullptr);
            if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            if (sampler) vkDestroySampler(device, sampler, nullptr);
            for (const auto& image : images) {
                if (image.view) vkDestroyImageView(device,image.view,nullptr);
                if (image.value) vkDestroyImage(device,image.value,nullptr);
                if (image.memory) vkFreeMemory(device,image.memory,nullptr);
            }
            for (const auto& buffer : buffers) {
                if (buffer.mapped) vkUnmapMemory(device,buffer.memory);
                if (buffer.value) vkDestroyBuffer(device,buffer.value,nullptr);
                if (buffer.memory) vkFreeMemory(device,buffer.memory,nullptr);
            }
            if (fence) vkDestroyFence(device,fence,nullptr);
            if (commandPool) vkDestroyCommandPool(device,commandPool,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if (messenger) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance,messenger,nullptr);
        }
        if (instance) vkDestroyInstance(instance,nullptr);
    }
    void init() {
        std::uint32_t count=0;
        check(vkEnumerateInstanceLayerProperties(&count,nullptr));
        std::vector<VkLayerProperties> layers(count); check(vkEnumerateInstanceLayerProperties(&count,layers.data()));
        const char* layer="VK_LAYER_KHRONOS_validation"; const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        validationEnabled=std::any_of(layers.begin(),layers.end(),[&](const auto& v){return std::strcmp(v.layerName,layer)==0;});
        check(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr));
        std::vector<VkExtensionProperties> extensions(count);check(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data()));
        validationEnabled=validationEnabled && std::any_of(extensions.begin(),extensions.end(),[&](const auto& v){return std::strcmp(v.extensionName,extension)==0;});
        VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
        if(validationEnabled){info.enabledLayerCount=info.enabledExtensionCount=1;info.ppEnabledLayerNames=&layer;info.ppEnabledExtensionNames=&extension;}
        check(vkCreateInstance(&info,nullptr,&instance));
        if(validationEnabled){
            VkDebugUtilsMessengerCreateInfoEXT debug{};debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback=validation;
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
            require(create!=nullptr,"debug messenger export");check(create(instance,&debug,nullptr,&messenger));
        }
        check(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count>0,"actual Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
        for(auto candidate:devices){
            VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(candidate,&features);
            VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(candidate,&props);
            if(!features.geometryShader || props.limits.maxColorAttachments<5)continue;
            std::uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,nullptr);
            std::vector<VkQueueFamilyProperties> q(queues);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queues,q.data());
            for(unsigned i=0;i<queues;++i)if(q[i].queueCount && (q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){
                physical=candidate;family=i;properties=props;break;
            }
            if(physical)break;
        }
        require(physical!=VK_NULL_HANDLE,"graphics device needs original Geometry capability and five color attachments");
        for(auto format:{VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R32G32B32A32_UINT}){
            VkFormatProperties fp{};vkGetPhysicalDeviceFormatProperties(physical,format,&fp);
            require((fp.optimalTilingFeatures&(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
                    ==(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT),"required offscreen attachment format");
        }
        VkPhysicalDeviceFeatures enabled{};enabled.geometryShader=VK_TRUE;
        float priority=1;
        VkDeviceQueueCreateInfo q{};q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{};d.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;d.pEnabledFeatures=&enabled;
        check(vkCreateDevice(physical,&d,nullptr,&device));vkGetDeviceQueue(device,family,0,&queue);
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device,&pool,nullptr,&commandPool));
        VkCommandBufferAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate.commandPool=commandPool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device,&allocate,&command));
        VkFenceCreateInfo fenceInfo{};fenceInfo.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;check(vkCreateFence(device,&fenceInfo,nullptr,&fence));
    }
    unsigned memoryType(unsigned mask,VkMemoryPropertyFlags required){
        for(unsigned i=0;i<memory.memoryTypeCount;++i)if((mask&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&required)==required)return i;
        throw std::runtime_error("owned memory type unavailable");
    }
    std::size_t buffer(VkDeviceSize bytes,VkBufferUsageFlags usage){
        buffers.emplace_back();auto index=buffers.size()-1;auto& b=buffers.back();
        VkBufferCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=bytes;info.usage=usage;
        check(vkCreateBuffer(device,&info,nullptr,&b.value));
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,b.value,&req);
        VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;
        a.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        b.coherent=memory.memoryTypes[a.memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        check(vkAllocateMemory(device,&a,nullptr,&b.memory));check(vkBindBufferMemory(device,b.value,b.memory,0));
        check(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped));return index;
    }
    void flush(std::size_t index){if(!buffers[index].coherent){VkMappedMemoryRange r{};r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;r.memory=buffers[index].memory;r.size=VK_WHOLE_SIZE;check(vkFlushMappedMemoryRanges(device,1,&r));}}
    std::size_t image(VkFormat format,unsigned width,unsigned height,VkImageUsageFlags usage){
        images.emplace_back();auto index=images.size()-1;auto& out=images.back();
        VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;info.format=format;
        info.extent={width,height,1};info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
        check(vkCreateImage(device,&info,nullptr,&out.value));
        VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,out.value,&req);
        VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=memoryType(req.memoryTypeBits,0);
        check(vkAllocateMemory(device,&a,nullptr,&out.memory));check(vkBindImageMemory(device,out.value,out.memory,0));
        VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=out.value;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=format;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        check(vkCreateImageView(device,&v,nullptr,&out.view));return index;
    }
    VkShaderModule module(const std::vector<std::uint32_t>& words){
        VkShaderModuleCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;info.codeSize=words.size()*4;info.pCode=words.data();VkShaderModule out{};
        check(vkCreateShaderModule(device,&info,nullptr,&out));return out;
    }
    void begin(){check(vkResetFences(device,1,&fence));check(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo b{};b.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&b));}
    void finish(){
        check(vkEndCommandBuffer(command));VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&command;
        check(vkQueueSubmit(queue,1,&s,fence));check(vkWaitForFences(device,1,&fence,VK_TRUE,10'000'000'000ull));
    }
    void resources(){
        b4=buffer(272,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);b6=buffer(2800,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        upload=buffer(16*16*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);readback=buffer(5*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        // Asymmetric normalized components. No claim that these are scene RGB.
        auto* texels=static_cast<float*>(buffers[upload].mapped);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){unsigned at=(y*16+x)*4;
            texels[at]=float(17+x*7+y*3)/255;texels[at+1]=float(31+x*2+y*8)/255;
            texels[at+2]=float(19+x*5+y*4)/255;texels[at+3]=1;}
        flush(upload);source=image(VK_FORMAT_R32G32B32A32_SFLOAT,16,16,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        for(unsigned i=0;i<5;++i)targets[i]=image(i==4?VK_FORMAT_R32G32B32A32_UINT:VK_FORMAT_R32G32B32A32_SFLOAT,1,1,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkSamplerCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;si.minFilter=si.magFilter=VK_FILTER_NEAREST;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;check(vkCreateSampler(device,&si,nullptr,&sampler));
        VkDescriptorSetLayoutBinding bindings[4]{{4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {25,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo l{};l.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;l.bindingCount=4;l.pBindings=bindings;check(vkCreateDescriptorSetLayout(device,&l,nullptr,&descriptorLayout));
        VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3},{VK_DESCRIPTOR_TYPE_SAMPLER,1}};
        VkDescriptorPoolCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;p.poolSizeCount=3;p.pPoolSizes=sizes;p.maxSets=1;check(vkCreateDescriptorPool(device,&p,nullptr,&descriptorPool));
        VkDescriptorSetAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;a.descriptorPool=descriptorPool;a.descriptorSetCount=1;a.pSetLayouts=&descriptorLayout;check(vkAllocateDescriptorSets(device,&a,&set));
        VkDescriptorBufferInfo bi[2]{{buffers[b4].value,0,272},{buffers[b6].value,0,2800}};
        // The original declares three sampled images. All entries intentionally
        // reference the same owned source; this fixture does not prove which
        // native source index is selected or that native sources are equivalent.
        VkDescriptorImageInfo ii[4]{{VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE,images[source].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}};
        VkWriteDescriptorSet writes[4]{};
        for(unsigned i=0;i<4;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=bindings[i].binding;writes[i].descriptorCount=bindings[i].descriptorCount;writes[i].descriptorType=bindings[i].descriptorType;
            if(i<2)writes[i].pBufferInfo=&bi[i];else writes[i].pImageInfo=&ii[i==2?0:3];}
        vkUpdateDescriptorSets(device,4,writes,0,nullptr);
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,16};
        VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=1;pl.pSetLayouts=&descriptorLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device,&pl,nullptr,&pipelineLayout));
        std::array<VkAttachmentDescription,5> descriptions{};std::array<VkAttachmentReference,5> refs{};std::array<VkImageView,5> views{};
        for(unsigned i=0;i<5;++i){auto& d=descriptions[i];d.format=i==4?VK_FORMAT_R32G32B32A32_UINT:VK_FORMAT_R32G32B32A32_SFLOAT;
            d.samples=VK_SAMPLE_COUNT_1_BIT;d.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;d.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            d.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;d.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            d.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;d.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            refs[i]={i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};views[i]=images[targets[i]].view;}
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=5;sub.pColorAttachments=refs.data();
        VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=5;rp.pAttachments=descriptions.data();rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dep;
        check(vkCreateRenderPass(device,&rp,nullptr,&renderPass));
        VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=renderPass;fb.attachmentCount=5;fb.pAttachments=views.data();fb.width=fb.height=fb.layers=1;check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        begin();VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=images[source].value;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};vkCmdCopyBufferToImage(command,buffers[upload].value,images[source].value,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);finish();
    }
    VkPipeline pipeline(VkShaderModule vertex,VkShaderModule fragment){
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="PS_DISTORT";
        VkPipelineVertexInputStateCreateInfo vertexInput{};vertexInput.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};
        VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        std::array<VkPipelineColorBlendAttachmentState,5> blend{};for(auto& b:blend)b.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blending{};blending.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blending.attachmentCount=5;blending.pAttachments=blend.data();
        VkGraphicsPipelineCreateInfo p{};p.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;p.stageCount=2;p.pStages=stages;p.pVertexInputState=&vertexInput;p.pInputAssemblyState=&assembly;p.pViewportState=&vp;p.pRasterizationState=&raster;p.pMultisampleState=&ms;p.pColorBlendState=&blending;p.layout=pipelineLayout;p.renderPass=renderPass;
        VkPipeline out{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&p,nullptr,&out));return out;
    }
    void pipelines(const char* vp,const char* originalPath,const char* probePath){
        VkShaderModule vertex{}, old{}, instrumented{};
        auto release=[&]{
            for(auto value:{instrumented,old,vertex})if(value)vkDestroyShaderModule(device,value,nullptr);
        };
        try{
            vertex=module(shader(vp));old=module(shader(originalPath));instrumented=module(shader(probePath));
            original=pipeline(vertex,old);probe=pipeline(vertex,instrumented);
        }catch(...){release();throw;}
        release();
    }
    std::array<std::uint32_t,20> draw(const Case& c,VkPipeline which){
        std::memset(buffers[b4].mapped,0,272);std::memset(buffers[b6].mapped,0,2800);
        auto word=[&](std::size_t b,unsigned at,std::uint32_t value){std::memcpy(static_cast<char*>(buffers[b].mapped)+at,&value,4);};
        for(unsigned i=0;i<4;++i)word(b4,160+i*4,bits(c.tint[i]));
        word(b4,256,c.eye);
        // Deliberately different other-eye data makes selection mistakes causal.
        for(unsigned eye=0;eye<2;++eye){unsigned at=432+448*eye;
            for(unsigned i=0;i<16;++i)word(b6,at+i*4,bits(-.375f-float(eye)*.125f));
            for(unsigned i=0;i<4;++i){word(b6,at+i*16,bits(c.r0[i]+float(eye!=c.eye)*.0625f));word(b6,at+4+i*16,bits(c.r1[i]+float(eye!=c.eye)*.125f));}
            word(b6,at+3*4,c.flags^(eye!=c.eye?0x00400000u:0u));
            word(b6,at+11*4,bits(c.patch[0]+float(eye!=c.eye)*.1875f));word(b6,at+15*4,bits(c.patch[1]+float(eye!=c.eye)*.25f));}
        flush(b4);flush(b6);begin();
        std::array<VkClearValue,5> clear{};
        for(unsigned i=0;i<4;++i)for(float& value:clear[i].color.float32)value=-99;
        for(auto& value:clear[4].color.uint32)value=0xdeadbeefu;
        VkRenderPassBeginInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;rp.renderPass=renderPass;rp.framebuffer=framebuffer;rp.renderArea={{0,0},{1,1}};rp.clearValueCount=5;rp.pClearValues=clear.data();
        vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,which);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);vkCmdPushConstants(command,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,16,c.u.data());vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
        for(unsigned i=0;i<5;++i){VkBufferImageCopy copy{};copy.bufferOffset=i*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};vkCmdCopyImageToBuffer(command,images[targets[i]].value,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[readback].value,1,&copy);}
        VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffers[readback].value;barrier.size=80;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);finish();
        if(!buffers[readback].coherent){VkMappedMemoryRange r{};r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;r.memory=buffers[readback].memory;r.size=VK_WHOLE_SIZE;check(vkInvalidateMappedMemoryRanges(device,1,&r));}
        std::array<std::uint32_t,20> result{};std::memcpy(result.data(),buffers[readback].mapped,80);return result;
    }
};
void equalFloat(std::uint32_t actual,float expected,const char* name){
    float f;std::memcpy(&f,&actual,4);require(std::isfinite(f) && std::abs(f-expected)<=0.000002f,std::string("export ")+name+" differs");
}
std::vector<Case> cases(){
    std::vector<Case> result;
    for(unsigned eye=0;eye<2;++eye)for(unsigned flags=0;flags<4;++flags)for(unsigned point=0;point<4;++point){
        Case c{};c.eye=eye;c.flags=0x02018018u|((flags&1u)<<17)|((flags&2u)<<17);
        c.r0={.18f,.22f,.62f,.66f};c.r1={.04f,.08f,.94f,.92f};c.patch={.07f+.03f*eye,.13f+.05f*eye,0,0};
        c.tint={.73f,.59f,.91f,1};
        constexpr Vec points[4]{{.35f,.44f,.625f,.875f},{.60f,.64f,.125f,.375f},{.90f,.88f,.75f,.25f},{-.02f,.95f,.5f,1}};
        c.u=points[point];result.push_back(c);
    }
    // Edge-case flags are moved as uint SSA, including float subnormal/NaN bits.
    for(auto flags:{0x00048018u,0x7fc48018u,0xffc28018u,0x80008018u}){auto c=result.front();c.flags=flags;result.push_back(c);}
    return result;
}
}
int main(int argc,char** argv){
    try{
        require(argc==4,"usage: fixture VERTEX ORIGINAL_FRAGMENT PROBE_FRAGMENT");
        Gpu gpu;gpu.init();gpu.resources();gpu.pipelines(argv[1],argv[2],argv[3]);
        unsigned completed=0;std::array<std::uint32_t,4> firstOutput{};bool varied=false;
        std::vector<std::array<std::uint32_t,4>> output0;
        for(const auto& c:cases()){
            const auto old=gpu.draw(c,gpu.original),observed=gpu.draw(c,gpu.probe);
            require(std::equal(old.begin(),old.begin()+4,observed.begin()),"original location0 changed by probe");
            require(old[3]==bits(1.f),"original output alpha one proves rasterized native shader");
            std::array<std::uint32_t,4> rgba{};std::copy_n(old.begin(),4,rgba.begin());output0.push_back(rgba);
            if(!completed)std::copy_n(old.begin(),4,firstOutput.begin());else varied|=!std::equal(old.begin(),old.begin()+4,firstOutput.begin());
            for(unsigned i=0;i<2;++i){equalFloat(observed[4+i],c.u[i],"loc1 input-u");equalFloat(observed[6+i],c.patch[i],"loc1 patch");}
            for(unsigned i=0;i<4;++i){equalFloat(observed[8+i],c.r0[i],"loc2 R0");equalFloat(observed[12+i],c.r1[i],"loc3 R1");}
            for(unsigned i=0;i<3;++i)require(observed[16+i]==bits(c.tint[i]),"export loc4 tint raw bits differ");
            require(observed[19]==c.flags,"export loc4 consumed flag raw bits differ");++completed;
        }
        require(varied,"asymmetric inputs produce distinct native original outputs");
        require(output0[0]!=output0[4],"inner-reconstruction flag changes original sampled output");
        require(output0[0]!=output0[8],"packed-decoder flag changes original sampled output");
        require(output0[0]!=output0[16],"selected-eye patch changes original sampled output");
        require(validationErrors.load()==0,"Vulkan validation errors");
        std::cout<<"{\"schema\":\"frame-native-fragment-ssa-gpu-controls-v1\",\"cases\":"<<completed<<",\"checks\":"<<checks
            <<",\"validation_enabled\":"<<(gpu.validationEnabled?"true":"false")<<",\"validation_errors\":"<<validationErrors.load()
            <<",\"device_vendor\":"<<gpu.properties.vendorID<<",\"device_id\":"<<gpu.properties.deviceID
            <<",\"device_name\":\""<<gpu.properties.deviceName<<"\",\"max_color_attachments\":"<<gpu.properties.limits.maxColorAttachments
            <<",\"output0_bit_identical\":true,\"native_frame_association_proven\":false,\"passthrough_authorized\":false}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"fixture failed: "<<e.what()<<"; completed_checks="<<checks<<'\n';return 1;}
}
