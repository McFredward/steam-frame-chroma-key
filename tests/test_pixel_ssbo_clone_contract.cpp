#include "layer/pixel_ssbo_clone_contract.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {
using namespace frame_pixel_ssbo_clone;
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T result{};result.sType=type;return result;}
template<class T>T object(std::uint64_t value){T result{};std::memcpy(&result,&value,sizeof(result));return result;}
void reason(const Refusal& why,Reason expected){require(why.reason==expected,"exact bounded first refusal");require(std::strcmp(reasonName(why.reason),"unknown_clone_refusal")!=0,"known refusal name");}
struct Fixture {
    VkPhysicalDeviceFeatures features{};VkDeviceCreateInfo device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    std::array<VkDescriptorSetLayoutBinding,7> bindings{};
    VkDescriptorSetLayoutCreateInfo set0=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    VkDescriptorSetLayout nativeSet=object<VkDescriptorSetLayout>(0x10);
    std::array<VkPushConstantRange,2> pushes{{{VK_SHADER_STAGE_FRAGMENT_BIT,64,16},{VK_SHADER_STAGE_VERTEX_BIT,0,64}}};
    VkPipelineLayoutCreateInfo layout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    std::array<VkAttachmentDescription,2> attachments{};
    std::array<VkAttachmentReference,1> color{{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}};
    VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};VkSubpassDependency dependency{};
    VkRenderPassCreateInfo pass=info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    std::array<VkVertexInputBindingDescription,2> vertexBindings{{{0,24,VK_VERTEX_INPUT_RATE_VERTEX},{1,16,VK_VERTEX_INPUT_RATE_INSTANCE}}};
    std::array<VkVertexInputAttributeDescription,2> attrs{{{0,0,VK_FORMAT_R32G32_SFLOAT,0},{1,1,VK_FORMAT_R32G32B32A32_SFLOAT,0}}};
    VkPipelineVertexInputStateCreateInfo vertex=info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    VkPipelineInputAssemblyStateCreateInfo assembly=info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    std::array<VkViewport,2> viewports{{{1,2,320,240,0,1},{7,11,640,-480,0,1}}};
    std::array<VkRect2D,2> scissors{{{{1,2},{320,240}},{{7,11},{640,480}}}};
    VkPipelineViewportStateCreateInfo viewport=info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    VkPipelineRasterizationStateCreateInfo raster=info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    VkSampleMask mask=0xf0fff0ffu;
    VkPipelineMultisampleStateCreateInfo multisample=info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    VkPipelineDepthStencilStateCreateInfo depth=info<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    VkPipelineColorBlendAttachmentState colorBlend{};
    VkPipelineColorBlendStateCreateInfo blend=info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    std::array<VkDynamicState,2> dynamics{{VK_DYNAMIC_STATE_LINE_WIDTH,VK_DYNAMIC_STATE_BLEND_CONSTANTS}};
    VkPipelineDynamicStateCreateInfo dynamic=info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    VkGraphicsPipelineCreateInfo graphics=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    Fixture(){
        features.fragmentStoresAndAtomics=VK_TRUE;device.pEnabledFeatures=&features;
        const std::uint32_t numbers[7]{0,2,3,4,6,9,25};
        for(unsigned i=0;i<7;++i)bindings[i]={numbers[i],i<5?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==5?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,i==5?3u:1u,VK_SHADER_STAGE_ALL,nullptr};
        set0.bindingCount=bindings.size();set0.pBindings=bindings.data();layout.setLayoutCount=1;layout.pSetLayouts=&nativeSet;
        layout.pushConstantRangeCount=pushes.size();layout.pPushConstantRanges=pushes.data();
        for(auto& a:attachments){a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;}
        attachments[0].format=VK_FORMAT_R32G32B32A32_SFLOAT;attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1].format=VK_FORMAT_D32_SFLOAT;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=color.data();sub.pDepthStencilAttachment=&depthRef;
        dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;dependency.srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        pass.attachmentCount=attachments.size();pass.pAttachments=attachments.data();pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=1;pass.pDependencies=&dependency;
        for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[0].module=object<VkShaderModule>(0x50);stages[0].pName="PS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[1].module=object<VkShaderModule>(0x40);stages[1].pName="VS_DISTORT";
        vertex.vertexBindingDescriptionCount=vertexBindings.size();vertex.pVertexBindingDescriptions=vertexBindings.data();vertex.vertexAttributeDescriptionCount=attrs.size();vertex.pVertexAttributeDescriptions=attrs.data();
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;viewport.viewportCount=viewports.size();viewport.pViewports=viewports.data();viewport.scissorCount=scissors.size();viewport.pScissors=scissors.data();
        raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1.25f;
        multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;multisample.pSampleMask=&mask;depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_LESS;depth.maxDepthBounds=1;
        colorBlend.blendEnable=VK_TRUE;colorBlend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;colorBlend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;colorBlend.colorBlendOp=VK_BLEND_OP_ADD;colorBlend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;colorBlend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;colorBlend.alphaBlendOp=VK_BLEND_OP_ADD;colorBlend.colorWriteMask=15;
        blend.attachmentCount=1;blend.pAttachments=&colorBlend;blend.blendConstants[0]=.25f;blend.blendConstants[1]=.75f;
        dynamic.dynamicStateCount=dynamics.size();dynamic.pDynamicStates=dynamics.data();
        graphics.stageCount=2;graphics.pStages=stages.data();graphics.pVertexInputState=&vertex;graphics.pInputAssemblyState=&assembly;graphics.pViewportState=&viewport;graphics.pRasterizationState=&raster;graphics.pMultisampleState=&multisample;graphics.pDepthStencilState=&depth;graphics.pColorBlendState=&blend;graphics.pDynamicState=&dynamic;
        graphics.layout=object<VkPipelineLayout>(0x20);graphics.renderPass=object<VkRenderPass>(0x30); // basePipelineIndex0 is ignored without DERIVATIVE.
    }
    Inputs inputs(VkShaderModuleCreateInfo& vs,VkShaderModuleCreateInfo& ps){
        Inputs in;in.device=1;in.deviceCreateSucceeded=true;in.deviceCreate=&device;in.set0=&set0;in.layout=&layout;in.renderPass=&pass;in.vertex=&vs;in.fragment=&ps;in.graphics=&graphics;
        in.set0Identity={1,0x10,1};in.layoutIdentity={1,0x20,2};in.renderPassIdentity={1,0x30,3};in.vertexIdentity={1,0x40,4};in.fragmentIdentity={1,0x50,5};in.pipelineIdentity={1,0x60,6};return in;
    }
    bool copyGraphics(GraphicsDefinition& out,Refusal& why){RenderPassDefinition rp;require(captureRenderPass(&pass,rp,why),"owned pass positive control");return captureGraphics(graphics,graphics.layout,graphics.renderPass,stages[1].module,stages[0].module,rp,out,why);}
};
OwnedObjects owned(){return {object<VkShaderModule>(0x140),object<VkShaderModule>(0x150),object<VkPipelineLayout>(0x120),object<VkRenderPass>(0x130),true};}

