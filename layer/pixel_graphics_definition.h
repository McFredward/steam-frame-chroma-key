// This include precedes the internal guard deliberately: direct inclusion may
// recurse through the contract, which needs these definitions before Recipe.
#include "pixel_ssbo_clone_contract.h"
#ifndef GHVR_PIXEL_GRAPHICS_DEFINITION_H
#define GHVR_PIXEL_GRAPHICS_DEFINITION_H

namespace frame_pixel_ssbo_clone {
// Exact finite graphics state, rebuilt only on the cold owner thread. Dynamic
// values are not replaced or replayed: the original command stream still owns
// them when the temporary compatible pipeline is bound.
constexpr std::size_t GraphicsDynamicCapacity=MaxDynamics,GraphicsExtensionCapacity=16,GraphicsSampleCapacity=128;
enum class GraphicsParent : unsigned {Vertex,Assembly,Viewport,Raster,Multisample,Depth,Blend,Dynamic,Count};
#define GHVR_GRAPHICS_EXTENSIONS(X) \
    X(Divisor,VkPipelineVertexInputDivisorStateCreateInfoEXT,divisor,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT) \
    X(Order,VkPipelineRasterizationStateRasterizationOrderAMD,order,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD) \
    X(Stream,VkPipelineRasterizationStateStreamCreateInfoEXT,stream,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT) \
    X(Conservative,VkPipelineRasterizationConservativeStateCreateInfoEXT,conservative,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT) \
    X(DepthClip,VkPipelineRasterizationDepthClipStateCreateInfoEXT,depthClip,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT) \
    X(Provoking,VkPipelineRasterizationProvokingVertexStateCreateInfoEXT,provoking,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT) \
    X(Line,VkPipelineRasterizationLineStateCreateInfoEXT,line,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT) \
    X(DepthControl,VkPipelineViewportDepthClipControlCreateInfoEXT,depthControl,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT) \
    X(WScaling,VkPipelineViewportWScalingStateCreateInfoNV,wScaling,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_W_SCALING_STATE_CREATE_INFO_NV) \
    X(Swizzle,VkPipelineViewportSwizzleStateCreateInfoNV,swizzle,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SWIZZLE_STATE_CREATE_INFO_NV) \
    X(Exclusive,VkPipelineViewportExclusiveScissorStateCreateInfoNV,exclusive,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_EXCLUSIVE_SCISSOR_STATE_CREATE_INFO_NV) \
    X(SampleLocations,VkPipelineSampleLocationsStateCreateInfoEXT,sampleLocations,VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT) \
    X(CoverageColor,VkPipelineCoverageToColorStateCreateInfoNV,coverageColor,VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_TO_COLOR_STATE_CREATE_INFO_NV) \
    X(CoverageTable,VkPipelineCoverageModulationStateCreateInfoNV,coverageTable,VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_MODULATION_STATE_CREATE_INFO_NV) \
    X(CoverageReduction,VkPipelineCoverageReductionStateCreateInfoNV,coverageReduction,VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_REDUCTION_STATE_CREATE_INFO_NV) \
    X(Advanced,VkPipelineColorBlendAdvancedStateCreateInfoEXT,advanced,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_ADVANCED_STATE_CREATE_INFO_EXT) \
    X(ColorWrite,VkPipelineColorWriteCreateInfoEXT,colorWrite,VK_STRUCTURE_TYPE_PIPELINE_COLOR_WRITE_CREATE_INFO_EXT)
enum class GraphicsExtension : unsigned {
#define ITEM(slot,type,field,tag) slot,
GHVR_GRAPHICS_EXTENSIONS(ITEM)
#undef ITEM
    Count
};
struct GraphicsExtensions {
#define ITEM(slot,type,field,tag) type field{};
GHVR_GRAPHICS_EXTENSIONS(ITEM)
#undef ITEM
    const void* link(GraphicsExtension selected,const void* next) noexcept {
        switch(selected){
#define ITEM(slot,type,field,tag) case GraphicsExtension::slot:field.pNext=next;return &field;
GHVR_GRAPHICS_EXTENSIONS(ITEM)
#undef ITEM
        case GraphicsExtension::Count:break;}return nullptr;
    }
};
struct GraphicsView {
    // Call-duration view only: rebuild after moving either the recipe or view.
    VkGraphicsPipelineCreateInfo info{};std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    VkPipelineVertexInputStateCreateInfo vertex{};VkPipelineInputAssemblyStateCreateInfo assembly{};
    VkPipelineViewportStateCreateInfo viewport{};VkPipelineRasterizationStateCreateInfo raster{};
    VkPipelineMultisampleStateCreateInfo multisample{};VkPipelineDepthStencilStateCreateInfo depth{};
    VkPipelineColorBlendStateCreateInfo blend{};VkPipelineDynamicStateCreateInfo dynamic{};
    GraphicsExtensions extensions{};
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
    std::array<VkDynamicState,GraphicsDynamicCapacity> dynamics{};std::array<VkSampleMask,2> sampleMask{};
    GraphicsExtensions extensions{};
    std::array<std::array<GraphicsExtension,GraphicsExtensionCapacity>,static_cast<unsigned>(GraphicsParent::Count)> extensionOrder{};
    std::array<unsigned,static_cast<unsigned>(GraphicsParent::Count)> extensionCounts{};
    std::uint64_t extensionMask=0;
    std::array<VkVertexInputBindingDivisorDescriptionEXT,MaxVertexBindings> divisors{};
    std::array<VkSampleLocationEXT,GraphicsSampleCapacity> sampleLocations{};
    std::array<VkBool32,MaxAttachments> colorWrites{};
    std::array<VkViewportWScalingNV,MaxViewports> wScalings{};
    std::array<VkViewportSwizzleNV,MaxViewports> swizzles{};
    std::array<VkRect2D,MaxViewports> exclusiveScissors{};
    std::array<float,64> coverageTable{};
    bool hasLocations=false,hasColorWrites=false,hasWScalings=false,hasSwizzles=false,hasExclusiveScissors=false,hasCoverageTable=false;
    bool hasVertex=true,hasAssembly=true,hasViewport=true,hasRaster=true,hasMultisample=true,hasBlend=true;
    bool hasMask=false,hasDepth=false,hasDynamic=false,hasViewports=false,hasScissors=false,hasColors=true,complete=false;
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
        out.depth=depth;out.blend=blend;out.blend.pAttachments=hasColors&&blend.attachmentCount?colors.data():nullptr;
        out.dynamic=dynamic;out.dynamic.pDynamicStates=dynamic.dynamicStateCount?dynamics.data():nullptr;
        out.extensions=extensions;out.extensions.divisor.pVertexBindingDivisors=extensions.divisor.vertexBindingDivisorCount?divisors.data():nullptr;
        out.extensions.sampleLocations.sampleLocationsInfo.pSampleLocations=hasLocations?sampleLocations.data():nullptr;
        out.extensions.colorWrite.pColorWriteEnables=hasColorWrites?colorWrites.data():nullptr;
        out.extensions.wScaling.pViewportWScalings=hasWScalings?wScalings.data():nullptr;
        out.extensions.swizzle.pViewportSwizzles=hasSwizzles?swizzles.data():nullptr;
        out.extensions.exclusive.pExclusiveScissors=hasExclusiveScissors?exclusiveScissors.data():nullptr;
        out.extensions.coverageTable.pCoverageModulationTable=hasCoverageTable?coverageTable.data():nullptr;
        std::array<const void*,static_cast<unsigned>(GraphicsParent::Count)> roots{};
        for(unsigned parent=0;parent<roots.size();++parent)for(unsigned i=extensionCounts[parent];i;--i)roots[parent]=out.extensions.link(extensionOrder[parent][i-1],roots[parent]);
        out.vertex.pNext=roots[unsigned(GraphicsParent::Vertex)];out.assembly.pNext=roots[unsigned(GraphicsParent::Assembly)];out.viewport.pNext=roots[unsigned(GraphicsParent::Viewport)];out.raster.pNext=roots[unsigned(GraphicsParent::Raster)];
        out.multisample.pNext=roots[unsigned(GraphicsParent::Multisample)];out.depth.pNext=roots[unsigned(GraphicsParent::Depth)];out.blend.pNext=roots[unsigned(GraphicsParent::Blend)];out.dynamic.pNext=roots[unsigned(GraphicsParent::Dynamic)];
        out.info.pStages=out.stages.data();out.info.pVertexInputState=hasVertex?&out.vertex:nullptr;out.info.pInputAssemblyState=hasAssembly?&out.assembly:nullptr;
        out.info.pViewportState=hasViewport?&out.viewport:nullptr;out.info.pRasterizationState=hasRaster?&out.raster:nullptr;out.info.pMultisampleState=hasMultisample?&out.multisample:nullptr;
        out.info.pDepthStencilState=hasDepth?&out.depth:nullptr;out.info.pColorBlendState=hasBlend?&out.blend:nullptr;out.info.pDynamicState=hasDynamic?&out.dynamic:nullptr;
        out.info.layout=objects.layout;out.info.renderPass=objects.renderPass;return true;
    }
};
inline bool knownDynamic(VkDynamicState value) noexcept {
    switch(value){
#define D(name) case VK_DYNAMIC_STATE_##name:return true;
    D(VIEWPORT) D(SCISSOR) D(LINE_WIDTH) D(DEPTH_BIAS) D(BLEND_CONSTANTS) D(DEPTH_BOUNDS)
    D(STENCIL_COMPARE_MASK) D(STENCIL_WRITE_MASK) D(STENCIL_REFERENCE)
    D(CULL_MODE) D(FRONT_FACE) D(PRIMITIVE_TOPOLOGY) D(VIEWPORT_WITH_COUNT) D(SCISSOR_WITH_COUNT)
    D(VERTEX_INPUT_BINDING_STRIDE) D(DEPTH_TEST_ENABLE) D(DEPTH_WRITE_ENABLE) D(DEPTH_COMPARE_OP)
    D(DEPTH_BOUNDS_TEST_ENABLE) D(STENCIL_TEST_ENABLE) D(STENCIL_OP)
    D(RASTERIZER_DISCARD_ENABLE) D(DEPTH_BIAS_ENABLE) D(PRIMITIVE_RESTART_ENABLE) D(PATCH_CONTROL_POINTS_EXT) D(LOGIC_OP_EXT)
    D(TESSELLATION_DOMAIN_ORIGIN_EXT) D(DEPTH_CLAMP_ENABLE_EXT) D(POLYGON_MODE_EXT) D(RASTERIZATION_SAMPLES_EXT)
    D(SAMPLE_MASK_EXT) D(ALPHA_TO_COVERAGE_ENABLE_EXT) D(ALPHA_TO_ONE_ENABLE_EXT) D(LOGIC_OP_ENABLE_EXT)
    D(COLOR_BLEND_ENABLE_EXT) D(COLOR_BLEND_EQUATION_EXT) D(COLOR_WRITE_MASK_EXT) D(RASTERIZATION_STREAM_EXT)
    D(CONSERVATIVE_RASTERIZATION_MODE_EXT) D(EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT) D(DEPTH_CLIP_ENABLE_EXT)
    D(SAMPLE_LOCATIONS_ENABLE_EXT) D(COLOR_BLEND_ADVANCED_EXT) D(PROVOKING_VERTEX_MODE_EXT) D(LINE_RASTERIZATION_MODE_EXT)
    D(LINE_STIPPLE_ENABLE_EXT) D(DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT) D(VIEWPORT_W_SCALING_ENABLE_NV) D(VIEWPORT_SWIZZLE_NV)
    D(COVERAGE_TO_COLOR_ENABLE_NV) D(COVERAGE_TO_COLOR_LOCATION_NV) D(COVERAGE_MODULATION_MODE_NV)
    D(COVERAGE_MODULATION_TABLE_ENABLE_NV) D(COVERAGE_MODULATION_TABLE_NV) D(SHADING_RATE_IMAGE_ENABLE_NV)
    D(REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV) D(COVERAGE_REDUCTION_MODE_NV)
    D(SAMPLE_LOCATIONS_EXT) D(LINE_STIPPLE_EXT) D(VERTEX_INPUT_EXT) D(COLOR_WRITE_ENABLE_EXT)
    D(VIEWPORT_W_SCALING_NV) D(DISCARD_RECTANGLE_EXT) D(DISCARD_RECTANGLE_ENABLE_EXT) D(DISCARD_RECTANGLE_MODE_EXT)
    D(VIEWPORT_SHADING_RATE_PALETTE_NV) D(VIEWPORT_COARSE_SAMPLE_ORDER_NV) D(EXCLUSIVE_SCISSOR_ENABLE_NV) D(EXCLUSIVE_SCISSOR_NV)
    D(FRAGMENT_SHADING_RATE_KHR) D(ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT)
#undef D
    default:return false;}
}
inline bool graphicsDynamic(const GraphicsDefinition& out,VkDynamicState value) noexcept {
    for(unsigned i=0;i<out.dynamic.dynamicStateCount;++i)if(out.dynamics[i]==value)return true;
    return false;
}
inline bool captureGraphicsExtensions(GraphicsParent parent,const void* input,GraphicsDefinition& out,Refusal& why) noexcept {
    std::array<const VkBaseInStructure*,GraphicsExtensionCapacity> seen{};unsigned count=0;
    const auto dynamic=[&](VkDynamicState value){return graphicsDynamic(out,value);};
    for(auto* node=static_cast<const VkBaseInStructure*>(input);node;node=node->pNext){
        for(unsigned i=0;i<count;++i)if(seen[i]==node)return refuse(why,Reason::ChainCycle,count,static_cast<std::uint32_t>(node->sType));
        if(count==seen.size())return refuse(why,Reason::Bounds,count,static_cast<std::uint32_t>(node->sType));
        seen[count++]=node;GraphicsExtension slot=GraphicsExtension::Count;
#define ITEM(kind,type,member,tag) case tag:slot=GraphicsExtension::kind;break;
        switch(node->sType){GHVR_GRAPHICS_EXTENSIONS(ITEM) default:return refuse(why,Reason::Extension,count-1,static_cast<std::uint32_t>(node->sType));}
#undef ITEM
        const auto mask=UINT64_C(1)<<unsigned(slot);
        if(out.extensionMask&mask)return refuse(why,Reason::DuplicateNode,count-1,static_cast<std::uint32_t>(node->sType));
#define COPY(tag,expected,member,kind) case tag: { \
            if(parent!=GraphicsParent::expected)return refuse(why,Reason::Extension,count-1,static_cast<std::uint32_t>(node->sType)); \
            slot=GraphicsExtension::kind;out.extensions.member=*reinterpret_cast<const decltype(out.extensions.member)*>(node);out.extensions.member.pNext=nullptr;auto& value=out.extensions.member;
#define DONE break; }
        switch(node->sType){
        COPY(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,Vertex,divisor,Divisor)
            value.pVertexBindingDivisors=nullptr;
            if(!copyArray(reinterpret_cast<const VkPipelineVertexInputDivisorStateCreateInfoEXT*>(node)->pVertexBindingDivisors,value.vertexBindingDivisorCount,out.divisors,why))return false;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD,Raster,order,Order)
            (void)value;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT,Raster,stream,Stream)
            if(value.flags)return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,Raster,conservative,Conservative)
            if(value.flags||(!dynamic(VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT)&&!std::isfinite(value.extraPrimitiveOverestimationSize)))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT,Raster,depthClip,DepthClip)
            if(value.flags||(!dynamic(VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT)&&!boolean(value.depthClipEnable)))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT,Raster,provoking,Provoking)
            (void)value;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,Raster,line,Line)
            if(!dynamic(VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT)&&!boolean(value.stippledLineEnable))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,Viewport,depthControl,DepthControl)
            if(!dynamic(VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT)&&!boolean(value.negativeOneToOne))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_W_SCALING_STATE_CREATE_INFO_NV,Viewport,wScaling,WScaling)
            const auto* original=reinterpret_cast<const VkPipelineViewportWScalingStateCreateInfoNV*>(node);value.pViewportWScalings=nullptr;
            if(!dynamic(VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV)&&!boolean(value.viewportWScalingEnable))return refuse(why,Reason::Scalar);
            out.hasWScalings=!dynamic(VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_NV)&&(dynamic(VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV)||value.viewportWScalingEnable);
            if(out.hasWScalings&&!copyArray(original->pViewportWScalings,value.viewportCount,out.wScalings,why))return false;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SWIZZLE_STATE_CREATE_INFO_NV,Viewport,swizzle,Swizzle)
            const auto* original=reinterpret_cast<const VkPipelineViewportSwizzleStateCreateInfoNV*>(node);value.pViewportSwizzles=nullptr;
            if(value.flags)return refuse(why,Reason::Scalar);
            out.hasSwizzles=!dynamic(VK_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV);
            if(out.hasSwizzles&&!copyArray(original->pViewportSwizzles,value.viewportCount,out.swizzles,why))return false;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_EXCLUSIVE_SCISSOR_STATE_CREATE_INFO_NV,Viewport,exclusive,Exclusive)
            const auto* original=reinterpret_cast<const VkPipelineViewportExclusiveScissorStateCreateInfoNV*>(node);value.pExclusiveScissors=nullptr;
            out.hasExclusiveScissors=!dynamic(VK_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_NV);
            if(out.hasExclusiveScissors&&!copyArray(original->pExclusiveScissors,value.exclusiveScissorCount,out.exclusiveScissors,why))return false;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT,Multisample,sampleLocations,SampleLocations)
            const auto* original=reinterpret_cast<const VkPipelineSampleLocationsStateCreateInfoEXT*>(node);
            value.sampleLocationsInfo.pSampleLocations=nullptr;value.sampleLocationsInfo.pNext=nullptr;
            if(!dynamic(VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT)&&!boolean(value.sampleLocationsEnable))return refuse(why,Reason::Scalar);
            out.hasLocations=!dynamic(VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT)&&(dynamic(VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT)||value.sampleLocationsEnable);
            if(out.hasLocations){const auto& location=original->sampleLocationsInfo;
                if(!ordinary(location,VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,why)||!sampleCount(location.sampleLocationsPerPixel)||!location.sampleLocationGridSize.width||!location.sampleLocationGridSize.height)return refuse(why,Reason::Scalar);
                if(location.sampleLocationGridSize.width>GraphicsSampleCapacity||location.sampleLocationGridSize.height>GraphicsSampleCapacity||location.sampleLocationsCount>GraphicsSampleCapacity)return refuse(why,Reason::Bounds);
                const std::uint64_t expected=std::uint64_t(location.sampleLocationsPerPixel)*location.sampleLocationGridSize.width*location.sampleLocationGridSize.height;
                if(expected!=location.sampleLocationsCount||!copyArray(location.pSampleLocations,location.sampleLocationsCount,out.sampleLocations,why))return refuse(why,Reason::Bounds);
                for(unsigned i=0;i<location.sampleLocationsCount;++i)if(!std::isfinite(out.sampleLocations[i].x)||!std::isfinite(out.sampleLocations[i].y))return refuse(why,Reason::Scalar,i);
            }
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_TO_COLOR_STATE_CREATE_INFO_NV,Multisample,coverageColor,CoverageColor)
            if(value.flags||(!dynamic(VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV)&&!boolean(value.coverageToColorEnable)))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_MODULATION_STATE_CREATE_INFO_NV,Multisample,coverageTable,CoverageTable)
            const auto* original=reinterpret_cast<const VkPipelineCoverageModulationStateCreateInfoNV*>(node);value.pCoverageModulationTable=nullptr;
            if(value.flags||(!dynamic(VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV)&&!boolean(value.coverageModulationTableEnable)))return refuse(why,Reason::Scalar);
            out.hasCoverageTable=!dynamic(VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV)&&(dynamic(VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV)||value.coverageModulationTableEnable);
            if(out.hasCoverageTable&&!copyArray(original->pCoverageModulationTable,value.coverageModulationTableCount,out.coverageTable,why))return false;
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_COVERAGE_REDUCTION_STATE_CREATE_INFO_NV,Multisample,coverageReduction,CoverageReduction)
            if(value.flags)return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_ADVANCED_STATE_CREATE_INFO_EXT,Blend,advanced,Advanced)
            if(!dynamic(VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT)&&(!boolean(value.srcPremultiplied)||!boolean(value.dstPremultiplied)))return refuse(why,Reason::Scalar);
        DONE
        COPY(VK_STRUCTURE_TYPE_PIPELINE_COLOR_WRITE_CREATE_INFO_EXT,Blend,colorWrite,ColorWrite)
            const auto* original=reinterpret_cast<const VkPipelineColorWriteCreateInfoEXT*>(node);value.pColorWriteEnables=nullptr;out.hasColorWrites=!dynamic(VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT);
            if(out.hasColorWrites){if(!copyArray(original->pColorWriteEnables,value.attachmentCount,out.colorWrites,why))return false;for(unsigned i=0;i<value.attachmentCount;++i)if(!boolean(out.colorWrites[i]))return refuse(why,Reason::Scalar,i);}
        DONE
        default:return refuse(why,Reason::Extension,count-1,static_cast<std::uint32_t>(node->sType));
        }
