#pragma once

// Cold, public format queries for an optional owned sampler diagnostic. A
// supported format is not an enabled device feature or a source lifetime proof.
// Admission never issues Vulkan calls and never treats DRM memory pitches as
// logical buffer-copy strides. Unknown routes remain unavailable.
#include "passive_binding_trace.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_pixel_sampler_format {

constexpr VkFormat NativeP010 = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
constexpr std::uint32_t MaximumModifiers = 16;
constexpr std::uint32_t MaximumDescriptorCount = 16;
constexpr std::uint32_t MaximumMemoryPlanes = 4;
constexpr std::size_t RouteCapacity = 2 + MaximumModifiers;
constexpr VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
constexpr VkFormatFeatureFlags RequiredFeatures =
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT |
    VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

struct Route {
    VkImageTiling tiling = VK_IMAGE_TILING_MAX_ENUM;
    std::uint64_t modifier = 0;
    VkFormatFeatureFlags formatFeatures = 0;
    VkExtent3D maxExtent{};
    std::uint32_t maxMipLevels = 0, maxArrayLayers = 0;
    VkSampleCountFlags sampleCounts = 0;
    std::uint32_t combinedDescriptorCount = 0, memoryPlaneCount = 0;
};

struct Support {
    bool complete = false;
    std::uint32_t routeCount = 0, poolDescriptorCount = 0;
    std::array<Route, RouteCapacity> routes{};
};

inline bool hasRequiredFeatures(VkFormatFeatureFlags flags) noexcept {
    return (flags & RequiredFeatures) == RequiredFeatures;
}

inline Support querySupport(VkPhysicalDevice physical,
                            PFN_vkGetPhysicalDeviceFormatProperties2 formatProperties,
                            PFN_vkGetPhysicalDeviceImageFormatProperties2 imageProperties) noexcept {
    Support out;
    if (!physical || !formatProperties || !imageProperties) return out;

    VkDrmFormatModifierPropertiesListEXT list{};
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    VkFormatProperties2 first{};
    first.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    first.pNext = &list;
    formatProperties(physical, NativeP010, &first);
    if (first.sType != VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 || first.pNext != &list ||
        list.sType != VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT ||
        list.pNext || list.pDrmFormatModifierProperties || list.drmFormatModifierCount > MaximumModifiers) return {};
    const auto count = list.drmFormatModifierCount;
    std::array<VkDrmFormatModifierPropertiesEXT, MaximumModifiers> modifiers{};
    list.pDrmFormatModifierProperties = modifiers.data();
    VkFormatProperties2 second{};
    second.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    second.pNext = &list;
    formatProperties(physical, NativeP010, &second);
    if (second.sType != VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 || second.pNext != &list ||
        list.sType != VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT || list.pNext ||
        list.pDrmFormatModifierProperties != modifiers.data() || list.drmFormatModifierCount != count ||
        first.formatProperties.linearTilingFeatures != second.formatProperties.linearTilingFeatures ||
        first.formatProperties.optimalTilingFeatures != second.formatProperties.optimalTilingFeatures ||
        first.formatProperties.bufferFeatures != second.formatProperties.bufferFeatures) return {};
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!modifiers[i].drmFormatModifierPlaneCount ||
            modifiers[i].drmFormatModifierPlaneCount > MaximumMemoryPlanes) return {};
        for (std::uint32_t previous = 0; previous < i; ++previous)
            if (modifiers[previous].drmFormatModifier == modifiers[i].drmFormatModifier) return {};
    }

    const auto query = [&](VkImageTiling tiling, std::uint64_t modifier,
                           VkFormatFeatureFlags features, std::uint32_t planes) noexcept {
        if (!hasRequiredFeatures(features)) return true; // This route is known unsupported.
        VkPhysicalDeviceImageDrmFormatModifierInfoEXT drm{};
        drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
        drm.drmFormatModifier = modifier;
        drm.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkPhysicalDeviceImageFormatInfo2 request{};
        request.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        request.format = NativeP010;
        request.type = VK_IMAGE_TYPE_2D;
        request.tiling = tiling;
        request.usage = Usage;
        request.pNext = tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT ? &drm : nullptr;
        VkSamplerYcbcrConversionImageFormatProperties conversion{};
        conversion.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES;
        VkImageFormatProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        properties.pNext = &conversion;
        const auto result = imageProperties(physical, &request, &properties);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) return true;
        if (result != VK_SUCCESS || properties.sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 ||
            properties.pNext != &conversion || conversion.pNext ||
            conversion.sType != VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES ||
            !conversion.combinedImageSamplerDescriptorCount ||
            conversion.combinedImageSamplerDescriptorCount > MaximumDescriptorCount) return false;
        const auto& p = properties.imageFormatProperties;
        if (p.maxExtent.width < 2 || p.maxExtent.height < 2 || !p.maxExtent.depth ||
            !p.maxMipLevels || !p.maxArrayLayers || !(p.sampleCounts & VK_SAMPLE_COUNT_1_BIT) ||
            out.routeCount == out.routes.size()) return false;
        auto& route = out.routes[out.routeCount++];
        route.tiling = tiling; route.modifier = modifier; route.formatFeatures = features;
        route.maxExtent = p.maxExtent; route.maxMipLevels = p.maxMipLevels;
        route.maxArrayLayers = p.maxArrayLayers; route.sampleCounts = p.sampleCounts;
        route.combinedDescriptorCount = conversion.combinedImageSamplerDescriptorCount;
        route.memoryPlaneCount = planes;
        if (route.combinedDescriptorCount > out.poolDescriptorCount)
            out.poolDescriptorCount = route.combinedDescriptorCount;
        return true;
    };
    if (!query(VK_IMAGE_TILING_OPTIMAL, 0, first.formatProperties.optimalTilingFeatures, 0) ||
        !query(VK_IMAGE_TILING_LINEAR, 0, first.formatProperties.linearTilingFeatures, 0)) return {};
    for (std::uint32_t i = 0; i < count; ++i)
        if (!query(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, modifiers[i].drmFormatModifier,
                   modifiers[i].drmFormatModifierTilingFeatures, modifiers[i].drmFormatModifierPlaneCount)) return {};
    out.complete = true;
    return out;
}

