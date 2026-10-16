#include "layer/pixel_definition_facts.h"
#include <cstdlib>
#include <iostream>

namespace facts=frame_pixel_definition_facts;
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T value{};value.sType=type;return value;}
void deviceChain(){
    facts::Facts out;
    auto ci=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto features=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    auto timeline=info<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    auto other=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));
    features.pNext=&timeline;timeline.pNext=&other;ci.pNext=&features;
    facts::device(out,ci,VK_ERROR_INITIALIZATION_FAILED);
    require(!out.deviceObserved && out.chainCount==0,"failed create contributes no observations");
    facts::device(out,ci,VK_SUCCESS);
    require(out.deviceObserved && out.chainEnd==facts::ChainEnd::Complete && out.chainCount==3,"complete public chain survives unrelated unknown feature payload");
    require(out.chainTypes[0]==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && out.chainTypes[1]==1000207000 && out.chainTypes[2]==0x6fffffff,"all headers retained in exact native order");
    timeline.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;other.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    require(out.chainTypes[1]==1000207000 && out.chainTypes[2]==0x6fffffff,"facts own values after native inputs change");
    ci.pNext=nullptr;facts::device(out,ci,VK_SUCCESS);
    require(out.chainCount==3,"later device callback cannot replace original input");
    facts::Facts empty;facts::device(empty,ci,VK_SUCCESS);
    require(empty.chainCount==0 && empty.chainEnd==facts::ChainEnd::Complete,"empty successful chain distinguished from absent observation");
    facts::Facts cycle;other.pNext=&other;ci.pNext=&other;facts::device(cycle,ci,VK_SUCCESS);
    require(cycle.chainEnd==facts::ChainEnd::Cycle && cycle.chainCount==1,"cycle is finite and specifically incomplete");
    std::array<VkBaseInStructure,33> nodes{};
    for(std::size_t i=0;i<nodes.size();++i){nodes[i].sType=static_cast<VkStructureType>(i+1);nodes[i].pNext=i+1<nodes.size()?&nodes[i+1]:nullptr;}
    facts::Facts bound;ci.pNext=nodes.data();facts::device(bound,ci,VK_SUCCESS);
    require(bound.chainEnd==facts::ChainEnd::Bounds && bound.chainCount==32,"fixed header budget rejects excess without following it");
    nodes[31].pNext=nullptr;facts::Facts exact;facts::device(exact,ci,VK_SUCCESS);
    require(exact.chainEnd==facts::ChainEnd::Complete && exact.chainCount==32,"exact final header completes without artificial truncation");
}
void setShapes(){
    facts::Facts out;
    auto device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto ci=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_ALL,nullptr};
    ci.bindingCount=1;ci.pBindings=&binding;
    facts::set(out,ci,VK_SUCCESS);require(out.setCount==0,"no successful device means no independent set observations");
    facts::device(out,device,VK_SUCCESS);facts::set(out,ci,VK_ERROR_OUT_OF_HOST_MEMORY);
    require(out.setCount==0,"failed set create never supplies a successful native shape");
    facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==1 && out.sets[0].bindings[0].count==1,"actual Frame count1 is retained without pretending it matches clone array3");
    facts::set(out,ci,VK_SUCCESS);require(out.setCount==1,"identical hot recreations cannot grow output");
    binding.descriptorCount=3;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==2 && out.sets[0].bindings[0].count==1 && out.sets[1].bindings[0].count==3,"distinct layouts never overwrite prior fact");
    binding.pImmutableSamplers=reinterpret_cast<const VkSampler*>(std::uintptr_t(1));facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==3 && out.sets[2].bindings[0].immutableSampler,"sampler pointer presence copied without following invalid pointer");
    binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==4 && out.sets[3].bindings[0].type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,"unexpected descriptor remains an observation only");
    binding.descriptorCount=8;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==4 && (out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Capacity)),"exhaustion cannot evict earlier shape");
    ci.pBindings=reinterpret_cast<const VkDescriptorSetLayoutBinding*>(std::uintptr_t(1));ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    facts::set(out,ci,VK_SUCCESS);require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Flags),"unknown array semantics refuse before ignored-array dereference");
    ci.flags=0;ci.pNext=reinterpret_cast<const void*>(std::uintptr_t(1));facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Extension),"unknown extension never followed or used to read arrays");
    ci.pNext=nullptr;ci.bindingCount=33;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Bounds),"oversized array refused before access");
    ci.bindingCount=1;ci.pBindings=nullptr;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::NullArray),"missing array has distinct bounded reason");
    ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Shape),"unexpected create shape does not leak payload");
    facts::Facts absent;facts::device(absent,device,VK_SUCCESS);ci=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    binding.binding=8;ci.bindingCount=1;ci.pBindings=&binding;facts::set(absent,ci,VK_SUCCESS);
    require(absent.setCount==0 && absent.setSkipReasons==0,"unrelated layouts do not consume target-shape budget");
}
void creationShapes(){
    facts::Facts out;auto device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto ci=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    ci.stageCount=UINT32_MAX;ci.pStages=reinterpret_cast<const VkPipelineShaderStageCreateInfo*>(std::uintptr_t(1));ci.pDynamicState=reinterpret_cast<const VkPipelineDynamicStateCreateInfo*>(std::uintptr_t(1));
    facts::graphics(out,ci,VK_SUCCESS,10);require(!out.creationCount,"unobserved device cannot contribute creation evidence");
    facts::device(out,device,VK_SUCCESS);facts::graphics(out,ci,VK_ERROR_INITIALIZATION_FAILED,10);require(!out.creationCount,"failed original create never inspects ignored subordinate arrays");
    auto extension=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));ci.pNext=&extension;ci.flags=VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;ci.subpass=3;ci.basePipelineIndex=-1;
    ci.layout=reinterpret_cast<VkPipelineLayout>(std::uintptr_t(20));ci.renderPass=reinterpret_cast<VkRenderPass>(std::uintptr_t(30));
    facts::graphics(out,ci,VK_SUCCESS,10);
    require(out.creationCount==1&&out.creations[0].shape.counts[0]==UINT32_MAX&&out.creations[0].shape.flags==ci.flags,"only safe top-level successful graphics words copied");
    require(out.creations[0].shape.chainCount==1&&out.creations[0].shape.chainTypes[0]==0x6fffffff&&out.creations[0].shape.chainEnd==facts::ChainEnd::Complete,"actual unsupported pNext header retained without payload interpretation");
    ci.layout=reinterpret_cast<VkPipelineLayout>(std::uintptr_t(21));facts::graphics(out,ci,VK_SUCCESS,11);
    require(out.creationCount==1&&out.creations[0].count==2&&out.creations[0].firstObject==10&&out.creations[0].lastObject==11&&out.creations[0].lastLayout==21,"same finite shape deduplicates changing native handles without losing bounded endpoints");
    extension.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;ci.stageCount=2;
    require(out.creations[0].shape.chainTypes[0]==0x6fffffff&&out.creations[0].shape.counts[0]==UINT32_MAX,"later native input overwrite cannot rewrite creation facts");
    auto rp2=info<VkRenderPassCreateInfo2>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);rp2.attachmentCount=3;rp2.subpassCount=1;rp2.dependencyCount=5;rp2.correlatedViewMaskCount=7;rp2.pAttachments=reinterpret_cast<const VkAttachmentDescription2*>(std::uintptr_t(1));
    facts::renderPass2(out,rp2,VK_SUCCESS,50);
    require(out.creationCount==2&&out.creations[1].shape.kind==facts::CreationKind::RenderPass2&&out.creations[1].shape.route==VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2&&out.creations[1].shape.counts==std::array<std::uint32_t,4>{{3,1,5,7}},"original RP2 route and finite counts observed without guessing attachment payloads");
    auto rp=info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);rp.subpassCount=1;facts::renderPass(out,rp,VK_SUCCESS,51);
    require(out.creationCount==3&&out.creations[2].shape.kind==facts::CreationKind::RenderPass,"legacy creation route remains distinguishable");
    facts::Facts capacity;facts::device(capacity,device,VK_SUCCESS);ci.pNext=nullptr;
    for(unsigned i=0;i<facts::MaxCreations;++i){ci.subpass=i;facts::graphics(capacity,ci,VK_SUCCESS,i+1);}
    ci.subpass=facts::MaxCreations;facts::graphics(capacity,ci,VK_SUCCESS,99);
    require(capacity.creationCount==facts::MaxCreations&&capacity.creationOverflow&&capacity.creations[0].firstObject==1,"creation evidence capacity is explicit and never evicts earlier shapes");
    facts::Creation cycle;extension.pNext=&extension;facts::creationChain(cycle,&extension);
    require(cycle.chainCount==1&&cycle.chainEnd==facts::ChainEnd::Cycle,"creation-chain cycle is bounded independently of native admission");
    std::array<VkBaseInStructure,33> nodes{};for(unsigned i=0;i<nodes.size();++i){nodes[i].sType=static_cast<VkStructureType>(i+1);nodes[i].pNext=i+1<nodes.size()?&nodes[i+1]:nullptr;}
    facts::Creation bounded;facts::creationChain(bounded,nodes.data());require(bounded.chainCount==32&&bounded.chainEnd==facts::ChainEnd::Bounds,"creation pNext census has exact finite bound");
}
void graphicsDetails(){
    facts::Facts out;auto device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);facts::device(out,device,VK_SUCCESS);
    auto ci=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].pName="VS_DISTORT";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].pName="PS_DISTORT";ci.stageCount=2;ci.pStages=stages.data();
    ci.layout=reinterpret_cast<VkPipelineLayout>(std::uintptr_t(20));ci.renderPass=reinterpret_cast<VkRenderPass>(std::uintptr_t(30));ci.subpass=2;
    auto dynamic=info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    std::array<VkDynamicState,64> values{};for(unsigned i=0;i<values.size();++i)values[i]=static_cast<VkDynamicState>(1000455000+i);
    values[0]=VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT;dynamic.dynamicStateCount=values.size();dynamic.pDynamicStates=values.data();ci.pDynamicState=&dynamic;
    auto raster=info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);ci.pRasterizationState=&raster;
    auto multisample=info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;ci.pMultisampleState=&multisample;
    auto sample=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);auto unknown=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));sample.pNext=&unknown;multisample.pNext=&sample;
    ci.pDepthStencilState=reinterpret_cast<const VkPipelineDepthStencilStateCreateInfo*>(std::uintptr_t(1));ci.pColorBlendState=reinterpret_cast<const VkPipelineColorBlendStateCreateInfo*>(std::uintptr_t(1));ci.pTessellationState=reinterpret_cast<const VkPipelineTessellationStateCreateInfo*>(std::uintptr_t(1));
    facts::graphicsDetails(out,ci,VK_ERROR_INITIALIZATION_FAILED,1);require(!out.graphicsCount,"failed create does not read deeper graphics pointers");
    facts::graphicsDetails(out,ci,VK_SUCCESS,10);
    const auto& first=out.graphics[0];require(out.graphicsCount==1&&first.dynamicRead==facts::GraphicsRead::Complete&&first.dynamicCount==64&&first.dynamics[0]==1000455017,"all finite original dynamic enum words retained including native sample-locations enable");
    require(first.pipeline==10&&first.layout==20&&first.renderPass==30&&first.subpass==2&&first.sequence==1,"exact target identity and selected subpass cannot disappear in top-level shape deduplication");
    require(first.states[static_cast<unsigned>(facts::GraphicsState::Multisample)].chainCount==2&&first.states[static_cast<unsigned>(facts::GraphicsState::Multisample)].chainTypes[1]==0x6fffffff,"all child extension headers retained without interpreting unknown payload");
    require(first.states[static_cast<unsigned>(facts::GraphicsState::DepthStencil)].read==facts::GraphicsRead::UnknownScope&&first.states[static_cast<unsigned>(facts::GraphicsState::Tessellation)].read==facts::GraphicsRead::Ignored,"unknown selected scope and absent tessellation never dereference poison state pointers");
    values[0]=VK_DYNAMIC_STATE_VIEWPORT;unknown.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    require(first.dynamics[0]==1000455017&&first.states[static_cast<unsigned>(facts::GraphicsState::Multisample)].chainTypes[1]==0x6fffffff,"graphics observation owns dynamic list and nested header values after overwrite");
    for(unsigned i=1;i<facts::MaxGraphics;++i)facts::graphicsDetails(out,ci,VK_SUCCESS,10+i,{true,0,false});
    require(out.graphicsCount==8&&out.graphics[5].pipeline==15&&out.graphics[7].sequence==8,"identical target shapes keep all six actual pipeline creations independently");
    facts::graphicsDetails(out,ci,VK_SUCCESS,100,{true,0,false});require(out.graphicsCount==8&&out.graphicsOverflow&&out.graphicsSequence==9&&out.graphics[0].pipeline==10,"bounded graphics observation overflow is explicit without eviction");
    facts::Facts discard;facts::device(discard,device,VK_SUCCESS);dynamic.dynamicStateCount=0;raster.rasterizerDiscardEnable=VK_TRUE;
    ci.pViewportState=reinterpret_cast<const VkPipelineViewportStateCreateInfo*>(std::uintptr_t(1));ci.pMultisampleState=reinterpret_cast<const VkPipelineMultisampleStateCreateInfo*>(std::uintptr_t(1));
    facts::graphicsDetails(discard,ci,VK_SUCCESS,101,{true,1,true});require(discard.graphics[0].states[static_cast<unsigned>(facts::GraphicsState::Multisample)].read==facts::GraphicsRead::Ignored,"rasterizer discard never follows ignored multisample/viewport/depth/blend pointers");
    facts::Facts vertex;facts::device(vertex,device,VK_SUCCESS);values[0]=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;dynamic.dynamicStateCount=1;ci.pVertexInputState=reinterpret_cast<const VkPipelineVertexInputStateCreateInfo*>(std::uintptr_t(1));
    facts::graphicsDetails(vertex,ci,VK_SUCCESS,102);require(vertex.graphics[0].states[static_cast<unsigned>(facts::GraphicsState::VertexInput)].read==facts::GraphicsRead::Ignored,"fully dynamic vertex input skips entire ignored pointer");
    facts::Facts unsupported;facts::device(unsupported,device,VK_SUCCESS);ci.flags=VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;ci.pStages=reinterpret_cast<const VkPipelineShaderStageCreateInfo*>(std::uintptr_t(1));facts::graphicsDetails(unsupported,ci,VK_SUCCESS,103);
    require(!unsupported.graphicsCount,"library/unknown top-level semantics remain top-level observations only");
    facts::Facts arrays;facts::device(arrays,device,VK_SUCCESS);ci.flags=0;ci.pStages=stages.data();ci.pVertexInputState=nullptr;raster.rasterizerDiscardEnable=VK_FALSE;
    std::array<VkDynamicState,6> blendDynamic{{VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,VK_DYNAMIC_STATE_LOGIC_OP_EXT,VK_DYNAMIC_STATE_BLEND_CONSTANTS}};dynamic.dynamicStateCount=blendDynamic.size();dynamic.pDynamicStates=blendDynamic.data();
    auto viewport=info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);viewport.pViewports=reinterpret_cast<const VkViewport*>(std::uintptr_t(1));viewport.pScissors=reinterpret_cast<const VkRect2D*>(std::uintptr_t(1));ci.pViewportState=&viewport;
    multisample.pNext=nullptr;multisample.pSampleMask=reinterpret_cast<const VkSampleMask*>(std::uintptr_t(1));ci.pMultisampleState=&multisample;
    auto blend=info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);blend.attachmentCount=UINT32_MAX;blend.pAttachments=reinterpret_cast<const VkPipelineColorBlendAttachmentState*>(std::uintptr_t(1));ci.pColorBlendState=&blend;
    facts::graphicsDetails(arrays,ci,VK_SUCCESS,104,{true,1,false});require(arrays.graphicsCount==1&&arrays.graphics[0].states[static_cast<unsigned>(facts::GraphicsState::ColorBlend)].counts[0]==UINT32_MAX,"EDS3 ignored attachment arrays remain untouched while valid parent flags/headers/count scalars are observed");
    ci.pColorBlendState=nullptr;ci.pViewportState=nullptr;ci.pMultisampleState=nullptr;facts::graphicsDetails(arrays,ci,VK_SUCCESS,105,{true,1,false});require(arrays.graphicsCount==2&&!arrays.graphics[1].states[static_cast<unsigned>(facts::GraphicsState::ColorBlend)].present,"optional null fully dynamic parent does not become a fabricated state definition");
}

}
int main(){deviceChain();setShapes();creationShapes();graphicsDetails();std::cout<<"{\"checks\":"<<checks<<",\"facts_bytes\":"<<sizeof(facts::Facts)<<",\"runtime_authority\":false}\n";}
