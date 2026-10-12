#include "import/import_vulkan_forwarding.h"
#include "layer/passive_binding_journal.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace frame_import_vulkan;
using frame_import_native::Entry;
unsigned checks = 0, calls = 0, publications = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %u: %s\n", checks, message); std::abort(); }
}
template<class T> T handle(std::uint64_t value) { return (T)(std::uintptr_t)value; }
template<class T> std::uint64_t number(T value) { return (std::uint64_t)(std::uintptr_t)value; }
template<class T> T structure(VkStructureType type) { T value{}; value.sType = type; return value; }
struct Context {
    std::array<Notification, 64> events{};
    unsigned size = 0;
    std::uint64_t tag = 91;
    bool recursive = false;
};
Context context;
const void* expected_info = nullptr;
const void* expected_allocator = nullptr;
const void* expected_output = nullptr;
VkDevice expected_device = handle<VkDevice>(0x1000);
VkResult native_result = VK_SUCCESS;
bool provider_throws = false;
struct NativeUnwind {};
VkResult VKAPI_CALL fakeImage(VkDevice device, const VkImageCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImage* output) {
    ++calls;
    require(device == expected_device && info == expected_info && allocator == expected_allocator &&
            output == expected_output, "image provider arguments changed");
    if (provider_throws) throw NativeUnwind{};
    *output = handle<VkImage>(0x7654); errno = EBUSY;
    return native_result;
}
VkResult VKAPI_CALL fakeBind(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    ++calls;
    require(device == expected_device && number(image) == 0x7654 && number(memory) == 0x9876 &&
            offset == 0x100000010ULL, "bind provider arguments changed");
    errno = EINTR; return native_result;
}
VkResult VKAPI_CALL fakeView(VkDevice device, const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImageView* output) {
    ++calls;
    require(device == expected_device && info == expected_info && allocator == expected_allocator &&
            output == expected_output, "view provider arguments changed");
    *output = handle<VkImageView>(0x1234); errno = ENOSPC;
    return native_result;
}
Providers fake{11, fakeImage, fakeBind, fakeView, nullptr};
Entry bindEntry() {
    Entry entry{};
    entry.x[0] = number(expected_device); entry.x[1] = 0x7654;
    entry.x[2] = 0x9876; entry.x[3] = 0x100000010ULL;
    entry.x[18] = 0xaabb; entry.x[29] = 0x8000; entry.x[30] = 0x99;
    entry.sp = 0x7000;
    return entry;
}
std::uint64_t observe(Notification event, void* opaque) noexcept {
    auto& owned = *static_cast<Context*>(opaque);
    require(opaque == &context && owned.size < owned.events.size(), "copied callback context changed");
    owned.events[owned.size++] = event;
    event.entry.x.fill(0); // Value copy cannot mutate the original public entry.
    errno = EIO;
    if (owned.recursive) {
        owned.recursive = false;
        (void)bind(bindEntry(), fake);
    }
    return event.phase == Phase::Before ? owned.tag : 999;
}
void fakePublish(const Publication& event) noexcept {
    ++publications;
    require(event.device == 11 && (event.invocation_tag == context.tag ||
            (notification_loss() && event.invocation_tag == 0)), "publication tag/device changed");
    if (event.operation == Operation::Image)
        require(event.image_info == expected_info && number(event.image) == 0x7654, "image publication changed");
    if (event.operation == Operation::View)
        require(event.view_info == expected_info && number(event.view) == 0x1234, "view publication changed");
    errno = EACCES;
}
frame_bindings::Journal* journal = nullptr;
void realPublish(const Publication& event) noexcept {
    require(event.device == 77, "real device identity changed");
    switch (event.operation) {
    case Operation::Image:
        require(journal->createImageTagged(event.device, event.image, *event.image_info,
                                          event.invocation_tag), "actual image publication failed");
        break;
    case Operation::Bind:
        journal->bindImageMemory(event.device, event.image, event.memory, event.offset); break;
    case Operation::View:
        require(journal->createViewTagged(event.device, event.view, *event.view_info,
                                         event.invocation_tag), "actual view publication failed"); break;
    }
}
void checkRealProviders() {
    VkInstance instance = VK_NULL_HANDLE;
    auto instance_info = structure<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    require(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS, "local Vulkan instance failed");
    std::uint32_t count = 0;
    require(vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count > 0 && count <= 16,
            "local Vulkan physical device unavailable");
    std::array<VkPhysicalDevice, 16> physical{};
    require(vkEnumeratePhysicalDevices(instance, &count, physical.data()) == VK_SUCCESS, "physical enumeration failed");
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &family_count, nullptr);
    require(family_count > 0 && family_count <= 32, "queue families unavailable");
    std::array<VkQueueFamilyProperties, 32> families{};
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &family_count, families.data());
    std::uint32_t family = 0;
    while (family < family_count && !families[family].queueCount) ++family;
    require(family < family_count, "queue family not found");
    float priority = 0.0f;
    auto queue_info = structure<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
    auto device_info = structure<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
    VkDevice device = VK_NULL_HANDLE;
    require(vkCreateDevice(physical[0], &device_info, nullptr, &device) == VK_SUCCESS, "local Vulkan device failed");
    Providers actual{77,
        reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device, "vkCreateImage")),
        reinterpret_cast<PFN_vkBindImageMemory>(vkGetDeviceProcAddr(device, "vkBindImageMemory")),
        reinterpret_cast<PFN_vkCreateImageView>(vkGetDeviceProcAddr(device, "vkCreateImageView")), realPublish};
    require(actual.image && actual.bind && actual.view, "actual per-device providers missing");
    auto owned_journal = std::make_unique<frame_bindings::Journal>(); journal = owned_journal.get();
    auto image_info = structure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {8, 8, 1}; image_info.mipLevels = 1; image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage created_image = VK_NULL_HANDLE;
    Entry image_entry{}; image_entry.x[0] = number(device);
    image_entry.x[1] = number(&image_info); image_entry.x[3] = number(&created_image);
    const auto first_event = context.size;
    require(image(image_entry, actual) == VK_SUCCESS && created_image, "actual image forwarding failed");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, created_image, &requirements);
    std::uint32_t type = 0;
    while (type < 32 && !(requirements.memoryTypeBits & (std::uint32_t{1} << type))) ++type;
    require(type < 32, "image memory type missing");
    auto allocation = structure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    require(vkAllocateMemory(device, &allocation, nullptr, &memory) == VK_SUCCESS, "native memory allocation failed");
    journal->allocateMemory(77, memory, allocation);
    Entry bind_entry{}; bind_entry.x[0] = number(device); bind_entry.x[1] = number(created_image);
    bind_entry.x[2] = number(memory);
    require(bind(bind_entry, actual) == VK_SUCCESS, "actual memory binding forwarding failed");
    auto view_info = structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view_info.image = created_image; view_info.viewType = VK_IMAGE_VIEW_TYPE_2D; view_info.format = image_info.format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView created_view = VK_NULL_HANDLE;
    Entry view_entry{}; view_entry.x[0] = number(device); view_entry.x[1] = number(&view_info);
    view_entry.x[3] = number(&created_view);
    require(view(view_entry, actual) == VK_SUCCESS && created_view, "actual view forwarding failed");
    require(context.size == first_event + 6, "actual provider notification count changed");
    require(context.events[first_event + 1].image == number(created_image) &&
            context.events[first_event + 5].view == number(created_view), "actual handles not copied");
    require(journal->requestImportResourceReceipt(77, created_image, created_view, context.tag, 501),
            "actual resource receipt publication failed");
    frame_bindings::ImportResourceReceipt receipt{};
    struct Collect {
        frame_bindings::ImportResourceReceipt& out;
        void operator()(std::uint32_t, const frame_bindings::Snapshot&) const noexcept { std::abort(); }
        void operator()(std::uint64_t cookie, const frame_bindings::ImportResourceReceipt& value) const noexcept {
            require(cookie == 501, "receipt cookie changed"); out = value;
        }
    } collect{receipt};
    while (journal->drain(collect)) {}
    require(receipt.resourceComplete && !receipt.errors, "real resources did not resolve through existing journal");
    require(journal->validateImportResourceReceipt(receipt), "fully replayed actual receipt refused");
    require(receipt.image.identity.handle == number(created_image) && receipt.image.identity.generation &&
            receipt.view.identity.handle == number(created_view) && receipt.view.identity.generation &&
            receipt.memory.identity.handle == number(memory) && receipt.memory.identity.generation,
            "real resource generations missing");
    require(!receipt.nativeImportAccepted && !receipt.nativeCrossProcessNamespace && !receipt.sourceFrameAssociated &&
            !receipt.acceptedSubmit && !receipt.gpuConsumedCoherent, "local Vulkan forwarding claimed native/video proof");
    journal->destroyView(77, created_view);
    require(!journal->validateImportResourceReceipt(receipt), "unreplayed view retirement accepted");
    while (journal->drain(collect)) {}
    require(!journal->validateImportResourceReceipt(receipt), "retired view accepted");
    vkDestroyImageView(device, created_view, nullptr); vkDestroyImage(device, created_image, nullptr);
    vkFreeMemory(device, memory, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    journal = nullptr;
}
}
int main() {
    // Exercise a real software Vulkan provider without depending on the host GPU.
    if (!std::getenv("VK_ICD_FILENAMES")) {
        for (const auto* path : {"/usr/share/vulkan/icd.d/lvp_icd.x86_64.json", "/usr/share/vulkan/icd.d/lvp_icd.json"}) {
            if (access(path, R_OK) == 0) { setenv("VK_ICD_FILENAMES", path, 1); break; }
        }
    }
    auto info = structure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.format = VK_FORMAT_R8G8B8A8_UNORM; info.extent = {16, 32, 1}; info.arrayLayers = 1;
    VkAllocationCallbacks allocator{};
    VkImage output{};
    Entry entry{}; entry.x[0] = number(expected_device); entry.x[1] = number(&info);
    entry.x[2] = number(&allocator); entry.x[3] = number(&output); entry.x[18] = 0xabcdef;
    expected_info = &info; expected_allocator = &allocator; expected_output = &output;
    errno = ENOENT;
    require(!callbacks_registered() && !notification_loss(), "startup diagnostics active");
    require(image(entry, fake) == VK_SUCCESS && number(output) == 0x7654 && errno == EBUSY && context.size == 0,
            "inactive native image forwarding changed");
    require(!register_callbacks_before_threads({observe, &context}, false), "unprepared registration accepted");
    require(!register_callbacks_before_threads({nullptr, &context}, true), "null callback accepted");
    Callbacks descriptor{observe, &context};
    require(register_callbacks_before_threads(descriptor, true), "prepared subscriber refused");
    descriptor = {}; // Registration owns its own immutable descriptor copy.
    require(callbacks_registered() && !register_callbacks_before_threads({observe, &context}, true),
            "subscriber replacement accepted");
    fake.publish = fakePublish;
    require(image(entry, fake) == VK_SUCCESS && errno == EBUSY, "observed provider result/errno changed");
    require(context.events[0].phase == Phase::Before && context.events[1].phase == Phase::After &&
            context.events[1].invocation_tag == context.tag && context.events[1].image == number(output) &&
            context.events[1].extent.width == 16 && context.events[1].entry.x[18] == 0xabcdef,
            "entry/public scalar copy changed");
    require(entry.x[18] == 0xabcdef && publications == 1, "callback modified original entry or duplicate journal event");
    native_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    require(image(entry, fake) == static_cast<std::uint32_t>(native_result) && errno == EBUSY,
            "negative VkResult bits changed");
    require(publications == 1 && context.events[3].image == 0, "failed image output falsely published");
    native_result = VK_SUCCESS;
    require(bind(bindEntry(), fake) == VK_SUCCESS && errno == EINTR && publications == 2, "bind result changed");
    auto view_info = structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view_info.image = output; view_info.format = info.format;
    VkImageView view_output{};
    entry.x[1] = number(&view_info); entry.x[3] = number(&view_output);
    expected_info = &view_info; expected_output = &view_output;
    require(view(entry, fake) == VK_SUCCESS && errno == ENOSPC && publications == 3, "view result changed");
    require(context.events[7].view == number(view_output) && context.events[7].image == number(output),
            "view relation changed");
    native_result = VK_ERROR_FORMAT_NOT_SUPPORTED;
    require(view(entry, fake) == static_cast<std::uint32_t>(native_result) && publications == 3 &&
            context.events[9].view == 0, "failed view output falsely published");
    native_result = VK_SUCCESS;
    checkRealProviders();
    entry.x[1] = number(&info); entry.x[3] = number(&output);
    expected_info = &info; expected_output = &output;
    const auto before_unwind = context.size;
    provider_throws = true;
    bool propagated = false;
    try { (void)image(entry, fake); } catch (const NativeUnwind&) { propagated = true; }
    provider_throws = false;
    require(propagated && context.size == before_unwind + 1 &&
            context.events[before_unwind].phase == Phase::Before, "provider unwind swallowed or fake After produced");
    const auto before_recursive = context.size;
    const auto before_calls = calls;
    context.recursive = true;
    require(bind(bindEntry(), fake) == VK_SUCCESS && calls == before_calls + 2 &&
            context.size == before_recursive + 2 && notification_loss(), "recursive loss erased or native call suppressed");
    std::printf("import Vulkan forwarding: %u checks passed; real local providers/journal, no Frame execution\n", checks);
}
