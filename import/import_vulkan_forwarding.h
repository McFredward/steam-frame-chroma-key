#pragma once

#include "native_import_route.h"
#include <vulkan/vulkan.h>
#include <cstdint>

// The existing layer supplies its already resolved, device-owned providers.
// These functions do not resolve symbols, read native stacks or enable capture.
namespace frame_import_vulkan {
enum class Operation : std::uint8_t { Image, Bind, View };
enum class Phase : std::uint8_t { Before, After };
struct Notification {
    Operation operation{};
    Phase phase{};
    frame_import_native::Entry entry{};
    std::uint64_t device = 0, invocation_tag = 0;
    std::uint64_t image = 0, memory = 0, view = 0, memory_offset = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    std::uint32_t layers = 0;
    VkResult result = VK_NOT_READY;
    int preserved_errno = 0;
};
// A nonzero return at Before is an externally validated invocation label, not a
// resource generation or source frame. After returns are ignored. The callback
// must be bounded, nonblocking, allocation/I/O-free and retain no native pointer.
using Notify = std::uint64_t (*)(Notification, void*) noexcept;
struct Callbacks { Notify notify = nullptr; void* context = nullptr; };
bool register_callbacks_before_threads(Callbacks, bool owned_prepared_context) noexcept;
bool callbacks_registered() noexcept;
bool notification_loss() noexcept;

// Publication is synchronous, before the public call returns. These two public
// API input pointers are borrowed only for the existing journal's bounded copy;
// a publisher must never retain them or read them later on a worker.
struct Publication {
    Operation operation{};
    std::uint64_t device = 0, invocation_tag = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    const VkImageCreateInfo* image_info = nullptr;
    const VkImageViewCreateInfo* view_info = nullptr;
};
using Publish = void (*)(const Publication&) noexcept;
struct Providers {
    std::uint64_t device_identity = 0;
    PFN_vkCreateImage image = nullptr;
    PFN_vkBindImageMemory bind = nullptr;
    PFN_vkCreateImageView view = nullptr;
    Publish publish = nullptr;
};

// Mandatory valid providers come from the layer's same live VkDevice record
// used by its ordinary hooks. Unsupported devices never receive these veneers.
// No fabricated VkResult or generic dlsym fallback substitutes for a provider.
// Not noexcept: a provider's unwind must cross the veneer unchanged.
std::uint64_t image(const frame_import_native::Entry&, const Providers&);
std::uint64_t bind(const frame_import_native::Entry&, const Providers&);
std::uint64_t view(const frame_import_native::Entry&, const Providers&);
} // namespace frame_import_vulkan