inline bool admitSource(const Support& support, const frame_bindings::Binding& binding) noexcept {
    const auto& image = binding.image;
    const auto& ext = image.extensions;
    if (!support.complete || !support.routeCount || support.routeCount > support.routes.size() ||
        !support.poolDescriptorCount || support.poolDescriptorCount > MaximumDescriptorCount ||
        !binding.complete || !image.complete || !image.identity.handle || !image.identity.generation ||
        image.format != NativeP010 || image.type != VK_IMAGE_TYPE_2D || image.flags ||
        image.usage != Usage || image.sharingMode != VK_SHARING_MODE_EXCLUSIVE ||
        (image.tiling != VK_IMAGE_TILING_OPTIMAL && image.tiling != VK_IMAGE_TILING_LINEAR &&
         image.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) ||
        image.samples != VK_SAMPLE_COUNT_1_BIT || image.mipLevels != 1 || image.arrayLayers != 1 ||
        !image.extent.width || !image.extent.height || image.extent.depth != 1 ||
        (image.extent.width & 1u) || (image.extent.height & 1u) || !ext.chain.complete || ext.hasDrmList) return false;
    const bool drm = image.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    if ((drm && (!ext.hasDrmExplicit || !ext.planeCount || ext.planeCount > MaximumMemoryPlanes)) ||
        (!drm && ext.hasDrmExplicit)) return false;
    for (std::uint32_t i = 0; i < support.routeCount; ++i) {
        const auto& route = support.routes[i];
        if (route.tiling != image.tiling || !hasRequiredFeatures(route.formatFeatures) ||
            !route.combinedDescriptorCount || route.combinedDescriptorCount > support.poolDescriptorCount ||
            !route.maxMipLevels || !route.maxArrayLayers || !(route.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) continue;
        if (drm && (route.modifier != ext.drmModifier || route.memoryPlaneCount != ext.planeCount)) continue;
        if (image.extent.width <= route.maxExtent.width && image.extent.height <= route.maxExtent.height &&
            image.extent.depth <= route.maxExtent.depth) return true;
    }
    return false;
}

} // namespace frame_pixel_sampler_format
