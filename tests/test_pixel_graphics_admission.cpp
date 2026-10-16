#include "layer/pixel_graphics_definition.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace frame_pixel_ssbo_clone;
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T out{};out.sType=type;return out;}
template<class T>T object(std::uint64_t value){T out{};std::memcpy(&out,&value,sizeof(out));return out;}
template<class T>const T* poison(){return reinterpret_cast<const T*>(std::uintptr_t(1));}
struct Fixture {
    VkPipelineVertexInputStateCreateInfo vertex=info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    VkPipelineInputAssemblyStateCreateInfo assembly=info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    VkPipelineViewportStateCreateInfo viewport=info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    VkPipelineRasterizationStateCreateInfo raster=info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    VkPipelineMultisampleStateCreateInfo multisample=info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    VkPipelineDepthStencilStateCreateInfo depth=info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    VkPipelineColorBlendStateCreateInfo blend=info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    VkPipelineDynamicStateCreateInfo dynamic=info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    VkGraphicsPipelineCreateInfo ci=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    VkVertexInputBindingDescription binding{2,24,VK_VERTEX_INPUT_RATE_INSTANCE};VkVertexInputAttributeDescription attribute{1,2,VK_FORMAT_R32G32_SFLOAT,8};
    VkViewport rectangle{1,2,320,-240,0,1};VkRect2D scissor{{3,4},{320,240}};
    VkPipelineColorBlendAttachmentState color{};VkSampleMask mask=0xff;
    std::vector<VkDynamicState> states;
    RenderPassDefinition pass{};
    Fixture(){
        pass.complete=true;pass.subpassCount=3;pass.colorCount=0;pass.following[0].colorCount=0;pass.following[1].colorCount=1;pass.following[1].hasDepth=true;
        for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=object<VkShaderModule>(40);stages[0].pName="VS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=object<VkShaderModule>(50);stages[1].pName="PS_DISTORT";
        vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.vertexAttributeDescriptionCount=1;vertex.pVertexAttributeDescriptions=&attribute;
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;viewport.viewportCount=1;viewport.pViewports=&rectangle;viewport.scissorCount=1;viewport.pScissors=&scissor;
        raster.polygonMode=VK_POLYGON_MODE_FILL;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;raster.depthBiasEnable=VK_TRUE;raster.depthBiasConstantFactor=.25f;
        multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;multisample.pSampleMask=&mask;depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_LESS;depth.maxDepthBounds=1;
        color.blendEnable=VK_TRUE;color.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;color.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;color.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;color.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;color.colorWriteMask=15;
        blend.attachmentCount=1;blend.pAttachments=&color;blend.blendConstants[0]=.375f;
        ci.stageCount=2;ci.pStages=stages.data();ci.pVertexInputState=&vertex;ci.pInputAssemblyState=&assembly;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;ci.pMultisampleState=&multisample;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blend;ci.pDynamicState=&dynamic;ci.layout=object<VkPipelineLayout>(20);ci.renderPass=object<VkRenderPass>(30);ci.subpass=2;
    }
    bool capture(GraphicsDefinition& out,Refusal& why){dynamic.dynamicStateCount=states.size();dynamic.pDynamicStates=states.empty()?nullptr:states.data();return captureGraphics(ci,ci.layout,ci.renderPass,stages[0].module,stages[1].module,pass,out,why);}
};
OwnedObjects owned(){return {object<VkShaderModule>(140),object<VkShaderModule>(150),object<VkPipelineLayout>(120),object<VkRenderPass>(130),true};}
void common(){
    Fixture f;GraphicsDefinition out;Refusal why;require(f.capture(out,why),"third original subpass rather than flattened first subpass is admitted");
    GraphicsDefinition copied=out;f.binding.stride=999;f.attribute.offset=999;f.color.colorWriteMask=0;f.rectangle.width=1;f.mask=0;f.raster.lineWidth=99;
    GraphicsView view;require(copied.view(owned(),view,why),"copied owned graphics reconstructs a call-duration view");
    require(view.info.subpass==2&&view.info.renderPass==owned().renderPass&&view.info.layout==owned().layout,"exact selected subpass retained and only owned object handles substituted");
    require(view.vertex.pVertexBindingDescriptions[0].stride==24&&view.vertex.pVertexAttributeDescriptions[0].offset==8&&view.viewport.pViewports[0].width==320,"arrays survive source overwrite");
    require(view.blend.pAttachments[0].colorWriteMask==15&&*view.multisample.pSampleMask==0xff&&view.raster.lineWidth==1,"static output-affecting fields survive source overwrite");
    require(view.stages[0].module==owned().vertex&&std::strcmp(view.stages[1].pName,"PS_DISTORT")==0,"exact shader roles and owned entry strings retained");
    f.ci.subpass=3;require(!f.capture(out,why)&&why.reason==Reason::RenderPassMissing,"out-of-cohort target subpass refuses");
    f.ci.subpass=0;f.ci.pDepthStencilState=poison<VkPipelineDepthStencilStateCreateInfo>();f.ci.pColorBlendState=poison<VkPipelineColorBlendStateCreateInfo>();f.ci.pTessellationState=poison<VkPipelineTessellationStateCreateInfo>();
    require(f.capture(out,why)&&!out.hasDepth&&!out.hasBlend,"empty first subpass ignores entire depth/blend/tessellation pointers");
    f.ci.basePipelineHandle=object<VkPipeline>(999);require(f.capture(out,why)&&out.info.basePipelineHandle==VK_NULL_HANDLE,"non-derivative ignored base pipeline handle never becomes owned dependency");
}
void dynamics(){
    Fixture f;GraphicsDefinition out;Refusal why;
    f.states={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_LINE_WIDTH,VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_DEPTH_BOUNDS,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_CULL_MODE,VK_DYNAMIC_STATE_FRONT_FACE,VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT,VK_DYNAMIC_STATE_LOGIC_OP_EXT,
        VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT,VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,VK_DYNAMIC_STATE_POLYGON_MODE_EXT,VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,VK_DYNAMIC_STATE_SAMPLE_MASK_EXT,VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT,VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT,VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT,VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT,VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT,VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT,VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT,VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT,VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT,VK_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV,VK_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV,VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV,VK_DYNAMIC_STATE_COVERAGE_TO_COLOR_LOCATION_NV,VK_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV,VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV,VK_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_NV,VK_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV,VK_DYNAMIC_STATE_REPRESENTATIVE_FRAGMENT_TEST_ENABLE_NV,VK_DYNAMIC_STATE_COVERAGE_REDUCTION_MODE_NV};
    const auto expected=f.states;f.viewport.pViewports=poison<VkViewport>();f.viewport.pScissors=poison<VkRect2D>();f.multisample.pSampleMask=poison<VkSampleMask>();f.multisample.rasterizationSamples=static_cast<VkSampleCountFlagBits>(0);f.blend.pAttachments=poison<VkPipelineColorBlendAttachmentState>();f.blend.attachmentCount=UINT32_MAX;
    f.raster.lineWidth=std::numeric_limits<float>::quiet_NaN();f.depth.depthTestEnable=2;f.multisample.alphaToOneEnable=2;
    require(f.capture(out,why),"whole core/EDS1/EDS2/EDS3 list preserves ignored scalar/array semantics beyond old16 bound");
    GraphicsDefinition copied=out;f.states.clear();GraphicsView view;require(copied.view(owned(),view,why)&&view.dynamic.dynamicStateCount==expected.size(),"full dynamic list owned after input list destroyed");
    for(unsigned i=0;i<expected.size();++i)require(view.dynamic.pDynamicStates[i]==expected[i],"every dynamic enum and original order remains exact");
    require(!view.viewport.pViewports&&!view.viewport.pScissors&&!view.multisample.pSampleMask&&!view.blend.pAttachments,"all ignored dynamic arrays stripped without reads");
    f.states={static_cast<VkDynamicState>(0x6fffffff)};require(!f.capture(out,why)&&why.reason==Reason::DynamicState&&why.type==0x6fffffff,"unknown enum refuses before any poison static array access");
    f.states={VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT,VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT};require(!f.capture(out,why)&&why.reason==Reason::DynamicState&&why.index==1,"duplicate known dynamic state refuses specifically");
    f.dynamic.dynamicStateCount=GraphicsDynamicCapacity+1;f.dynamic.pDynamicStates=poison<VkDynamicState>();require(!captureGraphics(f.ci,f.ci.layout,f.ci.renderPass,f.stages[0].module,f.stages[1].module,f.pass,out,why)&&why.reason==Reason::Bounds,"actual65-word bound refuses before array dereference");
}
void nullable(){
    Fixture f;GraphicsDefinition out;Refusal why;
    f.states={VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,VK_DYNAMIC_STATE_POLYGON_MODE_EXT,VK_DYNAMIC_STATE_CULL_MODE,VK_DYNAMIC_STATE_FRONT_FACE,VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,VK_DYNAMIC_STATE_DEPTH_BIAS,VK_DYNAMIC_STATE_LINE_WIDTH,VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,VK_DYNAMIC_STATE_SAMPLE_MASK_EXT,VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT,VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT,VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,VK_DYNAMIC_STATE_STENCIL_OP,VK_DYNAMIC_STATE_DEPTH_BOUNDS,VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,VK_DYNAMIC_STATE_LOGIC_OP_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT};
    f.ci.pVertexInputState=nullptr;f.ci.pInputAssemblyState=nullptr;f.ci.pViewportState=nullptr;f.ci.pRasterizationState=nullptr;f.ci.pMultisampleState=nullptr;f.ci.pDepthStencilState=nullptr;f.ci.pColorBlendState=nullptr;f.ci.pTessellationState=poison<VkPipelineTessellationStateCreateInfo>();
    require(f.capture(out,why),"all documented fully dynamic null state routes remain null");GraphicsView view;require(out.view(owned(),view,why)&&!view.info.pVertexInputState&&!view.info.pInputAssemblyState&&!view.info.pViewportState&&!view.info.pRasterizationState&&!view.info.pMultisampleState&&!view.info.pDepthStencilState&&!view.info.pColorBlendState&&!view.info.pTessellationState,"view never invents static state for optional null inputs");
    f.states.erase(f.states.begin()+2);require(!f.capture(out,why)&&why.reason==Reason::NullArray,"removing required null-state dynamic declaration is a causal refusal");
    Fixture discard;discard.raster.rasterizerDiscardEnable=VK_TRUE;discard.ci.pViewportState=poison<VkPipelineViewportStateCreateInfo>();discard.ci.pMultisampleState=poison<VkPipelineMultisampleStateCreateInfo>();discard.ci.pDepthStencilState=poison<VkPipelineDepthStencilStateCreateInfo>();discard.ci.pColorBlendState=poison<VkPipelineColorBlendStateCreateInfo>();require(discard.capture(out,why)&&!out.hasMultisample&&!out.hasViewport,"static discard never follows entire ignored fragment state pointers");
}
void advancedPolicy(){
    Fixture f;GraphicsDefinition out;Refusal why;f.states={VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT};
    require(f.capture(out,why)&&out.hasColors,"unknown device policy retains potentially meaningful advanced attachment state");
    VkPhysicalDeviceFeatures core{};core.fragmentStoresAndAtomics=VK_TRUE;auto device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);device.pEnabledFeatures=&core;FeatureFacts actual;
    require(enabledFeature(device,true,actual,why)&&actual.complete,"complete original checked device chain establishes bounded absence policy");
    f.dynamic.dynamicStateCount=f.states.size();f.dynamic.pDynamicStates=f.states.data();f.blend.pAttachments=poison<VkPipelineColorBlendAttachmentState>();f.blend.attachmentCount=UINT32_MAX;
    require(captureGraphics(f.ci,f.ci.layout,f.ci.renderPass,f.stages[0].module,f.stages[1].module,f.pass,out,why,&actual)&&!out.hasColors,"three dynamics skip attachments only with independently checked advanced-feature absence");
    FeatureFacts incomplete=actual;incomplete.complete=false;f.blend.pAttachments=&f.color;f.blend.attachmentCount=1;
    require(captureGraphics(f.ci,f.ci.layout,f.ci.renderPass,f.stages[0].module,f.stages[1].module,f.pass,out,why,&incomplete)&&out.hasColors,"incomplete device facts cannot establish absence");
    auto advanced=info<VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT);advanced.advancedBlendCoherentOperations=VK_TRUE;device.pNext=&advanced;
    require(!enabledFeature(device,true,actual,why)&&!actual.complete&&why.reason==Reason::Extension&&why.type==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT,"actual advanced-feature declaration is refused rather than silently assumed disabled");
}
void extensions(){
    Fixture f;GraphicsDefinition out;Refusal why;
    auto sample=info<VkPipelineSampleLocationsStateCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);sample.sampleLocationsEnable=VK_TRUE;sample.sampleLocationsInfo=info<VkSampleLocationsInfoEXT>(VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT);sample.sampleLocationsInfo.sampleLocationsPerPixel=VK_SAMPLE_COUNT_1_BIT;sample.sampleLocationsInfo.sampleLocationGridSize={2,2};
    std::array<VkSampleLocationEXT,4> locations{{{.1f,.2f},{.3f,.4f},{.5f,.6f},{.7f,.8f}}};sample.sampleLocationsInfo.sampleLocationsCount=locations.size();sample.sampleLocationsInfo.pSampleLocations=locations.data();f.multisample.pNext=&sample;
    auto line=info<VkPipelineRasterizationLineStateCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);line.lineRasterizationMode=VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT;line.stippledLineEnable=VK_TRUE;line.lineStippleFactor=2;line.lineStipplePattern=0xa55a;
    auto clip=info<VkPipelineRasterizationDepthClipStateCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT);clip.depthClipEnable=VK_TRUE;line.pNext=&clip;f.raster.pNext=&line;
    auto write=info<VkPipelineColorWriteCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_WRITE_CREATE_INFO_EXT);VkBool32 enabled=VK_TRUE;write.attachmentCount=1;write.pColorWriteEnables=&enabled;f.blend.pNext=&write;
    auto divisor=info<VkPipelineVertexInputDivisorStateCreateInfoEXT>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);VkVertexInputBindingDivisorDescriptionEXT d{2,3};divisor.vertexBindingDivisorCount=1;divisor.pVertexBindingDivisors=&d;f.vertex.pNext=&divisor;
    require(f.capture(out,why),"associated static sample-location and raster/blend/vertex extension payloads admitted together");GraphicsDefinition copied=out;locations.fill({0,0});enabled=VK_FALSE;d.divisor=99;line.lineStippleFactor=99;clip.depthClipEnable=VK_FALSE;
    GraphicsView view;require(copied.view(owned(),view,why),"owned extension chains rebuild after all native payloads overwritten");
    const auto* observedSample=static_cast<const VkPipelineSampleLocationsStateCreateInfoEXT*>(view.multisample.pNext);require(observedSample&&observedSample!=&sample&&observedSample->sampleLocationsInfo.pSampleLocations[3].y==.8f,"nested sample-location array points into owned definition");
    const auto* observedLine=static_cast<const VkPipelineRasterizationLineStateCreateInfoEXT*>(view.raster.pNext);const auto* observedClip=static_cast<const VkPipelineRasterizationDepthClipStateCreateInfoEXT*>(observedLine->pNext);require(observedLine->lineStippleFactor==2&&observedClip->depthClipEnable==VK_TRUE&&!observedClip->pNext,"original pNext order and scalar payload preserved exactly");
    require(static_cast<const VkPipelineColorWriteCreateInfoEXT*>(view.blend.pNext)->pColorWriteEnables[0]==VK_TRUE&&static_cast<const VkPipelineVertexInputDivisorStateCreateInfoEXT*>(view.vertex.pNext)->pVertexBindingDivisors[0].divisor==3,"other extension-owned arrays survive source overwrite");
    sample.sampleLocationsEnable=VK_FALSE;sample.sampleLocationsInfo.pSampleLocations=poison<VkSampleLocationEXT>();sample.sampleLocationsInfo.sampleLocationsCount=UINT32_MAX;sample.sampleLocationsInfo.pNext=poison<void>();require(f.capture(out,why)&&!out.hasLocations,"disabled sample locations never follow ignored nested pointer/chain");
    sample.sampleLocationsEnable=2;f.states={VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT,VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT};require(f.capture(out,why)&&!out.hasLocations,"dynamic enable and dynamic locations ignore their original static fields");
    f.states.clear();require(!f.capture(out,why)&&why.reason==Reason::Scalar,"without dynamic declaration invalid enabled scalar is rejected");
    sample.sampleLocationsEnable=VK_TRUE;sample.sampleLocationsInfo.pNext=nullptr;sample.sampleLocationsInfo.sampleLocationsCount=129;require(!f.capture(out,why)&&why.reason==Reason::Bounds,"sample capacity refuses before invalid array read");
    auto unknown=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));f.multisample.pNext=&unknown;require(!f.capture(out,why)&&why.reason==Reason::Extension&&why.type==0x6fffffff,"unknown child payload refuses with exact actual header rather than parent type");
    f.multisample.pNext=nullptr;clip.pNext=&line;require(!f.capture(out,why)&&why.reason==Reason::ChainCycle,"typed chain cycle refuses finitely");
    clip.pNext=nullptr;auto second=clip;second.pNext=nullptr;clip.pNext=&second;require(!f.capture(out,why)&&why.reason==Reason::DuplicateNode,"duplicate known child refuses before interpreting second payload");
    Fixture wrong;wrong.raster.pNext=&write;require(!wrong.capture(out,why)&&why.reason==Reason::Extension,"known extension in wrong parent chain is never silently relocated");
}
}
int main(){common();dynamics();nullable();advancedPolicy();extensions();std::cout<<"{\"checks\":"<<checks<<",\"graphics_bytes\":"<<sizeof(GraphicsDefinition)<<",\"recipe_bytes\":"<<sizeof(Recipe)<<",\"native_compatibility_proven\":false}\n";}
