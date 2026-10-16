#pragma once

// CPU-only admission for an optional owned sampling diagnostic. These facts
// come from the ACTUAL vkCreateDevice inputs and observed queue-family flags,
// never from physical-device support alone. Refusal disables only the extra
// diagnostic; it does not alter native device creation or native rendering.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace frame_pixel_sampler {

constexpr std::uint32_t MaximumDeviceExtensions = 128;
constexpr std::size_t MaximumDeviceChain = 32;
constexpr std::uint32_t MaximumReviewedExtensions = 1024;
constexpr std::uint32_t MaximumReviewedChainTypes = 1024;

struct ReviewedRoutes {
    // The owner supplies this only when all relevant core/API-version entry
    // points are intercepted. Querying a function pointer is not observation.
    bool coreComputeRoutesObserved = false;
    // Every query-begin route must also reach the owner's conservative exact-
    // recording history guard before an inserted dispatch can be admitted.
    bool queryRoutesObserved = false;
    // Every enabled extension must be explicitly reviewed against the actual
    // wrappers. Unknown extension routes cannot silently inherit core coverage.
    const char* const* extensionNames = nullptr;
    std::uint32_t extensionCount = 0;
    // Features2, Vulkan11Features and SamplerYcbcrConversionFeatures are parsed
    // here. Other pNext types require explicit owner review as well.
    const VkStructureType* additionalChainTypes = nullptr;
    std::uint32_t additionalChainTypeCount = 0;
};

enum class FeatureSource : std::uint8_t { None, Legacy, Features2 };
enum class RefusalReason : std::uint8_t {
    None, DeviceCreateType, DeviceCreateFlags, DeviceExtensionLimit,
    DeviceExtensionArray, ReviewedExtensionLimit, ReviewedExtensionArray,
    ReviewedChainLimit, ReviewedChainArray, PipelineStatisticsValue,
    DuplicateExtension, UnreviewedExtension, ChainLimit, ChainCycle,
    DuplicateChainType, LoaderFunction, DuplicateLoaderFunction,
    Features2Conflict, DuplicateYcbcrFeature, YcbcrFeatureValue,
    DeviceGroup, UnreviewedChainType,
};
constexpr std::uint32_t NoOffendingInput = UINT32_MAX;

inline const char* refusalName(RefusalReason reason) noexcept {
    switch (reason) {
    case RefusalReason::None: return "none";
    case RefusalReason::DeviceCreateType: return "device_create_type";
    case RefusalReason::DeviceCreateFlags: return "device_create_flags";
    case RefusalReason::DeviceExtensionLimit: return "device_extension_limit";
    case RefusalReason::DeviceExtensionArray: return "device_extension_array";
    case RefusalReason::ReviewedExtensionLimit: return "reviewed_extension_limit";
    case RefusalReason::ReviewedExtensionArray: return "reviewed_extension_array";
    case RefusalReason::ReviewedChainLimit: return "reviewed_chain_limit";
    case RefusalReason::ReviewedChainArray: return "reviewed_chain_array";
    case RefusalReason::PipelineStatisticsValue: return "pipeline_statistics_value";
    case RefusalReason::DuplicateExtension: return "duplicate_extension";
    case RefusalReason::UnreviewedExtension: return "unreviewed_extension";
    case RefusalReason::ChainLimit: return "chain_limit";
    case RefusalReason::ChainCycle: return "chain_cycle";
    case RefusalReason::DuplicateChainType: return "duplicate_chain_type";
    case RefusalReason::LoaderFunction: return "loader_function";
    case RefusalReason::DuplicateLoaderFunction: return "duplicate_loader_function";
    case RefusalReason::Features2Conflict: return "features2_conflict";
    case RefusalReason::DuplicateYcbcrFeature: return "duplicate_ycbcr_feature";
    case RefusalReason::YcbcrFeatureValue: return "ycbcr_feature_value";
    case RefusalReason::DeviceGroup: return "device_group";
    case RefusalReason::UnreviewedChainType: return "unreviewed_chain_type";
    }
    return "unknown_refusal";
}

