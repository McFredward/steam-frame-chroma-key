#pragma once

// Owned, bounded copies of public Vulkan creation inputs. These records retain
// no caller pointers, FD ownership, mapped contents or cross-process identities.
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace frame_bindings::extensions {

constexpr std::size_t ChainCapacity = 8;
constexpr std::size_t PlaneCapacity = 4;
constexpr std::size_t ModifierCapacity = 8;
enum class ChainError : std::uint8_t { None, Unsupported, Duplicate, Cycle, Bounds };
struct ChainSummary {
    std::array<VkStructureType, ChainCapacity> types{};
    std::uint32_t count = 0;
    ChainError error = ChainError::None;
    std::uint32_t errorFlags = 0;
    bool complete = true;
};
struct PlaneLayout {
    VkDeviceSize offset = 0, rowPitch = 0, arrayPitch = 0, depthPitch = 0;
};
struct ImageExtensions {
    ChainSummary chain{};
    VkExternalMemoryHandleTypeFlags externalHandleTypes = 0;
    std::uint64_t drmModifier = 0;
    std::uint32_t planeCount = 0, modifierCount = 0;
    std::array<PlaneLayout, PlaneCapacity> planes{};
    std::array<std::uint64_t, ModifierCapacity> modifiers{};
    bool hasExternal = false, hasDrmExplicit = false, hasDrmList = false;
};
struct MemoryExtensions {
    ChainSummary chain{};
    VkExternalMemoryHandleTypeFlagBits importType = static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
    VkExternalMemoryHandleTypeFlags exportTypes = 0;
    std::int32_t inputFD = -1;
    std::uint64_t dedicatedImageHandle = 0, dedicatedBufferHandle = 0;
    bool hasImport = false, hasExport = false, hasDedicated = false;
};
struct ViewExtensions {
    ChainSummary chain{};
    std::uint64_t conversionHandle = 0;
    bool hasConversion = false;
};
struct SamplerExtensions {
    ChainSummary chain{};
    std::array<std::uint32_t, 4> customBorderBits{};
    VkFormat customBorderFormat = VK_FORMAT_UNDEFINED;
    std::uint64_t conversionHandle = 0;
    bool hasCustomBorder = false, hasConversion = false;
};
struct ConversionExtensions { ChainSummary chain{}; };

template<class T> inline std::uint64_t scalarHandle(T value) noexcept {
    if constexpr (std::is_pointer_v<T>) return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
    else return static_cast<std::uint64_t>(value);
}
inline void reject(ChainSummary& summary, ChainError error) noexcept {
    summary.complete = false;
    summary.errorFlags |= 1U << static_cast<std::uint32_t>(error);
    if (summary.error == ChainError::None) summary.error = error;
}

template<class Visitor> inline void walk(const void* chain, ChainSummary& summary, Visitor visit) noexcept {
    std::array<const VkBaseInStructure*, ChainCapacity> visited{};
    auto* node = static_cast<const VkBaseInStructure*>(chain);
    while (node) {
        for (std::uint32_t i = 0; i < summary.count; ++i) {
            if (visited[i] == node) { reject(summary, ChainError::Cycle); return; }
        }
        // Do not dereference a ninth node or an oversized nested array.
        if (summary.count == ChainCapacity) { reject(summary, ChainError::Bounds); return; }
        bool duplicate = false;
        for (std::uint32_t i = 0; i < summary.count; ++i) duplicate = duplicate || summary.types[i] == node->sType;
        visited[summary.count] = node;
        summary.types[summary.count++] = node->sType;
        if (duplicate) reject(summary, ChainError::Duplicate);
        else visit(node);
        node = node->pNext;
    }
}

inline ImageExtensions parseImage(const void* chain) noexcept {
    ImageExtensions out;
    walk(chain, out.chain, [&](const VkBaseInStructure* node) noexcept {
        switch (node->sType) {
        case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO: {
            const auto& in = *reinterpret_cast<const VkExternalMemoryImageCreateInfo*>(node);
            out.hasExternal = true; out.externalHandleTypes = in.handleTypes; break;
        }
        case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT: {
            const auto& in = *reinterpret_cast<const VkImageDrmFormatModifierExplicitCreateInfoEXT*>(node);
            out.hasDrmExplicit = true; out.drmModifier = in.drmFormatModifier; out.planeCount = in.drmFormatModifierPlaneCount;
            if (!in.drmFormatModifierPlaneCount || in.drmFormatModifierPlaneCount > PlaneCapacity || !in.pPlaneLayouts) {
                reject(out.chain, ChainError::Bounds); break;
            }
            // VkImageDrmFormatModifierExplicitCreateInfoEXT: size is ignored by
            // Vulkan; memory-plane pitches are not eye rectangles.
            // https://docs.vulkan.org/refpages/latest/refpages/source/VkImageDrmFormatModifierExplicitCreateInfoEXT.html
            for (std::uint32_t i = 0; i < in.drmFormatModifierPlaneCount; ++i) {
                const auto& p = in.pPlaneLayouts[i];
                out.planes[i] = {p.offset, p.rowPitch, p.arrayPitch, p.depthPitch};
            }
            break;
        }
        case VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT: {
            const auto& in = *reinterpret_cast<const VkImageDrmFormatModifierListCreateInfoEXT*>(node);
            out.hasDrmList = true; out.modifierCount = in.drmFormatModifierCount;
            if (!in.drmFormatModifierCount || in.drmFormatModifierCount > ModifierCapacity || !in.pDrmFormatModifiers) {
                reject(out.chain, ChainError::Bounds); break;
            }
            for (std::uint32_t i = 0; i < in.drmFormatModifierCount; ++i) out.modifiers[i] = in.pDrmFormatModifiers[i];
            break;
        }
        default: reject(out.chain, ChainError::Unsupported); break;
        }
    });
    if (out.hasDrmExplicit && out.hasDrmList) reject(out.chain, ChainError::Unsupported);
    return out;
}

inline MemoryExtensions parseMemory(const void* chain) noexcept {
    MemoryExtensions out;
    walk(chain, out.chain, [&](const VkBaseInStructure* node) noexcept {
        switch (node->sType) {
        case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR: {
            const auto& in = *reinterpret_cast<const VkImportMemoryFdInfoKHR*>(node);
            out.hasImport = true; out.importType = in.handleType; out.inputFD = in.fd;
            // Successful imports transfer FD ownership to Vulkan. Copy the
            // input integer only: never inspect, duplicate, close or retain it.
            // https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html
            if (in.fd < 0 || (in.handleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
                              in.handleType != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)) reject(out.chain, ChainError::Unsupported);
            break;
        }
        case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO: {
            const auto& in = *reinterpret_cast<const VkExportMemoryAllocateInfo*>(node);
            out.hasExport = true; out.exportTypes = in.handleTypes; break;
        }
        case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO: {
            const auto& in = *reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(node);
            out.hasDedicated = true; out.dedicatedImageHandle = scalarHandle(in.image); out.dedicatedBufferHandle = scalarHandle(in.buffer);
            if (out.dedicatedImageHandle && out.dedicatedBufferHandle) reject(out.chain, ChainError::Unsupported);
            break;
        }
        default: reject(out.chain, ChainError::Unsupported); break;
        }
    });
    return out;
}
inline ViewExtensions parseView(const void* chain) noexcept {
    ViewExtensions out;
    walk(chain, out.chain, [&](const VkBaseInStructure* node) noexcept {
        if (node->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO) {
            const auto& in = *reinterpret_cast<const VkSamplerYcbcrConversionInfo*>(node);
            out.hasConversion = true; out.conversionHandle = scalarHandle(in.conversion);
            if (!out.conversionHandle) reject(out.chain, ChainError::Unsupported);
        } else reject(out.chain, ChainError::Unsupported);
    });
    return out;
}
inline SamplerExtensions parseSampler(const void* chain) noexcept {
    SamplerExtensions out;
    walk(chain, out.chain, [&](const VkBaseInStructure* node) noexcept {
        switch (node->sType) {
        case VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT: {
            const auto& in = *reinterpret_cast<const VkSamplerCustomBorderColorCreateInfoEXT*>(node);
            out.hasCustomBorder = true; out.customBorderFormat = in.format;
            std::memcpy(out.customBorderBits.data(), &in.customBorderColor, sizeof(in.customBorderColor)); break;
        }
        case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO: {
            const auto& in = *reinterpret_cast<const VkSamplerYcbcrConversionInfo*>(node);
            out.hasConversion = true; out.conversionHandle = scalarHandle(in.conversion);
            if (!out.conversionHandle) reject(out.chain, ChainError::Unsupported);
            break;
        }
        default: reject(out.chain, ChainError::Unsupported); break;
        }
    });
    return out;
}
inline ConversionExtensions parseConversion(const void* chain) noexcept {
    ConversionExtensions out;
    walk(chain, out.chain, [&](const VkBaseInStructure*) noexcept { reject(out.chain, ChainError::Unsupported); });
    return out;
}
static_assert(std::is_trivially_copyable_v<ImageExtensions> && std::is_trivially_copyable_v<MemoryExtensions> &&
              std::is_trivially_copyable_v<ViewExtensions> && std::is_trivially_copyable_v<SamplerExtensions> &&
              std::is_trivially_copyable_v<ConversionExtensions>, "Extension inputs must be owned scalar records");

} // namespace frame_bindings::extensions
