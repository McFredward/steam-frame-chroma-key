#pragma once

// Bounded, copied public creation metadata. These observations are deliberately
// independent of clone eligibility: an unsupported profile must explain itself
// without authorizing a pipeline or hiding all the remaining native inputs.
// The caller owns synchronization and invokes this only on cold callbacks.
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_pixel_definition_facts {
constexpr std::size_t MaxChain=32,MaxBindings=32,MaxSets=4;
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
struct Facts {
    std::array<std::uint32_t,MaxChain> chainTypes{};
    std::uint32_t chainCount=0;
    ChainEnd chainEnd=ChainEnd::NotObserved;
    std::array<Set,MaxSets> sets{};
    std::uint32_t setCount=0,setSkipReasons=0;
    bool deviceObserved=false;
};
static_assert(sizeof(Facts)<4096,"fixed, small public metadata budget");
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
} // namespace frame_pixel_definition_facts
