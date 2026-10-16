// Focused callback integration. Vulkan calls are recorded by the borrowed
// finite fake driver. Optional private ORIGINAL shaders exercise the real cold
// canonical transformer; this fixture is never a GPU/native lifetime proof.
#include "layer/pixel_ssbo_receiver_bridge.h"
#include <algorithm>
#include <set>
#include <fstream>
#include <vector>
#include <iostream>
#include <condition_variable>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main borrowed_capture_controls_main
#include "test_pixel_capture_tracker.cpp"
#undef main
namespace owned_fixture {
#define main borrowed_owned_controls_main
#include "test_pixel_ssbo_owned_clone.cpp"
#undef main
}
namespace journal_fixture {
#define main borrowed_journal_controls_main
#include "test_pixel_ssbo_definition_journal.cpp"
#undef main
}
#pragma GCC diagnostic pop

namespace {
using Bridge=frame_pixel_ssbo_receiver::Bridge;
namespace tx=frame_pixel_ssbo_transaction;
unsigned expectedPrefixBindings=6;
Bridge* failHistoryOnCreate=nullptr;
struct Calls {unsigned resets=0,draws=0,indexed=0,markers=0,fences=0;bool signaled=true;} calls;
VKAPI_ATTR void VKAPI_CALL mockBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
    std::uint32_t,const VkMemoryBarrier*,std::uint32_t,const VkBufferMemoryBarrier*,std::uint32_t,const VkImageMemoryBarrier*){}