void features(){
    Fixture f;FeatureFacts result;Refusal why;
    require(enabledFeature(f.device,true,result,why) && result.complete && result.enabled && result.observed,"actual legacy enabled fact");require(!result.nativeAuthority,"feature fact is no native authority");
    require(!enabledFeature(f.device,false,result,why),"failed create cannot supply enablement");reason(why,Reason::NativeCreationFailed);
    f.features.fragmentStoresAndAtomics=VK_FALSE;require(!enabledFeature(f.device,true,result,why) && result.observed && !result.enabled,"supported-but-disabled cannot admit");reason(why,Reason::FeatureDisabled);
    f.features.fragmentStoresAndAtomics=2;require(!enabledFeature(f.device,true,result,why),"noncanonical feature refused");reason(why,Reason::FeatureValue);
    f.device.pEnabledFeatures=nullptr;require(!enabledFeature(f.device,true,result,why) && !result.observed,"absent feature distinguished");reason(why,Reason::FeatureAbsent);
    auto core=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);core.features.fragmentStoresAndAtomics=VK_TRUE;f.device.pNext=&core;
    require(enabledFeature(f.device,true,result,why) && result.source==FeatureSource::Features2,"Features2 enablement exact");
    f.device.pEnabledFeatures=&f.features;require(!enabledFeature(f.device,true,result,why),"legacy and Features2 conflict");reason(why,Reason::FeatureValue); // First malformed legacy input wins.
    f.features.fragmentStoresAndAtomics=VK_TRUE;require(!enabledFeature(f.device,true,result,why),"canonical duplicate declaration refused");reason(why,Reason::FeatureConflict);
    f.device.pEnabledFeatures=nullptr;core.pNext=&core;require(!enabledFeature(f.device,true,result,why),"feature chain cycle");reason(why,Reason::ChainCycle);core.pNext=nullptr;
    VkLayerDeviceCreateInfo link{},callback{};link.sType=callback.sType=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;link.function=VK_LAYER_LINK_INFO;callback.function=VK_LOADER_DATA_CALLBACK;link.pNext=&callback;callback.pNext=&core;f.device.pNext=&link;
    require(enabledFeature(f.device,true,result,why),"valid repeated public loader nodes preserve feature");callback.function=VK_LAYER_LINK_INFO;require(!enabledFeature(f.device,true,result,why),"duplicate loader function");reason(why,Reason::DuplicateNode);
    callback.function=VK_LOADER_DATA_CALLBACK;const std::underlying_type_t<VkLayerFunction> unknown=99;std::memcpy(&callback.function,&unknown,sizeof(unknown));require(!enabledFeature(f.device,true,result,why),"unknown loader function");reason(why,Reason::LoaderFunction);
    auto foreign=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);f.device.pNext=&foreign;require(!enabledFeature(f.device,true,result,why),"unknown device pNext");reason(why,Reason::Extension);
    std::array<VkLayerDeviceCreateInfo,MaxChain+1> longChain{};for(std::size_t i=0;i<longChain.size();++i){longChain[i].sType=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;longChain[i].function=VK_LAYER_LINK_INFO;longChain[i].pNext=i+1<longChain.size()?&longChain[i+1]:nullptr;}
    // Repeated nodes fail earlier than a capacity overread, never follow an arbitrary long chain.
    f.device.pNext=longChain.data();require(!enabledFeature(f.device,true,result,why),"long malformed chain finite");reason(why,Reason::DuplicateNode);
}

