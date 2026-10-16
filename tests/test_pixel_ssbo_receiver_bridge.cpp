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
    require(first?ci->bindingCount==6:ci->bindingCount==1,"original six-binding mock prefix vs owned storage Set1");
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
void privateCallbacks(const char* vertexPath,const char* fragmentPath,unsigned mode){
    calls={};owned_fixture::Driver driver;owned_fixture::driver=&driver;driver.expectedCount=1;
    driver.expectedFragmentBytes=frame_pixel_ssbo_clone::ProbeInterface::Sparse32Bytes;
    owned_fixture::Fixture limits;
    auto native=uniformFixture();
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
    frame_pixel_ssbo_journal::Ready ready;require(journal.takeReady(ready),"one cold ready recipe");
    b.prepare(ready);b.tick(journal);require(!b.snapshot().ready&&b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::None,"queue retrieval may legally arrive later without dropping recipe");
    b.queueRetrieved(3,0,h<VkQueue>(2));b.tick(journal);if(!b.snapshot().ready){auto s=b.snapshot();std::cerr<<"cold failure reason="<<unsigned(s.refusal)<<" clone="<<s.cloneStatus<<" stage="<<s.cloneStage<<" tx="<<s.transaction<<" driver="<<s.cloneDriver<<"\n";}require(b.snapshot().ready&&driver.calls&&calls.fences==1,"late actual queue retrieval completes the retained cold recipe");
    const auto learned=native->finishPass();b.learn(learned);require(!calls.resets,"learning an unarmed live candidate never injects into its already-ended scope");
    require(native->tracker.beginCommand(device,cb,native->beginInfo,true),"next original successful native Begin");
    b.begin(native->tracker,device,cb,false);require(!calls.resets,"explicit manual arm required");
    b.begin(native->tracker,device,cb,true);require(calls.resets==1&&b.tracks(cb),"opaque live next-Begin ticket admits only one bounded GPU reset");
    const auto unrelated=b.beforeRetirement(tx::Retirement::BeginCommand,h<VkCommandBuffer>(999));
    const auto unrelatedPool=b.beforeRetirement(tx::Retirement::ResetPool,VK_NULL_HANDLE,h<VkCommandPool>(999));
    require(!unrelated.selected&&!unrelatedPool.selected,"unrelated original pools and commands never enter the one-generation controller");
    b.afterRetirement(unrelated,VK_SUCCESS);b.afterRetirement(unrelatedPool,VK_SUCCESS);
    native->bind(uiDs,uiPipeline,uiPl);b.pipeline(cb,uiPipeline);
    require(native->tracker.beginRenderPass(device,cb,VK_SUBPASS_CONTENTS_INLINE),"original mock render scope starts");b.beginPass(cb,shape.graphics.renderPass,VK_SUBPASS_CONTENTS_INLINE);
    native->tracker.draw(device,cb);require(!b.draw(native->tracker,device,cb,3,1,0,0,0,false),"preceding UI draw remains native and uninstrumented");
    native->bind();b.pipeline(cb,pipeline);native->tracker.draw(device,cb);
    require(b.draw(native->tracker,device,cb,6,1,0,0,-3,true)&&calls.indexed==1&&!calls.draws,"one exact indexed target replaced once with original args, never duplicated");
    require(native->tracker.observe([&](auto& core){auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;core.createPool(device,h<VkDescriptorPool>(999),pool);}),"observe independent unrelated original descriptor pool");
    b.descriptorPoolRetiring(native->tracker,device,h<VkDescriptorPool>(999));b.descriptorMutation(uiDs);b.resourceRetiring(tx::Resource::Image,999);
    require(b.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::None&&!b.snapshot().transaction,"unrelated descriptor pools, sets and image destruction preserve selected generation");
    if(mode==3)b.resourceRetiring(tx::Resource::Image,frame_bindings::handle(image));
    if(mode==4)b.descriptorPoolRetiring(native->tracker,device,dp);
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
    if(mode==0){
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
}
int main(int argc,char** argv){
    publicGuards();require(argc==1||argc==3,"optional private original VS/PS only");
    if(argc==3)for(unsigned mode=0;mode<5;++mode)privateCallbacks(argv[1],argv[2],mode);
    std::cout<<"{\"scope\":\"owned callback simulation; not native/GPU authority\",\"checks\":"<<checks.load()<<",\"private_pins_tested\":"<<(argc==3?"true":"false")<<",\"passthrough_authorized\":false}\n";
}