#undef COPY
#undef DONE
        out.extensionMask|=mask;out.extensionOrder[unsigned(parent)][out.extensionCounts[unsigned(parent)]++]=slot;
    }
    return true;
}
inline bool exactEntry(const char* source,const char* expected) noexcept {
    if(!source)return false;
    for(std::size_t i=0;i<16;++i){if(source[i]!=expected[i])return false;if(!expected[i])return true;}return false;
}
inline bool captureGraphics(const VkGraphicsPipelineCreateInfo& in,VkPipelineLayout resolvedLayout,
        VkRenderPass resolvedRenderPass,VkShaderModule vertexModule,VkShaderModule fragmentModule,
        const RenderPassDefinition& pass,GraphicsDefinition& out,Refusal& why,
        const FeatureFacts* admittedDevice=nullptr) noexcept {
    out={};why={};if(in.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO)return refuse(why,Reason::Type);
    if(in.pNext)return refuse(why,Reason::PipelineExtension);
    // basePipelineIndex is ignored when DERIVATIVE is absent; preserve its
    // scalar value (including zero-initialized native CIs), never invent flags.
    if(in.flags)return refuse(why,Reason::PipelineFlags);
    if(!resolvedLayout || !resolvedRenderPass || in.layout!=resolvedLayout || in.renderPass!=resolvedRenderPass)
        return refuse(why,Reason::Identity);
    const auto* selected=pass.subpass(in.subpass);
    if(!pass.complete || !selected)return refuse(why,Reason::RenderPassMissing);
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
#define STATE(field,type,member,parent) \
    if(in.field->sType!=type)return refuse(why,Reason::Type,UINT32_MAX,static_cast<std::uint32_t>(in.field->sType)); \
    out.member=*in.field;out.member.pNext=nullptr; \
    if(!captureGraphicsExtensions(GraphicsParent::parent,in.field->pNext,out,why))return false;
    if(in.pDynamicState){
        STATE(pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,dynamic,Dynamic)
        out.dynamic.pDynamicStates=nullptr;out.hasDynamic=true;
        if(out.dynamic.flags||!copyArray(in.pDynamicState->pDynamicStates,out.dynamic.dynamicStateCount,out.dynamics,why))return refuse(why,Reason::Bounds);
        for(unsigned i=0;i<out.dynamic.dynamicStateCount;++i){
            if(!knownDynamic(out.dynamics[i]))return refuse(why,Reason::DynamicState,i,static_cast<std::uint32_t>(out.dynamics[i]));
            for(unsigned j=0;j<i;++j)if(out.dynamics[i]==out.dynamics[j])return refuse(why,Reason::DynamicState,i,static_cast<std::uint32_t>(out.dynamics[i]));
        }
    }
    const auto dynamic=[&](VkDynamicState value){return graphicsDynamic(out,value);};
    const auto all=[&](std::initializer_list<VkDynamicState> states){for(const auto state:states)if(!dynamic(state))return false;return true;};
    const bool rasterDynamic=all({VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,VK_DYNAMIC_STATE_POLYGON_MODE_EXT,VK_DYNAMIC_STATE_CULL_MODE,VK_DYNAMIC_STATE_FRONT_FACE,VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_LINE_WIDTH});
    out.hasRaster=in.pRasterizationState!=nullptr;
    if(!out.hasRaster&&!rasterDynamic)return refuse(why,Reason::NullArray);
    if(out.hasRaster){
        STATE(pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,raster,Raster)
        if(out.raster.flags||(!dynamic(VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT)&&!boolean(out.raster.depthClampEnable))||
           (!dynamic(VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)&&!boolean(out.raster.rasterizerDiscardEnable))||
           (!dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE)&&!boolean(out.raster.depthBiasEnable))||
           (!dynamic(VK_DYNAMIC_STATE_LINE_WIDTH)&&(!std::isfinite(out.raster.lineWidth)||out.raster.lineWidth<=0))||
           (!dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS)&&(dynamic(VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE)||out.raster.depthBiasEnable)&&
            (!std::isfinite(out.raster.depthBiasConstantFactor)||!std::isfinite(out.raster.depthBiasClamp)||!std::isfinite(out.raster.depthBiasSlopeFactor))))return refuse(why,Reason::Scalar);
    }
    out.hasVertex=!dynamic(VK_DYNAMIC_STATE_VERTEX_INPUT_EXT)&&in.pVertexInputState;
    if(!out.hasVertex&&!dynamic(VK_DYNAMIC_STATE_VERTEX_INPUT_EXT))return refuse(why,Reason::NullArray);
    if(out.hasVertex){
        STATE(pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,vertex,Vertex)
        out.vertex.pVertexBindingDescriptions=nullptr;out.vertex.pVertexAttributeDescriptions=nullptr;
        if(out.vertex.flags||!copyArray(in.pVertexInputState->pVertexBindingDescriptions,out.vertex.vertexBindingDescriptionCount,out.vertexBindings,why)||
           !copyArray(in.pVertexInputState->pVertexAttributeDescriptions,out.vertex.vertexAttributeDescriptionCount,out.attributes,why))return refuse(why,Reason::Bounds);
    }
    out.hasAssembly=in.pInputAssemblyState!=nullptr;
    if(!out.hasAssembly&&!all({VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE}))return refuse(why,Reason::NullArray);
    if(out.hasAssembly){
        STATE(pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,assembly,Assembly)
        if(out.assembly.flags||(!dynamic(VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE)&&!boolean(out.assembly.primitiveRestartEnable)))return refuse(why,Reason::Scalar);
    }
    // No tessellation shaders exist in this pinned VS/FS route. The entire
    // pointer is ignored, including a nonnull value that is not readable.
    const bool discard=out.hasRaster&&!dynamic(VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)&&out.raster.rasterizerDiscardEnable==VK_TRUE;
    out.hasViewport=!discard&&in.pViewportState;
    if(!discard&&!out.hasViewport&&!all({VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT}))return refuse(why,Reason::NullArray);
    if(out.hasViewport){
        STATE(pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,viewport,Viewport)
        out.viewport.pViewports=nullptr;out.viewport.pScissors=nullptr;
        const bool viewportCount=dynamic(VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT),scissorCount=dynamic(VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT);
        if(out.viewport.flags||(!viewportCount&&(!out.viewport.viewportCount||out.viewport.viewportCount>MaxViewports))||
           (!scissorCount&&(!out.viewport.scissorCount||out.viewport.scissorCount>MaxViewports))||
           (!viewportCount&&!scissorCount&&out.viewport.viewportCount!=out.viewport.scissorCount))return refuse(why,Reason::Bounds);
        out.hasViewports=!viewportCount&&!dynamic(VK_DYNAMIC_STATE_VIEWPORT);out.hasScissors=!scissorCount&&!dynamic(VK_DYNAMIC_STATE_SCISSOR);
        if(out.hasViewports&&!copyArray(in.pViewportState->pViewports,out.viewport.viewportCount,out.viewports,why))return false;
        if(out.hasScissors&&!copyArray(in.pViewportState->pScissors,out.viewport.scissorCount,out.scissors,why))return false;
        if(out.hasViewports)for(unsigned i=0;i<out.viewport.viewportCount;++i){const auto& v=out.viewports[i];if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.width)||!std::isfinite(v.height)||!std::isfinite(v.minDepth)||!std::isfinite(v.maxDepth)||v.width<=0||v.height==0)return refuse(why,Reason::Scalar,i);}
    }
    out.hasMultisample=!discard&&in.pMultisampleState;
    if(!discard&&!out.hasMultisample&&!all({VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,VK_DYNAMIC_STATE_SAMPLE_MASK_EXT,VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT,VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT}))return refuse(why,Reason::NullArray);
    if(out.hasMultisample){
        STATE(pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,multisample,Multisample)
        out.multisample.pSampleMask=nullptr;out.hasMask=!dynamic(VK_DYNAMIC_STATE_SAMPLE_MASK_EXT)&&in.pMultisampleState->pSampleMask;
        if(out.multisample.flags||((!dynamic(VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT)||out.hasMask)&&!sampleCount(out.multisample.rasterizationSamples))||
           !boolean(out.multisample.sampleShadingEnable)||(!dynamic(VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT)&&!boolean(out.multisample.alphaToCoverageEnable))||
           (!dynamic(VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT)&&!boolean(out.multisample.alphaToOneEnable))||
           (out.multisample.sampleShadingEnable&&(!std::isfinite(out.multisample.minSampleShading)||out.multisample.minSampleShading<0||out.multisample.minSampleShading>1)))return refuse(why,Reason::Scalar);
        if(out.hasMask){const auto count=(static_cast<unsigned>(out.multisample.rasterizationSamples)+31)/32;for(unsigned i=0;i<count;++i)out.sampleMask[i]=in.pMultisampleState->pSampleMask[i];}
    }
    const bool depthDynamic=all({VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS});
    out.hasDepth=!discard&&selected->hasDepth&&in.pDepthStencilState;
    if(!discard&&selected->hasDepth&&!out.hasDepth&&!depthDynamic)return refuse(why,Reason::NullArray);
    if(out.hasDepth){
        STATE(pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,depth,Depth)
        if(out.depth.flags||(!dynamic(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE)&&!boolean(out.depth.depthTestEnable))||
           (!dynamic(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)&&!boolean(out.depth.depthWriteEnable))||
           (!dynamic(VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE)&&!boolean(out.depth.depthBoundsTestEnable))||
           (!dynamic(VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE)&&!boolean(out.depth.stencilTestEnable))||
           (!dynamic(VK_DYNAMIC_STATE_DEPTH_BOUNDS)&&(dynamic(VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE)||out.depth.depthBoundsTestEnable)&&(!std::isfinite(out.depth.minDepthBounds)||!std::isfinite(out.depth.maxDepthBounds))))return refuse(why,Reason::Scalar);
    }
    // enabledFeature currently rejects BLEND_OPERATION_ADVANCED_FEATURES_EXT;
    // complete checked device facts therefore establish its coherent-operation
    // feature absent (it is not folded into Vulkan11/12/13). If that whitelist
    // ever admits the feature, replace this absence proof with its actual bit.
    // A standalone graphics definition has no such checked device knowledge.
    // This is cold semantic policy, never native runtime authorization.
    const bool advancedDisabled=admittedDevice&&admittedDevice->complete;
    const bool dynamicBlendArrays=all({VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT})&&
        (dynamic(VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT)||advancedDisabled);
    const bool blendDynamic=dynamicBlendArrays&&all({VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,VK_DYNAMIC_STATE_LOGIC_OP_EXT,VK_DYNAMIC_STATE_BLEND_CONSTANTS});
    out.hasBlend=!discard&&selected->colorCount&&in.pColorBlendState;
    if(!discard&&selected->colorCount&&!out.hasBlend&&!blendDynamic)return refuse(why,Reason::NullArray);
    if(out.hasBlend){
        STATE(pColorBlendState,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,blend,Blend)
        out.blend.pAttachments=nullptr;out.hasColors=!dynamicBlendArrays;
        if(out.blend.flags||(!dynamic(VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT)&&!boolean(out.blend.logicOpEnable))||
           (out.hasColors&&(out.blend.attachmentCount!=selected->colorCount||!copyArray(in.pColorBlendState->pAttachments,out.blend.attachmentCount,out.colors,why))))return refuse(why,Reason::Bounds);
        if(out.hasColors&&!dynamic(VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT))for(unsigned i=0;i<out.blend.attachmentCount;++i)if(!boolean(out.colors[i].blendEnable))return refuse(why,Reason::Scalar,i);
        if(!dynamic(VK_DYNAMIC_STATE_BLEND_CONSTANTS))for(float value:out.blend.blendConstants)if(!std::isfinite(value))return refuse(why,Reason::Scalar);
    }
#undef STATE
    out.info=in;out.info.pNext=nullptr;out.info.pStages=nullptr;out.info.pVertexInputState=nullptr;out.info.pInputAssemblyState=nullptr;
    out.info.pTessellationState=nullptr;out.info.pViewportState=nullptr;out.info.pRasterizationState=nullptr;
    out.info.pMultisampleState=nullptr;out.info.pDepthStencilState=nullptr;out.info.pColorBlendState=nullptr;out.info.pDynamicState=nullptr;
    out.info.layout=VK_NULL_HANDLE;out.info.renderPass=VK_NULL_HANDLE;out.info.basePipelineHandle=VK_NULL_HANDLE;out.complete=true;return true;
}

} // namespace frame_pixel_ssbo_clone
#undef GHVR_GRAPHICS_EXTENSIONS
#endif
