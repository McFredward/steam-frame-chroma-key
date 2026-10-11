// Exercise the generated native-style SPIR-V, including partially covered pixels.
// Queue-idle waits and synchronous readback are test-only, never compositor code.
#include <vulkan/vulkan.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan error " + std::to_string(result));
}

static std::vector<uint32_t> readShader(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open shader");
    const auto count = file.tellg();
    if (count < 20 || count % 4) throw std::runtime_error("Invalid shader size");
    std::vector<uint32_t> words(static_cast<size_t>(count) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(words.data()), count))
        throw std::runtime_error("Cannot read shader");
    return words;
}

struct Image {
    VkImage handle{};
    VkDeviceMemory memory{};
    VkImageView view{};
};

struct Gpu {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool pool{};
    VkDescriptorSetLayout descriptorLayout{};
    VkDescriptorPool descriptors{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkRenderPass renderPass{};
    VkFramebuffer framebuffer{};
    VkSampler sampler{};
    VkBuffer staging{}, readback{};
    VkDeviceMemory stagingMemory{}, readbackMemory{};
    VkShaderModule vertex{}, fragment{};
    Image mask{}, target{};
    uint32_t family{};

    ~Gpu() {
        if (device) {
            vkDeviceWaitIdle(device);
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            vkDestroyRenderPass(device, renderPass, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroySampler(device, sampler, nullptr);
            vkDestroyDescriptorPool(device, descriptors, nullptr);
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            for (const auto &image : {mask, target}) {
                vkDestroyImageView(device, image.view, nullptr);
                vkDestroyImage(device, image.handle, nullptr);
                vkFreeMemory(device, image.memory, nullptr);
            }
            vkDestroyBuffer(device, staging, nullptr);
            vkDestroyBuffer(device, readback, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            vkFreeMemory(device, readbackMemory, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
    }

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            if ((bits & (1u << index)) &&
                (properties.memoryTypes[index].propertyFlags & required) == required) return index;
        throw std::runtime_error("Required memory type unavailable");
    }

    void buffer(VkBuffer &out, VkDeviceMemory &memory, VkDeviceSize size, VkBufferUsageFlags usage) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        check(vkCreateBuffer(device, &info, nullptr, &out));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, out, &requirements);
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &memory));
        check(vkBindBufferMemory(device, out, memory, 0));
    }

