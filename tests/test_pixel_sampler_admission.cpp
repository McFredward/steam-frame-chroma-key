#include "layer/pixel_sampler_admission.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
using namespace frame_pixel_sampler;
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

void enabledFeatureInputs() {
    auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    ReviewedRoutes routes;
    routes.coreComputeRoutesObserved = true;
    const auto missing = inspectDeviceCreate(create, routes);
    require(missing.complete && !missing.samplerYcbcrConversionEnabled && !missing.eligible(),
            "absence of enabled feature cannot borrow physical-device support");
    VkPhysicalDeviceFeatures oldFeatures{};
    oldFeatures.robustBufferAccess = VK_TRUE;
    create.pEnabledFeatures = &oldFeatures;
    require(!inspectDeviceCreate(create, routes).eligible(), "core1.0 feature array cannot enable YCbCr implicitly");
    create.pEnabledFeatures = nullptr;

    auto ycbcr = info<VkPhysicalDeviceSamplerYcbcrConversionFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES);
    ycbcr.samplerYcbcrConversion = VK_TRUE; create.pNext = &ycbcr;
    require(inspectDeviceCreate(create, routes).eligible(), "actual enabled KHR/core YCbCr feature is recognized");
    require(!inspectDeviceCreate(create, {}).eligible(), "missing explicit core-route coverage refuses sampling");
    ycbcr.samplerYcbcrConversion = VK_FALSE;
    const auto disabled = inspectDeviceCreate(create, routes);
    require(disabled.complete && !disabled.samplerYcbcrConversionEnabled && !disabled.eligible(),
            "explicitly disabled feature remains disabled despite physical support");
    ycbcr.samplerYcbcrConversion = 2;
    require(!inspectDeviceCreate(create, routes).complete, "noncanonical feature enablement is refused conservatively");
    ycbcr.samplerYcbcrConversion = VK_TRUE;

    auto vulkan11 = info<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    vulkan11.samplerYcbcrConversion = VK_TRUE; create.pNext = &vulkan11;
    require(inspectDeviceCreate(create, routes).eligible(), "actual Vulkan11 enabled feature is recognized");
    auto features2 = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    features2.pNext = &vulkan11; create.pNext = &features2;
    require(inspectDeviceCreate(create, routes).eligible(), "actual device Features2 chain is traversed to enabled feature");
    create.pEnabledFeatures = &oldFeatures;
    require(!inspectDeviceCreate(create, routes).complete, "conflicting Features2 and pEnabledFeatures is not silently accepted");
    create.pEnabledFeatures = nullptr;
    vulkan11.pNext = &ycbcr;
    require(!inspectDeviceCreate(create, routes).complete, "core11 and KHR duplicate YCbCr enablements are refused");
    vulkan11.pNext = nullptr;

    auto features12 = info<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    features12.pNext = &vulkan11; create.pNext = &features12;
    require(!inspectDeviceCreate(create, routes).complete, "additional feature route needs explicit owner review");
    const VkStructureType types[] = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    routes.additionalChainTypes = types; routes.additionalChainTypeCount = 1;
    require(inspectDeviceCreate(create, routes).eligible(), "explicitly audited additional feature chain is retained");
    auto duplicate = features12; duplicate.pNext = &features12; create.pNext = &duplicate;
    require(!inspectDeviceCreate(create, routes).complete, "duplicate additional pNext structure type is refused");
}

void routesAndQueueCapabilities() {
    auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto ycbcr = info<VkPhysicalDeviceSamplerYcbcrConversionFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES);
    ycbcr.samplerYcbcrConversion = VK_TRUE; create.pNext = &ycbcr;
    ReviewedRoutes routes; routes.coreComputeRoutesObserved = true;
    const char* known[] = {"VK_KHR_sampler_ycbcr_conversion", "VK_KHR_swapchain"};
    routes.extensionNames = known; routes.extensionCount = 2;
    create.ppEnabledExtensionNames = known; create.enabledExtensionCount = 2;
    require(inspectDeviceCreate(create, routes).eligible(), "each actual enabled extension is explicitly covered");
    const char* unknown[] = {"VK_EXT_descriptor_buffer"};
    create.ppEnabledExtensionNames = unknown; create.enabledExtensionCount = 1;
    require(!inspectDeviceCreate(create, routes).complete, "uncovered descriptor-buffer routes cannot inherit core observation");
    const char* shaderObjects[] = {"VK_EXT_shader_object"};
    create.ppEnabledExtensionNames = shaderObjects;
    require(!inspectDeviceCreate(create, routes).complete, "uncovered compute shader-object routes are refused independently");
    const char* future[] = {"VK_FUTURE_unobserved_compute_route"};
    create.ppEnabledExtensionNames = future;
    require(!inspectDeviceCreate(create, routes).complete, "future unknown extension is unavailable by default");
    const char* duplicated[] = {known[0], known[0]};
    create.ppEnabledExtensionNames = duplicated; create.enabledExtensionCount = 2;
    require(!inspectDeviceCreate(create, routes).complete, "duplicate native extension names are refused");
    const char* nullName[] = {nullptr};
    create.ppEnabledExtensionNames = nullName; create.enabledExtensionCount = 1;
    require(!inspectDeviceCreate(create, routes).complete, "null enabled extension name cannot match reviewed route");
    std::array<char, VK_MAX_EXTENSION_NAME_SIZE> unterminated{}; unterminated.fill('A');
    require(!exactExtensionName(unterminated.data(), unterminated.data()), "unterminated name has a strict scan bound");
    require(!exactExtensionName("", "") && !exactExtensionName(nullptr, ""), "empty/missing names are not enabled routes");
    require(!exactExtensionName("VK_KHR_swapchain", "VK_KHR_swapchain2"), "route matching is exact rather than prefix based");
    require(!queueSupportsCompute(0) && !queueSupportsCompute(VK_QUEUE_GRAPHICS_BIT),
            "graphics capability does not invent compute support");
    require(queueSupportsCompute(VK_QUEUE_COMPUTE_BIT) &&
            queueSupportsCompute(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT), "observed queue flags explicitly enable compute");
}

