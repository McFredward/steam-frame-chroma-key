// Actual owned Vulkan sampling. Native P010 profile is exercised when supported;
// the identical production shader also runs with an original RGBA8 source so
// shader addressing/bounds retain real GPU coverage on drivers without YCbCr.
// Only this fixture waits, allocates and uploads. No headset/native image used.
#include "layer/pixel_sampler_probe.h"
#include "layer/pixel_sampler_probe_spv.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace frame_pixel_sampler;
static unsigned checks = 0;
static std::atomic<unsigned> validationErrors{0};
static void require(bool yes, const char* message) {
    ++checks; if (!yes) throw std::runtime_error(message);
}
static void check(VkResult r) {
    if (r != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(r));
}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        const auto count = validationErrors.fetch_add(1);
        if (count < 10) std::cerr << "Validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}
struct Buffer { VkBuffer value{}; VkDeviceMemory memory{}; void* mapped{}; VkDeviceSize allocation{}; bool coherent{}; };
struct Image { VkImage value{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct Gpu {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkCommandPool commandPool{}; VkCommandBuffer command{}; VkFence fence{};
    VkDebugUtilsMessengerEXT messenger{};
    bool ycbcrEnabled = false, validationEnabled = false;
    std::uint32_t queueFamily = 0;
    std::string name;
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<Buffer> buffers;
    std::vector<Image> images;
    VkSamplerYcbcrConversion sourceConversion{};
    VkSampler sampler{}; VkDescriptorSetLayout layout{}; VkDescriptorPool pool{};
    VkDescriptorSet set{}; VkPipelineLayout pipelineLayout{}; VkPipeline pipeline{};
    ~Gpu() { shutdown(); }
    void init(VkFormatFeatureFlags preferredP010 = 0) {
        std::uint32_t count = 0;
        check(vkEnumerateInstanceLayerProperties(&count, nullptr));
        std::vector<VkLayerProperties> layers(count); check(vkEnumerateInstanceLayerProperties(&count, layers.data()));
        const bool layer = std::any_of(layers.begin(), layers.end(), [](const auto& v) { return !std::strcmp(v.layerName, "VK_LAYER_KHRONOS_validation"); });
        check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
        std::vector<VkExtensionProperties> extensions(count); check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()));
        const bool debug = std::any_of(extensions.begin(), extensions.end(), [](const auto& v) { return !std::strcmp(v.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME); });
        validationEnabled = layer && debug;
        const char* layerName = "VK_LAYER_KHRONOS_validation";
        const char* extensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo = &app;
        if (validationEnabled) { info.enabledLayerCount = info.enabledExtensionCount = 1; info.ppEnabledLayerNames = &layerName; info.ppEnabledExtensionNames = &extensionName; }
        check(vkCreateInstance(&info, nullptr, &instance));
        if (validationEnabled) {
            VkDebugUtilsMessengerCreateInfoEXT m{}; m.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            m.pfnUserCallback = validation;
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            require(create != nullptr, "validation messenger export"); check(create(instance, &m, nullptr, &messenger));
        }
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr)); require(count > 0, "actual Vulkan device available");
        std::vector<VkPhysicalDevice> devices(count); check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        // Prefer actual YCbCr support; otherwise retain RGBA shader GPU controls.
        for (unsigned pass = 0; pass < 2 && !physical; ++pass) for (const auto candidate : devices) {
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1) continue;
            VkPhysicalDeviceSamplerYcbcrConversionFeatures conversion{};
            conversion.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
            VkPhysicalDeviceFeatures2 features{}; features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; features.pNext = &conversion;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (pass == 0) {
                if (!conversion.samplerYcbcrConversion) continue;
                VkFormatProperties format{}; vkGetPhysicalDeviceFormatProperties(candidate, P010, &format);
                if ((format.optimalTilingFeatures & preferredP010) != preferredP010) continue;
            }
            std::uint32_t families = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> queues(families); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
            for (std::uint32_t i = 0; i < families; ++i) if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                physical = candidate; queueFamily = i; ycbcrEnabled = conversion.samplerYcbcrConversion; name = properties.deviceName; break;
            }
            if (physical) break;
        }
        require(physical != VK_NULL_HANDLE, "compute-capable Vulkan1.1 device available");
        VkPhysicalDeviceSamplerYcbcrConversionFeatures conversion{};
        conversion.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
        conversion.samplerYcbcrConversion = ycbcrEnabled;
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{}; queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{}; deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &conversion; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device)); vkGetDeviceQueue(device, queueFamily, 0, &queue);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        VkCommandPoolCreateInfo poolInfo{}; poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamily; poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));
        VkCommandBufferAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = commandPool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocate, &command));
        VkFenceCreateInfo fenceInfo{}; fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; check(vkCreateFence(device, &fenceInfo, nullptr, &fence));
    }
    void shutdown() noexcept {
        if (device) {
            vkDeviceWaitIdle(device); // Fixture only, after failure as well as success.
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
            if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
            if (sampler) vkDestroySampler(device, sampler, nullptr);
            for (const auto& image : images) {
                if (image.view) vkDestroyImageView(device, image.view, nullptr);
                if (image.value) vkDestroyImage(device, image.value, nullptr);
                if (image.memory) vkFreeMemory(device, image.memory, nullptr);
            }
            if (sourceConversion) vkDestroySamplerYcbcrConversion(device, sourceConversion, nullptr);
            for (const auto& buffer : buffers) {
                if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
                if (buffer.value) vkDestroyBuffer(device, buffer.value, nullptr);
                if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
            }
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyDevice(device, nullptr); device = VK_NULL_HANDLE;
        }
        if (messenger) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance, messenger, nullptr);
            messenger = VK_NULL_HANDLE;
        }
        if (instance) { vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE; }
    }
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags required) {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & required) == required) return i;
        throw std::runtime_error("owned fixture memory type unavailable");
    }
    std::size_t buffer(VkDeviceSize bytes, VkBufferUsageFlags usage) {
        buffers.emplace_back(); const auto index = buffers.size() - 1; auto& b = buffers.back();
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; info.size = bytes;
        info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; check(vkCreateBuffer(device, &info, nullptr, &b.value));
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, b.value, &requirements);
        VkMemoryAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        b.allocation = allocate.allocationSize; b.coherent = memory.memoryTypes[allocate.memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        check(vkAllocateMemory(device, &allocate, nullptr, &b.memory)); check(vkBindBufferMemory(device, b.value, b.memory, 0));
        check(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped)); return index;
    }
    std::size_t image(VkFormat format, std::uint32_t w, std::uint32_t h, VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE) {
        images.emplace_back(); const auto index = images.size() - 1; auto& image = images.back();
        VkImageCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format; info.extent = {w,h,1}; info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; check(vkCreateImage(device, &info, nullptr, &image.value));
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, image.value, &requirements);
        VkMemoryAllocateInfo allocate{}; allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0);
        check(vkAllocateMemory(device, &allocate, nullptr, &image.memory)); check(vkBindImageMemory(device, image.value, image.memory, 0));
        VkSamplerYcbcrConversionInfo attach{}; attach.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO; attach.conversion = conversion;
        VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.pNext = conversion ? &attach : nullptr; view.image = image.value; view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        check(vkCreateImageView(device, &view, nullptr, &image.view)); return index;
    }
    void flush(std::size_t index) {
        if (!buffers[index].coherent) { VkMappedMemoryRange r{}; r.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            r.memory = buffers[index].memory; r.size = VK_WHOLE_SIZE; check(vkFlushMappedMemoryRanges(device, 1, &r)); }
    }
    void begin() {
        check(vkResetFences(device, 1, &fence)); check(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; check(vkBeginCommandBuffer(command, &begin));
    }
    void upload(std::size_t imageIndex, std::size_t bufferIndex, std::uint32_t w, std::uint32_t h, bool p010) {
        VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[imageIndex].value; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copies[2]{}; copies[0].imageSubresource = {p010 ? VK_IMAGE_ASPECT_PLANE_0_BIT : VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copies[0].imageExtent = {w,h,1}; copies[1].bufferOffset = VkDeviceSize(w)*h*2;
        copies[1].imageSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT,0,0,1}; copies[1].imageExtent = {w/2,h/2,1};
        vkCmdCopyBufferToImage(command, buffers[bufferIndex].value, images[imageIndex].value,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, p010 ? 2 : 1, copies);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
    }
    void finish(std::size_t target) {
        check(vkEndCommandBuffer(command)); VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command; check(vkQueueSubmit(queue, 1, &submit, fence));
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000ull));
        if (!buffers[target].coherent) { VkMappedMemoryRange r{}; r.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            r.memory = buffers[target].memory; r.size = VK_WHOLE_SIZE; check(vkInvalidateMappedMemoryRanges(device, 1, &r)); }
    }
    void samplerPipeline(std::size_t imageIndex, std::size_t target, VkDeviceSize total,
                         VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE,
                         VkFilter filter = VK_FILTER_LINEAR, std::uint32_t descriptorCount = 1) {
        VkSamplerYcbcrConversionInfo attach{};
        attach.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO; attach.conversion = conversion;
        VkSamplerCreateInfo s{}; s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.pNext = conversion ? &attach : nullptr;
        s.minFilter = s.magFilter = filter; s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &s, nullptr, &sampler));
        VkDescriptorSetLayoutBinding bindings[2]{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,&sampler},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo l{}; l.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; l.bindingCount = 2; l.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &l, nullptr, &layout));
        VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,descriptorCount},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
        VkDescriptorPoolCreateInfo p{}; p.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; p.maxSets = 1; p.poolSizeCount = 2; p.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &p, nullptr, &pool));
        VkDescriptorSetAllocateInfo a{}; a.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; a.descriptorPool = pool;
        a.descriptorSetCount = 1; a.pSetLayouts = &layout; check(vkAllocateDescriptorSets(device, &a, &set));
        VkDescriptorImageInfo image{sampler,images[imageIndex].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo buffer{buffers[target].value,0,total}; VkWriteDescriptorSet writes[2]{};
        writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = writes[1].dstSet = set;
        writes[0].dstBinding = 0; writes[1].dstBinding = 1; writes[0].descriptorCount = writes[1].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo = &image;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &buffer; vkUpdateDescriptorSets(device,2,writes,0,nullptr);
        VkShaderModuleCreateInfo shaderInfo{}; shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.pCode = PixelSamplerProbeSpirv; shaderInfo.codeSize = sizeof(PixelSamplerProbeSpirv);
        VkShaderModule shader{}; check(vkCreateShaderModule(device,&shaderInfo,nullptr,&shader));
        VkPipelineLayoutCreateInfo pl{}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pl.setLayoutCount = 1; pl.pSetLayouts = &layout;
        const VkResult layoutResult = vkCreatePipelineLayout(device,&pl,nullptr,&pipelineLayout);
        if (layoutResult != VK_SUCCESS) { vkDestroyShaderModule(device,shader,nullptr); check(layoutResult); }
        VkComputePipelineCreateInfo cp{}; cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = shader; cp.stage.pName = "main"; cp.layout = pipelineLayout;
        const VkResult pipelineResult = vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);
        vkDestroyShaderModule(device,shader,nullptr); check(pipelineResult);
    }
};

