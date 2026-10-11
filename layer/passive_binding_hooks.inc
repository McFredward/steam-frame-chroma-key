// Scalar API metadata only; driver arguments/results remain untouched.

VKAPI_ATTR VkResult VKAPI_CALL traceCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDescriptorSetLayout* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateDescriptorSetLayout(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createSetLayout(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroySetLayout(d->id, object); });
    d->DestroyDescriptorSetLayout(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipelineLayout* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreatePipelineLayout(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createPipelineLayout(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyPipelineLayout(VkDevice device, VkPipelineLayout object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroyPipelineLayout(d->id, object); });
    d->DestroyPipelineLayout(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDescriptorPool* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateDescriptorPool(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createPool(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyDescriptorPool(VkDevice device, VkDescriptorPool object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroyPool(d->id, object); });
    d->DestroyDescriptorPool(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkBuffer* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateBuffer(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createBuffer(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyBuffer(VkDevice device, VkBuffer object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroyBuffer(d->id, object); });
    d->DestroyBuffer(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreateImage(VkDevice device, const VkImageCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImage* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateImage(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createImage(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyImage(VkDevice device, VkImage object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroyImage(d->id, object); });
    d->DestroyImage(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreateImageView(VkDevice device, const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImageView* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateImageView(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createView(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyImageView(VkDevice device, VkImageView object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroyView(d->id, object); });
    d->DestroyImageView(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceCreateSampler(VkDevice device, const VkSamplerCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkSampler* output) {
    auto* d = deviceFor(device);
    const auto result = d->CreateSampler(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.createSampler(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroySampler(VkDevice device, VkSampler object,
    const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.destroySampler(d->id, object); });
    d->DestroySampler(device, object, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks* allocator, VkDeviceMemory* output) {
    auto* d = deviceFor(device);
    const auto result = d->AllocateMemory(device, info, allocator, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.allocateMemory(d->id, *output, *info); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.freeMemory(d->id, memory); });
    d->FreeMemory(device, memory, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL traceBindBufferMemory(VkDevice device, VkBuffer object,
    VkDeviceMemory memory, VkDeviceSize offset) {
    auto* d = deviceFor(device);
    const auto result = d->BindBufferMemory(device, object, memory, offset);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.bindBufferMemory(d->id, object, memory, offset); });
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceBindBufferMemory2(VkDevice device, std::uint32_t count,
    const VkBindBufferMemoryInfo* info) {
    auto* d = deviceFor(device);
    const auto result = d->BindBufferMemory2(device, count, info);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) {
        if (count > CallRecordLimit) b.reportUnsupported(d->id);
        for (std::uint32_t i = 0; i < std::min(count, CallRecordLimit); ++i)
            b.bindBufferMemory(d->id, info[i].buffer, info[i].memory, info[i].memoryOffset, !info[i].pNext);
    });
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traceBindImageMemory(VkDevice device, VkImage object,
    VkDeviceMemory memory, VkDeviceSize offset) {
    auto* d = deviceFor(device);
    const auto result = d->BindImageMemory(device, object, memory, offset);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.bindImageMemory(d->id, object, memory, offset); });
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceBindImageMemory2(VkDevice device, std::uint32_t count,
    const VkBindImageMemoryInfo* info) {
    auto* d = deviceFor(device);
    const auto result = d->BindImageMemory2(device, count, info);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) {
        if (count > CallRecordLimit) b.reportUnsupported(d->id);
        for (std::uint32_t i = 0; i < std::min(count, CallRecordLimit); ++i)
            b.bindImageMemory(d->id, info[i].image, info[i].memory, info[i].memoryOffset, !info[i].pNext);
    });
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traceAllocateDescriptorSets(VkDevice device,
    const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
    auto* d = deviceFor(device);
    const auto result = d->AllocateDescriptorSets(device, info, output);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.allocateSets(d->id, *info, output); });
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceFreeDescriptorSets(VkDevice device, VkDescriptorPool pool,
    std::uint32_t count, const VkDescriptorSet* sets) {
    auto* d = deviceFor(device);
    const auto result = d->FreeDescriptorSets(device, pool, count, sets);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.freeSets(d->id, pool, count, sets); });
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceResetDescriptorPool(VkDevice device, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags) {
    auto* d = deviceFor(device);
    const auto result = d->ResetDescriptorPool(device, pool, flags);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.resetPool(d->id, pool); });
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceUpdateDescriptorSets(VkDevice device, std::uint32_t writeCount,
    const VkWriteDescriptorSet* writes, std::uint32_t copyCount, const VkCopyDescriptorSet* copies) {
    auto* d = deviceFor(device);
    d->UpdateDescriptorSets(device, writeCount, writes, copyCount, copies);
    observeBindings([&](auto& b) { b.updateSets(d->id, writeCount, writes, copyCount, copies); });
}
VKAPI_ATTR void VKAPI_CALL traceCmdBindDescriptorSets(VkCommandBuffer command, VkPipelineBindPoint point,
    VkPipelineLayout layout, std::uint32_t firstSet, std::uint32_t count, const VkDescriptorSet* sets,
    std::uint32_t dynamicCount, const std::uint32_t* offsets) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.bindSets(d->id, command, point, layout, firstSet, count, sets, dynamicCount, offsets); });
    d->CmdBindDescriptorSets(command, point, layout, firstSet, count, sets, dynamicCount, offsets);
}

VKAPI_ATTR void VKAPI_CALL traceUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set, VkDescriptorUpdateTemplate updateTemplate, const void* data) {
    auto* d = deviceFor(device);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->UpdateDescriptorSetWithTemplate(device, set, updateTemplate, data);
}

VKAPI_ATTR void VKAPI_CALL traceCmdPushDescriptorSetKHR(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout, std::uint32_t set, std::uint32_t count, const VkWriteDescriptorSet* writes) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdPushDescriptorSetKHR(command, point, layout, set, count, writes);
}

VKAPI_ATTR void VKAPI_CALL traceCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer command, VkDescriptorUpdateTemplate updateTemplate, VkPipelineLayout layout, std::uint32_t set, const void* data) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdPushDescriptorSetWithTemplateKHR(command, updateTemplate, layout, set, data);
}

VKAPI_ATTR void VKAPI_CALL traceCmdBindDescriptorBuffersEXT(VkCommandBuffer command, std::uint32_t count, const VkDescriptorBufferBindingInfoEXT* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdBindDescriptorBuffersEXT(command, count, info);
}

VKAPI_ATTR void VKAPI_CALL traceCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout, std::uint32_t firstSet, std::uint32_t count, const std::uint32_t* indices, const VkDeviceSize* offsets) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdSetDescriptorBufferOffsetsEXT(command, point, layout, firstSet, count, indices, offsets);
}

