#include "layer/passive_binding_trace.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
using namespace frame_bindings;
namespace ext = frame_bindings::extensions;
std::uint32_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }
const VkImage image = h<VkImage>(10);
const VkImageView view = h<VkImageView>(20);
const VkSampler sampler = h<VkSampler>(30);
const VkDeviceMemory memory = h<VkDeviceMemory>(40);
const VkDescriptorSetLayout setLayout = h<VkDescriptorSetLayout>(50);
const VkPipelineLayout pipelineLayout = h<VkPipelineLayout>(60);
const VkDescriptorPool pool = h<VkDescriptorPool>(70);
const VkDescriptorSet set = h<VkDescriptorSet>(80);
const VkCommandBuffer command = h<VkCommandBuffer>(90);
const VkCommandPool commandPool = h<VkCommandPool>(100);
const VkSamplerYcbcrConversion conversion = h<VkSamplerYcbcrConversion>(110);

struct Resources {
    std::unique_ptr<Core> core = std::make_unique<Core>();
    VkImageCreateInfo im = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    VkMemoryAllocateInfo mem = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    VkImageViewCreateInfo vi = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    VkSamplerCreateInfo si = info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    VkSamplerYcbcrConversionCreateInfo ci = info<VkSamplerYcbcrConversionCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);
    Resources() {
        im.imageType = VK_IMAGE_TYPE_2D; im.format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        im.extent = {1152, 4608, 1}; im.mipLevels = 1; im.arrayLayers = 1;
        im.samples = VK_SAMPLE_COUNT_1_BIT; im.tiling = VK_IMAGE_TILING_OPTIMAL; im.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        mem.allocationSize = 32 * 1024 * 1024; mem.memoryTypeIndex = 3;
        vi.image = image; vi.format = im.format; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
        ci.format = im.format; ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; ci.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        ci.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
        ci.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT; ci.yChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
        ci.chromaFilter = VK_FILTER_LINEAR; ci.forceExplicitReconstruction = VK_TRUE;
    }
    void create(bool makeConversion = false) {
        if (makeConversion) core->createConversion(1, conversion, ci);
        core->createImage(1, image, im); core->allocateMemory(1, memory, mem); core->bindImageMemory(1, image, memory, 0);
        core->createView(1, view, vi); core->createSampler(1, sampler, si);
    }
    void descriptor(VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        const VkDescriptorSetLayoutBinding definition{9, type, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
            type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? &sampler : nullptr};
        auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layout.bindingCount = 1; layout.pBindings = &definition; core->createSetLayout(1, setLayout, layout);
        auto pl = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pl.setLayoutCount = 1; pl.pSetLayouts = &setLayout; core->createPipelineLayout(1, pipelineLayout, pl);
        auto pi = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO); core->createPool(1, pool, pi);
        auto allocate = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocate.descriptorPool = pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &setLayout; core->allocateSets(1, allocate, &set);
        write(type);
        core->allocateCommands(1, commandPool, 1, &command); core->beginCommand(1, command);
        core->bindSets(1, command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
    }
    void write(VkDescriptorType type) {
        const VkDescriptorImageInfo value{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = set; write.dstBinding = 9; write.descriptorCount = 1; write.descriptorType = type; write.pImageInfo = &value;
        core->updateSets(1, 1, &write, 0, nullptr);
    }
    Snapshot snapshot() { return core->snapshot(1, command, pipelineLayout); }
};
const Binding& target(const Snapshot& snapshot) { return snapshot.bindings[5]; }