void layouts(){
    Fixture f;Set0Definition set;LayoutDefinition layout;Refusal why;
    require(captureSet0(f.set0,set,why) && set.bindings[5].descriptorCount==3,"full array[3] retained");
    require(captureLayout(f.layout,f.nativeSet,layout,why),"complete ordered push ranges captured");
    require(layout.ranges[0].stageFlags==VK_SHADER_STAGE_FRAGMENT_BIT && layout.ranges[0].offset==64,"push order preserved");
    f.bindings[5].descriptorCount=1;require(set.bindings[5].descriptorCount==3,"owned array independent of overwrite");require(!captureSet0(f.set0,set,why),"native scalar binding never silently expanded");reason(why,Reason::Image9Extent);require(f.bindings[5].descriptorCount==1,"source declaration untouched");
    f.bindings[5].descriptorCount=3;VkSampler immutable=object<VkSampler>(8);f.bindings[6].pImmutableSamplers=&immutable;require(!captureSet0(f.set0,set,why),"immutable handles not async authority");reason(why,Reason::ImmutableSampler);f.bindings[6].pImmutableSamplers=nullptr;
    f.bindings[5].binding=8;require(!captureSet0(f.set0,set,why),"missing image9");reason(why,Reason::MissingImage9);f.bindings[5].binding=9;
    f.set0.bindingCount=MaxBindings+1;f.set0.pBindings=reinterpret_cast<const VkDescriptorSetLayoutBinding*>(std::uintptr_t(1));require(!captureSet0(f.set0,set,why),"oversize binding count refuses before dereference");reason(why,Reason::Bounds);
    f.set0.bindingCount=1;f.set0.pBindings=nullptr;require(!captureSet0(f.set0,set,why),"missing binding array");reason(why,Reason::NullArray);
    f.layout.setLayoutCount=2;require(!captureLayout(f.layout,f.nativeSet,layout,why),"higher native set excludes Set1 injection");reason(why,Reason::Bounds);f.layout.setLayoutCount=1;
    f.pushes[0].offset=UINT32_MAX-3;require(!captureLayout(f.layout,f.nativeSet,layout,why),"push offset overflow refused");reason(why,Reason::PushRange);f.pushes[0].offset=64;
    f.pushes[1].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;require(!captureLayout(f.layout,f.nativeSet,layout,why),"overlapping stage declarations refused");reason(why,Reason::PushRange);
    f.layout.pushConstantRangeCount=MaxPushRanges+1;f.layout.pPushConstantRanges=reinterpret_cast<const VkPushConstantRange*>(std::uintptr_t(1));require(!captureLayout(f.layout,f.nativeSet,layout,why),"push count cap before dereference");reason(why,Reason::Bounds);
}