constexpr std::uint32_t Width = 10, Height = 6, Pixels = Width*Height;
constexpr VkDeviceSize RawBytes = Pixels*3, SampledBytes = Pixels*4, TotalBytes = Pixels*7, GuardBytes = 64;
static bool pixelMatch(const std::uint8_t* actual, const std::vector<std::uint8_t>& expected, unsigned tolerance) {
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (std::abs(int(actual[i])-int(expected[i])) > int(tolerance)) return false;
    return true;
}
static void checkOutput(Gpu& gpu, std::size_t target, const std::vector<std::uint8_t>& expected, unsigned tolerance) {
    const auto* bytes = static_cast<const std::uint8_t*>(gpu.buffers[target].mapped);
    require(std::all_of(bytes,bytes+RawBytes,[](auto v){return v==0x5a;}), "raw plane prefix is preserved");
    require(std::all_of(bytes+TotalBytes,bytes+TotalBytes+GuardBytes,[](auto v){return v==0x5a;}), "partial final workgroup cannot write beyond sampled extent");
    for (std::size_t i = 0; i < expected.size(); ++i) require(std::abs(int(bytes[RawBytes+i])-int(expected[i])) <= int(tolerance), "actual sampled pixel/component matches owned reference");
    auto corrupted = expected; corrupted[19] ^= 0x70;
    require(!pixelMatch(bytes+RawBytes,corrupted,tolerance), "reference defect is detected, not merely checking nonzero output");
}
static std::size_t targetBuffer(Gpu& gpu) {
    const auto i = gpu.buffer(TotalBytes+GuardBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::memset(gpu.buffers[i].mapped,0x5a,TotalBytes+GuardBytes); gpu.flush(i); return i;
}
static void rgba(Gpu& gpu) {
    const auto target = targetBuffer(gpu); const auto staging = gpu.buffer(SampledBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::vector<std::uint8_t> expected(SampledBytes);
    for (unsigned y=0;y<Height;++y) for(unsigned x=0;x<Width;++x) {
        const unsigned i=(y*Width+x)*4; expected[i]=(x*23+y*7)%256; expected[i+1]=(y*41+x*3)%256;
        expected[i+2]=(x*17+y*29)%256; expected[i+3]=(x*11+y*13+31)%256;
    }
    std::memcpy(gpu.buffers[staging].mapped,expected.data(),expected.size()); gpu.flush(staging);
    const auto image = gpu.image(VK_FORMAT_R8G8B8A8_UNORM,Width,Height); gpu.samplerPipeline(image,target,TotalBytes);
    gpu.begin(); gpu.upload(image,staging,Width,Height,false);
    vkCmdFillBuffer(gpu.command,gpu.buffers[target].value,0,RawBytes,0x5a5a5a5a);
    VkBufferMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.buffer=gpu.buffers[target].value;b.size=TotalBytes;
    vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&b,0,nullptr);
    vkCmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
    vkCmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,&gpu.set,0,nullptr);
    vkCmdDispatch(gpu.command,(Width+7)/8,(Height+7)/8,1);
    b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    gpu.finish(target); checkOutput(gpu,target,expected,1);
}
static bool p010(Gpu& gpu, std::string& skip) {
    if (!gpu.ycbcrEnabled) { skip="samplerYcbcrConversion feature unavailable; no P010 sampling claim"; return false; }
    VkFormatProperties format{}; vkGetPhysicalDeviceFormatProperties(gpu.physical,P010,&format);
    constexpr VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
        |VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;
    if ((format.optimalTilingFeatures&required)!=required) { skip="P010 midpoint/linear native profile unavailable; no P010 sampling claim"; return false; }
    VkPhysicalDeviceImageFormatInfo2 info{};info.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.format=P010;info.type=VK_IMAGE_TYPE_2D;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkSamplerYcbcrConversionImageFormatProperties conversion{};conversion.sType=VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};properties.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;properties.pNext=&conversion;
    if (vkGetPhysicalDeviceImageFormatProperties2(gpu.physical,&info,&properties)!=VK_SUCCESS || !conversion.combinedImageSamplerDescriptorCount) {
        skip="P010 sampled+upload image profile unavailable; no P010 sampling claim";return false;
    }
    const auto target=targetBuffer(gpu);const auto staging=gpu.buffer(RawBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto* words=static_cast<std::uint16_t*>(gpu.buffers[staging].mapped);std::vector<std::uint8_t> expected(SampledBytes);
    for(unsigned i=0;i<Pixels;++i) {
        const auto code=std::uint16_t((64+i*71)%960);words[i]=std::uint16_t(code<<6);
        expected[i*4]=191;expected[i*4+1]=std::uint8_t((unsigned(code)*255+511)/1023);expected[i*4+2]=64;expected[i*4+3]=255;
    }
    for(unsigned i=0;i<Pixels/4;++i) {words[Pixels+i*2]=256u<<6;words[Pixels+i*2+1]=768u<<6;}gpu.flush(staging);
    auto profile=SamplerProbe::profile();check(vkCreateSamplerYcbcrConversion(gpu.device,&profile,nullptr,&gpu.sourceConversion));
    const auto image=gpu.image(P010,Width,Height,gpu.sourceConversion);
    SamplerProbe probe;
    struct Release {SamplerProbe& probe;Gpu& gpu;~Release(){vkDeviceWaitIdle(gpu.device);probe.release(true);}} release{probe,gpu};
    Preparation preparation{};preparation.targetBuffer=gpu.buffers[target].value;preparation.capacity=TotalBytes+GuardBytes;
    preparation.samplerYcbcrConversionEnabled=true;preparation.combinedImageSamplerDescriptorCount=conversion.combinedImageSamplerDescriptorCount;
    require(probe.prepare(gpu.device,vkGetDeviceProcAddr,preparation)==Status::Ok,"production P010 sampler resources prepare on actual driver");
    Source source{};source.complete=true;source.view=gpu.images[image].view;source.viewGeneration=1;source.extent={Width,Height};source.format=P010;
    source.subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};source.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    source.hasViewConversion=source.conversionComplete=true;source.conversion=profile;Dispatch dispatch;
    require(probe.admit(source,dispatch),"production native-profile P010 view admitted");
    gpu.begin();gpu.upload(image,staging,Width,Height,true);vkCmdFillBuffer(gpu.command,gpu.buffers[target].value,0,RawBytes,0x5a5a5a5a);
    require(probe.record(gpu.command,dispatch)==Status::Ok,"production native-profile P010 compute sampling recorded");
    gpu.finish(target);checkOutput(gpu,target,expected,1);return true;
}
static bool supportedP010(Gpu& gpu, std::string& skip) {
    if (!gpu.ycbcrEnabled) { skip="samplerYcbcrConversion unavailable; no alternate-profile P010 GPU claim";return false; }
    VkFormatProperties format{};vkGetPhysicalDeviceFormatProperties(gpu.physical,P010,&format);
    constexpr VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;
    if((format.optimalTilingFeatures&required)!=required) {skip="cosited P010 fixture profile unavailable";return false;}
    VkPhysicalDeviceImageFormatInfo2 info{};info.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.format=P010;info.type=VK_IMAGE_TYPE_2D;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkSamplerYcbcrConversionImageFormatProperties conversion{};conversion.sType=VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};properties.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;properties.pNext=&conversion;
    if(vkGetPhysicalDeviceImageFormatProperties2(gpu.physical,&info,&properties)!=VK_SUCCESS||!conversion.combinedImageSamplerDescriptorCount) {
        skip="cosited sampled+upload P010 fixture unavailable";return false;
    }
    const auto target=targetBuffer(gpu);const auto staging=gpu.buffer(RawBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto* words=static_cast<std::uint16_t*>(gpu.buffers[staging].mapped);std::vector<std::uint8_t> expected(SampledBytes);
    for(unsigned i=0;i<Pixels;++i) {
        const auto code=std::uint16_t((64+i*71)%960);words[i]=std::uint16_t(code<<6);
        expected[i*4]=191;expected[i*4+1]=std::uint8_t((unsigned(code)*255+511)/1023);expected[i*4+2]=64;expected[i*4+3]=255;
    }
    for(unsigned i=0;i<Pixels/4;++i) {words[Pixels+i*2]=256u<<6;words[Pixels+i*2+1]=768u<<6;}gpu.flush(staging);
    // Fixture-only supported profile. Production profile()/matchesProfile() and
    // SamplerProbe admission remain exclusively MIDPOINT+LINEAR with no bypass.
    auto profile=SamplerProbe::profile();profile.xChromaOffset=profile.yChromaOffset=VK_CHROMA_LOCATION_COSITED_EVEN;
    profile.chromaFilter=VK_FILTER_NEAREST;
    require(!SamplerProbe::matchesProfile(profile),"supported fixture profile cannot pass production native-profile admission");
    check(vkCreateSamplerYcbcrConversion(gpu.device,&profile,nullptr,&gpu.sourceConversion));
    const auto image=gpu.image(P010,Width,Height,gpu.sourceConversion);
    gpu.samplerPipeline(image,target,TotalBytes,gpu.sourceConversion,VK_FILTER_NEAREST,conversion.combinedImageSamplerDescriptorCount);
    gpu.begin();gpu.upload(image,staging,Width,Height,true);
    vkCmdFillBuffer(gpu.command,gpu.buffers[target].value,0,RawBytes,0x5a5a5a5a);
    VkBufferMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.buffer=gpu.buffers[target].value;b.size=TotalBytes;
    vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&b,0,nullptr);
    vkCmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
    vkCmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,&gpu.set,0,nullptr);
    vkCmdDispatch(gpu.command,(Width+7)/8,(Height+7)/8,1);
    b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    gpu.finish(target);checkOutput(gpu,target,expected,1);return true;
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc==2?argv[1]:"all";
        if(argc>2||(mode!="all"&&mode!="rgba"&&mode!="p010-native"&&mode!="p010-supported"))throw std::runtime_error("expected all|rgba|p010-native|p010-supported");
        Gpu gpu;gpu.init(mode=="p010-supported" ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT : 0);
        std::string skip;bool native=false,supported=false;
        if(mode=="all"||mode=="rgba")rgba(gpu);
        if(mode=="all"||mode=="p010-native")native=p010(gpu,skip);
        if(mode=="p010-supported")supported=supportedP010(gpu,skip);
        const auto name=gpu.name;const auto validation=gpu.validationEnabled;gpu.shutdown();
        require(validationErrors.load()==0,"actual Vulkan lifetime/descriptors/commands have no validation errors");
        std::cout<<"{\"sampler_gpu_controls\":"<<checks<<",\"mode\":\""<<mode<<"\",\"rgba_shader_pixels\":"<<((mode=="all"||mode=="rgba")?Pixels:0)<<",\"native_p010_pixels\":"<<(native?Pixels:0)
            <<",\"native_p010_supported\":"<<(native?"true":"false")<<",\"native_p010_skip\":\""<<skip
            <<"\",\"alternate_profile_p010_pixels\":"<<(supported?Pixels:0)
            <<",\"alternate_profile\":\""<<(mode=="p010-supported"?"RGB_IDENTITY,FULL,COSITED_EVEN,NEAREST; fixture-only":"")
            <<"\",\"validation_enabled\":"<<(validation?"true":"false")<<",\"validation_errors\":"<<validationErrors.load()
            <<",\"device\":\""<<name<<"\"}\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