void knownChainsAndOwnership() {
    auto external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkSubresourceLayout layouts[2]{{0, 0, 2304, 0, 0}, {10616832, 0, 2304, 0, 0}};
    auto drm = info<VkImageDrmFormatModifierExplicitCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    drm.drmFormatModifier = 0x0500000000000001ULL; drm.drmFormatModifierPlaneCount = 2; drm.pPlaneLayouts = layouts;
    external.pNext = &drm;
    auto parsed = ext::parseImage(&external);
    require(parsed.chain.complete && parsed.chain.count == 2 && parsed.hasExternal && parsed.hasDrmExplicit, "known image chain rejected");
    require(parsed.chain.types[0] == external.sType && parsed.chain.types[1] == drm.sType && parsed.drmModifier == drm.drmFormatModifier &&
            parsed.planes[1].offset == layouts[1].offset && parsed.planes[1].rowPitch == 2304, "DRM scalar chain metadata omitted");
    layouts[1].offset = 1; drm.drmFormatModifier = 2; external.handleTypes = 0;
    require(parsed.planes[1].offset == 10616832 && parsed.drmModifier == 0x0500000000000001ULL &&
            parsed.externalHandleTypes == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, "owned image record retained caller storage");

    auto import = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; import.fd = 27;
    auto exported = info<VkExportMemoryAllocateInfo>(VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
    exported.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; import.pNext = &exported;
    auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = image; exported.pNext = &dedicated;
    const auto allocation = ext::parseMemory(&import);
    require(allocation.chain.complete && allocation.hasImport && allocation.hasExport && allocation.hasDedicated && allocation.inputFD == 27 &&
            allocation.importType == import.handleType && allocation.exportTypes == exported.handleTypes && allocation.dedicatedImageHandle == handle(image),
            "import/export/dedicated allocation input metadata omitted");
    import.fd = 88; dedicated.image = h<VkImage>(999);
    require(allocation.inputFD == 27 && allocation.dedicatedImageHandle == handle(image), "allocation record retained input pointers");
    require(ext::parseImage(nullptr).chain.complete && ext::parseMemory(nullptr).chain.complete &&
            ext::parseView(nullptr).chain.complete && ext::parseSampler(nullptr).chain.complete && ext::parseConversion(nullptr).chain.complete,
            "empty chains failed default metadata observation");
}

void unknownDuplicateCycleAndBounds() {
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
    require(!ext::parseImage(&unknown).chain.complete && ext::parseImage(&unknown).chain.error == ext::ChainError::Unsupported,
            "unknown chain certified");
    auto one = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    auto two = one; one.pNext = &two;
    const auto duplicate = ext::parseImage(&one);
    require(!duplicate.chain.complete && duplicate.chain.error == ext::ChainError::Duplicate && duplicate.chain.count == 2,
            "duplicate node certified or summary omitted");
    one.pNext = &one;
    require(ext::parseImage(&one).chain.error == ext::ChainError::Cycle && ext::parseImage(&one).chain.count == 1, "chain cycle did not stop bounded traversal");
    VkBaseInStructure nodes[ext::ChainCapacity]{};
    for (std::size_t i = 0; i < ext::ChainCapacity; ++i) {
        nodes[i].sType = static_cast<VkStructureType>(1000200000 + i);
        nodes[i].pNext = i + 1 == ext::ChainCapacity ? reinterpret_cast<const VkBaseInStructure*>(1) : &nodes[i + 1];
    }
    const auto longChain = ext::parseImage(nodes);
    require(!longChain.chain.complete && longChain.chain.count == ext::ChainCapacity &&
            (longChain.chain.errorFlags & (1U << static_cast<unsigned>(ext::ChainError::Bounds))), "overlong chain read ninth node or lost bounds diagnostic");
    auto drm = info<VkImageDrmFormatModifierExplicitCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    drm.drmFormatModifierPlaneCount = ext::PlaneCapacity + 1; drm.pPlaneLayouts = reinterpret_cast<const VkSubresourceLayout*>(1);
    require(!ext::parseImage(&drm).chain.complete && ext::parseImage(&drm).chain.error == ext::ChainError::Bounds,
            "oversized plane array read or certified");
    drm.drmFormatModifierPlaneCount = 2; drm.pPlaneLayouts = nullptr;
    require(ext::parseImage(&drm).chain.error == ext::ChainError::Bounds, "missing plane layouts certified");
    auto list = info<VkImageDrmFormatModifierListCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
    list.drmFormatModifierCount = ext::ModifierCapacity + 1; list.pDrmFormatModifiers = reinterpret_cast<const std::uint64_t*>(1);
    require(ext::parseImage(&list).chain.error == ext::ChainError::Bounds, "oversized modifier list read or certified");
    list.drmFormatModifierCount = 0; list.pDrmFormatModifiers = nullptr;
    require(ext::parseImage(&list).chain.error == ext::ChainError::Bounds, "empty modifier candidate list certified");
    const VkSubresourceLayout maximumPlanes[ext::PlaneCapacity]{};
    drm.drmFormatModifierPlaneCount = ext::PlaneCapacity; drm.pPlaneLayouts = maximumPlanes;
    require(ext::parseImage(&drm).chain.complete, "maximum bounded plane count rejected");
    const std::uint64_t maximumModifiers[ext::ModifierCapacity]{1, 2, 3, 4, 5, 6, 7, 8};
    list.drmFormatModifierCount = ext::ModifierCapacity; list.pDrmFormatModifiers = maximumModifiers;
    require(ext::parseImage(&list).chain.complete && ext::parseImage(&list).modifiers[ext::ModifierCapacity - 1] == 8,
            "maximum bounded modifier count lost data");
    drm.pNext = &list;
    require(!ext::parseImage(&drm).chain.complete, "conflicting explicit/list DRM chains accepted");
    require(!ext::parseMemory(&unknown).chain.complete && !ext::parseView(&unknown).chain.complete &&
            !ext::parseSampler(&unknown).chain.complete && !ext::parseConversion(&unknown).chain.complete, "one resource parser accepted unknown chains");
}

void customBorderRawBits() {
    auto custom = info<VkSamplerCustomBorderColorCreateInfoEXT>(VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
    const std::uint32_t bits[]{0x7fc01234, 0x80000000, 0x3f000000, 0xffffffff};
    std::memcpy(&custom.customBorderColor, bits, sizeof(bits)); custom.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    auto attached = info<VkSamplerYcbcrConversionInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO);
    attached.conversion = conversion; custom.pNext = &attached;
    const auto parsed = ext::parseSampler(&custom);
    require(parsed.chain.complete && parsed.hasCustomBorder && parsed.hasConversion && parsed.customBorderFormat == custom.format &&
            parsed.conversionHandle == handle(conversion), "custom border / conversion chain scalar metadata omitted");
    for (std::size_t i = 0; i < 4; ++i) require(parsed.customBorderBits[i] == bits[i], "custom border bit pattern canonicalized");
    const auto v = ext::parseView(&attached);
    require(v.chain.complete && v.conversionHandle == handle(conversion), "view conversion attachment rejected");
    attached.conversion = VK_NULL_HANDLE;
    require(!ext::parseView(&attached).chain.complete && !ext::parseSampler(&attached).chain.complete, "null conversion attachment certified");
}

void importedImageAndDedicatedIdentity() {
    Resources r;
    auto external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    const VkSubresourceLayout layouts[2]{{0, 0, 2304, 0, 0}, {10616832, 0, 2304, 0, 0}};
    auto drm = info<VkImageDrmFormatModifierExplicitCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    drm.drmFormatModifier = 0x0500000000000001ULL; drm.drmFormatModifierPlaneCount = 2; drm.pPlaneLayouts = layouts;
    external.pNext = &drm; r.im.pNext = &external; r.im.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    auto import = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    import.fd = 123; import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO); dedicated.image = image;
    import.pNext = &dedicated; r.mem.pNext = &import;
    r.create(); r.descriptor(); const auto snapshot = r.snapshot(); const auto& value = target(snapshot);
    require(snapshot.sourceComplete && !snapshot.gpuConsumedCoherent && r.core->validate(snapshot), "known imported resource source metadata rejected or claimed GPU coherence");
    require(value.image.extensions.hasDrmExplicit && value.image.extensions.planes[1].offset == layouts[1].offset &&
            value.image.allocation.extensions.inputFD == 123 && value.image.allocation.extensions.importType == import.handleType &&
            same(value.image.allocation.dedicatedImage, value.image.identity), "DRM/import/dedicated resource generations omitted");
    r.core->destroyImage(1, image); r.core->createImage(1, image, r.im); r.core->bindImageMemory(1, image, memory, 0);
    r.core->destroyView(1, view); r.core->createView(1, view, r.vi); r.write(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    require(!r.snapshot().sourceComplete && !r.core->validate(snapshot), "dedicated memory revived reused image identity");
}

void scalarOverloadsSurviveCallerRelease() {
    Resources r;
    auto external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    auto import = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    import.fd = 456; import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    const auto imageRecord = ext::parseImage(&external); const auto memoryRecord = ext::parseMemory(&import);
    // A background replay may carry an invalidated base pNext value. Only the
    // explicit owned overload consumes extension data, never that pointer.
    r.im.pNext = reinterpret_cast<const void*>(1); r.mem.pNext = reinterpret_cast<const void*>(1);
    r.core->createImage(1, image, r.im, imageRecord); r.core->allocateMemory(1, memory, r.mem, memoryRecord);
    r.core->bindImageMemory(1, image, memory, 0);
    r.vi.pNext = reinterpret_cast<const void*>(1); r.si.pNext = reinterpret_cast<const void*>(1);
    r.core->createView(1, view, r.vi, ext::ViewExtensions{}); r.core->createSampler(1, sampler, r.si, ext::SamplerExtensions{});
    r.descriptor();
    require(r.snapshot().sourceComplete && target(r.snapshot()).image.allocation.extensions.inputFD == 456,
            "owned replay overload dereferenced old pNext or lost allocation data");
}

void conversionLifetimeAndMatching() {
    Resources r;
    auto attached = info<VkSamplerYcbcrConversionInfo>(VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO); attached.conversion = conversion;
    r.vi.pNext = &attached; r.si.pNext = &attached; r.create(true); r.descriptor();
    const auto initial = r.snapshot(); const auto& value = target(initial);
    require(initial.sourceComplete && same(value.view.conversion.identity, value.sampler.conversion.identity) &&
            value.view.conversion.model == r.ci.ycbcrModel && value.view.conversion.range == r.ci.ycbcrRange &&
            value.view.conversion.components.r == r.ci.components.r && value.view.conversion.xChromaOffset == r.ci.xChromaOffset &&
            value.view.conversion.chromaFilter == VK_FILTER_LINEAR && value.view.conversion.forceExplicitReconstruction == VK_TRUE,
            "conversion metadata or attachment generation omitted");
    r.core->destroyConversion(1, conversion);
    require(!r.snapshot().sourceComplete && !target(r.snapshot()).view.conversion.complete && !r.core->validate(initial),
            "destroyed conversion preserved dependent descriptor proof");
    r.core->createConversion(1, conversion, r.ci);
    require(!r.snapshot().sourceComplete, "conversion handle reuse revived attachment without recreation");
    r.core->destroyView(1, view); r.core->createView(1, view, r.vi); r.write(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    require(!r.snapshot().sourceComplete, "new view conversion revived old immutable sampler conversion");
    r.core->deviceDestroyed(1);
    require(!r.core->validate(initial), "device destruction retained conversion resource proof");

    Resources missing;
    missing.vi.pNext = &attached; missing.si.pNext = &attached; missing.create(); missing.descriptor();
    require(!missing.snapshot().sourceComplete, "unobserved conversion accepted");
    Resources mismatch;
    mismatch.core->createConversion(1, conversion, mismatch.ci);
    const auto other = h<VkSamplerYcbcrConversion>(111); mismatch.core->createConversion(1, other, mismatch.ci);
    auto different = attached; different.conversion = other;
    mismatch.vi.pNext = &attached; mismatch.si.pNext = &different; mismatch.create(); mismatch.descriptor();
    require(!mismatch.snapshot().sourceComplete, "mismatched view/sampler conversion generations certified");
}

void unknownInputsRemainIncomplete() {
    Resources r; VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
    r.im.pNext = &unknown; r.create(); r.descriptor(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    const auto snapshot = r.snapshot();
    require(!snapshot.sourceComplete && target(snapshot).image.identity.handle == handle(image) &&
            target(snapshot).image.extensions.chain.types[0] == unknown.sType && !target(snapshot).image.extensions.chain.complete,
            "unsupported image chain lost identity or failed to invalidate proof");
    require(r.core->lostState() == 0, "known unsupported metadata incorrectly treated as missed lifecycle");
    auto import = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    import.fd = -1; import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    require(!ext::parseMemory(&import).chain.complete, "invalid input FD certified");
    import.fd = 9; import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    require(!ext::parseMemory(&import).chain.complete, "non-FD handle type certified by FD metadata path");
    auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = image; dedicated.buffer = h<VkBuffer>(123);
    require(!ext::parseMemory(&dedicated).chain.complete, "simultaneous dedicated image and buffer accepted");
    Resources list;
    const std::uint64_t candidates[]{42, 43};
    auto selected = info<VkImageDrmFormatModifierListCreateInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
    selected.drmFormatModifierCount = 2; selected.pDrmFormatModifiers = candidates;
    list.im.pNext = &selected; list.create(); list.descriptor(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    const auto listSnapshot = list.snapshot();
    require(!listSnapshot.sourceComplete && target(listSnapshot).image.extensions.modifiers[1] == 43,
            "modifier candidate list incorrectly claimed actual chosen modifier");
    Resources custom;
    custom.si.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT; custom.create(); custom.descriptor();
    require(!custom.snapshot().sourceComplete, "custom border enum without color metadata certified");
}

void dedicatedBufferAndMismatchedBinding() {
    auto core = std::make_unique<Core>();
    const auto first = h<VkBuffer>(201), second = h<VkBuffer>(202);
    auto bi = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO); bi.size = 1024; bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    core->createBuffer(1, first, bi); core->createBuffer(1, second, bi);
    auto dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO); dedicated.buffer = first;
    auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.pNext = &dedicated; allocation.allocationSize = 4096;
    core->allocateMemory(1, memory, allocation); core->bindBufferMemory(1, second, memory, 0);
    const VkDescriptorSetLayoutBinding definition{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    auto layout = info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = 1; layout.pBindings = &definition; core->createSetLayout(1, setLayout, layout);
    auto pl = info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1; pl.pSetLayouts = &setLayout; core->createPipelineLayout(1, pipelineLayout, pl);
    auto pi = info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO); core->createPool(1, pool, pi);
    auto ai = info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &setLayout; core->allocateSets(1, ai, &set);
    const VkDescriptorBufferInfo value{second, 0, 128};
    auto write = info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = set; write.descriptorCount = 1; write.descriptorType = definition.descriptorType; write.pBufferInfo = &value;
    core->updateSets(1, 1, &write, 0, nullptr);
    core->allocateCommands(1, commandPool, 1, &command); core->beginCommand(1, command);
    core->bindSets(1, command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
    const auto snapshot = core->snapshot(1, command, pipelineLayout);
    require(!snapshot.sourceComplete && snapshot.bindings[0].buffer.allocation.dedicatedBuffer.handle == handle(first),
            "binding another buffer to dedicated allocation certified");
}

void customBorderCoreAndConversionCapacity() {
    Resources r;
    auto custom = info<VkSamplerCustomBorderColorCreateInfoEXT>(VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
    custom.customBorderColor.float32[0] = 0.25F; custom.customBorderColor.float32[3] = 1.0F; custom.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    r.si.pNext = &custom; r.si.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT; r.create(); r.descriptor();
    const auto snapshot = r.snapshot(); const auto parsed = ext::parseSampler(&custom);
    require(snapshot.sourceComplete && target(snapshot).sampler.extensions.customBorderBits == parsed.customBorderBits &&
            target(snapshot).sampler.extensions.customBorderFormat == custom.format, "custom border input not retained in descriptor snapshot");
    for (std::size_t i = 0; i < ConversionCapacity + 1; ++i) r.core->createConversion(2, h<VkSamplerYcbcrConversion>(1000 + i), r.ci);
    require(r.core->capacityLossCount(Table::Conversion) == 1 && r.core->capacityLossCount(Table::Sampler) == 0 && !r.snapshot().sourceComplete,
            "conversion table saturation omitted sticky loss or misidentified table");
}
}

int main() {
    knownChainsAndOwnership(); unknownDuplicateCycleAndBounds(); customBorderRawBits(); importedImageAndDedicatedIdentity();
    scalarOverloadsSurviveCallerRelease(); conversionLifetimeAndMatching(); unknownInputsRemainIncomplete(); dedicatedBufferAndMismatchedBinding(); customBorderCoreAndConversionCapacity();
    std::cout << "passive binding extensions: " << checks << " checks passed; Snapshot=" << sizeof(Snapshot) << ", Core=" << sizeof(Core) << " bytes\n";
}