void definitions(){
    Fixture f;RenderPassDefinition rp;GraphicsDefinition g;Refusal why;GraphicsView view;
    require(captureRenderPass(&f.pass,rp,why),"complete pass copied");require(f.copyGraphics(g,why),"all supported nested graphics state copied");
    auto copy=g;auto moved=std::move(copy);require(moved.view(owned(),view,why),"moved recipe rebases to own nested arrays");
    require(view.info.layout==owned().layout && view.info.renderPass==owned().renderPass && view.info.layout!=f.graphics.layout,"native handles replaced only by explicit owned objects");
    require(view.info.pStages==view.stages.data() && view.info.pVertexInputState==&view.vertex && view.vertex.pVertexBindingDescriptions==moved.vertexBindings.data(),"view self and owned array pointers rebased");
    require(view.stages[0].stage==VK_SHADER_STAGE_FRAGMENT_BIT && view.stages[0].module==owned().fragment,"original reversed stage order preserved");
    require(view.multisample.pSampleMask==moved.sampleMask.data() && *view.multisample.pSampleMask==f.mask,"sample mask copied");
    require(!g.info.layout && !g.info.renderPass && !g.info.pStages && !g.stageInfo[0].module && !g.stageInfo[0].pName,"stored CI carries no native build handle/pointer");
    f.viewports[1].x=999;f.vertexBindings[0].stride=999;f.attrs[1].offset=999;f.colorBlend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;f.dynamics[0]=VK_DYNAMIC_STATE_LINE_WIDTH;f.mask=0;f.attachments[0].format=VK_FORMAT_UNDEFINED;
    require(view.viewport.pViewports[1].x==7 && view.vertex.pVertexBindingDescriptions[0].stride==24 && view.vertex.pVertexAttributeDescriptions[1].offset==0,"original nested arrays overwritten without changing view");
    require(view.blend.pAttachments[0].dstAlphaBlendFactor==VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA && view.dynamic.pDynamicStates[0]==VK_DYNAMIC_STATE_LINE_WIDTH && *view.multisample.pSampleMask!=0,"blend/dynamic/mask ownership");
    RenderPassView rpView;auto rpCopy=rp;require(rpCopy.view(rpView) && rpView.info.pSubpasses==&rpView.subpass && rpView.info.pAttachments==rpCopy.attachments.data(),"renderpass pointers rebased after copy");require(rpView.info.pAttachments[0].format==VK_FORMAT_R32G32B32A32_SFLOAT,"attachment definition survives native input overwrite");
    auto notOwned=owned();notOwned.createdAndOwnedByCaller=false;require(!g.view(notOwned,view,why),"native-looking handle is not inferred owned");reason(why,Reason::OwnedObjectsMissing);
    Fixture bad;bad.graphics.pNext=&bad.dynamic;require(!bad.copyGraphics(g,why),"unknown pipeline pNext");reason(why,Reason::PipelineExtension);bad.graphics.pNext=nullptr;
    bad.graphics.flags=VK_PIPELINE_CREATE_DERIVATIVE_BIT;require(!bad.copyGraphics(g,why),"derivative pipeline refused");reason(why,Reason::PipelineFlags);bad.graphics.flags=0;
    VkSpecializationInfo specialization{};bad.stages[0].pSpecializationInfo=&specialization;require(!bad.copyGraphics(g,why),"specialization refused");reason(why,Reason::Stage);bad.stages[0].pSpecializationInfo=nullptr;
    bad.stages[0].pName="main";require(!bad.copyGraphics(g,why),"exact original entry required");reason(why,Reason::Stage);bad.stages[0].pName="PS_DISTORT";
    bad.dynamic.dynamicStateCount=MaxDynamics+1;bad.dynamic.pDynamicStates=reinterpret_cast<const VkDynamicState*>(std::uintptr_t(1));require(!bad.copyGraphics(g,why),"dynamic array cap before dereference");reason(why,Reason::Bounds);bad.dynamic.dynamicStateCount=2;bad.dynamic.pDynamicStates=bad.dynamics.data();
    bad.dynamics[1]=VK_DYNAMIC_STATE_CULL_MODE;require(!bad.copyGraphics(g,why),"unreviewed extended dynamic state refused");reason(why,Reason::DynamicState);bad.dynamics[1]=VK_DYNAMIC_STATE_SCISSOR;
    bad.dynamics={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};bad.viewport.pViewports=nullptr;bad.viewport.pScissors=nullptr;require(bad.copyGraphics(g,why),"null static viewport arrays accepted only for declared dynamic states");bad.dynamic.dynamicStateCount=0;require(!bad.copyGraphics(g,why),"absent static and dynamic viewport refused");reason(why,Reason::NullArray);bad.dynamic.dynamicStateCount=2;
    bad.viewport.pViewports=reinterpret_cast<const VkViewport*>(std::uintptr_t(1));bad.viewport.pScissors=reinterpret_cast<const VkRect2D*>(std::uintptr_t(1));
    require(bad.copyGraphics(g,why) && !g.hasViewports && !g.hasScissors,"ignored dynamic viewport poison pointers never dereferenced");
    bad.sub.pDepthStencilAttachment=nullptr;bad.graphics.pDepthStencilState=reinterpret_cast<const VkPipelineDepthStencilStateCreateInfo*>(std::uintptr_t(1));
    require(bad.copyGraphics(g,why) && !g.hasDepth,"ignored depth state poison pointer never dereferenced");
    bad.sub.pDepthStencilAttachment=&bad.depthRef;bad.depthRef.attachment=VK_ATTACHMENT_UNUSED;
    require(bad.copyGraphics(g,why) && !g.hasDepth,"unused depth reference also ignores depth state pointer");
    bad.raster.rasterizerDiscardEnable=VK_TRUE;bad.graphics.pViewportState=reinterpret_cast<const VkPipelineViewportStateCreateInfo*>(std::uintptr_t(1));
    require(!bad.copyGraphics(g,why),"discard route refused before ignored viewport object");reason(why,Reason::Scalar);
    Fixture bounds;
    bounds.vertex.vertexAttributeDescriptionCount=MaxAttributes+1;bounds.vertex.pVertexAttributeDescriptions=reinterpret_cast<const VkVertexInputAttributeDescription*>(std::uintptr_t(1));require(!bounds.copyGraphics(g,why),"attribute count bound");reason(why,Reason::Bounds);
    require(!captureRenderPass(nullptr,rp,why),"async renderpass handle is insufficient");reason(why,Reason::RenderPassMissing);
    bad.pass.subpassCount=2;require(!captureRenderPass(&bad.pass,rp,why),"multiple subpasses conservative refusal");reason(why,Reason::RenderPassShape);bad.pass.subpassCount=1;
    bad.color[0].attachment=UINT32_MAX-1;require(!captureRenderPass(&bad.pass,rp,why),"invalid attachment index");reason(why,Reason::AttachmentIndex);
    bad.pass.attachmentCount=MaxAttachments+1;bad.pass.pAttachments=reinterpret_cast<const VkAttachmentDescription*>(std::uintptr_t(1));require(!captureRenderPass(&bad.pass,rp,why),"attachment count cap before dereference");reason(why,Reason::Bounds);
}
void unusedColor(){
    Fixture unused;RenderPassDefinition rp;Refusal why;
    unused.color[0].attachment=VK_ATTACHMENT_UNUSED;unused.sub.pResolveAttachments=reinterpret_cast<const VkAttachmentReference*>(std::uintptr_t(1));
    // This proves branch-before-unneeded-read, not that a poison array is valid
    // Vulkan input under the generic optional-array parameter rules.
    require(!captureRenderPass(&unused.pass,rp,why),"all-unused color refused before dependent resolve pointer");reason(why,Reason::RenderPassShape);
}