VKAPI_ATTR void VKAPI_CALL traceCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout, std::uint32_t set) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdBindDescriptorBufferEmbeddedSamplersEXT(command, point, layout, set);
}

VKAPI_ATTR void VKAPI_CALL traceCmdBindDescriptorSets2KHR(VkCommandBuffer command, const VkBindDescriptorSetsInfoKHR* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdBindDescriptorSets2KHR(command, info);
}

VKAPI_ATTR void VKAPI_CALL traceCmdPushDescriptorSet2KHR(VkCommandBuffer command, const VkPushDescriptorSetInfoKHR* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdPushDescriptorSet2KHR(command, info);
}

VKAPI_ATTR void VKAPI_CALL traceCmdPushDescriptorSetWithTemplate2KHR(VkCommandBuffer command, const VkPushDescriptorSetWithTemplateInfoKHR* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdPushDescriptorSetWithTemplate2KHR(command, info);
}

VKAPI_ATTR void VKAPI_CALL traceCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer command, const VkSetDescriptorBufferOffsetsInfoEXT* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdSetDescriptorBufferOffsets2EXT(command, info);
}

VKAPI_ATTR void VKAPI_CALL traceCmdBindDescriptorBufferEmbeddedSamplers2EXT(VkCommandBuffer command, const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* info) {
    auto* d = deviceFor(command);
    observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    d->CmdBindDescriptorBufferEmbeddedSamplers2EXT(command, info);
}

VKAPI_ATTR VkResult VKAPI_CALL traceQueueBindSparse(VkQueue queue, std::uint32_t count,
    const VkBindSparseInfo* info, VkFence fence) {
    auto* d = deviceFor(queue);
    const auto result = d->QueueBindSparse(queue, count, info, fence);
    if (result == VK_SUCCESS) observeBindings([&](auto& b) { b.reportUnsupported(d->id); });
    return result;
}
