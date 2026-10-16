// One new actual-driver creation case reuses the established owned factory
// fixture. Only this owned fixture's mutable input objects are extended BEFORE
// their original Vulkan creates; production/native call arguments are untouched.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
namespace graphics_gpu {
struct State {
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT enabled{};
    VkPipelineDynamicStateCreateInfo source{};
    std::array<VkDynamicState,8> words{},expected{};unsigned count=0,creates=0;
    std::array<const char*,2> extensions{};
    const VkGraphicsPipelineCreateInfo* original=nullptr;
    bool originalAdmitted=false,ownedViewAdmitted=false,inputsDestroyed=false;
} state;
VKAPI_ATTR VkResult VKAPI_CALL device(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
VKAPI_ATTR VkResult VKAPI_CALL pipeline(VkDevice,VkPipelineCache,std::uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL procedure(VkDevice,const char*);
}
#define vkCreateDevice graphics_gpu::device
#define vkCreateGraphicsPipelines graphics_gpu::pipeline
#define vkGetDeviceProcAddr graphics_gpu::procedure
#define main inherited_owned_factory_main
#include "../native-fragment-owned-clone-gpu/fixture.cpp"
#undef main
#undef vkGetDeviceProcAddr
#undef vkCreateGraphicsPipelines
#undef vkCreateDevice
namespace graphics_gpu {
VKAPI_ATTR VkResult VKAPI_CALL device(VkPhysicalDevice physical,const VkDeviceCreateInfo* supplied,const VkAllocationCallbacks* alloc,VkDevice* out){
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT supported{};supported.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    VkPhysicalDeviceFeatures2 query{};query.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;query.pNext=&supported;vkGetPhysicalDeviceFeatures2(physical,&query);
    std::uint32_t count=0;if(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)!=VK_SUCCESS)return VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkExtensionProperties> available(count);if(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data())!=VK_SUCCESS)return VK_ERROR_INITIALIZATION_FAILED;
    if(!std::any_of(available.begin(),available.end(),[](const auto& item){return std::strcmp(item.extensionName,VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME)==0;}))return VK_ERROR_EXTENSION_NOT_PRESENT;
    // These three independent real queried bits are required for this focused
    // proof. Optional queried bits add exact enums without inventing support.
    if(!supported.extendedDynamicState3RasterizationSamples||!supported.extendedDynamicState3SampleMask||!supported.extendedDynamicState3AlphaToCoverageEnable)return VK_ERROR_FEATURE_NOT_PRESENT;
    state.enabled.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    state.enabled.extendedDynamicState3RasterizationSamples=VK_TRUE;state.enabled.extendedDynamicState3SampleMask=VK_TRUE;state.enabled.extendedDynamicState3AlphaToCoverageEnable=VK_TRUE;
    state.enabled.extendedDynamicState3AlphaToOneEnable=supported.extendedDynamicState3AlphaToOneEnable;
    state.enabled.extendedDynamicState3SampleLocationsEnable=supported.extendedDynamicState3SampleLocationsEnable;
    auto& ownedInput=*const_cast<VkDeviceCreateInfo*>(supplied);
    if(ownedInput.enabledExtensionCount!=1)return VK_ERROR_INITIALIZATION_FAILED;
    state.extensions={ownedInput.ppEnabledExtensionNames[0],VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME};
    state.enabled.pNext=const_cast<void*>(ownedInput.pNext);ownedInput.pNext=&state.enabled;
    ownedInput.enabledExtensionCount=state.extensions.size();ownedInput.ppEnabledExtensionNames=state.extensions.data();
    for(auto value:{VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,VK_DYNAMIC_STATE_SAMPLE_MASK_EXT,VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT})state.words[state.count++]=value;
    if(state.enabled.extendedDynamicState3AlphaToOneEnable)state.words[state.count++]=VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT;
    if(state.enabled.extendedDynamicState3SampleLocationsEnable)state.words[state.count++]=VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT;
    state.expected=state.words;return vkCreateDevice(physical,&ownedInput,alloc,out);
}
VKAPI_ATTR VkResult VKAPI_CALL pipeline(VkDevice device,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* supplied,const VkAllocationCallbacks* alloc,VkPipeline* out){
    if(count!=1||!supplied)return VK_ERROR_INITIALIZATION_FAILED;
    ++state.creates;
    if(state.creates==1){
        state.source.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;state.source.dynamicStateCount=state.count;state.source.pDynamicStates=state.words.data();
        // The established fixture constructs a mutable local CI and captures
        // that SAME object after this successful downstream original call.
        auto& ownedInput=*const_cast<VkGraphicsPipelineCreateInfo*>(supplied);ownedInput.pDynamicState=&state.source;state.original=&ownedInput;
        const auto result=vkCreateGraphicsPipelines(device,cache,1,&ownedInput,alloc,out);state.originalAdmitted=result==VK_SUCCESS;return result;
    }
    if(state.creates!=2||!state.originalAdmitted||!state.original)return VK_ERROR_INITIALIZATION_FAILED;
    state.inputsDestroyed=!state.original->pStages&&!state.original->layout&&!state.original->renderPass;
    const auto* actual=supplied->pDynamicState;
    bool exact=state.inputsDestroyed&&actual&&actual!=&state.source&&actual->sType==VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO&&!actual->pNext&&!actual->flags&&actual->dynamicStateCount==state.count&&actual->pDynamicStates!=state.words.data();
    // Wipe the extra original arrays BEFORE reading the production factory's
    // view. Borrowing those inputs instead of owning them now fails causally.
    state.source={};state.words.fill(static_cast<VkDynamicState>(0x6fffffff));state.enabled={};
    if(exact)for(unsigned i=0;i<state.count;++i)exact=exact&&actual->pDynamicStates[i]==state.expected[i];
    if(!exact)return VK_ERROR_INITIALIZATION_FAILED;
    const auto result=vkCreateGraphicsPipelines(device,cache,1,supplied,alloc,out);state.ownedViewAdmitted=result==VK_SUCCESS;return result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL procedure(VkDevice device,const char* name){
    if(name&&std::strcmp(name,"vkCreateGraphicsPipelines")==0)return reinterpret_cast<PFN_vkVoidFunction>(pipeline);
    return vkGetDeviceProcAddr(device,name);
}
}
int main(int argc,char** argv){
    std::ostringstream captured;auto* previous=std::cout.rdbuf(captured.rdbuf());
    const int result=inherited_owned_factory_main(argc,argv);std::cout.rdbuf(previous);
    if(result||!graphics_gpu::state.originalAdmitted||!graphics_gpu::state.ownedViewAdmitted||!graphics_gpu::state.inputsDestroyed||graphics_gpu::state.creates!=2){std::cerr<<captured.str();return result?result:1;}
    auto output=captured.str();const auto end=output.rfind('}');if(end==std::string::npos)return 1;
    output.insert(end,",\"eds3_original_and_owned_factory_admitted\":true,\"extra_original_dynamic_inputs_wiped\":true,\"eds3_dynamic_count\":"+std::to_string(graphics_gpu::state.count));
    std::cout<<output;return 0;
}