std::vector<std::uint32_t> shader(const char* path){std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"explicit private module readable");auto bytes=file.tellg();require(bytes>0 && bytes<=std::streamoff(ShaderBytes) && bytes%4==0,"bounded private module bytes");std::vector<std::uint32_t> data(static_cast<std::size_t>(bytes)/4);file.seekg(0);require(bool(file.read(reinterpret_cast<char*>(data.data()),bytes)),"complete private source");return data;}
void privateComplete(const char* vsPath,const char* psPath){
    auto vs=shader(vsPath),ps=shader(psPath);auto vertex=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO),fragment=vertex;
    vertex.codeSize=vs.size()*4;vertex.pCode=vs.data();fragment.codeSize=ps.size()*4;fragment.pCode=ps.data();
    Fixture f;auto in=f.inputs(vertex,fragment);Recipe recipe;Refusal why;
    require(capture(in,recipe,why) && recipe.definitionComplete,"actual private pins complete owned recipe");require(!recipe.nativeCompatibilityProven && !recipe.nativeLifetimeProven && !recipe.runtimeActivationAuthorized,"complete definition never grants native authority");
    require(recipe.vertex.bytes==9460 && recipe.fragment.bytes==6620,"both exact original shader extents");
    auto copied=recipe;auto moved=std::move(copied);vs.assign(vs.size(),0);ps.assign(ps.size(),0);f.pushes[0].size=4;f.viewports[1].x=-123;
    require(recipe.vertex.words[0]==0x07230203u && moved.fragment.words[0]==0x07230203u,"private shader bytes owned after original storage overwritten");
    GraphicsView graphics;require(moved.graphics.view(owned(),graphics,why) && graphics.viewport.pViewports[1].x==7,"whole recipe move rebases full graphics data");
    LayoutView layout;require(moved.layoutView(object<VkDescriptorSetLayout>(0x110),object<VkDescriptorSetLayout>(0x111),layout) && layout.info.pSetLayouts==layout.sets.data() && layout.info.pPushConstantRanges==moved.layout.ranges.data() && layout.info.pPushConstantRanges[0].size==16,"extended own layout has exact original push ranges");
    require(moved.provenance[1].value==0x20 && !moved.graphics.info.layout,"native scalar provenance is no build handle");
    require(!capture(in,recipe,why),"overwritten original code no longer passes pin");reason(why,Reason::ShaderProfile);require(!recipe.definitionComplete,"failed capture not complete");
    vertex.pCode=moved.vertex.words.data();fragment.pCode=moved.fragment.words.data();in.vertexIdentity.generation=0;require(!capture(in,recipe,why),"missing source generation");reason(why,Reason::Identity);in.vertexIdentity.generation=4;
    in.fragmentIdentity.device=2;require(!capture(in,recipe,why),"foreign-device facts");reason(why,Reason::Identity);in.fragmentIdentity.device=1;
    in.renderPass=nullptr;require(!capture(in,recipe,why),"no async borrow of renderpass");reason(why,Reason::RenderPassMissing);
    auto corrupted=moved.vertex.words;corrupted[7]^=1;vertex.pCode=corrupted.data();ShaderDefinition out;
    require(!captureShader(vertex,ShaderRole::Vertex,out,why),"exact SHA pin prevents plausible native header impostor");reason(why,Reason::ShaderPin);
}
}
int main(int argc,char** argv){
    if(argc==2 && std::strcmp(argv[1],"--unused-color-only")==0){unusedColor();std::cout<<"{\"scope\":\"unused-color-subset\",\"checks\":"<<checks<<"}\n";return 0;}
    require(argc==1 || argc==3,"optional private VS PS paths only");features();layouts();definitions();unusedColor();
    const bool privateInput=argc==3;if(privateInput)privateComplete(argv[1],argv[2]);
    std::cout<<"{\"schema\":\"ssbo-clone-definition-controls-v1\",\"checks\":"<<checks<<",\"private_native_pins_tested\":"<<(privateInput?"true":"false")<<",\"recipe_bytes\":"<<sizeof(Recipe)<<",\"native_compatibility_proven\":false,\"runtime_activation_authorized\":false}\n";
}
