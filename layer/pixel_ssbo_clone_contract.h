#pragma once

// Cold, finite CPU definition capture only. Caller-resolved object identities
// are diagnostics, not live handle leases. This header makes no Vulkan calls,
// owns no native objects and grants no receiver/runtime pipeline permission.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "../receiver/sha256.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace frame_pixel_ssbo_clone {
constexpr std::size_t MaxBindings=32, MaxArray=16, MaxPushRanges=8, MaxVertexBindings=16;
constexpr std::size_t MaxAttributes=32, MaxViewports=4, MaxAttachments=8, MaxDynamics=16, MaxDependencies=8;
constexpr std::size_t MaxChain=32, ShaderBytes=9460;
enum class Reason : std::uint8_t {
    None, NativeCreationFailed, Type, Extension, Bounds, NullArray, Identity,
    FeatureAbsent, FeatureDisabled, FeatureValue, FeatureConflict, ChainCycle, DuplicateNode, LoaderFunction,
    ImmutableSampler, Descriptor, MissingImage9, Image9Extent, PushRange,
    ShaderProfile, ShaderPin, Stage, PipelineFlags, PipelineExtension, DynamicState,
    RenderPassMissing, RenderPassShape, AttachmentIndex, Scalar, OwnedObjectsMissing
};
struct Refusal {Reason reason=Reason::None;std::uint32_t index=UINT32_MAX,type=UINT32_MAX;};
inline const char* reasonName(Reason value) noexcept {
    switch(value){
#define NAME(e,s) case Reason::e:return s;
    NAME(None,"none") NAME(NativeCreationFailed,"native_creation_failed") NAME(Type,"type") NAME(Extension,"extension")
    NAME(Bounds,"bounds") NAME(NullArray,"null_array") NAME(Identity,"identity") NAME(FeatureAbsent,"feature_absent")
    NAME(FeatureDisabled,"feature_disabled") NAME(FeatureValue,"feature_value") NAME(FeatureConflict,"feature_conflict")
    NAME(ChainCycle,"chain_cycle") NAME(DuplicateNode,"duplicate_node") NAME(LoaderFunction,"loader_function")
    NAME(ImmutableSampler,"immutable_sampler") NAME(Descriptor,"descriptor") NAME(MissingImage9,"missing_image9")
    NAME(Image9Extent,"image9_extent") NAME(PushRange,"push_range") NAME(ShaderProfile,"shader_profile")
    NAME(ShaderPin,"shader_pin") NAME(Stage,"stage") NAME(PipelineFlags,"pipeline_flags")
    NAME(PipelineExtension,"pipeline_extension") NAME(DynamicState,"dynamic_state")
    NAME(RenderPassMissing,"renderpass_missing") NAME(RenderPassShape,"renderpass_shape")
    NAME(AttachmentIndex,"attachment_index") NAME(Scalar,"scalar") NAME(OwnedObjectsMissing,"owned_objects_missing")
#undef NAME
    }
    return "unknown_clone_refusal";
}
inline bool refuse(Refusal& out,Reason reason,std::uint32_t index=UINT32_MAX,
                   std::uint32_t type=UINT32_MAX) noexcept {
    if(out.reason==Reason::None)out={reason,index,type};
    return false;
}
inline bool boolean(VkBool32 value) noexcept{return value==VK_FALSE || value==VK_TRUE;}
inline bool stages(VkShaderStageFlags value) noexcept {
    return value && (value==VK_SHADER_STAGE_ALL || !(value&~VkShaderStageFlags(0x3f)));
}
inline bool digestMatches(const std::array<std::uint8_t,32>& digest,const char* hex) noexcept {
    constexpr char alphabet[]="0123456789abcdef";
    for(std::size_t i=0;i<digest.size();++i)if(alphabet[digest[i]>>4]!=hex[i*2] || alphabet[digest[i]&15]!=hex[i*2+1])return false;
    return true;
}
// Default preserves the exact original array3 interface. The only separate
// count1 profiles are deterministic original-invocation SSBO artifacts built
// by native_fragment_ssbo_profile.py: Set1, pixel(0,0), first-covered, or first
// eligible32x32grid invocation, exact original PS pin,
// two exhaustive B9 constant0 accesses and fresh owned interface types. A caller
// boolean/hash cannot attest that interface; admission hashes the actual bytes
// against this independently pinned transformer result. No native authority.
class ProbeInterface {
public:
    constexpr ProbeInterface() noexcept=default;
    bool specializedCount1() const noexcept{return kind_!=0;}
    bool firstCoveredInvocation() const noexcept{return kind_==2;}
    bool sparseGrid32() const noexcept{return kind_==3;}
    bool sameProfile(ProbeInterface other) const noexcept{return kind_==other.kind_;}
    static constexpr std::size_t Count1Bytes=9864;
    static constexpr const char* Count1Sha256="6d666c261114f93436e48c1f52793fe18afbce9552e07b53d4212654c4560020";
    static constexpr std::size_t FirstCoveredBytes=9616;
    static constexpr const char* FirstCoveredSha256="e76f8832c87229138e4c6b21bcab7b11e63d98bc016d42f5afb8dd7098c01bcd";
    static constexpr std::size_t Sparse32Bytes=9888;
    static constexpr const char* Sparse32Sha256="eb242f9252a51393e0794e12afed36a90de3e1059d45310d8b4c70d543019efd";
    static constexpr std::size_t MaximumBytes=Sparse32Bytes;
    static bool count1(const std::uint32_t* words,std::size_t bytes,ProbeInterface& out,Refusal& why) noexcept {
        out={};why={};
        if(!words || (bytes!=Count1Bytes && bytes!=FirstCoveredBytes && bytes!=Sparse32Bytes))return refuse(why,Reason::ShaderProfile);
        frame_receiver::Sha256 digest;digest.update(words,bytes);
        const auto hash=digest.finish();
        if(bytes==Count1Bytes && digestMatches(hash,Count1Sha256))out.kind_=1;
        else if(bytes==FirstCoveredBytes && digestMatches(hash,FirstCoveredSha256))out.kind_=2;
        else if(bytes==Sparse32Bytes && digestMatches(hash,Sparse32Sha256))out.kind_=3;
        else return refuse(why,Reason::ShaderPin);
        return true;
    }
    bool matches(const std::uint32_t* words,std::size_t bytes) const noexcept {
        if(!kind_)return true; // Historical array3 artifact transport contract.
        ProbeInterface verified;Refusal why;return count1(words,bytes,verified,why) && verified.kind_==kind_;
    }
private:
    std::uint8_t kind_=0;
};
template<class T> std::uint64_t handle(T value) noexcept {
    static_assert(sizeof(value)<=sizeof(std::uint64_t),"bounded public handle representation");
    std::uint64_t result=0;std::memcpy(&result,&value,sizeof(value));return result;
}
template<class T,std::size_t N> bool copyArray(const T* in,std::uint32_t count,std::array<T,N>& out,Refusal& why) noexcept {
    if(count>N)return refuse(why,Reason::Bounds);
    if(count && !in)return refuse(why,Reason::NullArray);
    for(std::uint32_t i=0;i<count;++i)out[i]=in[i];
    return true;
}
template<class T> bool ordinary(const T& in,VkStructureType type,Refusal& why) noexcept {
    if(in.sType!=type)return refuse(why,Reason::Type,UINT32_MAX,static_cast<std::uint32_t>(in.sType));
    return !in.pNext || refuse(why,Reason::Extension,UINT32_MAX,static_cast<std::uint32_t>(in.sType));
}
enum class FeatureSource : std::uint8_t {None,Legacy,Features2};
struct FeatureFacts {
    bool complete=false,observed=false,enabled=false;
    FeatureSource source=FeatureSource::None;
    VkBool32 raw=VK_FALSE;
    bool nativeAuthority=false;
};
// Public named scalar validation only; never scan struct padding or rewrite
// the original request. Non-storage feature declarations may be enabled or
// disabled independently. Their presence is not native route/lifetime proof.
inline bool featureValues(std::initializer_list<VkBool32> values,std::uint32_t index,
                          VkStructureType type,Refusal& why) noexcept {
    for(const auto value:values)if(!boolean(value))return refuse(why,Reason::FeatureValue,index,static_cast<std::uint32_t>(type));
    return true;
}
inline bool coreFeatureValues(const VkPhysicalDeviceFeatures& f,std::uint32_t index,
                              VkStructureType type,Refusal& why) noexcept {
    return featureValues({f.robustBufferAccess,f.fullDrawIndexUint32,f.imageCubeArray,f.independentBlend,f.geometryShader,
            f.tessellationShader,f.sampleRateShading,f.dualSrcBlend,f.logicOp,f.multiDrawIndirect,
            f.drawIndirectFirstInstance,f.depthClamp,f.depthBiasClamp,f.fillModeNonSolid,f.depthBounds,
            f.wideLines,f.largePoints,f.alphaToOne,f.multiViewport,f.samplerAnisotropy,f.textureCompressionETC2,
            f.textureCompressionASTC_LDR,f.textureCompressionBC,f.occlusionQueryPrecise,
            f.pipelineStatisticsQuery,f.vertexPipelineStoresAndAtomics,f.fragmentStoresAndAtomics,
            f.shaderTessellationAndGeometryPointSize,f.shaderImageGatherExtended,
            f.shaderStorageImageExtendedFormats,f.shaderStorageImageMultisample,
            f.shaderStorageImageReadWithoutFormat,f.shaderStorageImageWriteWithoutFormat,
            f.shaderUniformBufferArrayDynamicIndexing,f.shaderSampledImageArrayDynamicIndexing,
            f.shaderStorageBufferArrayDynamicIndexing,f.shaderStorageImageArrayDynamicIndexing,
            f.shaderClipDistance,f.shaderCullDistance,f.shaderFloat64,f.shaderInt64,f.shaderInt16,
            f.shaderResourceResidency,f.shaderResourceMinLod,f.sparseBinding,f.sparseResidencyBuffer,
            f.sparseResidencyImage2D,f.sparseResidencyImage3D,f.sparseResidency2Samples,
            f.sparseResidency4Samples,f.sparseResidency8Samples,f.sparseResidency16Samples,
            f.sparseResidencyAliased,f.variableMultisampleRate,f.inheritedQueries},index,type,why);
}
inline bool knownFeaturePayload(const VkBaseInStructure* node,std::uint32_t index,Refusal& why) noexcept {
    switch(node->sType){
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(node);
        return featureValues({f.storageBuffer16BitAccess,f.uniformAndStorageBuffer16BitAccess,f.storagePushConstant16,
            f.storageInputOutput16,f.multiview,f.multiviewGeometryShader,f.multiviewTessellationShader,
            f.variablePointersStorageBuffer,f.variablePointers,f.protectedMemory,f.samplerYcbcrConversion,
            f.shaderDrawParameters},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node);
        return featureValues({f.samplerMirrorClampToEdge,f.drawIndirectCount,f.storageBuffer8BitAccess,
            f.uniformAndStorageBuffer8BitAccess,f.storagePushConstant8,f.shaderBufferInt64Atomics,
            f.shaderSharedInt64Atomics,f.shaderFloat16,f.shaderInt8,f.descriptorIndexing,
            f.shaderInputAttachmentArrayDynamicIndexing,f.shaderUniformTexelBufferArrayDynamicIndexing,
            f.shaderStorageTexelBufferArrayDynamicIndexing,f.shaderUniformBufferArrayNonUniformIndexing,
            f.shaderSampledImageArrayNonUniformIndexing,f.shaderStorageBufferArrayNonUniformIndexing,
            f.shaderStorageImageArrayNonUniformIndexing,f.shaderInputAttachmentArrayNonUniformIndexing,
            f.shaderUniformTexelBufferArrayNonUniformIndexing,f.shaderStorageTexelBufferArrayNonUniformIndexing,
            f.descriptorBindingUniformBufferUpdateAfterBind,f.descriptorBindingSampledImageUpdateAfterBind,
            f.descriptorBindingStorageImageUpdateAfterBind,f.descriptorBindingStorageBufferUpdateAfterBind,
            f.descriptorBindingUniformTexelBufferUpdateAfterBind,
            f.descriptorBindingStorageTexelBufferUpdateAfterBind,f.descriptorBindingUpdateUnusedWhilePending,
            f.descriptorBindingPartiallyBound,f.descriptorBindingVariableDescriptorCount,
            f.runtimeDescriptorArray,f.samplerFilterMinmax,f.scalarBlockLayout,f.imagelessFramebuffer,
            f.uniformBufferStandardLayout,f.shaderSubgroupExtendedTypes,f.separateDepthStencilLayouts,
            f.hostQueryReset,f.timelineSemaphore,f.bufferDeviceAddress,f.bufferDeviceAddressCaptureReplay,
            f.bufferDeviceAddressMultiDevice,f.vulkanMemoryModel,f.vulkanMemoryModelDeviceScope,
            f.vulkanMemoryModelAvailabilityVisibilityChains,f.shaderOutputViewportIndex,f.shaderOutputLayer,
            f.subgroupBroadcastDynamicId},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(node);
        return featureValues({f.robustImageAccess,f.inlineUniformBlock,f.descriptorBindingInlineUniformBlockUpdateAfterBind,
            f.pipelineCreationCacheControl,f.privateData,f.shaderDemoteToHelperInvocation,
            f.shaderTerminateInvocation,f.subgroupSizeControl,f.computeFullSubgroups,f.synchronization2,
            f.textureCompressionASTC_HDR,f.shaderZeroInitializeWorkgroupMemory,f.dynamicRendering,
            f.shaderIntegerDotProduct,f.maintenance4},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(node);
        return featureValues({f.timelineSemaphore},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceHostQueryResetFeatures*>(node);
        return featureValues({f.hostQueryReset},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceCustomBorderColorFeaturesEXT*>(node);
        return featureValues({f.customBorderColors,f.customBorderColorWithoutFormat},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT*>(node);
        return featureValues({f.extendedDynamicState},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT*>(node);
        return featureValues({f.extendedDynamicState3TessellationDomainOrigin,f.extendedDynamicState3DepthClampEnable,
            f.extendedDynamicState3PolygonMode,f.extendedDynamicState3RasterizationSamples,
            f.extendedDynamicState3SampleMask,f.extendedDynamicState3AlphaToCoverageEnable,
            f.extendedDynamicState3AlphaToOneEnable,f.extendedDynamicState3LogicOpEnable,
            f.extendedDynamicState3ColorBlendEnable,f.extendedDynamicState3ColorBlendEquation,
            f.extendedDynamicState3ColorWriteMask,f.extendedDynamicState3RasterizationStream,
            f.extendedDynamicState3ConservativeRasterizationMode,
            f.extendedDynamicState3ExtraPrimitiveOverestimationSize,f.extendedDynamicState3DepthClipEnable,
            f.extendedDynamicState3SampleLocationsEnable,f.extendedDynamicState3ColorBlendAdvanced,
            f.extendedDynamicState3ProvokingVertexMode,f.extendedDynamicState3LineRasterizationMode,
            f.extendedDynamicState3LineStippleEnable,f.extendedDynamicState3DepthClipNegativeOneToOne,
            f.extendedDynamicState3ViewportWScalingEnable,f.extendedDynamicState3ViewportSwizzle,
            f.extendedDynamicState3CoverageToColorEnable,f.extendedDynamicState3CoverageToColorLocation,
            f.extendedDynamicState3CoverageModulationMode,f.extendedDynamicState3CoverageModulationTableEnable,
            f.extendedDynamicState3CoverageModulationTable,f.extendedDynamicState3CoverageReductionMode,
            f.extendedDynamicState3RepresentativeFragmentTestEnable,
            f.extendedDynamicState3ShadingRateImageEnable},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
        const auto& f=*reinterpret_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(node);
        return featureValues({f.samplerYcbcrConversion},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR: {
        const auto& f=*reinterpret_cast<const VkPhysicalDevicePresentIdFeaturesKHR*>(node);
        return featureValues({f.presentId},index,node->sType,why);
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR: {
        const auto& f=*reinterpret_cast<const VkPhysicalDevicePresentWaitFeaturesKHR*>(node);
        return featureValues({f.presentWait},index,node->sType,why);
    }
    default:return refuse(why,Reason::Extension,index,static_cast<std::uint32_t>(node->sType));
    }
}
inline bool enabledFeature(const VkDeviceCreateInfo& in,bool successfulCreate,
                           FeatureFacts& out,Refusal& why) noexcept {
    out={};why={};
    if(!successfulCreate)return refuse(why,Reason::NativeCreationFailed);
    if(in.sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO || in.flags)
        return refuse(why,Reason::Type,UINT32_MAX,static_cast<std::uint32_t>(in.sType));
    const auto observe=[&](VkBool32 value,FeatureSource source,std::uint32_t index=UINT32_MAX,
                           VkStructureType type=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO) noexcept {
        out.observed=true;out.raw=value;out.source=source;
        if(!boolean(value))return refuse(why,Reason::FeatureValue,index,static_cast<std::uint32_t>(type));
        out.enabled=value==VK_TRUE;return true;
    };
    if(in.pEnabledFeatures && (!observe(in.pEnabledFeatures->fragmentStoresAndAtomics,FeatureSource::Legacy) ||
       !coreFeatureValues(*in.pEnabledFeatures,UINT32_MAX,VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,why)))return false;
    std::array<const VkBaseInStructure*,MaxChain> seen{};std::uint32_t count=0,loaderFunctions=0;
    for(auto* node=static_cast<const VkBaseInStructure*>(in.pNext);node;node=node->pNext){
        if(count==seen.size())return refuse(why,Reason::Bounds,count);
        for(std::uint32_t i=0;i<count;++i){
            if(seen[i]==node)return refuse(why,Reason::ChainCycle,count);
            if(seen[i]->sType==node->sType && node->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
                return refuse(why,Reason::DuplicateNode,count,static_cast<std::uint32_t>(node->sType));
        }
        seen[count]=node;const auto index=count++;
        if(node->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO){
            const auto* loader=reinterpret_cast<const VkLayerDeviceCreateInfo*>(node);
            std::underlying_type_t<VkLayerFunction> function=0;
            std::memcpy(&function,&loader->function,sizeof(function));std::uint32_t bit=0;
            switch(function){
            case VK_LAYER_LINK_INFO:bit=1;break;case VK_LOADER_DATA_CALLBACK:bit=2;break;
            case VK_LOADER_LAYER_CREATE_DEVICE_CALLBACK:bit=4;break;case VK_LOADER_FEATURES:bit=8;break;
            default:return refuse(why,Reason::LoaderFunction,index,static_cast<std::uint32_t>(node->sType));
            }
            if(loaderFunctions&bit)return refuse(why,Reason::DuplicateNode,index,static_cast<std::uint32_t>(node->sType));
            loaderFunctions|=bit;continue; // Never call/dereference a loader union callback.
        }
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2){
            if(in.pEnabledFeatures)return refuse(why,Reason::FeatureConflict,index,static_cast<std::uint32_t>(node->sType));
            const auto& features=reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features;
            if(!observe(features.fragmentStoresAndAtomics,FeatureSource::Features2,index,node->sType) ||
               !coreFeatureValues(features,index,node->sType,why))return false;
        }else if(!knownFeaturePayload(node,index,why))return false;
    }
    if(!out.observed)return refuse(why,Reason::FeatureAbsent);
    if(!out.enabled)return refuse(why,Reason::FeatureDisabled);
    out.complete=true;return true;
}

struct Set0Definition {
    std::uint32_t count=0;std::array<VkDescriptorSetLayoutBinding,MaxBindings> bindings{};bool complete=false;
    ProbeInterface probeInterface{};
    bool pendingCount1=false;
    bool view(VkDescriptorSetLayoutCreateInfo& out) const noexcept {
        out={};if(!complete || pendingCount1)return false;
        out.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        out.bindingCount=count;out.pBindings=bindings.data();return true;
    }
};
namespace detail {
inline bool captureSet0Shape(const VkDescriptorSetLayoutCreateInfo& in,Set0Definition& out,Refusal& why,
                            ProbeInterface profile,bool pending) noexcept {
    out={};why={};if(!ordinary(in,VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,why))return false;
    if(in.flags || in.bindingCount>MaxBindings)return refuse(why,Reason::Bounds);
    if(in.bindingCount && !in.pBindings)return refuse(why,Reason::NullArray);
    bool found9=false;
    for(std::uint32_t i=0;i<in.bindingCount;++i){
        const auto& binding=in.pBindings[i];
        if(binding.pImmutableSamplers)return refuse(why,Reason::ImmutableSampler,i);
        if(binding.descriptorCount>MaxArray || (binding.descriptorCount && !stages(binding.stageFlags)) ||
           binding.descriptorType<VK_DESCRIPTOR_TYPE_SAMPLER || binding.descriptorType>VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
            return refuse(why,Reason::Descriptor,i);
        for(std::uint32_t j=0;j<i;++j)if(in.pBindings[j].binding==binding.binding)return refuse(why,Reason::Descriptor,i);
        if(binding.binding==9){
            found9=true;
            if(binding.descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || !(binding.stageFlags&VK_SHADER_STAGE_FRAGMENT_BIT))
                return refuse(why,Reason::Descriptor,i);
            // The exact original PS declares three descriptors. Actual Vulkan
            // validation requires that full extent even for its constant0
            // accesses. A separately validated specialized module needs its
            // own profile; native count1 must not silently pass this contract.
            if((profile.specializedCount1() || pending)?binding.descriptorCount!=1:binding.descriptorCount<3)
                return refuse(why,Reason::Image9Extent,i);
        }
        out.bindings[i]=binding; // Immutable pointers were rejected, never retained.
    }
    if(!found9)return refuse(why,Reason::MissingImage9);
    out.count=in.bindingCount;out.probeInterface=profile;out.complete=!pending;out.pendingCount1=pending;return true;
}
} // namespace detail
inline bool captureSet0(const VkDescriptorSetLayoutCreateInfo& in,Set0Definition& out,Refusal& why,
                        ProbeInterface profile={}) noexcept {
    return detail::captureSet0Shape(in,out,why,profile,false);
}
// Cold observation only. No complete definition, usable Vulkan view or profile
// is produced until independently verified probe bytes authorize promotion.
inline bool capturePendingCount1Set0(const VkDescriptorSetLayoutCreateInfo& in,Set0Definition& out,Refusal& why) noexcept {
    return detail::captureSet0Shape(in,out,why,{},true);
}
inline bool promotePendingCount1Set0(Set0Definition& definition,ProbeInterface profile,Refusal& why) noexcept {
    why={};if(!definition.pendingCount1 || definition.complete || !profile.specializedCount1())return refuse(why,Reason::ShaderProfile);
    VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount=definition.count;info.pBindings=definition.bindings.data();Set0Definition verified;
    if(!captureSet0(info,verified,why,profile))return false;
    definition=verified;return true;
}
struct LayoutDefinition {
    std::uint32_t count=0;std::array<VkPushConstantRange,MaxPushRanges> ranges{};bool complete=false;
};
inline bool captureLayout(const VkPipelineLayoutCreateInfo& in,VkDescriptorSetLayout resolvedSet0,
                          LayoutDefinition& out,Refusal& why) noexcept {
    out={};why={};if(!ordinary(in,VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,why))return false;
    if(in.flags || in.setLayoutCount!=1)return refuse(why,Reason::Bounds);
    if(!in.pSetLayouts || !resolvedSet0 || in.pSetLayouts[0]!=resolvedSet0)return refuse(why,Reason::Identity);
    if(!copyArray(in.pPushConstantRanges,in.pushConstantRangeCount,out.ranges,why))return false;
    VkShaderStageFlags used=0;
    for(std::uint32_t i=0;i<in.pushConstantRangeCount;++i){
        const auto& range=out.ranges[i];
        if(!stages(range.stageFlags) || (range.stageFlags&used) || !range.size || range.offset%4 || range.size%4 ||
           range.offset>4096 || range.size>4096-range.offset)return refuse(why,Reason::PushRange,i);
        used|=range.stageFlags;
    }
    out.count=in.pushConstantRangeCount;out.complete=true;return true;
}
// Call-duration view: recreate after copying/moving either this view or recipe.
struct RenderPassView {VkRenderPassCreateInfo info{};VkSubpassDescription subpass{};};
struct RenderPassDefinition {
    std::uint32_t attachmentCount=0,colorCount=0,preserveCount=0,dependencyCount=0;
    bool hasResolves=false,hasDepthReference=false,hasDepth=false,complete=false;
    std::array<VkAttachmentDescription,MaxAttachments> attachments{};
    std::array<VkAttachmentReference,MaxAttachments> colors{},resolves{};
    VkAttachmentReference depth{};std::array<std::uint32_t,MaxAttachments> preserves{};
    std::array<VkSubpassDependency,MaxDependencies> dependencies{};
    bool view(RenderPassView& out) const noexcept {
        out={};if(!complete)return false;
        out.subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;out.subpass.colorAttachmentCount=colorCount;
        out.subpass.pColorAttachments=colorCount?colors.data():nullptr;out.subpass.pResolveAttachments=hasResolves?resolves.data():nullptr;
        out.subpass.pDepthStencilAttachment=hasDepthReference?&depth:nullptr;out.subpass.preserveAttachmentCount=preserveCount;
        out.subpass.pPreserveAttachments=preserveCount?preserves.data():nullptr;
        out.info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;out.info.attachmentCount=attachmentCount;
        out.info.pAttachments=attachmentCount?attachments.data():nullptr;out.info.subpassCount=1;out.info.pSubpasses=&out.subpass;
        out.info.dependencyCount=dependencyCount;out.info.pDependencies=dependencyCount?dependencies.data():nullptr;return true;
    }
};
inline bool sampleCount(VkSampleCountFlagBits value) noexcept {
    const auto v=static_cast<std::uint32_t>(value);return v && v<=64 && !(v&(v-1));
}
inline bool captureRenderPass(const VkRenderPassCreateInfo* in,RenderPassDefinition& out,Refusal& why) noexcept {
    out={};why={};if(!in)return refuse(why,Reason::RenderPassMissing);
    if(!ordinary(*in,VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,why))return false;
    if(in->flags || in->subpassCount!=1 || !in->pSubpasses || !in->attachmentCount)return refuse(why,Reason::RenderPassShape);
    if(!copyArray(in->pAttachments,in->attachmentCount,out.attachments,why) ||
       !copyArray(in->pDependencies,in->dependencyCount,out.dependencies,why))return false;
    out.attachmentCount=in->attachmentCount;out.dependencyCount=in->dependencyCount;
    for(std::uint32_t i=0;i<out.attachmentCount;++i)if(out.attachments[i].flags || !sampleCount(out.attachments[i].samples) || out.attachments[i].format==VK_FORMAT_UNDEFINED)
        return refuse(why,Reason::RenderPassShape,i);
    const auto& sub=in->pSubpasses[0];
    if(sub.flags || sub.pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS || sub.inputAttachmentCount || !sub.colorAttachmentCount)
        return refuse(why,Reason::RenderPassShape);
    if(!copyArray(sub.pColorAttachments,sub.colorAttachmentCount,out.colors,why))return false;
    bool colorUsed=false;for(std::uint32_t i=0;i<sub.colorAttachmentCount;++i)colorUsed|=out.colors[i].attachment!=VK_ATTACHMENT_UNUSED;
    if(!colorUsed)return refuse(why,Reason::RenderPassShape); // Refuse before ignored dependent pointers.
    if(!copyArray(sub.pPreserveAttachments,sub.preserveAttachmentCount,out.preserves,why))return false;
    out.colorCount=sub.colorAttachmentCount;out.preserveCount=sub.preserveAttachmentCount;
    out.hasResolves=sub.pResolveAttachments!=nullptr;out.hasDepthReference=sub.pDepthStencilAttachment!=nullptr;
    if(out.hasResolves && !copyArray(sub.pResolveAttachments,sub.colorAttachmentCount,out.resolves,why))return false;
    if(out.hasDepthReference){out.depth=*sub.pDepthStencilAttachment;out.hasDepth=out.depth.attachment!=VK_ATTACHMENT_UNUSED;}
    const auto reference=[&](const VkAttachmentReference& value) noexcept {
        return value.attachment==VK_ATTACHMENT_UNUSED || value.attachment<out.attachmentCount;
    };
    for(std::uint32_t i=0;i<out.colorCount;++i)if(!reference(out.colors[i]) || (out.hasResolves && !reference(out.resolves[i])))
        return refuse(why,Reason::AttachmentIndex,i);
    if(out.hasDepthReference && !reference(out.depth))return refuse(why,Reason::AttachmentIndex);
    for(std::uint32_t i=0;i<out.preserveCount;++i)if(out.preserves[i]>=out.attachmentCount)
        return refuse(why,Reason::AttachmentIndex,i);
    for(std::uint32_t i=0;i<out.dependencyCount;++i)if((out.dependencies[i].srcSubpass!=0 && out.dependencies[i].srcSubpass!=VK_SUBPASS_EXTERNAL) ||
        (out.dependencies[i].dstSubpass!=0 && out.dependencies[i].dstSubpass!=VK_SUBPASS_EXTERNAL))return refuse(why,Reason::RenderPassShape,i);
    out.complete=true;return true;
}

enum class ShaderRole : std::uint8_t {Vertex,Fragment};
struct ShaderDefinition {
    std::uint32_t bytes=0;std::array<std::uint32_t,ShaderBytes/4> words{};
    std::array<std::uint8_t,32> sha256{};bool complete=false;
};
inline bool captureShader(const VkShaderModuleCreateInfo& in,ShaderRole role,ShaderDefinition& out,Refusal& why) noexcept {
    out={};why={};if(!ordinary(in,VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,why))return false;
    if(role!=ShaderRole::Vertex && role!=ShaderRole::Fragment)return refuse(why,Reason::ShaderProfile);
    const std::size_t expected=role==ShaderRole::Vertex?9460:6620;
    if(in.flags || in.codeSize!=expected || !in.pCode || in.pCode[0]!=0x07230203u)
        return refuse(why,Reason::ShaderProfile);
    frame_receiver::Sha256 hash;hash.update(in.pCode,in.codeSize);out.sha256=hash.finish();
    const char* pin=role==ShaderRole::Vertex?"94abc0c24124e8b4a301e7f1df5a4c8c0e8b0eb50e4c6ea78f3521370a6897c5":
        "f28dd5d22a570cbee64057da03d0418b80aee88a8eeed988b2f776e8d9a513f2";
    if(!digestMatches(out.sha256,pin))return refuse(why,Reason::ShaderPin);
    out.bytes=static_cast<std::uint32_t>(in.codeSize);std::memcpy(out.words.data(),in.pCode,in.codeSize);out.complete=true;return true;
}

struct GraphicsView {
    // Call-duration view only: rebuild after moving either the recipe or view.
    VkGraphicsPipelineCreateInfo info{};std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    VkPipelineVertexInputStateCreateInfo vertex{};VkPipelineInputAssemblyStateCreateInfo assembly{};
    VkPipelineViewportStateCreateInfo viewport{};VkPipelineRasterizationStateCreateInfo raster{};
    VkPipelineMultisampleStateCreateInfo multisample{};VkPipelineDepthStencilStateCreateInfo depth{};
    VkPipelineColorBlendStateCreateInfo blend{};VkPipelineDynamicStateCreateInfo dynamic{};
};
struct OwnedObjects {
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;VkRenderPass renderPass=VK_NULL_HANDLE;
    // This is an explicit caller obligation, never inferred from handle values.
    bool createdAndOwnedByCaller=false;
};
struct GraphicsDefinition {
    VkGraphicsPipelineCreateInfo info{};std::array<VkPipelineShaderStageCreateInfo,2> stageInfo{};
    std::array<std::array<char,16>,2> entries{};
    VkPipelineVertexInputStateCreateInfo vertex{};VkPipelineInputAssemblyStateCreateInfo assembly{};
    VkPipelineViewportStateCreateInfo viewport{};VkPipelineRasterizationStateCreateInfo raster{};
    VkPipelineMultisampleStateCreateInfo multisample{};VkPipelineDepthStencilStateCreateInfo depth{};
    VkPipelineColorBlendStateCreateInfo blend{};VkPipelineDynamicStateCreateInfo dynamic{};
    std::array<VkVertexInputBindingDescription,MaxVertexBindings> vertexBindings{};
    std::array<VkVertexInputAttributeDescription,MaxAttributes> attributes{};
    std::array<VkViewport,MaxViewports> viewports{};std::array<VkRect2D,MaxViewports> scissors{};
    std::array<VkPipelineColorBlendAttachmentState,MaxAttachments> colors{};
    std::array<VkDynamicState,MaxDynamics> dynamics{};std::array<VkSampleMask,2> sampleMask{};
    bool hasMask=false,hasDepth=false,hasDynamic=false,hasViewports=false,hasScissors=false,complete=false;
    bool view(const OwnedObjects& objects,GraphicsView& out,Refusal& why) const noexcept {
        out={};why={};
        if(!complete || !objects.createdAndOwnedByCaller || !objects.vertex || !objects.fragment || !objects.layout || !objects.renderPass)
            return refuse(why,Reason::OwnedObjectsMissing);
        out.info=info;out.stages=stageInfo;
        for(std::size_t i=0;i<2;++i){out.stages[i].pName=entries[i].data();out.stages[i].module=out.stages[i].stage==VK_SHADER_STAGE_VERTEX_BIT?objects.vertex:objects.fragment;}
        out.vertex=vertex;out.vertex.pVertexBindingDescriptions=vertex.vertexBindingDescriptionCount?vertexBindings.data():nullptr;
        out.vertex.pVertexAttributeDescriptions=vertex.vertexAttributeDescriptionCount?attributes.data():nullptr;
        out.assembly=assembly;out.viewport=viewport;out.viewport.pViewports=hasViewports?viewports.data():nullptr;out.viewport.pScissors=hasScissors?scissors.data():nullptr;
        out.raster=raster;out.multisample=multisample;out.multisample.pSampleMask=hasMask?sampleMask.data():nullptr;
        out.depth=depth;out.blend=blend;out.blend.pAttachments=blend.attachmentCount?colors.data():nullptr;
        out.dynamic=dynamic;out.dynamic.pDynamicStates=dynamic.dynamicStateCount?dynamics.data():nullptr;
        out.info.pStages=out.stages.data();out.info.pVertexInputState=&out.vertex;out.info.pInputAssemblyState=&out.assembly;
        out.info.pViewportState=&out.viewport;out.info.pRasterizationState=&out.raster;out.info.pMultisampleState=&out.multisample;
        out.info.pDepthStencilState=hasDepth?&out.depth:nullptr;out.info.pColorBlendState=&out.blend;out.info.pDynamicState=hasDynamic?&out.dynamic:nullptr;
        out.info.layout=objects.layout;out.info.renderPass=objects.renderPass;return true;
    }
};
inline bool knownDynamic(VkDynamicState value) noexcept {
    return value==VK_DYNAMIC_STATE_VIEWPORT || value==VK_DYNAMIC_STATE_SCISSOR || value==VK_DYNAMIC_STATE_LINE_WIDTH ||
        value==VK_DYNAMIC_STATE_DEPTH_BIAS || value==VK_DYNAMIC_STATE_BLEND_CONSTANTS || value==VK_DYNAMIC_STATE_DEPTH_BOUNDS ||
        value==VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK || value==VK_DYNAMIC_STATE_STENCIL_WRITE_MASK || value==VK_DYNAMIC_STATE_STENCIL_REFERENCE;
}
inline bool exactEntry(const char* source,const char* expected) noexcept {
    if(!source)return false;
    for(std::size_t i=0;i<16;++i){if(source[i]!=expected[i])return false;if(!expected[i])return true;}return false;
}
inline bool captureGraphics(const VkGraphicsPipelineCreateInfo& in,VkPipelineLayout resolvedLayout,
        VkRenderPass resolvedRenderPass,VkShaderModule vertexModule,VkShaderModule fragmentModule,
        const RenderPassDefinition& pass,GraphicsDefinition& out,Refusal& why) noexcept {
    out={};why={};if(in.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)return refuse(why,Reason::Type);
    if(in.pNext)return refuse(why,Reason::PipelineExtension);
    // basePipelineIndex is ignored when DERIVATIVE is absent; preserve its
    // scalar value (including zero-initialized native CIs), never invent flags.
    if(in.flags || in.basePipelineHandle)return refuse(why,Reason::PipelineFlags);
    if(!resolvedLayout || !resolvedRenderPass || in.layout!=resolvedLayout || in.renderPass!=resolvedRenderPass)
        return refuse(why,Reason::Identity);
    if(!pass.complete || in.subpass!=0)return refuse(why,Reason::RenderPassMissing);
    if(in.stageCount!=2 || !in.pStages || !vertexModule || !fragmentModule)return refuse(why,Reason::Stage);
    bool vertexFound=false,fragmentFound=false;
    for(std::size_t i=0;i<2;++i){
        const auto& stage=in.pStages[i];
        if(!ordinary(stage,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,why))return false;
        const bool vertex=stage.stage==VK_SHADER_STAGE_VERTEX_BIT;
        if(stage.flags || stage.pSpecializationInfo || (vertex?(vertexFound || stage.module!=vertexModule):
                (stage.stage!=VK_SHADER_STAGE_FRAGMENT_BIT || fragmentFound || stage.module!=fragmentModule)) ||
           !exactEntry(stage.pName,vertex?"VS_DISTORT":"PS_DISTORT"))return refuse(why,Reason::Stage,static_cast<std::uint32_t>(i));
        vertexFound|=vertex;fragmentFound|=!vertex;
        out.stageInfo[i]=stage;out.stageInfo[i].module=VK_NULL_HANDLE;out.stageInfo[i].pName=nullptr;
        std::memcpy(out.entries[i].data(),vertex?"VS_DISTORT":"PS_DISTORT",11);
    }
#define STATE(field,type,member) if(!ordinary(*in.field,type,why))return false;out.member=*in.field;
    // Reject unsupported dynamic routes before touching any static state that
    // such a route could make ignored. Ignored nonnull pointers are not readable
    // input arrays merely because an application supplied a pointer value.
    bool dynamicViewport=false,dynamicScissor=false;
    if(in.pDynamicState){
        STATE(pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,dynamic)
        out.dynamic.pDynamicStates=nullptr;out.hasDynamic=true;
        if(out.dynamic.flags || !copyArray(in.pDynamicState->pDynamicStates,out.dynamic.dynamicStateCount,out.dynamics,why))return refuse(why,Reason::Bounds);
        for(std::uint32_t i=0;i<out.dynamic.dynamicStateCount;++i){
            if(!knownDynamic(out.dynamics[i]))return refuse(why,Reason::DynamicState,i);
            for(std::uint32_t j=0;j<i;++j)if(out.dynamics[i]==out.dynamics[j])return refuse(why,Reason::DynamicState,i);
            dynamicViewport|=out.dynamics[i]==VK_DYNAMIC_STATE_VIEWPORT;dynamicScissor|=out.dynamics[i]==VK_DYNAMIC_STATE_SCISSOR;
        }
    }
    if(!in.pRasterizationState)return refuse(why,Reason::NullArray);
    STATE(pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,raster)
    if(out.raster.flags || !boolean(out.raster.depthClampEnable) || !boolean(out.raster.rasterizerDiscardEnable) ||
       !boolean(out.raster.depthBiasEnable) || out.raster.rasterizerDiscardEnable || !std::isfinite(out.raster.lineWidth) || out.raster.lineWidth<=0 ||
       !std::isfinite(out.raster.depthBiasConstantFactor) || !std::isfinite(out.raster.depthBiasClamp) || !std::isfinite(out.raster.depthBiasSlopeFactor))
        return refuse(why,Reason::Scalar);
    if(!in.pVertexInputState || !in.pInputAssemblyState || !in.pViewportState || !in.pMultisampleState || !in.pColorBlendState)
        return refuse(why,Reason::NullArray);
    if(in.pTessellationState)return refuse(why,Reason::Stage);
    STATE(pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,vertex)
    out.vertex.pVertexBindingDescriptions=nullptr;out.vertex.pVertexAttributeDescriptions=nullptr;
    if(out.vertex.flags || !copyArray(in.pVertexInputState->pVertexBindingDescriptions,out.vertex.vertexBindingDescriptionCount,out.vertexBindings,why) ||
       !copyArray(in.pVertexInputState->pVertexAttributeDescriptions,out.vertex.vertexAttributeDescriptionCount,out.attributes,why))return refuse(why,Reason::Bounds);
    STATE(pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,assembly)
    if(out.assembly.flags || !boolean(out.assembly.primitiveRestartEnable))return refuse(why,Reason::Scalar);
    STATE(pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,viewport)
    out.viewport.pViewports=nullptr;out.viewport.pScissors=nullptr;
    if(out.viewport.flags || !out.viewport.viewportCount || out.viewport.viewportCount!=out.viewport.scissorCount || out.viewport.viewportCount>MaxViewports)return refuse(why,Reason::Bounds);
    out.hasViewports=!dynamicViewport;out.hasScissors=!dynamicScissor;
    if(out.hasViewports && !copyArray(in.pViewportState->pViewports,out.viewport.viewportCount,out.viewports,why))return false;
    if(out.hasScissors && !copyArray(in.pViewportState->pScissors,out.viewport.scissorCount,out.scissors,why))return false;
    if(out.hasViewports)for(std::uint32_t i=0;i<out.viewport.viewportCount;++i){const auto& v=out.viewports[i];
        if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.width) || !std::isfinite(v.height) ||
           !std::isfinite(v.minDepth) || !std::isfinite(v.maxDepth) || v.width<=0 || v.height==0)
            return refuse(why,Reason::Scalar,i);
    }
    STATE(pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,multisample)
    out.multisample.pSampleMask=nullptr;out.hasMask=in.pMultisampleState->pSampleMask!=nullptr;
    if(out.multisample.flags || !sampleCount(out.multisample.rasterizationSamples) || !boolean(out.multisample.sampleShadingEnable) ||
       !boolean(out.multisample.alphaToCoverageEnable) || !boolean(out.multisample.alphaToOneEnable) ||
       !std::isfinite(out.multisample.minSampleShading) || out.multisample.minSampleShading<0 || out.multisample.minSampleShading>1)return refuse(why,Reason::Scalar);
    if(out.hasMask){const auto count=(static_cast<unsigned>(out.multisample.rasterizationSamples)+31)/32;
        for(unsigned i=0;i<count;++i)out.sampleMask[i]=in.pMultisampleState->pSampleMask[i];}
    if(pass.hasDepth && in.pDepthStencilState){
        STATE(pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,depth)
        if(out.depth.flags || !boolean(out.depth.depthTestEnable) || !boolean(out.depth.depthWriteEnable) ||
           !boolean(out.depth.depthBoundsTestEnable) || !boolean(out.depth.stencilTestEnable) ||
           !std::isfinite(out.depth.minDepthBounds) || !std::isfinite(out.depth.maxDepthBounds))return refuse(why,Reason::Scalar);
        out.hasDepth=true;
    }
    if(pass.hasDepth && !out.hasDepth)return refuse(why,Reason::NullArray);
    STATE(pColorBlendState,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,blend)
    out.blend.pAttachments=nullptr;
    if(out.blend.flags || !boolean(out.blend.logicOpEnable) || out.blend.attachmentCount!=pass.colorCount ||
       !copyArray(in.pColorBlendState->pAttachments,out.blend.attachmentCount,out.colors,why))return refuse(why,Reason::Bounds);
    for(std::uint32_t i=0;i<out.blend.attachmentCount;++i)if(!boolean(out.colors[i].blendEnable))return refuse(why,Reason::Scalar,i);
    for(float value:out.blend.blendConstants)if(!std::isfinite(value))return refuse(why,Reason::Scalar);
#undef STATE
    if((!out.hasViewports && !dynamicViewport) || (!out.hasScissors && !dynamicScissor))return refuse(why,Reason::NullArray);
    out.info=in;out.info.pNext=nullptr;out.info.pStages=nullptr;out.info.pVertexInputState=nullptr;out.info.pInputAssemblyState=nullptr;
    out.info.pTessellationState=nullptr;out.info.pViewportState=nullptr;out.info.pRasterizationState=nullptr;
    out.info.pMultisampleState=nullptr;out.info.pDepthStencilState=nullptr;out.info.pColorBlendState=nullptr;out.info.pDynamicState=nullptr;
    out.info.layout=VK_NULL_HANDLE;out.info.renderPass=VK_NULL_HANDLE;out.complete=true;return true;
}

struct Identity {std::uint64_t device=0,value=0,generation=0;};
struct Inputs {
    std::uint64_t device=0;bool deviceCreateSucceeded=false;
    const VkDeviceCreateInfo* deviceCreate=nullptr;const VkDescriptorSetLayoutCreateInfo* set0=nullptr;
    const VkPipelineLayoutCreateInfo* layout=nullptr;const VkRenderPassCreateInfo* renderPass=nullptr;
    const VkShaderModuleCreateInfo* vertex=nullptr;const VkShaderModuleCreateInfo* fragment=nullptr;
    const VkGraphicsPipelineCreateInfo* graphics=nullptr;
    Identity set0Identity{},layoutIdentity{},renderPassIdentity{},vertexIdentity{},fragmentIdentity{},pipelineIdentity{};
    ProbeInterface probeInterface{};
};
// Call-duration view with a self-pointer; recreate after copying/moving it.
struct LayoutView {VkPipelineLayoutCreateInfo info{};std::array<VkDescriptorSetLayout,2> sets{};};
struct Recipe {
    FeatureFacts feature;Set0Definition set0;LayoutDefinition layout;RenderPassDefinition renderPass;
    ShaderDefinition vertex,fragment;GraphicsDefinition graphics;
    std::array<Identity,6> provenance{}; // Scalar diagnostic history, never build handles.
    bool definitionComplete=false,nativeCompatibilityProven=false,nativeLifetimeProven=false,runtimeActivationAuthorized=false;
    bool layoutView(VkDescriptorSetLayout ownedSet0,VkDescriptorSetLayout ownedSet1,LayoutView& out) const noexcept {
        out={};if(!definitionComplete || !ownedSet0 || !ownedSet1)return false;
        out.sets={ownedSet0,ownedSet1};out.info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        out.info.setLayoutCount=2;out.info.pSetLayouts=out.sets.data();out.info.pushConstantRangeCount=layout.count;
        out.info.pPushConstantRanges=layout.count?layout.ranges.data():nullptr;return true;
    }
};
static_assert(sizeof(Recipe)<=65536,"bounded own cold recipe");
static_assert(std::is_trivially_copyable_v<Recipe>,"recipe has no borrowed native pointer ownership");
inline bool capture(const Inputs& in,Recipe& out,Refusal& why) noexcept {
    out={};why={};
    if(!in.deviceCreate || !in.set0 || !in.layout || !in.vertex || !in.fragment || !in.graphics)
        return refuse(why,Reason::NullArray);
    const std::array<Identity,6> identities{in.set0Identity,in.layoutIdentity,in.renderPassIdentity,
        in.vertexIdentity,in.fragmentIdentity,in.pipelineIdentity};
    if(!in.device)return refuse(why,Reason::Identity);
    for(std::size_t i=0;i<identities.size();++i)if(identities[i].device!=in.device || !identities[i].value || !identities[i].generation)
        return refuse(why,Reason::Identity,static_cast<std::uint32_t>(i));
    VkDescriptorSetLayout set0{};VkPipelineLayout layout{};VkRenderPass pass{};VkShaderModule vs{},ps{};
    std::memcpy(&set0,&in.set0Identity.value,sizeof(set0));std::memcpy(&layout,&in.layoutIdentity.value,sizeof(layout));
    std::memcpy(&pass,&in.renderPassIdentity.value,sizeof(pass));std::memcpy(&vs,&in.vertexIdentity.value,sizeof(vs));std::memcpy(&ps,&in.fragmentIdentity.value,sizeof(ps));
    if(!enabledFeature(*in.deviceCreate,in.deviceCreateSucceeded,out.feature,why) ||
       !captureSet0(*in.set0,out.set0,why,in.probeInterface) || !captureLayout(*in.layout,set0,out.layout,why) ||
       !captureRenderPass(in.renderPass,out.renderPass,why) || !captureShader(*in.vertex,ShaderRole::Vertex,out.vertex,why) ||
       !captureShader(*in.fragment,ShaderRole::Fragment,out.fragment,why) ||
       !captureGraphics(*in.graphics,layout,pass,vs,ps,out.renderPass,out.graphics,why))return false;
    out.provenance=identities;out.definitionComplete=true;return true;
}
} // namespace frame_pixel_ssbo_clone
