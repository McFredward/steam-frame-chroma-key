#include "layer/pixel_sampler_format.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace frame_pixel_sampler_format;
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
const auto physical = reinterpret_cast<VkPhysicalDevice>(std::uintptr_t(0x1234));

struct Fake {
    std::uint32_t firstCount = 1, secondCount = 1, formatCalls = 0, imageCalls = 0;
    std::array<VkDrmFormatModifierPropertiesEXT, MaximumModifiers> modifiers{};
    VkFormatProperties features{RequiredFeatures, RequiredFeatures, 0};
    std::array<VkResult, RouteCapacity> results{};
    std::array<std::uint32_t, RouteCapacity> descriptors{}, calls{};
    std::array<VkImageFormatProperties, RouteCapacity> properties{};
    bool corruptList = false, changedFeatures = false, unreadableModifiers = false;
    Fake() {
        for (std::size_t i = 0; i < modifiers.size(); ++i)
            modifiers[i] = {UINT64_C(0x0500000000000001) + i, 2, RequiredFeatures};
        for (std::size_t i = 0; i < RouteCapacity; ++i) {
            descriptors[i] = i == 0 ? 3 : i == 1 ? 5 : 7;
            properties[i] = {{4096, 8192, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT, 64 * 1024 * 1024};
        }
    }
};
Fake* current = nullptr;

VKAPI_ATTR void VKAPI_CALL formatProperties(VkPhysicalDevice device, VkFormat format, VkFormatProperties2* out) {
    auto& fake = *current;
    require(device == physical && format == NativeP010 && out && out->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
            "cold format query uses exact physical device, P010 and public output type");
    auto* list = static_cast<VkDrmFormatModifierPropertiesListEXT*>(out->pNext);
    require(list && list->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT && !list->pNext,
            "format query includes finite public DRM modifier list");
    out->formatProperties = fake.features;
    ++fake.formatCalls;
    if (fake.formatCalls == 1) {
        require(!list->pDrmFormatModifierProperties && !list->drmFormatModifierCount,
                "first modifier query requests only the advertised count");
        list->drmFormatModifierCount = fake.firstCount;
    } else {
        require(fake.formatCalls == 2 && list->pDrmFormatModifierProperties &&
                list->drmFormatModifierCount == fake.firstCount && fake.firstCount <= MaximumModifiers,
                "second modifier query uses fixed allocated capacity and original bounded count");
        if (!fake.unreadableModifiers)
            for (std::uint32_t i = 0; i < fake.firstCount; ++i) list->pDrmFormatModifierProperties[i] = fake.modifiers[i];
        list->drmFormatModifierCount = fake.secondCount;
        if (fake.changedFeatures) out->formatProperties.optimalTilingFeatures ^= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (fake.corruptList) list->pNext = reinterpret_cast<void*>(std::uintptr_t(1));
    }
}

VKAPI_ATTR VkResult VKAPI_CALL imageProperties(VkPhysicalDevice device, const VkPhysicalDeviceImageFormatInfo2* in,
                                               VkImageFormatProperties2* out) {
    auto& fake = *current;
    require(device == physical && in && in->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 &&
            in->format == NativeP010 && in->type == VK_IMAGE_TYPE_2D && in->usage == Usage && in->flags == 0,
            "image query retains exact format, image type, flags and sampled/transfer usage");
    std::size_t route = 0;
    if (in->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
        const auto* drm = static_cast<const VkPhysicalDeviceImageDrmFormatModifierInfoEXT*>(in->pNext);
        require(drm && drm->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT && !drm->pNext &&
                drm->sharingMode == VK_SHARING_MODE_EXCLUSIVE && !drm->queueFamilyIndexCount && !drm->pQueueFamilyIndices,
                "DRM image query uses exact modifier, exclusive sharing and no guessed queue list");
        route = 2;
        while (route < RouteCapacity && fake.modifiers[route - 2].drmFormatModifier != drm->drmFormatModifier) ++route;
        require(route < RouteCapacity, "only advertised modifier values are queried");
    } else {
        require(!in->pNext && (in->tiling == VK_IMAGE_TILING_OPTIMAL || in->tiling == VK_IMAGE_TILING_LINEAR),
                "basic tiling query has no irrelevant DRM input chain");
        route = in->tiling == VK_IMAGE_TILING_OPTIMAL ? 0 : 1;
    }
    require(out && out->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
            "image query uses public output type");
    auto* conversion = static_cast<VkSamplerYcbcrConversionImageFormatProperties*>(out->pNext);
    require(conversion && conversion->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES &&
            !conversion->pNext && !conversion->combinedImageSamplerDescriptorCount,
            "each image route requests its actual YCbCr descriptor requirement");
    ++fake.calls[route]; ++fake.imageCalls;
    if (fake.results[route] != VK_SUCCESS) return fake.results[route];
    out->imageFormatProperties = fake.properties[route];
    conversion->combinedImageSamplerDescriptorCount = fake.descriptors[route];
    return VK_SUCCESS;
}

Support query(Fake& fake) {
    current = &fake;
    const auto support = querySupport(physical, formatProperties, imageProperties);
    current = nullptr;
    return support;
}

frame_bindings::Binding source(VkImageTiling tiling) {
    frame_bindings::Binding binding{};
    binding.complete = true;
    auto& image = binding.image;
    image.complete = true; image.identity = {100, 200};
    image.type = VK_IMAGE_TYPE_2D; image.format = NativeP010; image.extent = {1152, 4608, 1};
    image.mipLevels = image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = Usage; image.sharingMode = VK_SHARING_MODE_EXCLUSIVE; image.tiling = tiling;
    image.extensions.chain.complete = true;
    if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
        image.extensions.hasDrmExplicit = true; image.extensions.drmModifier = UINT64_C(0x0500000000000001);
        image.extensions.planeCount = 2;
        // Actual historical TP10 UBWC physical layout is intentionally not the
        // logical P010 output-copy pitch. Admission never infers buffer strides.
        image.extensions.planes[0].rowPitch = image.extensions.planes[1].rowPitch = 1536;
        image.extensions.planes[1].offset = 7151616;
    }
    return binding;
}