VKAPI_ATTR void VKAPI_CALL mockFill(VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,std::uint32_t){++calls.resets;}
VKAPI_ATTR void VKAPI_CALL mockPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
VKAPI_ATTR void VKAPI_CALL mockSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,std::uint32_t,std::uint32_t,const VkDescriptorSet*,std::uint32_t,const std::uint32_t*){}
void emulateShader(){const std::array<std::uint32_t,4> header{{1,0x5353424f,1,32}};std::memcpy(owned_fixture::driver->mapped.data(),header.data(),16);}
VKAPI_ATTR void VKAPI_CALL mockDraw(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){++calls.draws;emulateShader();}
VKAPI_ATTR void VKAPI_CALL mockIndexed(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::int32_t,std::uint32_t){++calls.indexed;emulateShader();}
VKAPI_ATTR VkResult VKAPI_CALL mockFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){++calls.fences;*out=h<VkFence>(9000);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL mockDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){--calls.fences;}
VKAPI_ATTR VkResult VKAPI_CALL mockInvalidate(VkDevice,std::uint32_t,const VkMappedMemoryRange*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL mockReset(VkDevice,std::uint32_t,const VkFence*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL mockStatus(VkDevice,VkFence){return calls.signaled?VK_SUCCESS:VK_NOT_READY;}
VKAPI_ATTR VkResult VKAPI_CALL mockSubmit(VkQueue,std::uint32_t,const VkSubmitInfo*,VkFence){++calls.markers;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL mockSet(VkDevice d,const VkDescriptorSetLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    owned_fixture::common(d,a);const bool first=!owned_fixture::driver->set0;
    if(first && failHistoryOnCreate){auto* bridge=failHistoryOnCreate;failHistoryOnCreate=nullptr;bridge->definitionObservationFailed();}
    require(first?ci->bindingCount==expectedPrefixBindings:ci->bindingCount==1,"original retained prefix vs owned storage Set1");
    const auto result=owned_fixture::driver->create(first?frame_pixel_ssbo_owned::Stage::Set0:frame_pixel_ssbo_owned::Stage::Set1,out);
    if(result==VK_SUCCESS)(first?owned_fixture::driver->set0:owned_fixture::driver->set1)=*out;
    return result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL bridgeProc(VkDevice d,const char* name){
#define PROC(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    PROC(CreateDescriptorSetLayout,mockSet) PROC(CmdPipelineBarrier,mockBarrier) PROC(CmdFillBuffer,mockFill)
    PROC(CmdBindPipeline,mockPipeline) PROC(CmdBindDescriptorSets,mockSets) PROC(CmdDraw,mockDraw) PROC(CmdDrawIndexed,mockIndexed)
    PROC(CreateFence,mockFence) PROC(DestroyFence,mockDestroyFence) PROC(ResetFences,mockReset) PROC(GetFenceStatus,mockStatus) PROC(QueueSubmit,mockSubmit) PROC(InvalidateMappedMemoryRanges,mockInvalidate)
#undef PROC
    return owned_fixture::getProc(d,name);
}
void publicGuards(){
    Bridge b;frame_pixel_ssbo_transaction::RecordView output;frame_pixels::View absent;
    require(!b.publish(absent,output)&&!output.record,"unprepared bridge never publishes partial bytes");
    float priority=1;auto q=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);q.queueFamilyIndex=3;q.queueCount=2;q.pQueuePriorities=&priority;
    auto ci=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);ci.queueCreateInfoCount=1;ci.pQueueCreateInfos=&q;
    b.deviceCreated(h<VkDevice>(1),ci,nullptr,{},{});const auto observed=b.snapshot();
    require(observed.refusal==frame_pixel_ssbo_receiver::Refusal::Device&&observed.queueCount==2,"actual two-queue request refused and retained, never inferred single queue");
    require(!b.publish(absent,output)&&!observed.ready,"failed actual shape grants no clone or callback work");
}
void historyGuards(){
    Bridge b;b.definitionObservationFailed();const auto first=b.snapshot();
    require(first.refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory && !std::strcmp(frame_pixel_ssbo_receiver::refusalName(first.refusal),"definition_history"),"missing history has explicit named sticky refusal");
    b.definitionObservationFailed();require(b.snapshot().cancellationEpoch==first.cancellationEpoch && first.cancellationEpoch==1,"repeated failed callbacks do not restart cancellation generations");
    tx::RecordView output;frame_pixels::View absent;require(!b.publish(absent,output) && !output.record,"unprepared incomplete history cannot publish");
    Bridge earlier;auto ci=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);earlier.deviceCreated(h<VkDevice>(1),ci,nullptr,{},{});earlier.definitionObservationFailed();
    require(earlier.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::Device,"history failure never overwrites an earlier permanent refusal");
}
void privateCallbacks(const char* vertexPath,const char* fragmentPath,unsigned mode){
    calls={};failHistoryOnCreate=nullptr;expectedPrefixBindings=6;owned_fixture::Driver driver;owned_fixture::driver=&driver;driver.expectedCount=1;
    driver.expectedFragmentBytes=frame_pixel_ssbo_clone::ProbeInterface::Sparse32Bytes;
    owned_fixture::Fixture limits;
    auto native=std::make_unique<Fixture>(true,0,VK_COMMAND_BUFFER_LEVEL_PRIMARY,0,true);
    Bridge b;frame_pixel_ssbo_journal::Journal journal{1,{},frame_pixel_ssbo_journal::PendingCount1::RetainFacts};
    journal_fixture::Fixture shape;shape.nativeSet=sl;shape.graphics.layout=pl;shape.graphics.renderPass=h<VkRenderPass>(30);
    shape.stages[0].module=vs;shape.stages[1].module=fs;shape.push={VK_SHADER_STAGE_VERTEX_BIT,0,16};
    std::array<VkDescriptorSetLayoutBinding,6> definitions{};
    for(unsigned i=0;i<5;++i)definitions[i]={frame_pixel_uniform::UniformBindings[i],i==1?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_ALL,nullptr};
    definitions.back()={9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};shape.set.bindingCount=6;shape.set.pBindings=definitions.data();
    float priority=1;auto q=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);q.queueCount=1;q.queueFamilyIndex=3;q.pQueuePriorities=&priority;
    shape.device.queueCreateInfoCount=1;shape.device.pQueueCreateInfos=&q;
    VkPhysicalDeviceProperties properties{};properties.limits=limits.p.limits;
    b.deviceCreated(h<VkDevice>(1),shape.device,bridgeProc,properties,limits.p.memory);
    require(native->tracker.ssboDeviceCreated(device,h<VkDevice>(1)),"production tracker observes actual mock device identity");
    shape.initialize(journal);shape.definitions(journal);
    require(native->tracker.destroyPipeline(device,pipeline)&&native->tracker.destroyShader(device,vs)&&native->tracker.destroyShader(device,fs),"retire synthetic setup target before real original definitions");
    require(native->tracker.observe([&](auto& core){core.destroyPipelineLayout(device,pl);core.createPipelineLayout(device,pl,shape.layout);}),"both observers consume identical original push-range/layout input");
    const auto vertex=journal_fixture::readShader(vertexPath),fragment=journal_fixture::readShader(fragmentPath);
    auto module=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);module.codeSize=fragment.size()*4;module.pCode=fragment.data();
    b.shaderCreated(module,journal);require(b.snapshot().profile,"actual private original PS pins sparse32 canonical count1 artifact");
    module.codeSize=vertex.size()*4;module.pCode=vertex.data();require(journal.createShader(vs,module,VK_SUCCESS)&&native->tracker.createShader(device,vs,module),"same original private VS callback to both observers");
    module.codeSize=fragment.size()*4;module.pCode=fragment.data();require(journal.createShader(fs,module,VK_SUCCESS)&&native->tracker.createShader(device,fs,module),"same original private PS callback to both observers");
    frame_pixel_ssbo_journal::Event event;require(journal.createPipeline(pipeline,shape.graphics,VK_SUCCESS,&event)&&native->tracker.createPipeline(device,pipeline,shape.graphics),"production cold recipe assembled from retained actual private shader definitions");
    require(native->tracker.ssboPipelineCreated(device,pipeline,{1,event.value,event.generation}),"original pipeline creation generation associated synchronously");
    native->bind(); // Rebind after the original layout/pipeline generations changed.
    frame_pixel_ssbo_journal::Ready ready;require(journal.takeReady(ready),"one reporting receipt; does not choose a clone");
    b.tick(journal);require(!b.snapshot().ready&&!driver.calls,"no arbitrary first-created recipe prepared before actual candidate selection");
    const auto learned=native->finishPass();require(!learned.oneTimeSubmit&&learned.retirementRequired,"serial replay control uses flags0 reusable native command recording");
    b.learn(learned);b.tick(journal);require(!b.snapshot().ready&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::None,"exact selected recipe waits for later actual queue retrieval");
    if(mode==5){
        b.definitionObservationFailed();b.queueRetrieved(3,0,h<VkQueue>(2));b.tick(journal);
        require(!b.snapshot().ready && !driver.calls && b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory,"failed history before preparation prevents every owned driver create");
        require(native->tracker.beginCommand(device,cb,native->beginInfo,true),"native recording may continue after cold history refusal");b.begin(native->tracker,device,cb,true);
        require(!calls.resets && !b.tracks(cb) && !b.draw(native->tracker,device,cb,6,1,0,0,0,true),"refused unprepared history cannot adopt or replace original draw");
        require(b.release(VK_SUCCESS) && driver.live.empty() && !calls.fences,"unprepared history refusal retains ordinary safe release");return;
    }
    if(mode==9)failHistoryOnCreate=&b;
    b.queueRetrieved(3,0,h<VkQueue>(2));b.tick(journal);
    if(mode==9){
        require(!b.snapshot().ready && driver.calls && driver.live.empty() && !calls.fences && b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory,"history failure inside cold driver preparation releases unused children before configure");
        require(b.release(VK_SUCCESS) && driver.live.empty(),"cancelled preparation never leaks or double releases children");return;
    }
    require(b.snapshot().ready&&driver.calls&&calls.fences==1,"late actual queue retrieval completes the exact selected cold recipe");
    if(mode==6){
        const auto created=driver.calls;b.definitionObservationFailed();b.tick(journal);
        require(native->tracker.beginCommand(device,cb,native->beginInfo,true),"next original Begin still succeeds after prepared history loss");b.begin(native->tracker,device,cb,true);
        require(b.snapshot().ready && b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory && driver.calls==created && !calls.resets && !b.tracks(cb),"owned ready resources remain releasable but cannot adopt a new generation");
        require(b.release(VK_SUCCESS) && driver.live.empty() && !calls.fences,"prepared history loss preserves explicit device idle and release");return;
    }
    require(!calls.resets,"selected completed candidate never injects into its already-ended scope");
    require(native->tracker.beginCommand(device,cb,native->beginInfo,true),"next original successful native Begin");
    b.begin(native->tracker,device,cb,false);require(!calls.resets,"explicit manual arm required");
    b.begin(native->tracker,device,cb,true);require(calls.resets==1&&b.tracks(cb),"opaque live next-Begin ticket admits only one bounded GPU reset");
    const auto unrelated=b.beforeRetirement(tx::Retirement::BeginCommand,h<VkCommandBuffer>(999));
    const auto unrelatedPool=b.beforeRetirement(tx::Retirement::ResetPool,VK_NULL_HANDLE,h<VkCommandPool>(999));
    require(!unrelated.selected&&!unrelatedPool.selected,"unrelated original pools and commands never enter the one-generation controller");
    b.afterRetirement(unrelated,VK_SUCCESS);b.afterRetirement(unrelatedPool,VK_SUCCESS);
    native->bind(uiDs,uiPipeline,uiPl);b.pipeline(cb,uiPipeline);
    require(native->tracker.beginRenderPass(device,cb,VK_SUBPASS_CONTENTS_INLINE),"original mock render scope starts");b.beginPass(cb,shape.graphics.renderPass,VK_SUBPASS_CONTENTS_INLINE);
    native->tracker.draw(device,cb,nullptr,false);require(!b.draw(native->tracker,device,cb,0,1,0,0,0,false),"zero-count preceding direct draw preserves matching observed ordinals");
    native->tracker.draw(device,cb,nullptr,false);require(!b.draw(native->tracker,device,cb,0,1,0,0,0,true),"zero-count preceding indexed draw preserves matching observed ordinals");
    native->tracker.draw(device,cb);require(!b.draw(native->tracker,device,cb,3,1,0,0,0,false),"preceding UI draw remains native and uninstrumented");
    native->bind();b.pipeline(cb,pipeline);native->tracker.draw(device,cb);
    if(mode==7){
        b.definitionObservationFailed();
        require(!b.draw(native->tracker,device,cb,6,1,0,0,-3,true) && !calls.indexed && !calls.draws,"history loss after adoption leaves target draw to original caller");
        require(b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory && calls.resets==1,"one prior reset does not grant permission after history loss");
        const auto retirement=b.beforeRetirement(tx::Retirement::ResetCommand,cb);b.afterRetirement(retirement,VK_SUCCESS);require(!b.tracks(cb),"failed history still retires exact adopted command generation");
        require(b.release(VK_SUCCESS) && driver.live.empty() && !calls.fences,"adopted history loss does not loosen retirement or leak owned resources");return;
    }
    require(b.draw(native->tracker,device,cb,6,1,0,0,-3,true)&&calls.indexed==1&&!calls.draws,"one exact indexed target replaced once with original args, never duplicated");
    require(native->tracker.observe([&](auto& core){auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;core.createPool(device,h<VkDescriptorPool>(999),pool);}),"observe independent unrelated original descriptor pool");
    b.descriptorPoolRetiring(native->tracker,device,h<VkDescriptorPool>(999));b.descriptorMutation(uiDs);b.resourceRetiring(tx::Resource::Image,999);
    require(b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::None&&!b.snapshot().transaction,"unrelated descriptor pools, sets and image destruction preserve selected generation");
    if(mode==3)b.resourceRetiring(tx::Resource::Image,frame_bindings::handle(image));
    if(mode==4)b.descriptorPoolRetiring(native->tracker,device,dp);
    require(b.snapshot().drawOrdinal==4,"selected original ordinal includes both empty direct/indexed calls plus prior UI draw");
    Candidate candidate;require(native->tracker.endRenderPass(device,cb,candidate),"matching source candidate from production tracker");b.endPass(cb);
    if(mode!=1)b.copyRecorded(candidate);
    b.endCommand(cb,VK_SUCCESS);
    auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&cb;
    for(unsigned i=0;i<(mode?1u:2u);++i){const auto ticket=b.beforeSubmit(h<VkQueue>(2),1,&submit);b.afterSubmit(ticket,h<VkQueue>(2),1,&submit,VK_SUCCESS);}
    frame_pixels::View source;source.source=image;source.sourceGeneration=candidate.binding.image.identity.generation;source.commandGeneration=candidate.commandGeneration;
    source.layout.extent={1152,4608};source.generationRetired=true;
    tx::RecordView record;
    require(!b.publish(source,record),"raw retirement observer alone cannot race the still-open bridge generation");
    const auto ticket=b.beforeRetirement(tx::Retirement::BeginCommand,cb);b.afterRetirement(ticket,VK_SUCCESS);
    require(!b.tracks(cb),"exact old generation closes synchronously before native caller can reuse same raw CB");
    if(mode==8){
        b.definitionObservationFailed();const auto markers=calls.markers;
        require(!b.publish(source,record) && !record.record && !record.bytes && b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory,"retired otherwise-complete record cannot publish after missing definition history");
        b.definitionObservationFailed();require(!b.publish(source,record) && calls.markers==markers,"repeated late history failure neither publishes bytes nor submits work");
    }else if(mode==0){
        require(b.publish(source,record)&&record.execution==2,"independently counted final raw-copy replay matches original-invocation SSBO execution");
        require(native->tracker.beginCommand(device,cb,native->beginInfo,true),"replacement generation legitimately reuses native raw handle");
        b.pipeline(cb,pipeline);b.descriptorMutation(ds);b.resourceRetiring(tx::Resource::Image,frame_bindings::handle(image));
        b.definitionRetiring(frame_pixel_ssbo_journal::ObjectKind::Pipeline,frame_bindings::handle(pipeline));
        const auto later=b.beforeSubmit(h<VkQueue>(2),1,&submit);b.afterSubmit(later,h<VkQueue>(2),1,&submit,VK_SUCCESS);
        require(b.publish(source,record)&&record.execution==2&&b.snapshot().sourceExecutions==2,"replacement generation/descriptor updates never alter old immutable record");
    }else if(mode==1){
        require(!b.publish(source,record)&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::CopyCohort,"missing raw-copy cohort never publishes or strands accepted private submit nonce");
    }else if(mode==3){
        require(!b.publish(source,record)&&b.snapshot().transaction==unsigned(tx::Reason::SourceRetired),"selected original source destruction before raw-copy callback invalidates pairing");
    }else if(mode==4){
        require(!b.publish(source,record)&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DescriptorMutation,"only the actual selected descriptor pool invalidates the open generation");
    }else{
        std::uint32_t bad=0;std::memcpy(driver.mapped.data(),&bad,4);
        require(!b.publish(source,record)&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::Record,"empty/lost fragment winner rejected instead of exporting uninitialized record");
    }
    require(b.release(VK_SUCCESS)&&driver.live.empty()&&!calls.fences,"actual device-destroy entry plus actual idle frees every owned child exactly once");
}
// The recorded Set0 census is29 bindings: UBO0..7, storage8,
// sampled-image9..24 and sampler25..28, all count1/stage ALL. These
// finite callback controls retain six potentially-live full variants; archived
// creation metadata does not establish their simultaneous native lifetimes.
void privateVariantSelection(const char* vertexPath,const char* fragmentPath,unsigned selected,bool stale){
    calls={};expectedPrefixBindings=29;owned_fixture::Driver driver;owned_fixture::driver=&driver;
    driver.expectedCount=1;driver.expectedFragmentBytes=frame_pixel_ssbo_clone::ProbeInterface::Sparse32Bytes;
    owned_fixture::Fixture limits;auto native=std::make_unique<Fixture>(true,0,VK_COMMAND_BUFFER_LEVEL_PRIMARY,0,true);
    Bridge b;frame_pixel_ssbo_journal::Journal journal{1,{},frame_pixel_ssbo_journal::PendingCount1::RetainFacts};
    journal_fixture::Fixture shape;
    std::array<VkDescriptorSetLayoutBinding,29> bindings{};
    for(unsigned i=0;i<bindings.size();++i)bindings[i]={i,i<8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<25?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_ALL,nullptr};
    shape.set.bindingCount=bindings.size();shape.set.pBindings=bindings.data();shape.push={VK_SHADER_STAGE_VERTEX_BIT,0,16};
    float priority=1;auto queue=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);queue.queueCount=1;queue.queueFamilyIndex=3;queue.pQueuePriorities=&priority;
    shape.device.queueCreateInfoCount=1;shape.device.pQueueCreateInfos=&queue;
    VkPhysicalDeviceProperties properties{};properties.limits=limits.p.limits;
    b.deviceCreated(h<VkDevice>(1),shape.device,bridgeProc,properties,limits.p.memory);shape.initialize(journal);
    b.queueRetrieved(3,0,h<VkQueue>(2));require(native->tracker.ssboDeviceCreated(device,h<VkDevice>(1)),"variant selection uses actual original device identity");
    require(native->tracker.destroyPipeline(device,pipeline)&&native->tracker.destroyShader(device,vs)&&native->tracker.destroyShader(device,fs),"retire only synthetic seed target before actual private definitions");
    require(native->tracker.observe([&](auto& core){core.destroyPipelineLayout(device,pl);core.destroySetLayout(device,sl);core.freeSets(device,dp,1,&ds);auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=16;core.createPool(device,h<VkDescriptorPool>(100000),pool);auto sampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);core.createSampler(device,h<VkSampler>(100001),sampler);}),"prepare independent actual-prefix mock descriptor pool and sampler");
    const auto vertex=journal_fixture::readShader(vertexPath),fragment=journal_fixture::readShader(fragmentPath);
    auto module=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);module.codeSize=fragment.size()*4;module.pCode=fragment.data();b.shaderCreated(module,journal);
    std::array<VkDescriptorSet,6> sets{};std::array<VkPipelineLayout,6> layouts{};std::array<VkPipeline,6> pipelines{};std::array<VkRenderPass,6> passes{};
    std::array<frame_pixel_ssbo_clone::Identity,6> identities{};
    for(unsigned i=0;i<6;++i){
        const auto delta=1000*i;shape.nativeSet=h<VkDescriptorSetLayout>(10+delta);shape.graphics.layout=layouts[i]=h<VkPipelineLayout>(20+delta);shape.graphics.renderPass=passes[i]=h<VkRenderPass>(30+delta);sets[i]=h<VkDescriptorSet>(40+delta);pipelines[i]=h<VkPipeline>(110+delta);
        shape.stages[0].module=h<VkShaderModule>(100+delta);shape.stages[1].module=h<VkShaderModule>(101+delta);
        shape.definitions(journal);
        require(native->tracker.observe([&](auto& core){core.createSetLayout(device,shape.nativeSet,shape.set);core.createPipelineLayout(device,layouts[i],shape.layout);auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=h<VkDescriptorPool>(100000);allocate.descriptorSetCount=1;allocate.pSetLayouts=&shape.nativeSet;core.allocateSets(device,allocate,&sets[i]);
            constexpr std::array<VkDeviceSize,5> ranges{{272,2800,400,272,2800}};
            for(unsigned n=0;n<5;++n){VkDescriptorBufferInfo buffer{native->uniformBuffers[n],256,ranges[n]};auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=sets[i];write.dstBinding=frame_pixel_uniform::UniformBindings[n];write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffer;core.updateSets(device,1,&write,0,nullptr);}
            VkDescriptorImageInfo sampler{h<VkSampler>(100001),VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=sets[i];write.dstBinding=25;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;write.pImageInfo=&sampler;core.updateSets(device,1,&write,0,nullptr);
        }),"both observers retain the same29-binding prefix and independent layout");
        native->write(sets[i]);
        module.codeSize=vertex.size()*4;module.pCode=vertex.data();require(journal.createShader(shape.stages[0].module,module,VK_SUCCESS)&&native->tracker.createShader(device,shape.stages[0].module,module),"each variant owns independently observed private VS module");
        module.codeSize=fragment.size()*4;module.pCode=fragment.data();require(journal.createShader(shape.stages[1].module,module,VK_SUCCESS)&&native->tracker.createShader(device,shape.stages[1].module,module),"all twelve original shader module definitions retained");
        frame_pixel_ssbo_journal::Event event;require(journal.createPipeline(pipelines[i],shape.graphics,VK_SUCCESS,&event)&&native->tracker.createPipeline(device,pipelines[i],shape.graphics),"all six distinct full pipeline definitions retained");
        identities[i]={1,event.value,event.generation};require(native->tracker.ssboPipelineCreated(device,pipelines[i],identities[i]),"same successful native callback associates exact journal generation");
    }
    // Model the existing four progress receipts. Selection must not drain or
    // depend on this separate reporting allowance.
    frame_pixel_ssbo_journal::Ready reported;for(unsigned i=0;i<4;++i)require(journal.takeReady(reported)&&reported.pipeline.value==frame_bindings::handle(pipelines[i]),"first four progress receipts do not select a driver clone");
    b.tick(journal);require(!driver.calls&&!b.snapshot().ready,"six creates and four reports alone issue no owned driver call");
    const auto other=h<VkCommandBuffer>(61);auto allocate=native->allocate;allocate.commandBufferCount=1;require(native->tracker.allocateCommands(device,allocate,&other)&&native->tracker.beginCommand(device,other,native->beginInfo,true),"second independent original primary command buffer");
    const auto bind=[&](VkCommandBuffer command,unsigned i){require(native->tracker.bindPipeline(device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[i])&&native->tracker.observe([&](auto& core){core.bindSets(device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,layouts[i],0,1,&sets[i],0,nullptr);}),"bind actual chosen variant and its full native prefix");};
    const auto candidate=[&](VkCommandBuffer command,unsigned i){bind(command,i);Candidate out;require(native->tracker.beginRenderPass(device,command,VK_SUBPASS_CONTENTS_INLINE)&&native->tracker.draw(device,command)&&native->tracker.endRenderPass(device,command,out),"actual original target produces generation-associated candidate metadata");require(out.ssboPipeline.value==identities[i].value&&out.ssboPipeline.generation==identities[i].generation,"candidate identifies its selected draw, never first-created pipeline");return out;};
    const auto fifth=candidate(cb,4),sixth=candidate(other,5);const auto chosen=selected==4?fifth:sixth;const auto ignored=selected==4?sixth:fifth;
    b.learn(chosen);b.learn(ignored);
    if(stale){require(journal.destroyPipeline(pipelines[selected])&&native->tracker.destroyPipeline(device,pipelines[selected]),"selected original generation actually retired before cold lookup");require(journal.createPipeline(pipelines[selected],shape.graphics,VK_SUCCESS),"same numeric pipeline may be reused for another generation");b.tick(journal);require(b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::Selection&&!driver.calls,"stale actual selected generation has no first-ready or reused-handle fallback");require(b.release(VK_SUCCESS)&&driver.live.empty(),"stale selection allocated no Vulkan objects");return;}
    b.tick(journal);require(b.snapshot().ready&&b.snapshot().selectedPipeline.value==identities[selected].value&&calls.fences==1,"cold clone selects actual fifth/sixth pipeline independently of creation/report order");
    if(selected==4){const auto before=b.beforeRetirement(tx::Retirement::ResetPool,VK_NULL_HANDLE,pool);require(native->tracker.resetCommandPool(device,pool),"actual successful pool reset between scalar selection and next Begin");b.afterRetirement(before,VK_SUCCESS);require(b.snapshot().ready&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::None,"legitimate pre-adoption pool reset preserves exact selected object generation");}
    require(native->tracker.beginCommand(device,ignored.command,native->beginInfo,true),"other live candidate begins a later recording");b.begin(native->tracker,device,ignored.command,true);require(!calls.resets,"other concurrently valid pipeline command cannot adopt selected clone");
    require(native->tracker.beginCommand(device,chosen.command,native->beginInfo,true),"selected exact command begins its next real generation");b.begin(native->tracker,device,chosen.command,true);require(calls.resets==1,"only selected command next Begin records one GPU reset");bind(chosen.command,selected);b.pipeline(chosen.command,pipelines[selected]);require(native->tracker.beginRenderPass(device,chosen.command,VK_SUBPASS_CONTENTS_INLINE),"chosen original render pass starts");b.beginPass(chosen.command,passes[selected],VK_SUBPASS_CONTENTS_INLINE);native->tracker.draw(device,chosen.command);require(b.draw(native->tracker,device,chosen.command,6,1,0,0,0,true),"selected exact variant indexed draw receives original-output observer");
    Candidate copied;require(native->tracker.endRenderPass(device,chosen.command,copied),"selected source scope completes");b.endPass(chosen.command);b.copyRecorded(copied);b.endCommand(chosen.command,VK_SUCCESS);
    auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&chosen.command;const auto ticket=b.beforeSubmit(h<VkQueue>(2),1,&submit);b.afterSubmit(ticket,h<VkQueue>(2),1,&submit,VK_SUCCESS);const auto retire=b.beforeRetirement(tx::Retirement::BeginCommand,chosen.command);b.afterRetirement(retire,VK_SUCCESS);
    frame_pixels::View source;source.source=image;source.sourceGeneration=copied.binding.image.identity.generation;source.commandGeneration=copied.commandGeneration;source.layout.extent={1152,4608};source.generationRetired=true;tx::RecordView output;require(b.publish(source,output)&&output.execution==1,"same execution source pairing survives out-of-order multi-variant selection");require(b.release(VK_SUCCESS)&&driver.live.empty()&&!calls.fences,"only selected variant owned children explicitly released");
}
}
int main(int argc,char** argv){
    if(argc==4&&std::strcmp(argv[1],"--history-only")==0){historyGuards();for(unsigned mode=5;mode<=9;++mode)privateCallbacks(argv[2],argv[3],mode);std::cout<<"{\"scope\":\"definition-history-cancellation-subset,fake-dispatch\",\"checks\":"<<checks.load()<<",\"native_authority\":false,\"actual_r2_cause\":false}\n";return 0;}
    if(argc==4&&std::strcmp(argv[1],"--variants-only")==0){privateVariantSelection(argv[2],argv[3],4,false);privateVariantSelection(argv[2],argv[3],5,false);privateVariantSelection(argv[2],argv[3],5,true);std::cout<<"{\"scope\":\"private-pipeline-selection-subset, fake dispatch only\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;}
    publicGuards();historyGuards();require(argc==1||argc==3,"optional private original VS/PS only");
    if(argc==3){for(unsigned mode=0;mode<5;++mode)privateCallbacks(argv[1],argv[2],mode);privateVariantSelection(argv[1],argv[2],4,false);privateVariantSelection(argv[1],argv[2],5,false);privateVariantSelection(argv[1],argv[2],5,true);}
    std::cout<<"{\"scope\":\"owned callback simulation; not native/GPU authority\",\"checks\":"<<checks.load()<<",\"private_pins_tested\":"<<(argc==3?"true":"false")<<",\"passthrough_authorized\":false}\n";
}