    void image(Image &out, VkFormat format, VkImageUsageFlags usage) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {4, 1, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        check(vkCreateImage(device, &info, nullptr, &out.handle));
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, out.handle, &requirements);
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, 0);
        check(vkAllocateMemory(device, &allocation, nullptr, &out.memory));
        check(vkBindImageMemory(device, out.handle, out.memory, 0));
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = out.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &view, nullptr, &out.view));
    }

    VkShaderModule module(const char *path) {
        const auto code = readShader(path);
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * 4;
        info.pCode = code.data();
        VkShaderModule out{};
        check(vkCreateShaderModule(device, &info, nullptr, &out));
        return out;
    }

    void init(const char *vertexPath, const char *fragmentPath, bool premultiplied) {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        check(vkCreateInstance(&info, nullptr, &instance));
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        for (auto candidate : devices) {
            uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> properties(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
            for (uint32_t index = 0; index < families; ++index)
                if (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    physical = candidate;
                    family = index;
                    break;
                }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("No headless Vulkan graphics device");
        float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        check(vkCreateDevice(physical, &deviceInfo, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "GPU test device: " << properties.deviceName << '\n';

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = family;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
        buffer(staging, stagingMemory, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        buffer(readback, readbackMemory, 4 * 4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        image(mask, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        image(target, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        check(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 31;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout));
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo descriptorInfo{};
        descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorInfo.maxSets = descriptorInfo.poolSizeCount = 1;
        descriptorInfo.pPoolSizes = &poolSize;
        check(vkCreateDescriptorPool(device, &descriptorInfo, nullptr, &descriptors));
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptors;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout;
        VkDescriptorSet set{};
        check(vkAllocateDescriptorSets(device, &setInfo, &set));
        VkDescriptorImageInfo imageInfo{sampler, mask.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 31;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        VkPushConstantRange constant{VK_SHADER_STAGE_VERTEX_BIT, 0, 4 * sizeof(float)};
        VkPipelineLayoutCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &descriptorLayout;
        pipelineInfo.pushConstantRangeCount = 1;
        pipelineInfo.pPushConstantRanges = &constant;
        check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &pipelineLayout));
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        passInfo.attachmentCount = passInfo.subpassCount = passInfo.dependencyCount = 1;
        passInfo.pAttachments = &attachment;
        passInfo.pSubpasses = &subpass;
        passInfo.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &passInfo, nullptr, &renderPass));
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &target.view;
        framebufferInfo.width = 4;
        framebufferInfo.height = framebufferInfo.layers = 1;
        check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer));

        vertex = module(vertexPath);
        fragment = module(fragmentPath);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto &stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        VkPipelineVertexInputStateCreateInfo input{};
        input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, 4, 1, 0, 1};
        VkRect2D scissor{{0, 0}, {4, 1}};
        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.viewportCount = viewportInfo.scissorCount = 1;
        viewportInfo.pViewports = &viewport;
        viewportInfo.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blending{};
        blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;
        VkGraphicsPipelineCreateInfo graphics{};
        graphics.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphics.stageCount = stages.size();
        graphics.pStages = stages.data();
        graphics.pVertexInputState = &input;
        graphics.pInputAssemblyState = &assembly;
        graphics.pViewportState = &viewportInfo;
        graphics.pRasterizationState = &raster;
        graphics.pMultisampleState = &samples;
        graphics.pColorBlendState = &blending;
        graphics.layout = pipelineLayout;
        graphics.renderPass = renderPass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics, nullptr, &pipeline));

        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = pool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(device, &commandInfo, &command));
        bool initialized = false;
        for (const auto &color : std::array<std::array<float, 4>, 3>{{
            {{0, 1, 0, 1}}, {{0.2f, 0.4f, 0.8f, 1}}, {{1, 0, 1, 1}}}}) {
            for (unsigned scenario = 0; scenario < (premultiplied ? 6u : 2u); ++scenario) {
                std::array<uint8_t, 4> coverage{{0, 64, 128, 255}};
                auto sourceColor = color;
                if (scenario == 1) coverage.fill(255);
                if (scenario >= 2) {
                    const auto value = coverage[scenario - 2];
                    coverage.fill(value);
                    for (size_t channel = 0; channel < 3; ++channel) sourceColor[channel] *= value / 255.f;
                }
                void *mapped = nullptr;
                check(vkMapMemory(device, stagingMemory, 0, 4, 0, &mapped));
                std::memcpy(mapped, coverage.data(), 4);
                vkUnmapMemory(device, stagingMemory);
                check(vkResetCommandBuffer(command, 0));
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                check(vkBeginCommandBuffer(command, &begin));
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = mask.handle;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(command, initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {4, 1, 1};
                vkCmdCopyBufferToImage(command, staging, mask.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
                VkRenderPassBeginInfo draw{};
                draw.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                draw.renderPass = renderPass;
                draw.framebuffer = framebuffer;
                draw.renderArea = scissor;
                vkCmdBeginRenderPass(command, &draw, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
                vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, sourceColor.data());
                vkCmdDraw(command, 3, 1, 0, 0);
                vkCmdEndRenderPass(command);
                vkCmdCopyImageToBuffer(command, target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
                VkBufferMemoryBarrier readBarrier{};
                readBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                readBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                readBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                readBarrier.srcQueueFamilyIndex = readBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                readBarrier.buffer = readback;
                readBarrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 0, nullptr, 1, &readBarrier, 0, nullptr);
                check(vkEndCommandBuffer(command));
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &command;
                check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
                check(vkQueueWaitIdle(queue));
                check(vkMapMemory(device, readbackMemory, 0, 64, 0, &mapped));
                const auto *pixels = static_cast<const float *>(mapped);
                for (size_t pixel = 0; pixel < 4; ++pixel) {
                    const float alpha = coverage[pixel] / 255.f;
                    for (size_t channel = 0; channel < 4; ++channel) {
                        const float expected = channel == 3 ? alpha : sourceColor[channel] *
                            (premultiplied ? (alpha > 0 ? 1.f : 0.f) : alpha);
                        if (std::abs(pixels[pixel * 4 + channel] - expected) > 0.0002f) {
                            vkUnmapMemory(device, readbackMemory);
                            throw std::runtime_error("Generated GPU shader changed mask coverage or multiplied RGB incorrectly");
                        }
                    }
                }
                vkUnmapMemory(device, readbackMemory);
                initialized = true;
            }
        }
        std::cout << "PASS: generated mask shader (" << (premultiplied ? "premultiplied" : "straight")
                  << "), partial edges, colour independence and opaque fallback\n";
    }
};

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    try {
        Gpu gpu;
        gpu.init(argv[1], argv[2], argc == 4 && std::string(argv[3]) == "premultiplied");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