void normalQueriesAndExactSource() {
    Fake fake; const auto support = query(fake);
    require(support.complete && support.routeCount == 3 && support.poolDescriptorCount == 7 &&
            fake.formatCalls == 2 && fake.imageCalls == 3, "all supported basic/modifier routes queried once with actual maximum descriptor count");
    for (auto tiling : {VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_TILING_LINEAR, VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT})
        require(admitSource(support, source(tiling)), "exact queried source tiling and optional modifier are admitted");
    require(support.routes[0].combinedDescriptorCount == 3 && support.routes[1].combinedDescriptorCount == 5 &&
            support.routes[2].combinedDescriptorCount == 7 && support.routes[2].memoryPlaneCount == 2,
            "route properties and physical plane count are retained independently");
    auto wrongModifier = source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
    ++wrongModifier.image.extensions.drmModifier;
    require(!admitSource(support, wrongModifier), "unqueried modifier cannot borrow optimal or another DRM route");
    auto guessed = source(VK_IMAGE_TILING_OPTIMAL);
    guessed.image.extensions = source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT).image.extensions;
    require(!admitSource(support, guessed), "explicit DRM layout cannot masquerade as optimal tiling");
    Fake noOptimal; noOptimal.features.optimalTilingFeatures = 0;
    const auto onlyOther = query(noOptimal);
    require(onlyOther.complete && !admitSource(onlyOther, source(VK_IMAGE_TILING_OPTIMAL)) &&
            admitSource(onlyOther, source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)), "source cannot borrow support from a different tiling");
}

void boundedModifierListsAndUnsupportedRoutes() {
    Fake none; none.firstCount = none.secondCount = 0;
    const auto basic = query(none);
    require(basic.complete && basic.routeCount == 2 && basic.poolDescriptorCount == 5 && none.formatCalls == 2 &&
            !admitSource(basic, source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)), "zero advertised modifiers does not invent historical QCOM support");
    Fake full; full.firstCount = full.secondCount = MaximumModifiers;
    for (std::size_t i = 2; i < RouteCapacity; ++i) full.descriptors[i] = i - 1;
    const auto all = query(full);
    require(all.complete && all.routeCount == RouteCapacity && all.poolDescriptorCount == MaximumDescriptorCount &&
            full.imageCalls == RouteCapacity, "finite16-modifier table and maximum pool requirement are retained without heap growth");
    Fake oversized; oversized.firstCount = MaximumModifiers + 1;
    require(!query(oversized).complete && oversized.formatCalls == 1 && !oversized.imageCalls,
            "oversized advertised list refuses before array population or image queries");
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        Fake invalid;
        if (mutation == 0) invalid.secondCount = 2;
        if (mutation == 1) invalid.secondCount = 0;
        if (mutation == 2) { invalid.firstCount = invalid.secondCount = 2; invalid.modifiers[1] = invalid.modifiers[0]; }
        if (mutation == 3) invalid.corruptList = true;
        if (mutation == 4) invalid.changedFeatures = true;
        if (mutation == 5) invalid.unreadableModifiers = true;
        const auto refused = query(invalid);
        require(!refused.complete && !refused.routeCount && !refused.poolDescriptorCount && !invalid.imageCalls,
                "growth/shrink/duplicate/corrupt/unstable/unwritten modifier result refuses all sampling admission");
    }
    for (auto planes : {0u, MaximumMemoryPlanes + 1}) {
        Fake invalid; invalid.modifiers[0].drmFormatModifierPlaneCount = planes;
        require(!query(invalid).complete, "invalid DRM memory-plane count refuses before resource assumptions");
    }
    Fake unsupported; unsupported.results[0] = VK_ERROR_FORMAT_NOT_SUPPORTED;
    const auto remaining = query(unsupported);
    require(remaining.complete && remaining.routeCount == 2 && !admitSource(remaining, source(VK_IMAGE_TILING_OPTIMAL)),
            "explicitly unsupported image route is skipped without fabricating source support");
    Fake unavailable; unavailable.features = {}; unavailable.modifiers[0].drmFormatModifierTilingFeatures = 0;
    const auto empty = query(unavailable);
    require(empty.complete && !empty.routeCount && !empty.poolDescriptorCount && !unavailable.imageCalls,
            "format with no required features has no eligible source and issues no image query");
}

