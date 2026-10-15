#pragma once

// CPU-only admission for an optional owned sampling diagnostic. These facts
// come from the ACTUAL vkCreateDevice inputs and observed queue-family flags,
// never from physical-device support alone. Refusal disables only the extra
// diagnostic; it does not alter native device creation or native rendering.
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_pixel_sampler {

constexpr std::uint32_t MaximumDeviceExtensions = 128;
constexpr std::size_t MaximumDeviceChain = 32;
constexpr std::uint32_t MaximumReviewedExtensions = 1024;
constexpr std::uint32_t MaximumReviewedChainTypes = 1024;

struct ReviewedRoutes {
    // The owner supplies this only when all relevant core/API-version entry
    // points are intercepted. Querying a function pointer is not observation.
    bool coreComputeRoutesObserved = false;
    // Every enabled extension must be explicitly reviewed against the actual
    // wrappers. Unknown extension routes cannot silently inherit core coverage.
    const char* const* extensionNames = nullptr;
    std::uint32_t extensionCount = 0;
    // Features2, Vulkan11Features and SamplerYcbcrConversionFeatures are parsed
    // here. Other pNext types require explicit owner review as well.
    const VkStructureType* additionalChainTypes = nullptr;
    std::uint32_t additionalChainTypeCount = 0;
};

struct DeviceEligibility {
    bool complete = false;
    bool samplerYcbcrConversionEnabled = false;
    bool computeRoutesObserved = false;
    bool eligible() const noexcept {
        return complete && samplerYcbcrConversionEnabled && computeRoutesObserved;
    }
};

inline bool exactExtensionName(const char* left, const char* right) noexcept {
    if (!left || !right) return false;
    for (std::size_t i = 0; i < VK_MAX_EXTENSION_NAME_SIZE; ++i) {
        if (left[i] != right[i]) return false;
        if (!left[i]) return i != 0;
    }
    return false;
}

inline DeviceEligibility inspectDeviceCreate(const VkDeviceCreateInfo& info,
                                             const ReviewedRoutes& routes) noexcept {
    DeviceEligibility out;
    if (info.sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO || info.flags ||
        info.enabledExtensionCount > MaximumDeviceExtensions ||
        (info.enabledExtensionCount && !info.ppEnabledExtensionNames) ||
        routes.extensionCount > MaximumReviewedExtensions ||
        (routes.extensionCount && !routes.extensionNames) ||
        routes.additionalChainTypeCount > MaximumReviewedChainTypes ||
        (routes.additionalChainTypeCount && !routes.additionalChainTypes)) return out;
    for (std::uint32_t i = 0; i < info.enabledExtensionCount; ++i) {
        for (std::uint32_t previous = 0; previous < i; ++previous)
            if (exactExtensionName(info.ppEnabledExtensionNames[i], info.ppEnabledExtensionNames[previous])) return out;
        bool reviewed = false;
        for (std::uint32_t j = 0; j < routes.extensionCount; ++j)
            if (exactExtensionName(info.ppEnabledExtensionNames[i], routes.extensionNames[j])) {
                reviewed = true;
                break;
            }
        if (!reviewed) return out;
    }

    std::array<const VkBaseInStructure*, MaximumDeviceChain> visited{};
    std::size_t count = 0;
    bool seenFeatures2 = false, seenYcbcr = false;
    auto* node = static_cast<const VkBaseInStructure*>(info.pNext);
    for (; node; node = node->pNext) {
        if (count == visited.size()) return out;
        for (std::size_t i = 0; i < count; ++i)
            if (visited[i] == node || visited[i]->sType == node->sType) return out;
        visited[count++] = node;
        switch (node->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            if (seenFeatures2 || info.pEnabledFeatures) return out;
            seenFeatures2 = true;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            if (seenYcbcr) return out;
            seenYcbcr = true;
            const auto* features = reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(node);
            if (features->samplerYcbcrConversion != VK_TRUE && features->samplerYcbcrConversion != VK_FALSE) return out;
            out.samplerYcbcrConversionEnabled = features->samplerYcbcrConversion == VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
            if (seenYcbcr) return out;
            seenYcbcr = true;
            const auto* features = reinterpret_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(node);
            if (features->samplerYcbcrConversion != VK_TRUE && features->samplerYcbcrConversion != VK_FALSE) return out;
            out.samplerYcbcrConversionEnabled = features->samplerYcbcrConversion == VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO:
            // This single-device experiment never infers device-group ownership.
            return out;
        default: {
            bool reviewed = false;
            for (std::uint32_t i = 0; i < routes.additionalChainTypeCount; ++i)
                if (routes.additionalChainTypes[i] == node->sType) {
                    reviewed = true;
                    break;
                }
            if (!reviewed) return out;
            break;
        }
        }
    }
    out.complete = true;
    out.computeRoutesObserved = routes.coreComputeRoutesObserved;
    return out;
}

inline bool queueSupportsCompute(VkQueueFlags flags) noexcept {
    return (flags & VK_QUEUE_COMPUTE_BIT) != 0;
}

} // namespace frame_pixel_sampler