struct DeviceEligibility {
    bool complete = false;
    bool samplerYcbcrConversionObserved = false;
    bool samplerYcbcrConversionEnabled = false;
    bool computeRoutesObserved = false;
    bool queryRoutesObserved = false;
    RefusalReason refusal = RefusalReason::None;
    std::uint32_t offendingIndex = NoOffendingInput;
    std::uint32_t offendingStructureType = NoOffendingInput;
    std::uint32_t offendingLoaderFunction = NoOffendingInput;
    bool pipelineStatisticsObserved = false;
    bool pipelineStatisticsEnabled = false;
    FeatureSource pipelineStatisticsSource = FeatureSource::None;
    VkBool32 pipelineStatisticsValue = VK_FALSE;
    bool eligible() const noexcept {
        return complete && samplerYcbcrConversionEnabled && computeRoutesObserved && queryRoutesObserved;
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
    const auto refuse = [&](RefusalReason reason, std::uint32_t index = NoOffendingInput,
                            std::uint32_t type = NoOffendingInput,
                            std::uint32_t function = NoOffendingInput) noexcept {
        out.refusal = reason; out.offendingIndex = index;
        out.offendingStructureType = type; out.offendingLoaderFunction = function;
        return out;
    };
    const auto statistics = [&](VkBool32 value, FeatureSource source) noexcept {
        out.pipelineStatisticsObserved = true; out.pipelineStatisticsSource = source;
        out.pipelineStatisticsValue = value;
        out.pipelineStatisticsEnabled = value == VK_TRUE;
        return value == VK_FALSE || value == VK_TRUE;
    };
    if (info.sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)
        return refuse(RefusalReason::DeviceCreateType, NoOffendingInput, static_cast<std::uint32_t>(info.sType));
    if (info.flags) return refuse(RefusalReason::DeviceCreateFlags);
    if (info.enabledExtensionCount > MaximumDeviceExtensions) return refuse(RefusalReason::DeviceExtensionLimit);
    if (info.enabledExtensionCount && !info.ppEnabledExtensionNames) return refuse(RefusalReason::DeviceExtensionArray);
    if (routes.extensionCount > MaximumReviewedExtensions) return refuse(RefusalReason::ReviewedExtensionLimit);
    if (routes.extensionCount && !routes.extensionNames) return refuse(RefusalReason::ReviewedExtensionArray);
    if (routes.additionalChainTypeCount > MaximumReviewedChainTypes) return refuse(RefusalReason::ReviewedChainLimit);
    if (routes.additionalChainTypeCount && !routes.additionalChainTypes) return refuse(RefusalReason::ReviewedChainArray);
    // Enabled support alone does not mean a query is active. Actual query-begin
    // history is the owner's responsibility and is independently mandatory.
    if (info.pEnabledFeatures && !statistics(info.pEnabledFeatures->pipelineStatisticsQuery, FeatureSource::Legacy))
        return refuse(RefusalReason::PipelineStatisticsValue);
    for (std::uint32_t i = 0; i < info.enabledExtensionCount; ++i) {
        for (std::uint32_t previous = 0; previous < i; ++previous)
            if (exactExtensionName(info.ppEnabledExtensionNames[i], info.ppEnabledExtensionNames[previous]))
                return refuse(RefusalReason::DuplicateExtension, i);
        bool reviewed = false;
        for (std::uint32_t j = 0; j < routes.extensionCount; ++j)
            if (exactExtensionName(info.ppEnabledExtensionNames[i], routes.extensionNames[j])) {
                reviewed = true;
                break;
            }
        if (!reviewed) return refuse(RefusalReason::UnreviewedExtension, i);
    }

    std::array<const VkBaseInStructure*, MaximumDeviceChain> visited{};
    std::size_t count = 0;
    bool seenYcbcr = false;
    std::uint32_t loaderFunctions = 0;
    auto* node = static_cast<const VkBaseInStructure*>(info.pNext);
    for (; node; node = node->pNext) {
        if (count == visited.size()) return refuse(RefusalReason::ChainLimit, static_cast<std::uint32_t>(count));
        const auto nativeIndex = static_cast<std::uint32_t>(count);
        const auto nativeType = static_cast<std::uint32_t>(node->sType);
        const bool loader = node->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
        for (std::size_t i = 0; i < count; ++i) {
            if (visited[i] == node) return refuse(RefusalReason::ChainCycle, nativeIndex, nativeType);
            if (!loader && visited[i]->sType == node->sType)
                return refuse(RefusalReason::DuplicateChainType, nativeIndex, nativeType);
        }
        visited[count++] = node;
        if (loader) {
            // The public loader ABI uses one sType for several distinct data
            // records. They are not duplicate feature enablements. Retain the
            // input chain and never dereference/call its union callbacks.
            const auto* data = reinterpret_cast<const VkLayerDeviceCreateInfo*>(node);
            std::underlying_type_t<VkLayerFunction> function = 0;
            // Unknown future wire values must be rejected without first
            // evaluating an out-of-range C++ enum value.
            std::memcpy(&function, &data->function, sizeof(function));
            std::uint32_t bit = 0;
            switch (function) {
            case VK_LAYER_LINK_INFO: bit = 1u << 0; break;
            case VK_LOADER_DATA_CALLBACK: bit = 1u << 1; break;
            case VK_LOADER_LAYER_CREATE_DEVICE_CALLBACK: bit = 1u << 2; break;
            case VK_LOADER_FEATURES: bit = 1u << 3; break;
            default: return refuse(RefusalReason::LoaderFunction, nativeIndex, nativeType, static_cast<std::uint32_t>(function));
            }
            if (loaderFunctions & bit)
                return refuse(RefusalReason::DuplicateLoaderFunction, nativeIndex, nativeType, static_cast<std::uint32_t>(function));
            loaderFunctions |= bit;
        }
        switch (node->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            if (info.pEnabledFeatures) return refuse(RefusalReason::Features2Conflict, nativeIndex, nativeType);
            if (!statistics(reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features.pipelineStatisticsQuery,
                            FeatureSource::Features2))
                return refuse(RefusalReason::PipelineStatisticsValue, nativeIndex, nativeType);
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            if (seenYcbcr) return refuse(RefusalReason::DuplicateYcbcrFeature, nativeIndex, nativeType);
            seenYcbcr = true;
            const auto* features = reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(node);
            if (features->samplerYcbcrConversion != VK_TRUE && features->samplerYcbcrConversion != VK_FALSE)
                return refuse(RefusalReason::YcbcrFeatureValue, nativeIndex, nativeType);
            out.samplerYcbcrConversionObserved = true;
            out.samplerYcbcrConversionEnabled = features->samplerYcbcrConversion == VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
            if (seenYcbcr) return refuse(RefusalReason::DuplicateYcbcrFeature, nativeIndex, nativeType);
            seenYcbcr = true;
            const auto* features = reinterpret_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(node);
            if (features->samplerYcbcrConversion != VK_TRUE && features->samplerYcbcrConversion != VK_FALSE)
                return refuse(RefusalReason::YcbcrFeatureValue, nativeIndex, nativeType);
            out.samplerYcbcrConversionObserved = true;
            out.samplerYcbcrConversionEnabled = features->samplerYcbcrConversion == VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO:
            // This single-device experiment never infers device-group ownership.
            return refuse(RefusalReason::DeviceGroup, nativeIndex, nativeType);
        default: {
            bool reviewed = false;
            for (std::uint32_t i = 0; i < routes.additionalChainTypeCount; ++i)
                if (routes.additionalChainTypes[i] == node->sType) {
                    reviewed = true;
                    break;
                }
            if (!reviewed) return refuse(RefusalReason::UnreviewedChainType, nativeIndex, nativeType);
            break;
        }
        }
    }
    out.complete = true;
    out.computeRoutesObserved = routes.coreComputeRoutesObserved;
    out.queryRoutesObserved = routes.queryRoutesObserved;
    return out;
}

inline bool queueSupportsCompute(VkQueueFlags flags) noexcept {
    return (flags & VK_QUEUE_COMPUTE_BIT) != 0;
}

} // namespace frame_pixel_sampler
