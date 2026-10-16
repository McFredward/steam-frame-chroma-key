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
constexpr std::size_t MaxGraphics=8,MaxGraphicsDynamics=64,GraphicsStateCount=12;
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
enum class GraphicsState : std::uint32_t {Pipeline,VertexStage,FragmentStage,Dynamic,VertexInput,Assembly,Tessellation,Viewport,Rasterization,Multisample,DepthStencil,ColorBlend};
enum class GraphicsRead : std::uint32_t {NotObserved,Complete,Ignored,UnknownScope,UnsupportedPrefix,Type,Bounds,NullArray};
struct GraphicsScope {bool known=false;std::uint32_t colorCount=0;bool hasDepth=false;};
struct GraphicsStateObservation {
    std::uint32_t type=0,flags=0;std::array<std::uint32_t,2> counts{};
    std::array<std::uint32_t,MaxChain> chainTypes{};std::uint32_t chainCount=0;
    ChainEnd chainEnd=ChainEnd::NotObserved;GraphicsRead read=GraphicsRead::NotObserved;
    bool present=false;
};
struct GraphicsObservation {
    std::uint64_t sequence=0,pipeline=0,layout=0,renderPass=0;std::uint32_t subpass=0;
    GraphicsScope scope{};std::array<std::uint32_t,MaxGraphicsDynamics> dynamics{};
    std::uint32_t dynamicCount=0;GraphicsRead dynamicRead=GraphicsRead::NotObserved;
    std::array<GraphicsStateObservation,GraphicsStateCount> states{};
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
    std::array<GraphicsObservation,MaxGraphics> graphics{};std::uint32_t graphicsCount=0;
    std::uint64_t graphicsSequence=0;bool graphicsOverflow=false;
};
static_assert(sizeof(Facts)<32768,"fixed, small public metadata budget");
inline const char* chainEndName(ChainEnd end) noexcept {
    switch(end){case ChainEnd::NotObserved:return "not_observed";case ChainEnd::Complete:return "complete";
        case ChainEnd::Cycle:return "cycle";case ChainEnd::Bounds:return "bounds";}
    return "invalid";
}
inline const char* graphicsReadName(GraphicsRead read) noexcept {
    switch(read){case GraphicsRead::NotObserved:return "not_observed";case GraphicsRead::Complete:return "complete";
    case GraphicsRead::Ignored:return "ignored";case GraphicsRead::UnknownScope:return "unknown_scope";
    case GraphicsRead::UnsupportedPrefix:return "unsupported_prefix";case GraphicsRead::Type:return "type";
    case GraphicsRead::Bounds:return "bounds";case GraphicsRead::NullArray:return "null_array";}return "invalid";
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
inline bool graphicsEntry(const char* text,const char* expected) noexcept {
    if(!text)return false;
    for(unsigned i=0;i<16;++i){if(text[i]!=expected[i])return false;if(!expected[i])return true;}return false;
}
inline void graphicsState(GraphicsStateObservation& out,const void* chain) noexcept {
    Creation copied;creationChain(copied,chain);out.chainTypes=copied.chainTypes;
    out.chainCount=copied.chainCount;out.chainEnd=copied.chainEnd;
}
// Only successful ordinary complete VS/PS creates permit these subordinate
// reads. Scope is copied from the retained actual selected subpass; an absent
// scope never permits depth/blend pointer reads. It is observation, not a lease
// or a replacement for strict graphics admission.
inline void graphicsDetails(Facts& out,const VkGraphicsPipelineCreateInfo& ci,VkResult result,
                            std::uint64_t object,GraphicsScope scope={}) noexcept {
    if(result!=VK_SUCCESS||!out.deviceObserved||ci.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||
       ci.flags||ci.pNext||ci.stageCount!=2||!ci.pStages)return;
    const VkPipelineShaderStageCreateInfo *vertex=nullptr,*fragment=nullptr;
    for(unsigned i=0;i<2;++i){const auto& stage=ci.pStages[i];
        if(stage.sType!=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)return;
        if(stage.stage==VK_SHADER_STAGE_VERTEX_BIT&&!vertex&&graphicsEntry(stage.pName,"VS_DISTORT"))vertex=&stage;
        else if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT&&!fragment&&graphicsEntry(stage.pName,"PS_DISTORT"))fragment=&stage;
        else return;
    }
    if(!vertex||!fragment)return;
    if(out.graphicsSequence!=UINT64_MAX)++out.graphicsSequence;
    if(out.graphicsCount==MaxGraphics){out.graphicsOverflow=true;return;}
    auto& captured=out.graphics[out.graphicsCount++];captured.sequence=out.graphicsSequence;
    captured.pipeline=object;captured.layout=rawHandle(ci.layout);captured.renderPass=rawHandle(ci.renderPass);captured.subpass=ci.subpass;captured.scope=scope;
    const auto stage=[&](GraphicsState which,const VkPipelineShaderStageCreateInfo& in){auto& state=captured.states[static_cast<unsigned>(which)];state.present=true;state.type=in.sType;state.flags=in.flags;state.counts={in.pSpecializationInfo!=nullptr,in.module!=VK_NULL_HANDLE};state.read=GraphicsRead::Complete;graphicsState(state,in.pNext);};
    stage(GraphicsState::VertexStage,*vertex);stage(GraphicsState::FragmentStage,*fragment);
    auto& pipeline=captured.states[static_cast<unsigned>(GraphicsState::Pipeline)];pipeline.present=true;pipeline.type=ci.sType;pipeline.flags=ci.flags;pipeline.counts={ci.stageCount,ci.subpass};pipeline.read=GraphicsRead::Complete;pipeline.chainEnd=ChainEnd::Complete;
    if(ci.pDynamicState){const auto& in=*ci.pDynamicState;auto& state=captured.states[static_cast<unsigned>(GraphicsState::Dynamic)];state.present=true;state.type=in.sType;
        if(in.sType!=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO){state.read=captured.dynamicRead=GraphicsRead::Type;return;}
        state.flags=in.flags;state.counts[0]=in.dynamicStateCount;graphicsState(state,in.pNext);
        if(in.flags||in.pNext){state.read=captured.dynamicRead=GraphicsRead::UnsupportedPrefix;return;}
        if(in.dynamicStateCount>MaxGraphicsDynamics){state.read=captured.dynamicRead=GraphicsRead::Bounds;return;}
        if(in.dynamicStateCount&&!in.pDynamicStates){state.read=captured.dynamicRead=GraphicsRead::NullArray;return;}
        for(unsigned i=0;i<in.dynamicStateCount;++i)captured.dynamics[captured.dynamicCount++]=static_cast<std::uint32_t>(in.pDynamicStates[i]);
        state.read=GraphicsRead::Complete;
    }
    captured.dynamicRead=GraphicsRead::Complete;
    const auto dynamic=[&](VkDynamicState value){for(unsigned i=0;i<captured.dynamicCount;++i)if(captured.dynamics[i]==static_cast<std::uint32_t>(value))return true;return false;};
    // An ignored entire state pointer may be nonnull without denoting readable
    // memory. Retain its presence, never its address or pointed-to bytes.
    const auto ignored=[&](GraphicsState which,const void* pointer,GraphicsRead why=GraphicsRead::Ignored){auto& state=captured.states[static_cast<unsigned>(which)];state.present=pointer!=nullptr;state.read=why;};
    const auto observe=[&](GraphicsState which,const auto* input,VkStructureType expected,std::uint32_t first=0,std::uint32_t second=0){auto& state=captured.states[static_cast<unsigned>(which)];state.present=input!=nullptr;if(!input)return;
        state.type=input->sType;if(input->sType!=expected){state.read=GraphicsRead::Type;return;}state.flags=input->flags;state.counts={first,second};state.read=GraphicsRead::Complete;graphicsState(state,input->pNext);};
    if(dynamic(VK_DYNAMIC_STATE_VERTEX_INPUT_EXT))ignored(GraphicsState::VertexInput,ci.pVertexInputState);
    else if(ci.pVertexInputState)observe(GraphicsState::VertexInput,ci.pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,ci.pVertexInputState->vertexBindingDescriptionCount,ci.pVertexInputState->vertexAttributeDescriptionCount);
    observe(GraphicsState::Assembly,ci.pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ignored(GraphicsState::Tessellation,ci.pTessellationState); // Exact VS/FS route has no tessellation stages.
    observe(GraphicsState::Rasterization,ci.pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    const bool discard=ci.pRasterizationState&&ci.pRasterizationState->sType==VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO&&!dynamic(VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)&&ci.pRasterizationState->rasterizerDiscardEnable==VK_TRUE;
    if(discard){ignored(GraphicsState::Viewport,ci.pViewportState);ignored(GraphicsState::Multisample,ci.pMultisampleState);ignored(GraphicsState::DepthStencil,ci.pDepthStencilState);ignored(GraphicsState::ColorBlend,ci.pColorBlendState);return;}
    if(ci.pViewportState)observe(GraphicsState::Viewport,ci.pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,ci.pViewportState->viewportCount,ci.pViewportState->scissorCount);
    if(ci.pMultisampleState)observe(GraphicsState::Multisample,ci.pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,static_cast<std::uint32_t>(ci.pMultisampleState->rasterizationSamples),ci.pMultisampleState->pSampleMask!=nullptr);
    if(!scope.known){ignored(GraphicsState::DepthStencil,ci.pDepthStencilState,GraphicsRead::UnknownScope);ignored(GraphicsState::ColorBlend,ci.pColorBlendState,GraphicsRead::UnknownScope);return;}
    if(scope.hasDepth)observe(GraphicsState::DepthStencil,ci.pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    else ignored(GraphicsState::DepthStencil,ci.pDepthStencilState);
    if(scope.colorCount){if(ci.pColorBlendState)observe(GraphicsState::ColorBlend,ci.pColorBlendState,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,ci.pColorBlendState->attachmentCount);}
    else ignored(GraphicsState::ColorBlend,ci.pColorBlendState);
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
