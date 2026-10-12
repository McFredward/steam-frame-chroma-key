#include "import_vulkan_forwarding.h"

#include <atomic>
#include <cerrno>

namespace frame_import_vulkan {
namespace {
std::atomic<unsigned> registration{0};
std::atomic<bool> lost{false};
Callbacks callbacks{};
thread_local bool notifying __attribute__((tls_model("initial-exec"))) = false;
static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);

template<class T> T argument(std::uint64_t value) noexcept {
    return (T)(std::uintptr_t)value;
}
template<class T> std::uint64_t number(T value) noexcept { return (std::uint64_t)(std::uintptr_t)value; }
std::uint64_t notify(Notification event) noexcept {
    if (notifying) { lost.store(true); return 0; }
    const auto state = registration.load(std::memory_order_acquire);
    if (state != 2) { if (state == 1) lost.store(true); return 0; }
    const auto saved = errno;
    notifying = true;
    const auto tag = callbacks.notify(event, callbacks.context);
    notifying = false;
    errno = saved;
    return tag;
}
Notification before(Operation op, const frame_import_native::Entry& entry,
                    const Providers& provider) noexcept {
    Notification event{};
    event.operation = op; event.phase = Phase::Before; event.entry = entry;
    event.device = provider.device_identity; event.preserved_errno = errno;
    return event;
}
void after(Notification event, VkResult result, int provider_errno) noexcept {
    event.phase = Phase::After; event.result = result; event.preserved_errno = provider_errno;
    errno = provider_errno;
    (void)notify(event);
}
void publish(const Providers& providers, const Publication& event) noexcept {
    if (providers.publish) providers.publish(event);
}
std::uint64_t bits(VkResult result) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(result));
}
}

bool register_callbacks_before_threads(Callbacks value, bool prepared) noexcept {
    if (!prepared || !value.notify || !value.context) return false;
    unsigned inactive = 0;
    if (!registration.compare_exchange_strong(inactive, 1)) return false;
    callbacks = value;
    registration.store(2, std::memory_order_release);
    return true;
}
bool callbacks_registered() noexcept { return registration.load(std::memory_order_acquire) == 2; }
bool notification_loss() noexcept { return lost.load(); }

std::uint64_t image(const frame_import_native::Entry& entry, const Providers& providers) {
    const auto* info = argument<const VkImageCreateInfo*>(entry.x[1]);
    auto* output = argument<VkImage*>(entry.x[3]);
    auto event = before(Operation::Image, entry, providers);
    event.format = info->format; event.extent = info->extent; event.layers = info->arrayLayers;
    event.invocation_tag = notify(event);
    const auto result = providers.image(argument<VkDevice>(entry.x[0]), info,
        argument<const VkAllocationCallbacks*>(entry.x[2]), output);
    const auto saved_errno = errno;
    if (result == VK_SUCCESS) {
        event.image = number(*output);
        Publication publication{};
        publication.operation = Operation::Image; publication.device = providers.device_identity;
        publication.invocation_tag = event.invocation_tag;
        publication.image = *output; publication.image_info = info;
        publish(providers, publication);
    }
    after(event, result, saved_errno);
    errno = saved_errno;
    return bits(result);
}
std::uint64_t bind(const frame_import_native::Entry& entry, const Providers& providers) {
    auto event = before(Operation::Bind, entry, providers);
    event.image = entry.x[1]; event.memory = entry.x[2]; event.memory_offset = entry.x[3];
    event.invocation_tag = notify(event);
    const auto result = providers.bind(argument<VkDevice>(entry.x[0]), argument<VkImage>(entry.x[1]),
        argument<VkDeviceMemory>(entry.x[2]), entry.x[3]);
    const auto saved_errno = errno;
    if (result == VK_SUCCESS) {
        Publication publication{};
        publication.operation = Operation::Bind; publication.device = providers.device_identity;
        publication.invocation_tag = event.invocation_tag;
        publication.image = argument<VkImage>(entry.x[1]);
        publication.memory = argument<VkDeviceMemory>(entry.x[2]); publication.offset = entry.x[3];
        publish(providers, publication);
    }
    after(event, result, saved_errno);
    errno = saved_errno;
    return bits(result);
}
std::uint64_t view(const frame_import_native::Entry& entry, const Providers& providers) {
    const auto* info = argument<const VkImageViewCreateInfo*>(entry.x[1]);
    auto* output = argument<VkImageView*>(entry.x[3]);
    auto event = before(Operation::View, entry, providers);
    event.image = number(info->image); event.format = info->format;
    event.invocation_tag = notify(event);
    const auto result = providers.view(argument<VkDevice>(entry.x[0]), info,
        argument<const VkAllocationCallbacks*>(entry.x[2]), output);
    const auto saved_errno = errno;
    if (result == VK_SUCCESS) {
        event.view = number(*output);
        Publication publication{};
        publication.operation = Operation::View; publication.device = providers.device_identity;
        publication.invocation_tag = event.invocation_tag; publication.image = info->image;
        publication.view = *output; publication.view_info = info;
        publish(providers, publication);
    }
    after(event, result, saved_errno);
    errno = saved_errno;
    return bits(result);
}
} // namespace frame_import_vulkan
