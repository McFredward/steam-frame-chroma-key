#pragma once

// Bounded, copied public creation metadata. These observations are deliberately
// independent of clone eligibility: an unsupported profile must explain itself
// without authorizing a pipeline or hiding all the remaining native inputs.
// The caller owns synchronization and invokes this only on cold callbacks.
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace frame_pixel_definition_facts {
constexpr std::size_t MaxChain=32,MaxBindings=32,MaxSets=4,MaxCreations=16;
enum class ChainEnd : std::uint8_t {NotObserved,Complete,Cycle,Bounds};
enum class SetSkip : std::uint32_t {Shape=1,Extension=2,Flags=4,Bounds=8,NullArray=16,Capacity=32};
struct Binding {
    std::uint32_t binding=0,type=0,count=0,stages=0;
    bool immutableSampler=false;
};
struct Set {
    std::array<Binding,MaxBindings> bindings{};
    std::uint32_t count=0;
};
enum class CreationKind : std::uint32_t {Graphics,RenderPass,RenderPass2};
struct Creation {
    CreationKind kind=CreationKind::Graphics;
    std::uint32_t route=0,flags=0;
    std::array<std::uint32_t,4> counts{};
    std::int32_t baseIndex=0;bool baseHandle=false;
    std::array<std::uint32_t,MaxChain> chainTypes{};std::uint32_t chainCount=0;
    ChainEnd chainEnd=ChainEnd::NotObserved;
};
struct CreationObservation {
    Creation shape{};
    std::uint64_t firstObject=0,lastObject=0,firstLayout=0,lastLayout=0,firstPass=0,lastPass=0,count=0;
};
struct Facts {
    std::array<std::uint32_t,MaxChain> chainTypes{};
    std::uint32_t chainCount=0;
    ChainEnd chainEnd=ChainEnd::NotObserved;
    std::array<Set,MaxSets> sets{};
    std::uint32_t setCount=0,setSkipReasons=0;
    bool deviceObserved=false;
    std::array<CreationObservation,MaxCreations> creations{};std::uint32_t creationCount=0;
    bool creationOverflow=false;
};
static_assert(sizeof(Facts)<16384,"fixed, small public metadata budget");
inline const char* chainEndName(ChainEnd end) noexcept {
    switch(end){case ChainEnd::NotObserved:return "not_observed";case ChainEnd::Complete:return "complete";
        case ChainEnd::Cycle:return "cycle";case ChainEnd::Bounds:return "bounds";}
    return "invalid";
}
inline void device(Facts& out,const VkDeviceCreateInfo& ci,VkResult result) noexcept {
    // Failed creates and repeated observations cannot replace a successful fact.
    if(result!=VK_SUCCESS || out.deviceObserved || ci.sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)return;
    out.deviceObserved=true;
    std::array<const VkBaseInStructure*,MaxChain> seen{};
    for(auto* node=static_cast<const VkBaseInStructure*>(ci.pNext);node;node=node->pNext){
        for(std::uint32_t i=0;i<out.chainCount;++i)if(seen[i]==node){out.chainEnd=ChainEnd::Cycle;return;}
        if(out.chainCount==MaxChain){out.chainEnd=ChainEnd::Bounds;return;}
        // Unknown types are recorded only as headers, never cast to an assumed
        // payload. Their presence grants no feature or clone admission.
        seen[out.chainCount]=node;out.chainTypes[out.chainCount++]=static_cast<std::uint32_t>(node->sType);
    }
    out.chainEnd=ChainEnd::Complete;
}
inline bool same(const Set& a,const Set& b) noexcept {
    if(a.count!=b.count)return false;
    for(std::uint32_t i=0;i<a.count;++i){const auto& x=a.bindings[i];const auto& y=b.bindings[i];
        if(x.binding!=y.binding || x.type!=y.type || x.count!=y.count || x.stages!=y.stages || x.immutableSampler!=y.immutableSampler)return false;
    }
    return true;
}
inline void set(Facts& out,const VkDescriptorSetLayoutCreateInfo& ci,VkResult result) noexcept {
    if(result!=VK_SUCCESS || !out.deviceObserved)return;
    const auto skip=[&](SetSkip reason) noexcept {out.setSkipReasons|=static_cast<std::uint32_t>(reason);};
    // Unknown flags/extension payloads may change whether arrays are meaningful.
    // Preserve a reason without reading those potentially ignored arrays.
    if(ci.sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO){skip(SetSkip::Shape);return;}
    if(ci.pNext){skip(SetSkip::Extension);return;}
    if(ci.flags){skip(SetSkip::Flags);return;}
    if(ci.bindingCount>MaxBindings){skip(SetSkip::Bounds);return;}
    if(ci.bindingCount && !ci.pBindings){skip(SetSkip::NullArray);return;}
    Set value;value.count=ci.bindingCount;bool has9=false;
    for(std::uint32_t i=0;i<ci.bindingCount;++i){const auto& in=ci.pBindings[i];auto& binding=value.bindings[i];
        binding={in.binding,static_cast<std::uint32_t>(in.descriptorType),in.descriptorCount,in.stageFlags,in.pImmutableSamplers!=nullptr};
        has9=has9 || in.binding==9;
        // A sampler pointer is recorded as a boolean only; never followed or
        // retained. Count1, arrays and unexpected types remain facts, not recipes.
    }
    if(!has9)return;
    for(std::uint32_t i=0;i<out.setCount;++i)if(same(out.sets[i],value))return;
    if(out.setCount==MaxSets){skip(SetSkip::Capacity);return;}
    out.sets[out.setCount++]=value;
}
// These are copied top-level successful creation inputs, never admission.
// No stage, attachment, dynamic-state or other possibly ignored subordinate
// pointer is followed. An unknown pNext payload contributes only its header.
inline void creationChain(Creation& out,const void* input) noexcept {
    std::array<const VkBaseInStructure*,MaxChain> seen{};
    for(auto* node=static_cast<const VkBaseInStructure*>(input);node;node=node->pNext){
        for(std::uint32_t i=0;i<out.chainCount;++i)if(seen[i]==node){out.chainEnd=ChainEnd::Cycle;return;}
        if(out.chainCount==MaxChain){out.chainEnd=ChainEnd::Bounds;return;}
        seen[out.chainCount]=node;out.chainTypes[out.chainCount++]=static_cast<std::uint32_t>(node->sType);
    }out.chainEnd=ChainEnd::Complete;
}
inline bool same(const Creation& a,const Creation& b) noexcept {
    if(a.kind!=b.kind||a.route!=b.route||a.flags!=b.flags||a.counts!=b.counts||a.baseIndex!=b.baseIndex||a.baseHandle!=b.baseHandle||a.chainCount!=b.chainCount||a.chainEnd!=b.chainEnd)return false;
    for(std::uint32_t i=0;i<a.chainCount;++i)if(a.chainTypes[i]!=b.chainTypes[i])return false;
    return true;
}
inline void creation(Facts& out,const Creation& shape,std::uint64_t object,std::uint64_t layout,std::uint64_t pass) noexcept {
    for(std::uint32_t i=0;i<out.creationCount;++i)if(same(out.creations[i].shape,shape)){
        auto& seen=out.creations[i];seen.lastObject=object;seen.lastLayout=layout;seen.lastPass=pass;
        if(seen.count!=UINT64_MAX)++seen.count;
        return;
    }
    if(out.creationCount==MaxCreations){out.creationOverflow=true;return;}
    out.creations[out.creationCount++]={shape,object,object,layout,layout,pass,pass,1};
}
template<class T>inline std::uint64_t rawHandle(T value) noexcept {
    if constexpr(std::is_pointer_v<T>)return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
    else return static_cast<std::uint64_t>(value);
}
inline void graphics(Facts& out,const VkGraphicsPipelineCreateInfo& ci,VkResult result,std::uint64_t object=0) noexcept {
    if(result!=VK_SUCCESS||!out.deviceObserved||ci.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)return;
    Creation shape;shape.kind=CreationKind::Graphics;shape.route=ci.sType;shape.flags=ci.flags;shape.counts[0]=ci.stageCount;shape.counts[1]=ci.subpass;shape.baseIndex=ci.basePipelineIndex;shape.baseHandle=ci.basePipelineHandle!=VK_NULL_HANDLE;creationChain(shape,ci.pNext);
    creation(out,shape,object,rawHandle(ci.layout),rawHandle(ci.renderPass));
}
inline void renderPass(Facts& out,const VkRenderPassCreateInfo& ci,VkResult result,std::uint64_t object=0) noexcept {
    if(result!=VK_SUCCESS||!out.deviceObserved||ci.sType!=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)return;
    Creation shape;shape.kind=CreationKind::RenderPass;shape.route=ci.sType;shape.flags=ci.flags;shape.counts={ci.attachmentCount,ci.subpassCount,ci.dependencyCount,0};creationChain(shape,ci.pNext);creation(out,shape,object,0,object);
}
inline void renderPass2(Facts& out,const VkRenderPassCreateInfo2& ci,VkResult result,std::uint64_t object=0) noexcept {
    if(result!=VK_SUCCESS||!out.deviceObserved||ci.sType!=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2)return;
    Creation shape;shape.kind=CreationKind::RenderPass2;shape.route=ci.sType;shape.flags=ci.flags;shape.counts={ci.attachmentCount,ci.subpassCount,ci.dependencyCount,ci.correlatedViewMaskCount};creationChain(shape,ci.pNext);creation(out,shape,object,0,object);
}
} // namespace frame_pixel_definition_facts
