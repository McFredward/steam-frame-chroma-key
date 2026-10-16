// Offline causal dispatch/lifetime tests. This does not contact a receiver or
// prove its frame/eye bindings. Reuse the existing finite owned-object fixture.
#include "../layer/pixel_ssbo_draw_transaction.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
namespace owned_fixture {
#define main borrowed_owned_clone_test_main
#include "test_pixel_ssbo_owned_clone.cpp"
#undef main
}
#pragma GCC diagnostic pop

namespace frame_pixel_ssbo_transaction {
// This test friend models synchronous live-tracker events; production tokens
// have no public factory and can only be minted by the actual LiveTracker.
struct TransactionTestAccess {
    static std::uint64_t epoch(const Transaction& t){return t.epoch_;}
    static Transaction::RecordingTicket recording(std::uint64_t generation=41,std::uint64_t poolGeneration=17){
        Transaction::RecordingTicket t;t.tracker_=reinterpret_cast<const frame_pixel_capture::LiveTracker*>(std::uintptr_t(0x111));
        t.device_=1;t.command_=owned_fixture::object<VkCommandBuffer>(44);t.pool_=owned_fixture::object<VkCommandPool>(55);
        t.commandGeneration_=generation;t.poolGeneration_=poolGeneration;t.family_=7;return t;
    }
    static Transaction::TargetTicket target(const Cohort& source,std::uint64_t ordinal=1){
        Transaction::TargetTicket t;t.tracker_=reinterpret_cast<const frame_pixel_capture::LiveTracker*>(std::uintptr_t(0x111));
        t.device_=1;t.command_=owned_fixture::object<VkCommandBuffer>(44);t.pool_=owned_fixture::object<VkCommandPool>(55);
        t.commandGeneration_=41;t.poolGeneration_=17;t.ordinal_=ordinal;t.pipeline_={1,60,1};t.layout_={1,20,1};t.set0_={1,70,88};t.source_=source;return t;
    }
};
static_assert(!std::is_aggregate_v<Transaction::RecordingTicket> && !std::is_aggregate_v<Transaction::TargetTicket> &&
    !std::is_aggregate_v<Transaction::SubmitTicket> && !std::is_aggregate_v<Transaction::RetirementTicket>,"no public all-true lease aggregates");
}
using namespace frame_pixel_ssbo_transaction;
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
template<class T>T obj(std::uint64_t n){return owned_fixture::object<T>(n);}
struct Recorder {
    enum class Op {Barrier,Fill,Sets,Pipeline,Draw,DrawIndexed};
    struct Command {Op op;std::uint64_t object=0;std::array<std::uint32_t,5> args{};};
    std::vector<Command> commands;
    std::set<std::uint64_t> fences;
    unsigned fenceCreates=0,fenceDestroys=0,fenceResets=0,markerSubmits=0,invalidates=0,polls=0;
    VkResult createResult=VK_SUCCESS,resetResult=VK_SUCCESS,markerResult=VK_SUCCESS,status=VK_NOT_READY,invalidateResult=VK_SUCCESS;
    bool reenter=false,block=false,entered=false,continueCall=false;
    std::mutex mutex;std::condition_variable condition;
    Transaction* transaction=nullptr;
    VkDevice device=obj<VkDevice>(1);VkQueue queue=obj<VkQueue>(2);VkFence fence=obj<VkFence>(3);
    VkCommandBuffer command=obj<VkCommandBuffer>(4);VkCommandPool pool=obj<VkCommandPool>(5);
    VkPipeline original=obj<VkPipeline>(60),probe=VK_NULL_HANDLE;VkPipelineLayout layout=obj<VkPipelineLayout>(20);
    VkRenderPass pass=obj<VkRenderPass>(30);VkBuffer record=VK_NULL_HANDLE;VkDescriptorSet set1=VK_NULL_HANDLE;
    void add(Op op,std::uint64_t value=0,std::array<std::uint32_t,5> args={}){commands.push_back({op,value,args});}
}* recorder=nullptr;
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer cb,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags dependency,
    std::uint32_t memory,const VkMemoryBarrier*,std::uint32_t buffers,const VkBufferMemoryBarrier* barriers,
    std::uint32_t images,const VkImageMemoryBarrier*){
    require(cb==recorder->command && !dependency && !memory && buffers==1 && !images,"reset barriers modify only own record buffer");
    require(barriers->buffer==recorder->record && !barriers->offset && barriers->size==128 &&
        barriers->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED && barriers->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED,"bounded own128B ignored-family barrier");
    if(recorder->commands.empty())require(source==(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT) &&
        destination==VK_PIPELINE_STAGE_TRANSFER_BIT && barriers->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT,"replay-write to GPU reset dependency");
    else if(barriers->dstAccessMask==VK_ACCESS_HOST_READ_BIT)require(source==(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT) &&
        destination==VK_PIPELINE_STAGE_HOST_BIT && barriers->srcAccessMask==(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT),"GPU write to host read after original EndRenderPass");
    else require(source==VK_PIPELINE_STAGE_TRANSFER_BIT && destination==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
        barriers->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT && barriers->dstAccessMask==(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT),"GPU reset to storage writer dependency");
    recorder->add(Recorder::Op::Barrier);
}
VKAPI_ATTR void VKAPI_CALL fill(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes,std::uint32_t value){
    require(cb==recorder->command && buffer==recorder->record && !offset && bytes==128 && !value,"recorded GPU reset, not CPU memset");
    recorder->add(Recorder::Op::Fill);
}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipeline){
    require(cb==recorder->command && point==VK_PIPELINE_BIND_POINT_GRAPHICS,"original exact command/graphics binding");
    recorder->add(Recorder::Op::Pipeline,frame_pixel_ssbo_clone::handle(pipeline));
}
VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,std::uint32_t first,
    std::uint32_t count,const VkDescriptorSet* sets,std::uint32_t dynamic,const std::uint32_t* offsets){
    require(cb==recorder->command && point==VK_PIPELINE_BIND_POINT_GRAPHICS && layout!=recorder->layout && first==1 && count==1 &&
        sets && sets[0]==recorder->set1 && !dynamic && !offsets,"ONLY owned compatible Set1, native Set0/push/dynamic state not rebound");
    recorder->add(Recorder::Op::Sets,frame_pixel_ssbo_clone::handle(sets[0]));
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer cb,std::uint32_t v,std::uint32_t i,std::uint32_t firstV,std::uint32_t firstI){
    require(cb==recorder->command,"one exact original direct draw command");
    recorder->add(Recorder::Op::Draw,0,{v,i,firstV,firstI});
    if(recorder->reenter)recorder->transaction->unknownRoute(cb);
    if(recorder->block){std::unique_lock<std::mutex> lock(recorder->mutex);recorder->entered=true;recorder->condition.notify_all();
        recorder->condition.wait(lock,[]{return recorder->continueCall;});}
}
VKAPI_ATTR void VKAPI_CALL drawIndexed(VkCommandBuffer cb,std::uint32_t n,std::uint32_t i,std::uint32_t first,std::int32_t offset,std::uint32_t instance){
    require(cb==recorder->command,"one exact original indexed draw command");
    recorder->add(Recorder::Op::DrawIndexed,0,{n,i,first,static_cast<std::uint32_t>(offset),instance});
}
VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice device,const VkFenceCreateInfo* ci,const VkAllocationCallbacks* allocator,VkFence* result){
    require(device==recorder->device && ci->sType==VK_STRUCTURE_TYPE_FENCE_CREATE_INFO && !ci->pNext && !ci->flags && !allocator,"cold unsignaled owned fence");
    *result=recorder->createResult==VK_SUCCESS?obj<VkFence>(3+recorder->fenceCreates):obj<VkFence>(0xbad);
    ++recorder->fenceCreates;if(recorder->createResult==VK_SUCCESS)recorder->fences.insert(frame_pixel_ssbo_clone::handle(*result));return recorder->createResult;
}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice device,VkFence fence,const VkAllocationCallbacks* allocator){
    require(device==recorder->device && recorder->fences.erase(frame_pixel_ssbo_clone::handle(fence))==1 && !allocator,"destroy only owned fence");++recorder->fenceDestroys;
}
VKAPI_ATTR VkResult VKAPI_CALL resetFence(VkDevice device,std::uint32_t count,const VkFence* fences){
    require(device==recorder->device && count==1 && recorder->fences.count(frame_pixel_ssbo_clone::handle(fences[0]))==1,"reset own fence only");++recorder->fenceResets;return recorder->resetResult;
}
VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice device,VkFence fence){
    require(device==recorder->device && recorder->fences.count(frame_pixel_ssbo_clone::handle(fence))==1,"query own fence only");++recorder->polls;return recorder->status;
}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue queue,std::uint32_t count,const VkSubmitInfo* infos,VkFence fence){
    require(queue==recorder->queue && count==1 && infos && infos[0].sType==VK_STRUCTURE_TYPE_SUBMIT_INFO && !infos[0].pNext &&
        !infos[0].commandBufferCount && !infos[0].pCommandBuffers && !infos[0].waitSemaphoreCount && !infos[0].signalSemaphoreCount &&
        recorder->fences.count(frame_pixel_ssbo_clone::handle(fence))==1,"only empty completion marker after untouched native submit, no duplicate native draw/CB");
    ++recorder->markerSubmits;return recorder->markerResult;
}
VKAPI_ATTR VkResult VKAPI_CALL invalidate(VkDevice device,std::uint32_t count,const VkMappedMemoryRange* ranges){
    require(device==recorder->device && count==1 && ranges && ranges[0].sType==VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE &&
        !ranges[0].pNext && ranges[0].memory && !ranges[0].offset && ranges[0].size==VK_WHOLE_SIZE,"whole own mapped record invalidate after fence+retirement");
    ++recorder->invalidates;return recorder->invalidateResult;
}
struct Fixture {
    owned_fixture::Driver ownerDriver;Recorder driver;Transaction transaction;
    owned_fixture::Fixture captured;frame_pixel_ssbo_owned::Clone clone;
    VkDescriptorSet set0=obj<VkDescriptorSet>(70);Cohort source{};
    static owned_fixture::Driver* activate(owned_fixture::Driver& d){owned_fixture::driver=&d;return &d;}
    Fixture(bool noncoherent=false):ownerDriver(),driver(),transaction(),captured((activate(ownerDriver),owned_fixture::Fixture{})){
        ownerDriver.noncoherent=noncoherent;
        if(noncoherent)captured.p.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        for(unsigned i=0;i<captured.recipe.provenance.size();++i)captured.recipe.provenance[i]={1,(i+1)*10,1};
        require(clone.prepare(captured.p).status==frame_pixel_ssbo_owned::Status::Ok,"reuse existing real owned-clone finite creation fixture");
        recorder=&driver;driver.transaction=&transaction;driver.probe=clone.pipeline();driver.record=clone.recordBuffer();driver.set1=clone.set1();
        source={Id{1,1001,1},Id{1,1002,1},Id{1,1003,1},VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,{16,16}};
    }
    Configuration config(){Configuration c;
        c.dispatch={barrier,fill,bindPipeline,bindSets,draw,drawIndexed,createFence,destroyFence,resetFence,fenceStatus,submit,invalidate};return c;}
    void observeDevice(){float priority=1;VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex=7;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkDeviceCreateInfo device{};device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
        require(transaction.deviceCreated(driver.device,device,VK_SUCCESS),"actual original single-queue DeviceCreate observed");
        require(transaction.queueRetrieved(driver.device,7,0,driver.queue),"observed original exact GetDeviceQueue");}
    void initialize(){observeDevice();
        require(transaction.poolCreated(driver.pool,7,VK_SUCCESS),"observed original pool creation");
        require(transaction.commandAllocated(driver.pool,driver.command,VK_COMMAND_BUFFER_LEVEL_PRIMARY,VK_SUCCESS),"observed original primary allocation");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        require(transaction.beginSucceeded(driver.command,begin,VK_SUCCESS),"observed successful original flags0 begin");
        require(transaction.configure(clone,config()),"cold clone attaches after already observed live allocation/begin history");}
    void arm(){require(transaction.arm(driver.command),"explicit arm records reset before RP");}
    void prefix(){transaction.pipelineBound(driver.command,driver.original);transaction.set0Bound(driver.command,driver.layout,0,1,&set0,0,Id{1,70,1});
        const std::array<float,4> push{.125f,.25f,.5f,1};transaction.pushConstants(driver.command,driver.layout,VK_SHADER_STAGE_VERTEX_BIT,0,16,push.data());
        transaction.sourceBound(driver.command,source);}
    void record(){initialize();arm();require(transaction.beginRenderPass(driver.command,driver.pass,VK_SUBPASS_CONTENTS_INLINE),"matching original inline legacy pass");prefix();
        require(transaction.draw(driver.command,3,2,4,5)==DrawResult::ReplacedOriginal,"single instrumented original draw replaces forwarding");
        transaction.endRenderPass(driver.command);transaction.endCommand(driver.command,VK_SUCCESS);}
    Transaction::SubmitTicket before(){VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&driver.command;
        return transaction.beforeSubmit(driver.queue,1,&info);}
    SourceReadbackMetadata readback(){return {source,driver.command,driver.pool,transaction.generation(),transaction.poolGeneration(),transaction.execution()};}
    void submitOriginal(){const auto ticket=before();require(transaction.afterSubmit(ticket,VK_SUCCESS),"observed original submit then owned empty marker");}
    void retire(){const auto ticket=transaction.beforeRetirement(Retirement::ResetCommand,driver.command);require(transaction.afterRetirement(ticket,VK_SUCCESS),"exact old generation reset retirement");}
    void release(){if(!transaction.retired())retire();transaction.afterDeviceWaitIdle(VK_SUCCESS);require(transaction.release(),"explicit retired+native-quiescence release");require(ownerDriver.live.empty(),"no owned clone leak");}
};
void serial(bool coherent){
    Fixture f(!coherent);f.record();const auto recorded=f.driver.commands;
    require(recorded.size()==8 && recorded[0].op==Recorder::Op::Barrier && recorded[1].op==Recorder::Op::Fill && recorded[2].op==Recorder::Op::Barrier,
        "exact prepass GPU reset dependency sequence");
    require(recorded[3].op==Recorder::Op::Sets && recorded[4].op==Recorder::Op::Pipeline && recorded[5].op==Recorder::Op::Draw &&
        recorded[6].op==Recorder::Op::Pipeline && recorded[6].object==frame_pixel_ssbo_clone::handle(f.driver.original),"immediate original-pipeline restore, no inserted command");
    require(recorded[5].args==std::array<std::uint32_t,5>{3,2,4,5,0},"all original draw parameters preserved");
    RecordView view;
    // Replay the SAME recorded CB three times. The fake GPU recognizes the
    // recorded Fill command on every execution, not any CPU reinitialization.
    std::array<std::uint32_t,32> gpu{};
    for(unsigned execution=1;execution<=3;++execution){
        gpu.fill(0xdeadbeef);unsigned draws=0;
        for(const auto& command:recorded){if(command.op==Recorder::Op::Fill)gpu.fill(0);
            else if(command.op==Recorder::Op::Draw){require(gpu[0]==0,"each serial replay executes GPU zero before the one draw");gpu[0]=execution;++draws;}}
        require(draws==1 && gpu[0]==execution,"one fresh original draw cohort per serial execution");
        f.driver.status=VK_SUCCESS;f.submitOriginal();
        require(!f.transaction.publish(f.readback(),view) && !view.record,"fence completion cannot publish reusable generation before retirement");
    }
    require(f.driver.commands.size()==recorded.size() && f.driver.markerSubmits==3,"record once, three markers, no hidden rerecord/duplicate native submit");
    f.retire();f.driver.status=VK_NOT_READY;require(!f.transaction.publish(f.readback(),view),"retirement alone cannot substitute fence completion");
    f.driver.status=VK_SUCCESS;require(f.transaction.publish(f.readback(),view) && view.record==f.clone.mappedRecord() && view.bytes==128 &&
        view.commandGeneration==1 && view.execution==3,"final serial cohort published after exact fence+retirement");
    require(f.driver.invalidates==(coherent?0u:1u),"noncoherent read invalidated only after full proof");
    require(!f.transaction.release(),"retired marker is not native device quiescence for release");f.release();
}
void boundedRefusals(){
    for(unsigned mode=0;mode<14;++mode){Fixture f;f.initialize();if(mode!=0)f.arm();
        if(mode==0){require(!f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"unarmed native pass stays original-only");require(!f.transaction.arm(f.driver.command),"cannot insert prepass reset after native pass began");}
        else {
            require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"test pass");f.prefix();
            if(mode==1)f.transaction.set0Bound(f.driver.command,obj<VkPipelineLayout>(999),0,1,&f.set0,0,Id{1,70,1});
            if(mode==2)f.transaction.set0Bound(f.driver.command,f.driver.layout,0,1,&f.set0,1,Id{1,70,1});
            if(mode==3){auto wrong=f.source;wrong.image.generation++;f.transaction.sourceBound(f.driver.command,wrong);}
            if(mode==4)f.transaction.set0Bound(f.driver.command,f.driver.layout,0,1,&f.set0,0,Id{1,70,0});
            if(mode==5)f.transaction.nextSubpass(f.driver.command);
            if(mode==6)f.transaction.executeSecondary(f.driver.command);
            if(mode==7)f.transaction.unknownRoute(f.driver.command);
            const auto result=f.transaction.draw(f.driver.command,mode==8?0:3,1,0,0);
            if(mode==3){require(result==DrawResult::ReplacedOriginal,"source selection accepts observed new image generation before selected draw");
                f.transaction.sourceBound(f.driver.command,f.source);}
            else if(mode>=9){require(result==DrawResult::ReplacedOriginal,"selected before later invalidation");
                if(mode==9)require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"later original draw forwarded, never instrumented twice");
                if(mode==10)f.transaction.resourceRetiring(Resource::Image,f.source.image);
                if(mode==11)f.transaction.resourceRetiring(Resource::View,f.source.view);
                if(mode==12)f.transaction.resourceRetiring(Resource::Memory,f.source.memory);
                if(mode==13)f.transaction.laterDraw(f.driver.command);
            }else require(result==DrawResult::ForwardOriginal,"ineligible draw remains original-only");
        }
        require(f.transaction.refusal()!=Reason::None,"all paths give precise bounded refusal");
        RecordView view;require(!f.transaction.publish(f.readback(),view) && !view.record,"invalid recording never publishes");f.release();
    }
}
void ticketsAndFences(){
    for(unsigned mode=0;mode<12;++mode){Fixture f;f.record();RecordView view;
        if(mode==0){const auto good=f.before();require(!f.transaction.afterSubmit(Transaction::SubmitTicket{},VK_SUCCESS),"default token grants no native completion lease");
            require(f.transaction.afterSubmit(good,VK_SUCCESS),"failed fake token cannot consume actual token");
            require(!f.transaction.afterSubmit(good,VK_SUCCESS) && f.driver.markerSubmits==1,"submit token is single-use");}
        if(mode==1){const auto ticket=f.before();require(!f.transaction.afterSubmit(ticket,VK_ERROR_OUT_OF_DEVICE_MEMORY) && !f.driver.markerSubmits,"failed native submit does not create marker");}
        if(mode==2){f.driver.markerResult=VK_ERROR_DEVICE_LOST;const auto ticket=f.before();require(!f.transaction.afterSubmit(ticket,VK_SUCCESS),"marker failure refuses publish");}
        if(mode==3){f.driver.resetResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;const auto ticket=f.before();require(!f.transaction.afterSubmit(ticket,VK_SUCCESS) && !f.driver.markerSubmits,"fence reset failure preserves original submit, no marker");}
        if(mode==4){f.submitOriginal();(void)f.before();require(f.transaction.refusal()==Reason::Overlap,"parallel same reusable CB execution refused");}
        if(mode==5){const auto pending=f.before();(void)f.before();require(f.transaction.refusal()==Reason::Overlap,"unresolved native submit call refused");
            require(!f.transaction.afterSubmit(pending,VK_ERROR_OUT_OF_HOST_MEMORY),"finish original call before retiring its generation");}
        if(mode==6){VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&f.driver.command;
            (void)f.transaction.beforeSubmit(obj<VkQueue>(999),1,&info);require(f.transaction.refusal()==Reason::UnknownQueue,"cross-queue history never accepted");}
        if(mode==7){VkCommandBuffer duplicate[2]{f.driver.command,f.driver.command};VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=2;info.pCommandBuffers=duplicate;
            (void)f.transaction.beforeSubmit(f.driver.queue,1,&info);require(f.transaction.refusal()==Reason::SubmitShape,"same CB twice in batch refused");}
        if(mode==8){f.submitOriginal();f.driver.status=VK_SUCCESS;f.retire();auto wrong=f.readback();wrong.source.view.generation++;
            require(!f.transaction.publish(wrong,view) && f.transaction.refusal()==Reason::CohortMismatch,"equal handles but new source generation cannot publish");}
        if(mode==9){f.submitOriginal();f.retire();f.driver.status=VK_ERROR_DEVICE_LOST;require(!f.transaction.publish(f.readback(),view),"fence device-loss never publishes");}
        if(mode==10){f.submitOriginal();f.driver.status=VK_SUCCESS;
            const auto old=f.transaction.beforeRetirement(Retirement::BeginCommand,f.driver.command);
            require(!f.transaction.afterRetirement(old,VK_ERROR_OUT_OF_HOST_MEMORY) && !f.transaction.retired(),"failed replacement Begin does not retire old generation");
            const auto fresh=f.transaction.beforeRetirement(Retirement::BeginCommand,f.driver.command);
            require(!f.transaction.afterRetirement(old,VK_SUCCESS) && !f.transaction.retired(),"stale old retirement nonce cannot retire current saved generation");
            require(f.transaction.afterRetirement(fresh,VK_SUCCESS),"actual current pre-call retirement ticket");
            require(!f.transaction.afterRetirement(fresh,VK_SUCCESS),"retirement token single use");}
        if(mode==11){const auto other=f.transaction.beforeRetirement(Retirement::ResetPool,VK_NULL_HANDLE,obj<VkCommandPool>(999));
            require(!f.transaction.afterRetirement(other,VK_SUCCESS) && !f.transaction.retired(),"foreign pool reset cannot cancel matching raw command handle");}
        f.release();
    }
    // A private ticket from another object has no authority even at matching
    // raw handles and generation 1. Keep the first object's driver active only
    // for its own calls; foreign-ticket rejection performs no Vulkan calls.
    Fixture first;first.record();const auto firstTicket=first.before();
    Fixture second;second.record();const auto secondTicket=second.before();
    require(!second.transaction.afterSubmit(firstTicket,VK_SUCCESS),"cross-controller token cannot fabricate lifetime lease");
    require(second.transaction.afterSubmit(secondTicket,VK_SUCCESS),"own token remains usable");second.release();
    recorder=&first.driver;owned_fixture::driver=&first.ownerDriver;
    require(first.transaction.afterSubmit(firstTicket,VK_SUCCESS),"first owner token retained");first.release();
}
void boundedNativeBatches(){
    for(unsigned mode=0;mode<10;++mode){Fixture f;f.record();
        VkCommandBuffer commands[3]{obj<VkCommandBuffer>(100),f.driver.command,obj<VkCommandBuffer>(101)};
        VkSemaphore semaphores[2]{obj<VkSemaphore>(102),obj<VkSemaphore>(103)};
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;std::uint64_t values[2]{7,9};
        VkTimelineSemaphoreSubmitInfo timeline{};timeline.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timeline.waitSemaphoreValueCount=timeline.signalSemaphoreValueCount=1;timeline.pWaitSemaphoreValues=&values[0];timeline.pSignalSemaphoreValues=&values[1];
        VkSubmitInfo infos[3]{};for(unsigned i=0;i<3;++i){infos[i].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;infos[i].commandBufferCount=1;infos[i].pCommandBuffers=&commands[i];}
        infos[1].pNext=&timeline;infos[1].waitSemaphoreCount=1;infos[1].pWaitSemaphores=&semaphores[0];infos[1].pWaitDstStageMask=&stage;
        infos[1].signalSemaphoreCount=1;infos[1].pSignalSemaphores=&semaphores[1];
        if(mode==1)commands[2]=f.driver.command;
        if(mode==2)timeline.signalSemaphoreValueCount=0;
        if(mode==3)timeline.pWaitSemaphoreValues=nullptr;
        if(mode==4)timeline.pNext=&infos[0];
        if(mode==5)timeline.sType=VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO;
        if(mode==6)infos[1].commandBufferCount=65;
        if(mode==7)infos[1].pCommandBuffers=nullptr;
        if(mode==8)commands[1]=obj<VkCommandBuffer>(111);
        const auto before=infos[1];const auto beforeTimeline=timeline;const auto token=f.transaction.beforeSubmit(f.driver.queue,mode==9?33:3,infos);
        require(infos[1].pNext==before.pNext && infos[1].pCommandBuffers==before.pCommandBuffers && infos[1].commandBufferCount==before.commandBufferCount &&
            timeline.waitSemaphoreValueCount==beforeTimeline.waitSemaphoreValueCount && timeline.signalSemaphoreValueCount==beforeTimeline.signalSemaphoreValueCount &&
            timeline.pWaitSemaphoreValues==beforeTimeline.pWaitSemaphoreValues && timeline.pSignalSemaphoreValues==beforeTimeline.pSignalSemaphoreValues && timeline.pNext==beforeTimeline.pNext,"no original batch, semaphore values, arrays or pNext changed");
        if(mode==0){require(f.transaction.afterSubmit(token,VK_SUCCESS) && f.driver.markerSubmits==1,"original bounded multi-CB/multi-info canonical timeline submit accepted once");
            require(f.transaction.submitObservation().selectedOccurrences==1,"exact selected occurrence reported");}
        else if(mode==8){require(!f.transaction.afterSubmit(token,VK_SUCCESS) && f.transaction.refusal()==Reason::None && !f.driver.markerSubmits,"unrelated valid native batch does not poison capture or create marker");
            f.submitOriginal();}
        else require(!f.transaction.afterSubmit(token,VK_SUCCESS) && f.transaction.refusal()!=Reason::None && !f.driver.markerSubmits,
            "duplicate, unknown extension, inconsistent counts, null arrays or bounds fail closed without modifying original");
        f.release();
    }
}
void transactionEpochReuse(){
    alignas(Transaction) std::array<std::byte,sizeof(Transaction)> memory{};
    auto* first=new(memory.data())Transaction;
    const auto firstEpoch=TransactionTestAccess::epoch(*first);first->~Transaction();
    auto* second=new(memory.data())Transaction;
    const auto secondEpoch=TransactionTestAccess::epoch(*second);
    require(firstEpoch && secondEpoch && firstEpoch!=secondEpoch,"reused controller address cannot reuse private token authority epoch");
    second->~Transaction();
}
void rejectSharedReferencedOwner(){
    Fixture f;f.initialize();Transaction second;
    float priority=1;VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex=7;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo device{};device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
    require(second.deviceCreated(f.driver.device,device,VK_SUCCESS) && second.queueRetrieved(f.driver.device,7,0,f.driver.queue),"second observed cold controller");
    require(second.configure(f.clone,f.config()),"second observer configured while Clone still unreferenced");
    require(second.adoptRecording(TransactionTestAccess::recording()),"second observer has its own known original generation");
    f.arm();const auto commands=f.driver.commands.size();
    require(!second.arm(obj<VkCommandBuffer>(44)) && f.driver.commands.size()==commands,"second controller cannot record using an already referenced Clone");
    require(!second.release() && f.clone.ready() && f.clone.referenced(),"unarmed observer cannot certify retirement of another controller's recorded resources");
    f.release();require(second.release() && f.driver.fences.empty(),"after actual owner release second cold fence is independently disposable");
}
void sourceCohortMismatch(){
    for(unsigned mode=0;mode<9;++mode){Fixture f;f.record();f.submitOriginal();f.retire();f.driver.status=VK_SUCCESS;
        auto stale=f.readback();
        if(mode==0)stale.command=obj<VkCommandBuffer>(99);
        if(mode==1)stale.commandGeneration++;
        if(mode==2)stale.pool=obj<VkCommandPool>(99);
        if(mode==3)stale.poolGeneration++;
        if(mode==4)stale.execution++;
        if(mode==5)stale.source.image.generation++;
        if(mode==6)stale.source.view.generation++;
        if(mode==7)stale.source.memory.generation++;
        if(mode==8)stale.source.extent.width++;
        RecordView view;require(!f.transaction.publish(stale,view) && !view.record && f.transaction.refusal()==Reason::CohortMismatch,
            "same source bytes cannot publish for wrong command/pool/serial/image/view/memory/extent cohort");f.release();
    }
}
void adoptionAndQueueHistory(){
    {Fixture f;f.observeDevice();require(f.transaction.configure(f.clone,f.config()),"cold attach with no invented pool/allocation callbacks");
        const auto token=TransactionTestAccess::recording();
        require(f.transaction.adoptRecording(token) && f.transaction.generation()==41,"opaque fresh synchronous tracker begin adopts existing generation");
        f.driver.command=obj<VkCommandBuffer>(44);f.driver.pool=obj<VkCommandPool>(55);f.arm();
        require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"adopted actual legacy pass");
        require(f.transaction.adoptTargetState(TransactionTestAccess::target(f.source)),"opaque target snapshot proves actual native state without rebinding it");
        require(f.transaction.drawIndexed(f.driver.command,12,1,3,-7,0)==DrawResult::ReplacedOriginal,"adopted native indexed path");
        f.transaction.endRenderPass(f.driver.command);f.transaction.endCommand(f.driver.command,VK_SUCCESS);f.submitOriginal();f.retire();
        f.driver.status=VK_SUCCESS;RecordView view;require(f.transaction.publish(f.readback(),view) && view.commandGeneration==41,"real external generation retained exactly");f.release();}
    for(unsigned mode=0;mode<7;++mode){Fixture f;f.observeDevice();require(f.transaction.configure(f.clone,f.config()),"adoption negative fixture attach");
        if(mode==0)require(!f.transaction.adoptRecording(Transaction::RecordingTicket{}),"default all-zero token is not a native begin lease");
        else {
            const auto token=TransactionTestAccess::recording();require(f.transaction.adoptRecording(token),"adopt first valid generation");
            if(mode==1)require(!f.transaction.adoptRecording(token),"same recording token cannot be replayed later");
            else {f.driver.command=obj<VkCommandBuffer>(44);f.driver.pool=obj<VkCommandPool>(55);f.arm();
                require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"adoption target negative fixture pass");
                auto source=f.source;if(mode==3)source.image.device=999;if(mode==4)source.layout=VK_IMAGE_LAYOUT_GENERAL;
                const auto target=mode==2?Transaction::TargetTicket{}:TransactionTestAccess::target(source,mode==5?2:1);
                if(mode==6){require(f.transaction.adoptTargetState(target),"target complete source snapshot");
                    require(!f.transaction.adoptRecording(TransactionTestAccess::recording(42)),"cannot replace a referenced armed generation with a new tracker begin");}
                else require(!f.transaction.adoptTargetState(target),"default/stale/wrong-device/layout/ordinal target token refused");
            }
        }
        if(f.transaction.generation()){f.driver.command=obj<VkCommandBuffer>(44);f.driver.pool=obj<VkCommandPool>(55);f.release();}
        else {require(f.transaction.release(),"failed unreferenced adoption releases cold owner");}
    }
    // Retain lifecycle BEFORE attaching the Clone; earlier native generations
    // complete normally. A later Begin gets a new identity without fake replay.
    {Fixture f;f.observeDevice();require(f.transaction.poolCreated(f.driver.pool,7,VK_SUCCESS),"pool originally created before cold preparation");
        require(f.transaction.commandAllocated(f.driver.pool,f.driver.command,VK_COMMAND_BUFFER_LEVEL_PRIMARY,VK_SUCCESS),"allocation originally existed");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        require(f.transaction.beginSucceeded(f.driver.command,begin,VK_SUCCESS),"first original begin before clone");
        f.transaction.pipelineBound(f.driver.command,obj<VkPipeline>(999));f.transaction.set0Bound(f.driver.command,obj<VkPipelineLayout>(999),0,1,&f.set0,0,Id{1,70,1});
        require(!f.transaction.beginRenderPass(f.driver.command,obj<VkRenderPass>(999),VK_SUBPASS_CONTENTS_INLINE),"prior unrelated native scope is not activated");
        require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"prior native recording untouched");
        f.transaction.endRenderPass(f.driver.command);f.transaction.endCommand(f.driver.command,VK_SUCCESS);
        const auto retirement=f.transaction.beforeRetirement(Retirement::ResetPool,f.driver.command,f.driver.pool);
        require(f.transaction.afterRetirement(retirement,VK_SUCCESS),"retire exact earlier generation before clone");
        require(f.transaction.beginSucceeded(f.driver.command,begin,VK_SUCCESS) && f.transaction.generation()==2,"native later Begin maintains actual generation progression");
        require(f.transaction.configure(f.clone,f.config()),"actual previously observed history attaches clone at later Begin");f.arm();f.release();}
    for(unsigned mode=0;mode<5;++mode){Fixture f;float priority=1;VkDeviceQueueCreateInfo queues[2]{};
        for(auto& q:queues){q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueFamilyIndex=7;q.queueCount=1;q.pQueuePriorities=&priority;}
        VkDeviceCreateInfo device{};device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device.queueCreateInfoCount=mode==0?2:1;device.pQueueCreateInfos=queues;
        if(mode==1)queues[0].queueCount=2;
        if(mode==2)queues[0].flags=VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
        if(mode==3)queues[0].pNext=&device;
        if(mode==4)priority=-1;
        require(!f.transaction.deviceCreated(f.driver.device,device,VK_SUCCESS),"actual multi/protected/extended/invalid queue definitions refuse, no singleQueue boolean");
        require(f.clone.release().status==frame_pixel_ssbo_owned::Status::Ok,"rejected device history never references clone");
    }
}
void precedingNativeDraws(){
    Fixture f;f.initialize();f.arm();
    require(!f.transaction.beginRenderPass(f.driver.command,obj<VkRenderPass>(999),VK_SUBPASS_CONTENTS_INLINE),"unrelated preceding original pass remains native");
    f.transaction.pipelineBound(f.driver.command,obj<VkPipeline>(999));
    require(f.transaction.draw(f.driver.command,0,1,0,0)==DrawResult::ForwardOriginal && f.transaction.refusal()==Reason::None,"first unrelated empty original draw forwarded without optional poison");
    require(f.transaction.drawIndexed(f.driver.command,6,1,0,0,0)==DrawResult::ForwardOriginal,"second unrelated original indexed draw forwarded");
    f.transaction.endRenderPass(f.driver.command);
    require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"later actual target-compatible pass");
    f.transaction.pipelineBound(f.driver.command,obj<VkPipeline>(999));
    require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"third unrelated draw within matching pass native");
    f.prefix();
    require(f.transaction.drawIndexed(f.driver.command,42,3,7,-11,5)==DrawResult::ReplacedOriginal,"actual target at ordinal four supports indexed original route");
    require(f.transaction.drawOrdinal()==4 && f.transaction.selectedOrdinal()==4,"observed exact ordinal exposes selected/later-draw reasons");
    require(f.driver.commands.size()==7 && f.driver.commands[5].op==Recorder::Op::DrawIndexed &&
        f.driver.commands[5].args==std::array<std::uint32_t,5>{42,3,7,static_cast<std::uint32_t>(-11),5} &&
        f.driver.commands[6].op==Recorder::Op::Pipeline,"exact indexed parameters and immediate original-pipeline restore");
    f.transaction.endRenderPass(f.driver.command);f.transaction.endCommand(f.driver.command,VK_SUCCESS);
    f.submitOriginal();f.retire();f.driver.status=VK_SUCCESS;RecordView view;require(f.transaction.publish(f.readback(),view),"unrelated preceding scopes do not poison target cohort");f.release();
}
void retirementOperations(){
    for(auto operation:{Retirement::ResetCommand,Retirement::BeginCommand,Retirement::FreeCommand,Retirement::ResetPool,Retirement::DestroyPool}){
        Fixture f;f.record();f.submitOriginal();f.driver.status=VK_SUCCESS;
        const auto token=f.transaction.beforeRetirement(operation,f.driver.command,f.driver.pool);
        require(f.transaction.afterRetirement(token,VK_SUCCESS),"every exact original reset/free/pool retirement supported");
        f.transaction.afterDeviceWaitIdle(VK_TIMEOUT);require(!f.transaction.release(),"non-success/nonlost quiescence cannot release references");
        f.transaction.afterDeviceWaitIdle(VK_ERROR_DEVICE_LOST);require(f.transaction.release(),"retired + terminal device loss permits explicit release");
    }
}
void originalDeviceDestroyEntry(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.record();
        Transaction::SubmitTicket pendingSubmit;Transaction::RetirementTicket pendingRetire;
        if(mode==0)f.submitOriginal();
        if(mode==2)pendingSubmit=f.before();
        if(mode==3)pendingRetire=f.transaction.beforeRetirement(Retirement::ResetCommand,f.driver.command);
        require(!f.transaction.deviceDestroying(f.driver.device) && !f.transaction.retired(),"DestroyDevice entry alone cannot replace actual idle/lost");
        f.transaction.afterDeviceWaitIdle(mode==1?VK_ERROR_DEVICE_LOST:VK_SUCCESS);
        require(!f.transaction.release(),"even actual idle/lost alone does not retire reusable original recording");
        if(mode==0)require(!f.transaction.deviceDestroying(obj<VkDevice>(999)) && !f.transaction.retired(),"another actual device's Destroy cannot retire this generation");
        require(f.transaction.deviceDestroying(f.driver.device) && f.transaction.retired(),"exact original DestroyDevice ENTRY after idle/lost closes final reusable generation");
        require(!f.transaction.afterSubmit(pendingSubmit,VK_SUCCESS) && !f.transaction.afterRetirement(pendingRetire,VK_SUCCESS),"teardown cancels only obsolete exact generation tickets");
        RecordView view;require(!f.transaction.publish(f.readback(),view) && !view.record,"DestroyDevice path cannot create a late diagnostic publication");
        require(f.transaction.release() && f.ownerDriver.live.empty() && f.driver.fences.empty(),"owned clone/fence destroyed before untouched native DestroyDevice");
    }
}
void reentryAndConcurrency(){
    for(bool threaded:{false,true}){
        Fixture f;f.initialize();f.arm();require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"test pass");f.prefix();
        f.driver.reenter=!threaded;f.driver.block=threaded;
        if(threaded){std::thread worker([&]{require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ReplacedOriginal,"successful recording cannot duplicate draw after contention");});
            {std::unique_lock<std::mutex> lock(f.driver.mutex);f.driver.condition.wait(lock,[&]{return f.driver.entered;});}
            f.transaction.unknownRoute(f.driver.command);
            {std::lock_guard<std::mutex> lock(f.driver.mutex);f.driver.continueCall=true;}f.driver.condition.notify_all();worker.join();
        }else require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ReplacedOriginal,"reentry still replaces the one forwarded draw");
        require(f.transaction.refusal()==Reason::Contention && f.driver.commands.back().op==Recorder::Op::Pipeline &&
            f.driver.commands.back().object==frame_pixel_ssbo_clone::handle(f.driver.original),"reentry/concurrency invalidates publication but NEVER skips immediate original restore");f.release();
    }
}
}
int main(){serial(true);serial(false);boundedRefusals();ticketsAndFences();boundedNativeBatches();transactionEpochReuse();rejectSharedReferencedOwner();sourceCohortMismatch();adoptionAndQueueHistory();precedingNativeDraws();retirementOperations();originalDeviceDestroyEntry();reentryAndConcurrency();
    std::cout<<"ssbo_draw_transaction checks="<<checks<<" owned_fixture_checks="<<owned_fixture::checks<<'\n';}