void boundedMalformedInputs() {
    auto create = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto ycbcr = info<VkPhysicalDeviceSamplerYcbcrConversionFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES);
    ycbcr.samplerYcbcrConversion = VK_TRUE; create.pNext = &ycbcr;
    ReviewedRoutes routes; routes.coreComputeRoutesObserved = true;
    auto malformed = create; malformed.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    require(!inspectDeviceCreate(malformed, routes).complete, "wrong device create type is refused");
    malformed = create; malformed.flags = 1;
    require(!inspectDeviceCreate(malformed, routes).complete, "unknown device-create flags are refused");
    malformed = create; malformed.enabledExtensionCount = MaximumDeviceExtensions + 1;
    malformed.ppEnabledExtensionNames = reinterpret_cast<const char* const*>(1);
    require(!inspectDeviceCreate(malformed, routes).complete, "extension count is bounded before touching its pointer");
    malformed = create; malformed.enabledExtensionCount = 1;
    require(!inspectDeviceCreate(malformed, routes).complete, "missing native extension array is refused");
    auto badRoutes = routes; badRoutes.extensionCount = MaximumReviewedExtensions + 1;
    badRoutes.extensionNames = reinterpret_cast<const char* const*>(1);
    require(!inspectDeviceCreate(create, badRoutes).complete, "reviewed extension inventory is bounded before access");
    badRoutes = routes; badRoutes.additionalChainTypeCount = MaximumReviewedChainTypes + 1;
    badRoutes.additionalChainTypes = reinterpret_cast<const VkStructureType*>(1);
    require(!inspectDeviceCreate(create, badRoutes).complete, "reviewed pNext inventory is bounded before access");
    badRoutes = routes; badRoutes.additionalChainTypeCount = 1;
    require(!inspectDeviceCreate(create, badRoutes).complete, "missing reviewed pNext type array is refused");
    std::array<const char*, MaximumReviewedExtensions> reviewedNames{};
    reviewedNames.fill("VK_KHR_sampler_ycbcr_conversion");
    auto broadRoutes = routes; broadRoutes.extensionNames = reviewedNames.data();
    broadRoutes.extensionCount = reviewedNames.size();
    const char* enabledName = reviewedNames[0];
    auto oneExtension = create; oneExtension.enabledExtensionCount = 1; oneExtension.ppEnabledExtensionNames = &enabledName;
    require(inspectDeviceCreate(oneExtension, broadRoutes).eligible(),
            "finite1024-name reviewed registry does not raise actual enabled-extension bound");
    std::array<VkStructureType, MaximumReviewedChainTypes> reviewedTypes{};
    reviewedTypes.fill(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    broadRoutes.additionalChainTypes = reviewedTypes.data(); broadRoutes.additionalChainTypeCount = reviewedTypes.size();
    auto features12 = info<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    features12.pNext = &ycbcr; auto twoFeatures = create; twoFeatures.pNext = &features12;
    require(inspectDeviceCreate(twoFeatures, broadRoutes).eligible(),
            "finite1024-type reviewed registry does not raise32-node actual pNext bound");
    ycbcr.pNext = &ycbcr;
    require(!inspectDeviceCreate(create, routes).complete, "self-referential pNext chain terminates with refusal");
    auto features2 = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    features2.pNext = &ycbcr; ycbcr.pNext = &features2;
    require(!inspectDeviceCreate(create, routes).complete, "multi-node pNext cycle terminates with refusal");
    ycbcr.pNext = nullptr;
    auto deviceGroup = info<VkDeviceGroupDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO);
    deviceGroup.pNext = &ycbcr; create.pNext = &deviceGroup;
    const VkStructureType groupType = deviceGroup.sType;
    routes.additionalChainTypes = &groupType; routes.additionalChainTypeCount = 1;
    require(!inspectDeviceCreate(create, routes).complete, "device-group ownership is refused even if type is listed");

    // Synthetic, explicitly owner-reviewed types exercise the finite traversal
    // boundary; they are not claimed to be valid native feature structures.
    std::array<VkBaseInStructure, MaximumDeviceChain> nodes{};
    std::array<VkStructureType, MaximumDeviceChain> reviewed{};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        reviewed[i] = static_cast<VkStructureType>(1000998000 + i);
        nodes[i].sType = reviewed[i]; nodes[i].pNext = i + 1 == nodes.size() ? nullptr : &nodes[i + 1];
    }
    routes.additionalChainTypes = reviewed.data(); routes.additionalChainTypeCount = reviewed.size();
    create.pNext = &ycbcr; ycbcr.pNext = nodes.data();
    nodes[nodes.size() - 2].pNext = nullptr;
    require(inspectDeviceCreate(create, routes).eligible(), "exact32-node explicitly reviewed chain is bounded and complete");
    nodes[nodes.size() - 2].pNext = &nodes.back();
    require(!inspectDeviceCreate(create, routes).complete, "33rd node is refused without unbounded traversal");
}
} // namespace

int main() {
    enabledFeatureInputs(); routesAndQueueCapabilities(); boundedMalformedInputs();
    std::cout << "pixel sampler admission: " << checks << " checks passed; no device support or native mapping inferred\n";
}
