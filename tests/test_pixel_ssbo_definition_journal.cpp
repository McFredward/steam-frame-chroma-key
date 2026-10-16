#include "layer/pixel_ssbo_definition_journal.h"
#include "layer/pixel_ssbo_profile_transform.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {
namespace journal=frame_pixel_ssbo_journal;
namespace clone=frame_pixel_ssbo_clone;
std::atomic<unsigned> checks{0};
void require(bool value,const char* text){++checks;if(!value){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T out{};out.sType=type;return out;}
template<class T>T object(std::uint64_t value){T out{};std::memcpy(&out,&value,sizeof(out));return out;}
void reason(const journal::Event& event,journal::Reason expected){
    require(event.reason==expected,"exact journal refusal");
    require(std::strcmp(journal::reasonName(event.reason),"unknown_journal_refusal")!=0,"bounded known reason");
}
struct Fixture {
    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    VkDescriptorSetLayout nativeSet=object<VkDescriptorSetLayout>(0x10);
    VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_ALL,nullptr};
    VkDescriptorSetLayoutCreateInfo set=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,64};
    VkPipelineLayoutCreateInfo layout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    VkAttachmentDescription attachment{};VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    VkRenderPassCreateInfo pass=info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    VkVertexInputBindingDescription vertexBinding{0,16,VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0,0,VK_FORMAT_R32G32_SFLOAT,4};
    VkPipelineVertexInputStateCreateInfo vertex=info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    VkPipelineInputAssemblyStateCreateInfo assembly=info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    VkViewport screen{3,7,640,480,0,1};VkRect2D rect{{5,9},{640,480}};
    VkPipelineViewportStateCreateInfo viewport=info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    VkPipelineRasterizationStateCreateInfo raster=info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    VkPipelineMultisampleStateCreateInfo samples=info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    VkPipelineColorBlendAttachmentState colorBlend{};
    VkPipelineColorBlendStateCreateInfo blend=info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    VkGraphicsPipelineCreateInfo graphics=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    Fixture(){
        features.fragmentStoresAndAtomics=VK_TRUE;device.pEnabledFeatures=&features;
        set.bindingCount=1;set.pBindings=&binding;layout.setLayoutCount=1;layout.pSetLayouts=&nativeSet;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
        attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
        pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&sub;
        for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=object<VkShaderModule>(0x40);stages[0].pName="VS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=object<VkShaderModule>(0x50);stages[1].pName="PS_DISTORT";
        vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&vertexBinding;vertex.vertexAttributeDescriptionCount=1;vertex.pVertexAttributeDescriptions=&attribute;
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        viewport.viewportCount=1;viewport.pViewports=&screen;viewport.scissorCount=1;viewport.pScissors=&rect;
        raster.polygonMode=VK_POLYGON_MODE_FILL;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;
        samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;colorBlend.colorWriteMask=15;blend.attachmentCount=1;blend.pAttachments=&colorBlend;
        graphics.stageCount=2;graphics.pStages=stages.data();graphics.pVertexInputState=&vertex;graphics.pInputAssemblyState=&assembly;
        graphics.pViewportState=&viewport;graphics.pRasterizationState=&raster;graphics.pMultisampleState=&samples;graphics.pColorBlendState=&blend;
        graphics.layout=object<VkPipelineLayout>(0x20);graphics.renderPass=object<VkRenderPass>(0x30);
    }
    void initialize(journal::Journal& out){require(out.createDevice(object<VkDevice>(1),device,VK_SUCCESS),"successful actual device feature input");}
    void definitions(journal::Journal& out){
        require(out.createSet(nativeSet,set,VK_SUCCESS),"set definition");
        require(out.createLayout(graphics.layout,layout,VK_SUCCESS),"layout definition with own Set0 snapshot");
        require(out.createRenderPass(graphics.renderPass,pass,VK_SUCCESS),"render pass definition");
    }
};
struct Gate {
    std::mutex mutex;std::condition_variable condition;unsigned arrived=0;bool open=false;
    void wait(unsigned count){std::unique_lock<std::mutex> lock(mutex);++arrived;if(arrived==count){open=true;condition.notify_all();}else condition.wait(lock,[&]{return open;});}
};
void deviceControls(){
    Fixture f;journal::Journal j;journal::Event event;
    require(!j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"uninitialized journal refuses");reason(event,journal::Reason::NotInitialized);
    auto broken=f.device;broken.pNext=reinterpret_cast<const void*>(std::uintptr_t(1));
    require(!j.createDevice(object<VkDevice>(1),broken,VK_ERROR_INITIALIZATION_FAILED,&event),"failed native create never reads pointers");reason(event,journal::Reason::NativeCreationFailed);
    require(!j.inspectStats().initialized,"failed device create initializes nothing");
    require(!j.createDevice(VK_NULL_HANDLE,f.device,VK_SUCCESS,&event),"null successful output is not recorded");reason(event,journal::Reason::NullHandle);
    f.initialize(j);const auto stats=j.inspectStats();require(stats.feature.observed && stats.feature.enabled && !stats.feature.nativeAuthority,"actual enabled facts copied with false authority");
    f.features.fragmentStoresAndAtomics=VK_FALSE;require(j.inspectStats().feature.enabled,"later caller overwrite cannot change device facts");
    require(!j.createDevice(object<VkDevice>(2),f.device,VK_SUCCESS,&event),"cannot replace initialized device");reason(event,journal::Reason::AlreadyInitialized);
    j.destroyDevice();require(!j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"destroyed device cannot accept children");reason(event,journal::Reason::DeviceIneligible);
    require(!j.createDevice(object<VkDevice>(1),f.device,VK_SUCCESS,&event),"retired journal never resets generations");reason(event,journal::Reason::AlreadyInitialized);
    journal::Journal disabled;require(!disabled.createDevice(object<VkDevice>(1),f.device,VK_SUCCESS,&event),"supported but disabled feature refuses");reason(event,journal::Reason::DeviceIneligible);
    require(event.contract.reason==clone::Reason::FeatureDisabled && disabled.inspectStats().feature.observed,"feature refusal and observation retained");
    f.features.fragmentStoresAndAtomics=2;journal::Journal malformed;require(!malformed.createDevice(object<VkDevice>(1),f.device,VK_SUCCESS,&event),"noncanonical enabled feature refuses");
    require(event.contract.reason==clone::Reason::FeatureValue,"canonical feature cause exact");
    f.device.pEnabledFeatures=nullptr;journal::Journal absent;require(!absent.createDevice(object<VkDevice>(1),f.device,VK_SUCCESS,&event),"capability-only/absent fact refuses");
    require(!absent.inspectStats().feature.observed && event.contract.reason==clone::Reason::FeatureAbsent,"absence distinguished from false");
    auto feature2=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);feature2.features.fragmentStoresAndAtomics=VK_TRUE;f.device.pNext=&feature2;
    journal::Journal second;f.initialize(second);require(second.inspectStats().feature.source==clone::FeatureSource::Features2,"Features2 actual enabled source retained");
    require(stats.journalEpoch!=second.inspectStats().journalEpoch,"new journal has different token namespace");
}
void publicInventory(){
    Fixture f;journal::Journal j;journal::Event event;f.initialize(j);
    auto bad=f.set;bad.bindingCount=clone::MaxBindings+1;bad.pBindings=reinterpret_cast<const VkDescriptorSetLayoutBinding*>(std::uintptr_t(1));
    require(!j.createSet(f.nativeSet,bad,VK_SUCCESS,&event),"unsupported original set never dereferences oversized array");reason(event,journal::Reason::Contract);require(event.contract.reason==clone::Reason::Bounds,"first nested cause exact");
    const auto first=j.inspectStats().firstRefusal;
    require(!j.createSet(f.nativeSet,bad,VK_ERROR_OUT_OF_HOST_MEMORY,&event),"failed object creation does not inspect CI");reason(event,journal::Reason::NativeCreationFailed);
    for(std::uint64_t i=0;i<journal::SetCapacity;++i){require(j.createSet(object<VkDescriptorSetLayout>(0x10+i),f.set,VK_SUCCESS,&event),"fill finite set inventory");require(event.generation==i+1,"only successful captured creates advance serial");}
    require(!j.createSet(object<VkDescriptorSetLayout>(0x10+journal::SetCapacity),f.set,VK_SUCCESS,&event),"capacity refuses optional tracking only");reason(event,journal::Reason::Capacity);
    require(!j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"live duplicate never replaces existing identity");reason(event,journal::Reason::DuplicateHandle);
    require(j.destroySet(f.nativeSet,&event) && event.generation==1,"destroy entry reports old generation");
    require(j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event) && event.generation==journal::SetCapacity+1,"same raw handle receives fresh generation");
    require(j.destroySet(VK_NULL_HANDLE),"native null destroy remains no-op");
    require(!j.destroySet(object<VkDescriptorSetLayout>(0x999),&event),"unknown destroy named");reason(event,journal::Reason::UnknownObject);
    require(!j.createLayout(f.graphics.layout,f.layout,VK_ERROR_OUT_OF_HOST_MEMORY,&event),"failed layout not captured");
    auto layout=f.layout;auto unknown=object<VkDescriptorSetLayout>(0x999);layout.pSetLayouts=&unknown;
    require(!j.createLayout(f.graphics.layout,layout,VK_SUCCESS,&event),"unknown current layout dependency refuses");reason(event,journal::Reason::UnknownSet);
    layout.setLayoutCount=2;layout.pSetLayouts=reinterpret_cast<const VkDescriptorSetLayout*>(std::uintptr_t(1));
    require(!j.createLayout(f.graphics.layout,layout,VK_SUCCESS,&event),"unsupported set count before dereference");require(event.contract.reason==clone::Reason::Bounds,"layout bound cause");
    require(j.createLayout(f.graphics.layout,f.layout,VK_SUCCESS),"current Set0 joins correctly after reuse");
    require(j.destroySet(f.nativeSet),"native may destroy set after layout creation");
    require(j.createRenderPass(f.graphics.renderPass,f.pass,VK_SUCCESS),"full render pass retained");
    auto pass=f.pass;pass.pNext=reinterpret_cast<const void*>(std::uintptr_t(1));
    require(!j.createRenderPass(object<VkRenderPass>(0x31),pass,VK_SUCCESS,&event),"RP extension named without reading pNext");require(event.contract.reason==clone::Reason::Extension,"RP extension cause");
    require(!j.unsupported(journal::ObjectKind::RenderPass,0x99,VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,&event),"RP2 stays explicit unsupported");reason(event,journal::Reason::UnsupportedRoute);
    require(event.contract.type==VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,"unsupported public route type retained");
    auto shader=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);shader.codeSize=4;shader.pCode=reinterpret_cast<const std::uint32_t*>(std::uintptr_t(1));
    require(!j.createShader(object<VkShaderModule>(0x40),shader,VK_SUCCESS,&event),"nonprofile shader filtered without code read");require(event.contract.reason==clone::Reason::ShaderProfile,"shader profile cause");
    std::array<std::uint32_t,9460/4> impostor{};impostor[0]=0x07230203;shader.codeSize=9460;shader.pCode=impostor.data();
    require(!j.createShader(object<VkShaderModule>(0x40),shader,VK_SUCCESS,&event),"plausible original extent does not bypass pin");require(event.contract.reason==clone::Reason::ShaderPin,"strict shader SHA cause");
    require(!j.createPipeline(object<VkPipeline>(0x60),f.graphics,VK_SUCCESS,&event),"unobserved shaders cannot produce recipe");reason(event,journal::Reason::UnknownShader);
    auto graphics=f.graphics;graphics.layout=object<VkPipelineLayout>(0x99);require(!j.createPipeline(object<VkPipeline>(0x60),graphics,VK_SUCCESS,&event),"unknown layout");reason(event,journal::Reason::UnknownLayout);
    graphics=f.graphics;graphics.renderPass=object<VkRenderPass>(0x99);require(!j.createPipeline(object<VkPipeline>(0x60),graphics,VK_SUCCESS,&event),"unknown pass");reason(event,journal::Reason::UnknownRenderPass);
    graphics=f.graphics;graphics.stageCount=3;graphics.pStages=reinterpret_cast<const VkPipelineShaderStageCreateInfo*>(std::uintptr_t(1));
    require(!j.createPipeline(object<VkPipeline>(0x60),graphics,VK_SUCCESS,&event),"stage bound before array read");require(event.contract.reason==clone::Reason::Stage,"stage refusal exact");
    graphics.pNext=reinterpret_cast<const void*>(std::uintptr_t(1));require(!j.createPipeline(object<VkPipeline>(0x60),graphics,VK_SUCCESS,&event),"unsupported pipeline route rejected before ignored stage data");require(event.contract.reason==clone::Reason::PipelineExtension,"pipeline extension exact");
    const auto stats=j.inspectStats();require(stats.firstRefusal.reason==first.reason && stats.firstRefusal.contract.reason==first.contract.reason,"later failures never overwrite first refusal");
    require(stats.refusals[static_cast<std::size_t>(journal::Reason::Contract)]>=4 && (stats.seenReasons&(1ull<<static_cast<unsigned>(journal::Reason::Capacity))),"fixed counters and deduplicated reason mask");
    journal::Ready ready;require(!j.takeReady(ready,&event) && !ready.recipe.definitionComplete,"no invented ready recipe");reason(event,journal::Reason::NoReady);
    journal::Journal last(UINT64_MAX);f.initialize(last);require(last.createSet(f.nativeSet,f.set,VK_SUCCESS,&event) && event.generation==UINT64_MAX,"last nonzero serial is exact");require(last.destroySet(f.nativeSet),"retire final serial");
    require(!last.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"wrap cannot alias old generation");reason(event,journal::Reason::GenerationExhausted);
    journal::Journal zero(0);f.initialize(zero);require(!zero.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"invalid zero seed always refuses");reason(event,journal::Reason::GenerationExhausted);
}
void publicConcurrency(){
    Fixture f;journal::Journal j;f.initialize(j);Gate start;std::array<std::thread,4> threads;
    for(unsigned t=0;t<threads.size();++t)threads[t]=std::thread([&,t]{
        Fixture local;start.wait(4);
        for(unsigned i=0;i<24;++i){
            const auto set=object<VkDescriptorSetLayout>(0x1000+t);const auto layout=object<VkPipelineLayout>(0x2000+t);
            local.nativeSet=set;
            require(j.createSet(set,local.set,VK_SUCCESS),"parallel independent set creates serialize");
            require(j.createLayout(layout,local.layout,VK_SUCCESS),"parallel layout deep snapshots serialize");
            require(j.destroySet(set),"parallel set retirement");
            require(j.destroyLayout(layout),"parallel layout retirement");
            journal::Ready ready;journal::Event event;require(!j.takeReady(ready,&event) && event.reason==journal::Reason::NoReady,"concurrent consumers cannot invent pipeline recipes");
            const auto stats=j.inspectStats();require(stats.feature.complete && stats.device==1,"concurrent inspect copies complete scalar facts");
        }
    });
    for(auto& thread:threads)thread.join();
    const auto stats=j.inspectStats();require(stats.created==192 && stats.destroyed==192,"all disjoint create/destroy events retained");
    require(stats.refusals[static_cast<std::size_t>(journal::Reason::Capacity)]==0,"ordinary metadata concurrency never poisons capacity");
}
void layoutFlags(){
    Fixture f;journal::Journal j;f.initialize(j);journal::Event event;
    f.layout.flags=VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
    f.layout.pSetLayouts=reinterpret_cast<const VkDescriptorSetLayout*>(std::uintptr_t(1));
    require(!j.createLayout(f.graphics.layout,f.layout,VK_SUCCESS,&event),"unsupported layout flags refuse before any potentially ignored array");
    reason(event,journal::Reason::Contract);require(event.contract.reason==clone::Reason::Bounds,"layout flags preserve contract reason");
}
void metadataDespiteRefusal(){
    Fixture f;journal::Journal j;journal::Event event;
    auto core=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    auto timeline=info<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    auto future=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));
    core.features.fragmentStoresAndAtomics=VK_TRUE;core.pNext=&timeline;timeline.pNext=&future;
    f.device.pEnabledFeatures=nullptr;f.device.pNext=&core;f.binding.descriptorCount=1;
    require(!j.createDevice(object<VkDevice>(1),f.device,VK_SUCCESS,&event),"actual unknown chain still refuses clone eligibility");
    require(event.contract.reason==clone::Reason::Extension && event.contract.type==0x6fffffff,"first still-unknown feature retains exact refusal context");
    require(!j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"metadata discovery never bypasses ineligible device admission");
    auto observed=j.inspectFacts();
    require(observed.deviceObserved && observed.chainCount==3 && observed.chainTypes[2]==0x6fffffff,"all later public headers observed despite first strict refusal");
    require(observed.setCount==1 && observed.sets[0].bindings[0].count==1,"successful native layout observed despite ineligible clone profile");
    const auto stats=j.inspectStats();journal::Ready ready;
    require(!stats.feature.complete && stats.feature.enabled && stats.created==0 && !j.takeReady(ready),"raw diagnostic metadata grants no recipe or native authority");
    f.binding.descriptorCount=3;j.destroyDevice();
    require(!j.createSet(f.nativeSet,f.set,VK_SUCCESS) && j.inspectFacts().setCount==1,"retired device cannot acquire more diagnostic shapes");
}
std::vector<std::uint32_t> readShader(const char* path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);require(bool(input),"private input readable");const auto size=input.tellg();
    require(size>0 && size<=static_cast<std::streamoff>(clone::ShaderBytes) && size%4==0,"bounded private input size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);require(bool(input),"private input read complete");return words;
}
void loadShaders(journal::Journal& j,const std::vector<std::uint32_t>& vs,const std::vector<std::uint32_t>& ps){
    auto ci=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);ci.codeSize=vs.size()*4;ci.pCode=vs.data();
    require(j.createShader(object<VkShaderModule>(0x40),ci,VK_SUCCESS),"exact private VS definition captured");ci.codeSize=ps.size()*4;ci.pCode=ps.data();
    require(j.createShader(object<VkShaderModule>(0x50),ci,VK_SUCCESS),"exact private PS definition captured");
}
void privateRecipes(const std::vector<std::uint32_t>& sourceVs,const std::vector<std::uint32_t>& sourcePs){
    Fixture f;journal::Journal j;journal::Event event;f.initialize(j);
    require(j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event) && event.generation==1,"first set identity");
    require(j.createLayout(f.graphics.layout,f.layout,VK_SUCCESS),"layout snapshots original Set0");
    require(j.destroySet(f.nativeSet),"SetLayout may legally die before graphics create");f.binding.descriptorCount=4;
    require(j.createSet(f.nativeSet,f.set,VK_SUCCESS,&event) && event.generation==3,"reused SetLayout has new generation");
    require(j.createRenderPass(f.graphics.renderPass,f.pass,VK_SUCCESS),"current pass");
    auto vs=sourceVs,ps=sourcePs;loadShaders(j,vs,ps);std::fill(vs.begin(),vs.end(),0);std::fill(ps.begin(),ps.end(),0);
    f.push.size=4;f.attachment.format=VK_FORMAT_R8_UNORM;
    require(!j.createPipeline(object<VkPipeline>(0x60),f.graphics,VK_ERROR_OUT_OF_HOST_MEMORY,&event),"failed native pipeline never made ready");
    require(j.createPipeline(object<VkPipeline>(0x60),f.graphics,VK_SUCCESS,&event) && event.generation==7,"only successful graphics creates get new identities");
    require(!j.createPipeline(object<VkPipeline>(0x60),f.graphics,VK_SUCCESS,&event),"duplicate live pipeline does not replace recipe");reason(event,journal::Reason::DuplicateHandle);
    require(j.destroyLayout(f.graphics.layout) && j.destroyRenderPass(f.graphics.renderPass) && j.destroyShader(object<VkShaderModule>(0x40)) && j.destroyShader(object<VkShaderModule>(0x50)),"all legal intermediate destructions preserve completed own recipe");
    journal::Ready ready;require(j.takeReady(ready,&event) && ready.recipe.definitionComplete,"take independent ready recipe");
    require(ready.recipe.set0.bindings[0].descriptorCount==3 && ready.recipe.provenance[0].generation==1,"layout's old Set0 definition cannot alias reused handle");
    require(ready.recipe.layout.ranges[0].size==64 && ready.recipe.renderPass.attachments[0].format==VK_FORMAT_R32G32B32A32_SFLOAT,"earlier CI array overwrite cannot alter retained defs");
    require(ready.recipe.vertex.words[0]==0x07230203 && ready.recipe.fragment.words[0]==0x07230203,"original private code buffers owned before overwrite");
    require(!ready.nativeAuthority && !ready.recipe.nativeCompatibilityProven && !ready.recipe.nativeLifetimeProven && !ready.recipe.runtimeActivationAuthorized,"definition recipe is no native authority");
    require(j.current(ready.pipeline),"live original pipeline token survives intermediate destruction");
    auto copied=ready;auto moved=std::move(copied);clone::GraphicsView view;clone::Refusal why;
    clone::OwnedObjects owned{object<VkShaderModule>(0x140),object<VkShaderModule>(0x150),object<VkPipelineLayout>(0x120),object<VkRenderPass>(0x130),true};
    f.screen.x=-100;f.attribute.offset=100;require(moved.recipe.graphics.view(owned,view,why) && view.viewport.pViewports[0].x==3 && view.vertex.pVertexAttributeDescriptions[0].offset==4,"taken/moved graphics views rebase to own arrays");
    require(!moved.recipe.graphics.info.layout && !moved.recipe.graphics.info.renderPass && !moved.recipe.graphics.stageInfo[0].module,"no borrowed operational native dependency handles");
    journal::Ready empty;require(!j.takeReady(empty,&event),"recipe consumed only once");reason(event,journal::Reason::NoReady);
    const auto token=ready.pipeline;require(j.destroyPipeline(object<VkPipeline>(0x60)),"pipeline destroy entry cancels future activation");
    require(!j.current(token,&event),"taken copy cannot keep original pipeline current");reason(event,journal::Reason::StalePipeline);
    require(ready.recipe.definitionComplete && ready.recipe.vertex.words[0]==0x07230203,"cancelled token does not invalidate owned recipe bytes");
    Fixture fresh;require(j.createLayout(fresh.graphics.layout,fresh.layout,VK_SUCCESS),"new layout can reference live new Set0");require(j.createRenderPass(fresh.graphics.renderPass,fresh.pass,VK_SUCCESS),"pass handle reuse");loadShaders(j,sourceVs,sourcePs);
    require(j.createPipeline(object<VkPipeline>(0x60),fresh.graphics,VK_SUCCESS),"pipeline numeric handle reused");journal::Ready newer;require(j.takeReady(newer),"take fresh reuse");
    require(newer.pipeline.generation>token.generation && j.current(newer.pipeline) && !j.current(token),"same raw pipeline never aliases old token");
    auto foreign=newer.pipeline;++foreign.device;require(!j.current(foreign),"foreign device token refused");foreign=newer.pipeline;++foreign.journalEpoch;require(!j.current(foreign),"foreign journal token refused");
    for(unsigned i=1;i<journal::PipelineCapacity;++i)require(j.createPipeline(object<VkPipeline>(0x60+i),fresh.graphics,VK_SUCCESS),"fill pipeline inventory");
    require(!j.createPipeline(object<VkPipeline>(0x70),fresh.graphics,VK_SUCCESS,&event),"consumed live recipe slot is not reusable storage");reason(event,journal::Reason::Capacity);
    require(j.destroyPipeline(object<VkPipeline>(0x61)),"destroy pending pipeline");
    require(j.inspectStats().cancelled==1,"queued cancellation counted once");
    require(j.takeReady(empty) && empty.pipeline.value==0x62,"FIFO remaining ready generation after cancellation");
    require(j.takeReady(empty) && empty.pipeline.value==0x63,"next FIFO generation");
    for(unsigned i=4;i<journal::PipelineCapacity;++i)require(j.takeReady(empty)&&empty.pipeline.value==0x60+i,"remaining expanded FIFO variants independently retained");
    require(!j.takeReady(empty),"cancelled pending recipe never delivered");
    journal::Journal replacement;fresh.initialize(replacement);fresh.definitions(replacement);loadShaders(replacement,sourceVs,sourcePs);require(replacement.createPipeline(object<VkPipeline>(0x60),fresh.graphics,VK_SUCCESS),"new journal can see identical device/pipeline handles");
    require(!replacement.current(newer.pipeline),"old device lifetime token cannot alias replacement journal");
    j.destroyDevice();require(!j.current(newer.pipeline),"device destruction cancels all current tokens");
}
void privateConcurrency(const std::vector<std::uint32_t>& vs,const std::vector<std::uint32_t>& ps){
    Fixture f;journal::Journal j;f.initialize(j);f.definitions(j);loadShaders(j,vs,ps);
    Gate begin,consumed;std::atomic<bool> published{false};std::atomic<unsigned> taken{0};std::array<journal::PipelineToken,4> tokens{};
    std::array<std::thread,4> producers;
    for(unsigned t=0;t<producers.size();++t)producers[t]=std::thread([&,t]{
        Fixture local;begin.wait(4);require(j.createPipeline(object<VkPipeline>(0x80+t),local.graphics,VK_SUCCESS),"parallel recipes have independent source definitions");
        consumed.wait(4);if(t==0)published.store(true,std::memory_order_release);
    });
    std::thread consumer([&]{
        while(taken.load(std::memory_order_relaxed)<4){journal::Ready ready;if(j.takeReady(ready)){
            const auto index=taken.fetch_add(1,std::memory_order_relaxed);tokens[index]=ready.pipeline;
            require(ready.recipe.definitionComplete && ready.recipe.vertex.bytes==9460 && ready.recipe.fragment.bytes==6620,"consumer sees only complete independently owned recipe");
        }else std::this_thread::yield();}
    });
    for(auto& producer:producers)producer.join();
    consumer.join();require(published.load(std::memory_order_acquire),"all real producer threads reached completion");
    require(taken==4,"all concurrent recipes delivered exactly once");
    std::array<std::thread,4> destroyers;for(unsigned t=0;t<destroyers.size();++t)destroyers[t]=std::thread([&,t]{require(j.destroyPipeline(object<VkPipeline>(0x80+t)),"parallel retirement");});
    for(auto& destroyer:destroyers)destroyer.join();
    for(const auto& token:tokens)require(!j.current(token),"every consumed token stale after joined destroy");
    require(j.inspectStats().taken==4 && j.inspectStats().created==9,"bounded inventory retains every successful causal event");
}
void privateVariants(const std::vector<std::uint32_t>& vs,const std::vector<std::uint32_t>& ps){
    Fixture f;journal::Journal j(1,{},journal::PendingCount1::RetainFacts);f.initialize(j);
    std::array<VkDescriptorSetLayoutBinding,29> bindings{};
    for(unsigned i=0;i<bindings.size();++i)bindings[i]={i,i<8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<25?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_ALL,nullptr};
    f.set.bindingCount=bindings.size();f.set.pBindings=bindings.data();
    auto shader=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);shader.codeSize=ps.size()*4;shader.pCode=ps.data();clone::ShaderDefinition original;clone::Refusal why;
    require(clone::captureShader(shader,clone::ShaderRole::Fragment,original,why),"private native original PS pins canonical profile input");
    frame_pixel_ssbo_profile::Artifact artifact;require(frame_pixel_ssbo_profile::transform(original,frame_pixel_ssbo_profile::Election::Sparse32,artifact,why),"actual canonical sparse32 profile, never caller Boolean authority");
    std::array<clone::Identity,6> identities{};
    for(unsigned i=0;i<6;++i){
        const auto offset=i*0x1000;f.nativeSet=object<VkDescriptorSetLayout>(0x10+offset);f.graphics.layout=object<VkPipelineLayout>(0x20+offset);f.graphics.renderPass=object<VkRenderPass>(0x30+offset);f.stages[0].module=object<VkShaderModule>(0x40+offset);f.stages[1].module=object<VkShaderModule>(0x50+offset);
        f.definitions(j);
        if(!i)require(j.authorizeProbeInterface(artifact.profile),"first29-binding pending original Set0/layout promote without callback replay");
        shader.codeSize=vs.size()*4;shader.pCode=vs.data();require(j.createShader(f.stages[0].module,shader,VK_SUCCESS),"all six distinct original VS modules retained");
        shader.codeSize=ps.size()*4;shader.pCode=ps.data();require(j.createShader(f.stages[1].module,shader,VK_SUCCESS),"all six distinct original PS modules retained");
        journal::Event event;require(j.createPipeline(object<VkPipeline>(0x60+offset),f.graphics,VK_SUCCESS,&event),"six complete potentially-live variants fit finite inventories");identities[i]={1,event.value,event.generation};
    }
    journal::Ready out;for(unsigned i=0;i<4;++i)require(j.takeReady(out)&&out.pipeline.value==identities[i].value,"four reporting receipts consumed independently");
    const auto taken=j.inspectStats().taken;
    for(const auto i:{5u,4u,0u}){
        require(j.lookupPipeline(identities[i],out)&&out.recipe.provenance[5].generation==identities[i].generation,"exact generation lookup works in opposite creation order and after receipt consumption");
        require(out.recipe.set0.count==29&&out.recipe.set0.probeInterface.sparseGrid32()&&out.recipe.provenance[1].value==0x20+i*0x1000&&out.recipe.provenance[3].value==0x40+i*0x1000,"lookup retains exact own original prefix/layout/shader variant");
        require(!out.nativeAuthority&&!out.recipe.nativeLifetimeProven&&!out.recipe.runtimeActivationAuthorized,"lookup is cold definition metadata only");
    }
    require(j.inspectStats().taken==taken&&j.inspectStats().created==36,"selection never consumes reporting slots or invents creates");
    auto bad=identities[5];++bad.device;require(!j.lookupPipeline(bad,out)&&!out.recipe.definitionComplete,"wrong-device selected identity grants no fallback recipe");bad=identities[5];++bad.generation;require(!j.lookupPipeline(bad,out),"wrong selected generation grants no first-ready fallback");
    require(j.destroyPipeline(object<VkPipeline>(identities[5].value))&&j.createPipeline(object<VkPipeline>(identities[5].value),f.graphics,VK_SUCCESS),"same native numeric handle can be recreated with different exact generation");
    require(!j.lookupPipeline(identities[5],out)&&!out.recipe.definitionComplete,"stale original selection cannot alias reused live pipeline");
    require(j.lookupPipeline(identities[4],out)&&out.pipeline.value==identities[4].value,"independent current actual variant remains selectable after another variant retires");
}
void count1Profile(const char* vsPath,const char* psPath,const char* probePath){
    const auto vs=readShader(vsPath),ps=readShader(psPath);
    std::ifstream input(probePath,std::ios::binary|std::ios::ate);require(bool(input),"private canonical probe readable");const auto bytes=input.tellg();
    require(bytes==std::streamoff(clone::ProbeInterface::Count1Bytes) || bytes==std::streamoff(clone::ProbeInterface::FirstCoveredBytes) || bytes==std::streamoff(clone::ProbeInterface::Sparse32Bytes),"canonical count1 probe extent");std::vector<std::uint32_t> probe(static_cast<std::size_t>(bytes)/4);input.seekg(0);
    require(bool(input.read(reinterpret_cast<char*>(probe.data()),bytes)),"complete canonical count1 probe");
    clone::ProbeInterface profile;clone::Refusal why;require(clone::ProbeInterface::count1(probe.data(),probe.size()*4,profile,why),"actual-byte verified cold count1 interface");
    Fixture f;f.binding.descriptorCount=1;journal::Journal original;f.initialize(original);journal::Event event;
    require(!original.createSet(f.nativeSet,f.set,VK_SUCCESS,&event) && event.contract.reason==clone::Reason::Image9Extent,"existing journal API rejects unchanged original array3 against count1");
    journal::Journal specialized(1,profile);f.initialize(specialized);f.definitions(specialized);
    auto module=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);module.codeSize=vs.size()*4;module.pCode=vs.data();
    require(specialized.createShader(f.stages[0].module,module,VK_SUCCESS),"count1 still requires exact original vertex");
    module.codeSize=ps.size()*4;module.pCode=ps.data();require(specialized.createShader(f.stages[1].module,module,VK_SUCCESS),"count1 still requires exact original fragment");
    require(specialized.createPipeline(object<VkPipeline>(0x80),f.graphics,VK_SUCCESS),"count1 recipe assembled with separately verified original-invocation probe profile");
    journal::Ready ready;require(specialized.takeReady(ready) && ready.recipe.set0.bindings[0].descriptorCount==1 && ready.recipe.set0.probeInterface.specializedCount1(),"journal retains exact count1 definition and verified profile");
    require(!ready.nativeAuthority && !ready.recipe.nativeCompatibilityProven && !ready.recipe.runtimeActivationAuthorized,"cold profile grants no native lease or activation");
    journal::Journal pending(1,{},journal::PendingCount1::RetainFacts);f.initialize(pending);
    require(pending.createSet(f.nativeSet,f.set,VK_SUCCESS,&event),"explicit observational mode retains early successful count1 set callback");const auto setGeneration=event.generation;
    require(pending.createLayout(f.graphics.layout,f.layout,VK_SUCCESS,&event),"early native layout retains owned pending set shape");const auto layoutGeneration=event.generation;
    require(pending.createRenderPass(f.graphics.renderPass,f.pass,VK_SUCCESS),"pending profile does not lose other native definitions");
    require(!pending.createPipeline(object<VkPipeline>(0x80),f.graphics,VK_SUCCESS,&event) && event.contract.reason==clone::Reason::Image9Extent,"no pipeline recipe admitted before actual probe verification");
    require(!pending.authorizeProbeInterface({},&event) && event.contract.reason==clone::Reason::ShaderProfile,"caller default/boolean cannot promote pending observations");
    require(pending.destroySet(f.nativeSet),"actual set may die while native layout retains its shape");
    const auto before=pending.inspectStats();require(pending.authorizeProbeInterface(profile),"actual-byte verified later shader profile promotes original pending layout in place");
    require(pending.authorizeProbeInterface(profile),"identical later profile authorization idempotent");
    const auto after=pending.inspectStats();require(before.created==after.created && before.destroyed==after.destroyed,"promotion neither invents native create callbacks nor changes generations");
    module.codeSize=vs.size()*4;module.pCode=vs.data();require(pending.createShader(f.stages[0].module,module,VK_SUCCESS),"later actual source VS callback");
    module.codeSize=ps.size()*4;module.pCode=ps.data();require(pending.createShader(f.stages[1].module,module,VK_SUCCESS),"later actual source PS callback");
    require(pending.createPipeline(object<VkPipeline>(0x80),f.graphics,VK_SUCCESS),"real later pipeline callback assembles promoted definitions");
    require(pending.takeReady(ready) && ready.recipe.set0.complete && !ready.recipe.set0.pendingCount1 && ready.recipe.set0.probeInterface.sameProfile(profile),"promoted owned recipe has verified exact count1 interface");
    require(ready.recipe.provenance[0].generation==setGeneration && ready.recipe.provenance[1].generation==layoutGeneration,"earlier actual source generations survive promotion even after set destruction");
}
}
int main(int argc,char** argv){
    if(argc==4 && std::strcmp(argv[1],"--variants-only")==0){privateVariants(readShader(argv[2]),readShader(argv[3]));std::cout<<"{\"scope\":\"private-pipeline-variants-subset\",\"checks\":"<<checks.load()<<",\"journal_bytes\":"<<sizeof(journal::Journal)<<",\"native_authority\":false}\n";return 0;}
    if(argc==2 && std::strcmp(argv[1],"--layout-flags-only")==0){layoutFlags();std::cout<<"{\"scope\":\"layout-flags-subset\",\"checks\":"<<checks.load()<<"}\n";return 0;}
    if(argc==2 && std::strcmp(argv[1],"--metadata-facts-only")==0){metadataDespiteRefusal();std::cout<<"{\"scope\":\"metadata-facts-subset\",\"checks\":"<<checks.load()<<"}\n";return 0;}
    require(argc==1 || argc==3 || argc==4,"optional private VS/PS [count1 probe] arguments only");
    deviceControls();publicInventory();publicConcurrency();layoutFlags();metadataDespiteRefusal();
    if(argc>=3){const auto vs=readShader(argv[1]),ps=readShader(argv[2]);privateRecipes(vs,ps);privateConcurrency(vs,ps);privateVariants(vs,ps);}
    if(argc==4)count1Profile(argv[1],argv[2],argv[3]);
    std::cout<<"{\"schema\":\"ssbo-definition-journal-controls-v1\",\"checks\":"<<checks.load()<<",\"journal_bytes\":"<<sizeof(journal::Journal)
             <<",\"event_bytes\":"<<sizeof(journal::Event)<<",\"private_native_pins_tested\":"<<(argc>=3?"true":"false")
             <<",\"native_authority\":false,\"runtime_activation_authorized\":false}\n";
    return 0;
}