void featureAndQueryFailures() {
    for (auto feature : {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
                         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
                         VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT,
                         VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT}) {
        Fake fake; fake.features.optimalTilingFeatures &= ~feature;
        const auto support = query(fake);
        require(support.complete && support.routeCount == 2 && !fake.calls[0] &&
                !admitSource(support, source(VK_IMAGE_TILING_OPTIMAL)), "every required route feature is independently mandatory");
    }
    Fake driverError; driverError.results[1] = VK_ERROR_OUT_OF_HOST_MEMORY;
    const auto failed = query(driverError);
    require(!failed.complete && !failed.routeCount && !failed.poolDescriptorCount,
            "unexpected query failure discards partially collected support instead of guessing pool size");
    for (auto count : {0u, MaximumDescriptorCount + 1}) {
        Fake fake; fake.descriptors[2] = count;
        require(!query(fake).complete, "zero or out-of-budget YCbCr descriptor requirement refuses sampling");
    }
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        Fake fake; auto& p = fake.properties[0];
        if (mutation == 0) p.maxExtent.width = 1;
        if (mutation == 1) p.maxExtent.height = 1;
        if (mutation == 2) p.maxExtent.depth = 0;
        if (mutation == 3) p.maxMipLevels = 0;
        if (mutation == 4) p.maxArrayLayers = 0;
        if (mutation == 5) p.sampleCounts = VK_SAMPLE_COUNT_4_BIT;
        require(!query(fake).complete, "malformed extent/mip/layer/sample properties cannot create a route");
    }
    require(!querySupport(VK_NULL_HANDLE, formatProperties, imageProperties).complete &&
            !querySupport(physical, nullptr, imageProperties).complete &&
            !querySupport(physical, formatProperties, nullptr).complete,
            "missing physical device or original PFNs refuse without linked Vulkan calls");
}

void sourceMismatchGuards() {
    Fake fake; const auto support = query(fake);
    for (unsigned mutation = 0; mutation < 21; ++mutation) {
        auto binding = source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
        auto& im = binding.image;
        if (mutation == 0) binding.complete = false;
        if (mutation == 1) im.complete = false;
        if (mutation == 2) im.identity.generation = 0;
        if (mutation == 3) im.format = VK_FORMAT_R8G8B8A8_UNORM;
        if (mutation == 4) im.type = VK_IMAGE_TYPE_3D;
        if (mutation == 5) im.flags = VK_IMAGE_CREATE_PROTECTED_BIT;
        if (mutation == 6) im.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (mutation == 7) im.sharingMode = VK_SHARING_MODE_CONCURRENT;
        if (mutation == 8) im.samples = VK_SAMPLE_COUNT_4_BIT;
        if (mutation == 9) im.mipLevels = 2;
        if (mutation == 10) im.arrayLayers = 2;
        if (mutation == 11) im.extent.width = 4098;
        if (mutation == 12) im.extent.height = 8194;
        if (mutation == 13) im.extent.depth = 2;
        if (mutation == 14) im.extent.width = 1153;
        if (mutation == 15) im.extensions.hasDrmList = true;
        if (mutation == 16) im.extensions.hasDrmExplicit = false;
        if (mutation == 17) im.extensions.planeCount = 3;
        if (mutation == 18) im.extensions.chain.complete = false;
        if (mutation == 19) im.tiling = VK_IMAGE_TILING_MAX_ENUM;
        if (mutation == 20) im.extent.width = 0;
        require(!admitSource(support, binding), "source metadata mismatch never borrows another format route");
    }
    auto damaged = support; damaged.routes[2].formatFeatures = 0;
    require(!admitSource(damaged, source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)), "source route features are checked again without issuing a query");
    damaged = support; damaged.poolDescriptorCount = 6;
    require(!admitSource(damaged, source(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)), "source cannot exceed actually prepared pool capacity");
    damaged = support; damaged.routeCount = RouteCapacity + 1;
    require(!admitSource(damaged, source(VK_IMAGE_TILING_OPTIMAL)), "damaged route count is rejected before table traversal");
    damaged = support; damaged.complete = false;
    require(!admitSource(damaged, source(VK_IMAGE_TILING_OPTIMAL)), "partial cold support cannot admit a source");
}
} // namespace

int main() {
    normalQueriesAndExactSource(); boundedModifierListsAndUnsupportedRoutes(); featureAndQueryFailures(); sourceMismatchGuards();
    std::cout << "pixel sampler format: " << checks << " checks passed; no live device or source mapping claimed\n";
}
