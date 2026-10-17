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
    // Metadata-only fixture: no token, original callback or GPU authority.
    static void contextSeed(Transaction& t){
        t.device_=owned_fixture::object<VkDevice>(1);t.command_=owned_fixture::object<VkCommandBuffer>(44);
        t.pool_=owned_fixture::object<VkCommandPool>(55);t.generation_=41;t.poolGeneration_=17;
        t.recording_=true;t.armed_=true;t.subpassCount_=3;t.targetSubpass_=1;
    }
    static void phase(Transaction& t,bool pass,bool target,std::uint32_t subpass,bool selected=false){
        t.inPass_=pass;t.targetPass_=target;t.currentSubpass_=subpass;t.selected_=selected;
    }
    static void ordinal(Transaction& t,std::uint64_t value){t.drawOrdinal_=value;}
    static void retireContext(Transaction& t){t.retired_=true;t.generation_=42;}
    static void unscopedRefusal(Transaction& t,Reason r){t.refuse(r);}
    static bool delayedContention(Transaction& t,const CommandContextInput& owner,const CommandContextInput& contender){
        Transaction::Guard guard(t);bool winner=false;
        {Transaction::ContextScope scope(t,t.command_,owner,Operation::CmdDraw);
            Transaction::Guard loser(t);winner=loser.firstRefusal();}
        // The owner completed before the winning contender publishes metadata.
        const bool unavailable=!t.recordingContexts().firstRefusalAvailable;
        t.contextContention(t.command_,contender,winner);return unavailable&&winner;
    }
    static void contention(Transaction& t,const CommandContextInput& input){
        Transaction::Guard guard(t);Transaction::Guard loser(t);t.contextContention(t.command_,input,loser.firstRefusal());
    }

    static Transaction::RecordingTicket recording(std::uint64_t generation=41,std::uint64_t poolGeneration=17){
        Transaction::RecordingTicket t;t.tracker_=reinterpret_cast<const frame_pixel_capture::LiveTracker*>(std::uintptr_t(0x111));
        t.device_=1;t.command_=owned_fixture::object<VkCommandBuffer>(44);t.pool_=owned_fixture::object<VkCommandPool>(55);
        t.commandGeneration_=generation;t.poolGeneration_=poolGeneration;t.family_=7;return t;
    }
    static void restore(Transaction::TargetTicket& t,Id layout,std::initializer_list<std::uint32_t> values){t.restoreLayout_=layout;t.restoreCount_=unsigned(values.size());std::copy(values.begin(),values.end(),t.restoreOffsets_.begin());}
    static Transaction::TargetTicket target(const Cohort& source,std::uint64_t ordinal=1,std::uint32_t subpass=0){
        Transaction::TargetTicket t;t.tracker_=reinterpret_cast<const frame_pixel_capture::LiveTracker*>(std::uintptr_t(0x111));
        t.device_=1;t.command_=owned_fixture::object<VkCommandBuffer>(44);t.pool_=owned_fixture::object<VkCommandPool>(55);
        t.commandGeneration_=41;t.poolGeneration_=17;t.ordinal_=ordinal;t.subpass_=subpass;t.pipeline_={1,60,1};t.layout_={1,20,1};t.set0_={1,70,88};t.source_=source;return t;
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
    bool mirror=false;std::array<std::uint32_t,5> expectedOffsets{{16,32,64,128,256}};
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
    if(recorder->mirror){require(cb==recorder->command&&point==VK_PIPELINE_BIND_POINT_GRAPHICS&&!first&&count==1&&sets&&dynamic==5&&offsets,"mirror and restore touch only Set0 with full original dynamic list");
        require(std::equal(recorder->expectedOffsets.begin(),recorder->expectedOffsets.end(),offsets),"all dynamic offsets restored including vertex-stage0/2/3 beyond fragment4/6");
        require(layout==recorder->layout?sets[0]==obj<VkDescriptorSet>(70):sets[0]==obj<VkDescriptorSet>(900),"owned mirror never substitutes original restore set");
    }else require(cb==recorder->command && point==VK_PIPELINE_BIND_POINT_GRAPHICS && layout!=recorder->layout && first==1 && count==1 &&
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
    if(recorder->reenter)recorder->transaction->unknownRoute(cb);
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
std::uint32_t expectedSubpassCount=1;
VKAPI_ATTR VkResult VKAPI_CALL multiplePass(VkDevice device,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* allocator,VkRenderPass* out){
    owned_fixture::common(device,allocator);
    require(ci->subpassCount==expectedSubpassCount&&ci->pSubpasses,"owned pass preserves exact bounded original subpass count");
    for(unsigned i=0;i<ci->subpassCount;++i)require(ci->pSubpasses[i].colorAttachmentCount==1&&ci->pSubpasses[i].pColorAttachments[0].attachment==0,"every owned subpass retains its actual color reference");
    const auto result=owned_fixture::driver->create(frame_pixel_ssbo_owned::Stage::RenderPass,out);
    if(result==VK_SUCCESS)owned_fixture::driver->pass=*out;
    return result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL multipleProc(VkDevice device,const char* name){
    if(!std::strcmp(name,"vkCreateRenderPass"))return reinterpret_cast<PFN_vkVoidFunction>(multiplePass);
    return owned_fixture::getProc(device,name);
}
struct Fixture {
    owned_fixture::Driver ownerDriver;Recorder driver;Transaction transaction;
    owned_fixture::Fixture captured;frame_pixel_ssbo_owned::Clone clone;
    VkDescriptorSet set0=obj<VkDescriptorSet>(70);Cohort source{};std::vector<std::uint32_t> mirrorWords;
    static owned_fixture::Driver* activate(owned_fixture::Driver& d){owned_fixture::driver=&d;return &d;}
    Fixture(bool noncoherent=false,std::uint32_t subpasses=1,std::uint32_t selected=0,const char* vs=nullptr,const char* ps=nullptr,const char* probe=nullptr):ownerDriver(),driver(),transaction(),captured((activate(ownerDriver),owned_fixture::Fixture{})){
        ownerDriver.noncoherent=noncoherent;
        if(noncoherent)captured.p.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        for(unsigned i=0;i<captured.recipe.provenance.size();++i)captured.recipe.provenance[i]={1,(i+1)*10,1};
        if(subpasses>1){
            captured.recipe.renderPass.subpassCount=subpasses;
            for(unsigned i=1;i<subpasses;++i)*captured.recipe.renderPass.subpass(i)=*captured.recipe.renderPass.subpass(0);
            captured.recipe.graphics.info.subpass=selected;expectedSubpassCount=subpasses;captured.p.downstream=multipleProc;
        }
        if(vs){namespace c=frame_pixel_ssbo_clone;ownerDriver.mirror=true;ownerDriver.mirrorSource=set0;driver.mirror=true;
            const auto vertex=owned_fixture::readShader(vs),fragment=owned_fixture::readShader(ps);mirrorWords=owned_fixture::readShader(probe);c::Refusal why;c::ProbeInterface profile;
            require(c::ProbeInterface::set0Mirror(vertex.data(),vertex.size()*4,fragment.data(),fragment.size()*4,mirrorWords.data(),mirrorWords.size()*4,profile,why),"private mirror requires both exact original source stages");
            std::copy(vertex.begin(),vertex.end(),captured.recipe.vertex.words.begin());std::copy(fragment.begin(),fragment.end(),captured.recipe.fragment.words.begin());
            std::array<VkDescriptorSetLayoutBinding,29> bindings{};for(unsigned i=0;i<29;++i)bindings[i]={i,i<8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<25?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_ALL,nullptr};
            for(unsigned i:{0u,2u,3u,4u,6u})bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            VkDescriptorSetLayoutCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;ci.bindingCount=bindings.size();ci.pBindings=bindings.data();require(c::captureSet0(ci,captured.recipe.set0,why,profile),"mirror retains every original descriptor including every attested dynamic stage binding");
            captured.p.fragment=mirrorWords.data();captured.p.fragmentBytes=mirrorWords.size()*4;ownerDriver.expectedFragmentBytes=unsigned(captured.p.fragmentBytes);frame_receiver::Sha256 hash;hash.update(captured.p.fragment,captured.p.fragmentBytes);captured.p.fragmentSha256=hash.finish();
        }
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
void originalQueuePriorities(){
    for(unsigned mode=0;mode<7;++mode){
        Transaction transaction;float value=1;
        VkDeviceQueueGlobalPriorityCreateInfoKHR priority{};
        priority.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR;
        const std::array<VkQueueGlobalPriorityKHR,4> levels{{VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR,
            VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR}};
        priority.globalPriority=levels[mode%levels.size()];
        if(mode==4)priority.globalPriority=static_cast<VkQueueGlobalPriorityKHR>(0);
        if(mode==5)priority.pNext=&priority;
        if(mode==6)priority.sType=VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO;
        VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex=7;queue.queueCount=1;queue.pQueuePriorities=&value;queue.pNext=&priority;
        VkDeviceCreateInfo device{};device.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
        const auto before=priority;
        require(transaction.deviceCreated(obj<VkDevice>(1),device,VK_SUCCESS)==(mode<4),
                "known original queue priority preserves one-queue proof; malformed/unknown chains refuse");
        require(!std::memcmp(&priority,&before,sizeof(priority)) && queue.pNext==&priority && value==1,
                "observation never edits native scheduling request or chain pointers");
        if(mode<4)require(transaction.queueRetrieved(obj<VkDevice>(1),7,0,obj<VkQueue>(2)),
                "priority is not substituted for exact original queue retrieval");
    }
}
void multipleSubpassRecordings(){
    for(unsigned count:{2u,3u}){
        Fixture f(false,count,1);f.initialize();f.arm();
        require(f.transaction.targetSubpass()==1&&f.transaction.subpassCount()==count,"target subpass derives exclusively from immutable actual prepared graphics/pass recipe");
        require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"enter complete target pass at original subpass0");
        f.transaction.pipelineBound(f.driver.command,obj<VkPipeline>(999));
        require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"preceding unrelated original draw remains unmodified");
        const auto beforeNext=f.driver.commands.size();
        require(f.transaction.nextSubpass(f.driver.command)&&f.transaction.currentSubpass()==1&&f.driver.commands.size()==beforeNext,"original inline subpass transition emits no owned command");
        f.prefix();require(f.transaction.drawIndexed(f.driver.command,6,1,2,-3,4)==DrawResult::ReplacedOriginal,"exact configured nonzero-subpass original indexed draw instrumented once");
        const auto beforeEnd=f.driver.commands.size();
        if(count==3)require(f.transaction.nextSubpass(f.driver.command)&&f.transaction.currentSubpass()==2&&f.driver.commands.size()==beforeEnd,"empty remaining original subpass preserves target record without premature HOST barrier");
        require(f.driver.commands.back().op==Recorder::Op::Pipeline&&f.driver.commands.back().object==frame_pixel_ssbo_clone::handle(f.driver.original),"original pipeline restoration remains immediate at selected indexed draw");
        f.transaction.endRenderPass(f.driver.command);require(f.driver.commands.size()==beforeEnd+1&&f.driver.commands.back().op==Recorder::Op::Barrier,"one HOST completion barrier only after whole original pass ends");
        f.transaction.endCommand(f.driver.command,VK_SUCCESS);f.submitOriginal();f.retire();f.driver.status=VK_SUCCESS;
        RecordView record;require(f.transaction.publish(f.readback(),record)&&record.bytes==128,"full original multi-subpass command cohort publishes only after matching completion and retirement");f.release();
    }
    for(unsigned mutation=0;mutation<6;++mutation){
        Fixture f(false,3,1);f.initialize();f.arm();
        if(mutation==0)require(!f.transaction.nextSubpass(f.driver.command),"Next outside original pass refuses");
        else{
            require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE),"negative original multi-subpass pass");
            if(mutation==1)f.transaction.endRenderPass(f.driver.command);
            else if(mutation==2)require(!f.transaction.nextSubpass(f.driver.command,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS),"secondary contents never acquire primary native authorization");
            else{
                require(f.transaction.nextSubpass(f.driver.command),"original transition to actual target subpass");f.prefix();
                require(f.transaction.drawIndexed(f.driver.command,6,1,2,-3,4)==DrawResult::ReplacedOriginal,"target selected before later route refusal");
                if(mutation==3){require(f.transaction.nextSubpass(f.driver.command),"advance to last original subpass");require(!f.transaction.nextSubpass(f.driver.command),"too many original subpass transitions refused");}
                if(mutation==4){require(f.transaction.nextSubpass(f.driver.command),"remaining original subpass");require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"later original draw forwards once while publication is invalidated");}
                if(mutation==5)f.transaction.endRenderPass(f.driver.command);
            }
        }
        require(f.transaction.refusal()!=Reason::None,"bad original subpass route has precise bounded refusal");
        RecordView record;require(!f.transaction.publish(f.readback(),record)&&!record.record,"incomplete/out-of-bounds/secondary/later-draw route never publishes");f.release();
    }
    for(bool correct:{false,true}){
        Fixture f(false,2,1);f.observeDevice();require(f.transaction.configure(f.clone,f.config()),"cold multi-subpass owner attaches before original adoption");
        require(f.transaction.adoptRecording(TransactionTestAccess::recording()),"actual modeled fresh recording token");
        f.driver.command=obj<VkCommandBuffer>(44);f.driver.pool=obj<VkCommandPool>(55);f.arm();
        require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE)&&f.transaction.nextSubpass(f.driver.command),"actual target subpass1 reached");
        require(f.transaction.adoptTargetState(TransactionTestAccess::target(f.source,1,correct?1:0))==correct,"opaque ticket must prove same actual target and current subpass, never caller-selected index");f.release();
    }
}
CommandContextInput contextInput(Operation operation,std::uint32_t detail=0){
    CommandContextInput c;c.operation=operation;c.source=ContextSource::Wrapper;c.detailReason=detail;
    c.structureType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;c.index=2;c.chain=1;c.contents=VK_SUBPASS_CONTENTS_INLINE;c.object=111;
    for(unsigned i=0;i<c.chains.size();++i){c.chains[i].rootType=100+i;c.chains[i].count=2;c.chains[i].types[0]=200+i;c.chains[i].types[1]=300+i;c.chains[i].termination=1;}
    return c;
}
void recordingContexts(){
    const auto command=obj<VkCommandBuffer>(44);const auto seed=[](Transaction& t){TransactionTestAccess::contextSeed(t);};
    {
        Transaction t;seed(t);auto c=contextInput(Operation::CmdBeginRenderPass2KHR);const auto original=c;
        t.observe(command,c);c.chains[0].types[0]=0;c.object=222;t.observe(command,c);
        auto first=t.recordingContexts();require(first.count==2&&first.entries[0].first.input.chains[0].types[0]==original.chains[0].types[0],"owned header census survives caller mutation and distinct header shapes remain separate");
        require(first.entries[0].first.commandGeneration==41&&first.entries[0].first.poolGeneration==17&&first.entries[0].first.phase==RecordingPhase::RecordingOutsidePass,"generation and phase copied coherently under existing guard");
        const auto before=first.attempts;t.observe(obj<VkCommandBuffer>(999),original);require(t.recordingContexts().attempts==before,"unrelated raw command cannot contaminate selected generation");
        TransactionTestAccess::phase(t,true,true,0);t.observe(command,original);
        TransactionTestAccess::phase(t,true,true,1);t.observe(command,original);
        TransactionTestAccess::phase(t,true,true,1,true);t.observe(command,original);
        TransactionTestAccess::phase(t,true,true,2,true);t.observe(command,original);
        const auto phases=t.recordingContexts();require(phases.entries[2].first.phase==RecordingPhase::BeforeTargetSubpass&&phases.entries[3].first.phase==RecordingPhase::TargetSubpassBeforeDraw&&phases.entries[4].first.phase==RecordingPhase::TargetSubpassAfterDraw&&phases.entries[5].first.phase==RecordingPhase::AfterTargetSubpass,"complete target-subpass route retains exact phase distinctions");
        auto rejected=contextInput(Operation::CmdDispatchBaseKHR,13);t.unknownRoute(command,rejected);const auto failure=t.recordingContexts();
        require(t.refusal()==Reason::UnknownRoute&&failure.firstRefusalAvailable&&failure.firstRefusal.input.operation==Operation::CmdDispatchBaseKHR&&failure.firstRefusal.before==Reason::None&&failure.firstRefusal.after==Reason::UnknownRoute,"first actual rejected callback records stable precise cause");
        t.unknownRoute(command,contextInput(Operation::CmdBeginQuery));const auto later=t.recordingContexts();
        require(later.firstRefusal.input.operation==Operation::CmdDispatchBaseKHR&&later.count==failure.count+1&&later.entries[later.count-1].first.before==Reason::UnknownRoute,"later distinct callback survives sticky refusal without replacing first cause");
        TransactionTestAccess::retireContext(t);const auto count=later.attempts;t.observe(command,original);t.unknownRoute(command,original);require(t.recordingContexts().attempts==count,"closed old generation ignores reused raw handle observations");
    }
    {
        Transaction t;seed(t);auto c=contextInput(Operation::CmdDrawIndexed);const auto firstObject=c.object;
        for(unsigned i=0;i<5000;++i){c.object=1000+i;TransactionTestAccess::ordinal(t,i);t.observe(command,c);}
        auto repeat=t.recordingContexts();require(repeat.count==1&&repeat.entries[0].count==5000&&repeat.entries[0].first.input.object==1000&&repeat.entries[0].first.drawOrdinal==0,"per-draw objects and ordinals cannot exhaust finite context bank");
        (void)firstObject;c.targetPipeline=true;t.observe(command,c);require(t.recordingContexts().count==2,"target pipeline encounter stays distinct from unrelated pipeline observations");
        for(unsigned i=0;i<80;++i)t.observe(command,contextInput(Operation::CmdDraw,100+i));
        const auto full=t.recordingContexts();require(full.count==RecordingContextCapacity&&full.overflow==18,"distinct overflow is explicit with no eviction");
        t.unknownRoute(command,contextInput(Operation::CmdExecuteCommands,999));const auto failed=t.recordingContexts();
        require(failed.count==64&&failed.overflow==19&&failed.firstRefusalAvailable&&failed.firstRefusal.input.operation==Operation::CmdExecuteCommands,"independent first refusal survives a full accepted-context bank");
        require(failed.entries[0].count==5000&&failed.entries[0].first.sequence==1,"overflow preserves earliest immutable payload and repeat count");
    }
    {
        Transaction t;seed(t);auto bad=contextInput(static_cast<Operation>(UINT16_MAX));bad.source=static_cast<ContextSource>(255);for(auto& chain:bad.chains)chain.count=UINT32_MAX;
        t.observe(command,bad);const auto normal=t.recordingContexts().entries[0].first;
        require(normal.metadataTruncated&&normal.input.operation==Operation::Unspecified&&normal.input.source==ContextSource::Unspecified,"malformed enum metadata is normalized on guarded path");
        for(const auto& chain:normal.input.chains)require(chain.count==16,"guarded path never publishes an out-of-bounds header count");
        Transaction contended;seed(contended);TransactionTestAccess::contention(contended,bad);const auto first=contended.recordingContexts();
        require(first.contended&&first.firstRefusalAvailable&&first.firstRefusal.after==Reason::Contention&&first.firstRefusal.phase==RecordingPhase::Unavailable&&!first.firstRefusal.commandGeneration,"contended first cause never fabricates unlocked generation/phase");
        require(first.firstRefusal.input.operation==Operation::Unspecified&&first.firstRefusal.input.source==ContextSource::Unspecified,"contention uses same bounded enum normalization");
        for(const auto& chain:first.firstRefusal.input.chains)require(chain.count==16,"contention first record also bounds every chain");
    }
    {
        Transaction t;seed(t);require(TransactionTestAccess::delayedContention(t,contextInput(Operation::CmdDraw),contextInput(Operation::CmdDispatch)),"guard owner cannot steal Contention cause before winning loser publishes");
        const auto first=t.recordingContexts();require(first.firstRefusalAvailable&&first.firstRefusal.input.operation==Operation::CmdDispatch,"winning loser owns the exact first Contention callback");
        Transaction previous;seed(previous);previous.unknownRoute(command,contextInput(Operation::CmdBeginQuery));TransactionTestAccess::contention(previous,contextInput(Operation::CmdDispatch));
        require(previous.recordingContexts().firstRefusal.input.operation==Operation::CmdBeginQuery&&previous.refusal()==Reason::UnknownRoute,"losing Contention CAS cannot steal prior UnknownRoute context");
        Transaction unavailable;seed(unavailable);TransactionTestAccess::unscopedRefusal(unavailable,Reason::Identity);unavailable.unknownRoute(command,contextInput(Operation::CmdDispatch));
        require(!unavailable.recordingContexts().firstRefusalAvailable&&unavailable.refusal()==Reason::Identity,"unobserved earlier cause remains unavailable rather than falsely attributed later");
    }
    {
        Transaction t;seed(t);std::atomic<bool> done{false},valid{true};
        std::thread reader([&]{while(!done.load(std::memory_order_acquire)){const auto view=t.recordingContexts();if(view.count>64)valid=false;for(unsigned i=0;i<view.count;++i){const auto& entry=view.entries[i];if(!entry.count||!entry.first.sequence||entry.first.commandGeneration!=41||entry.first.input.chains[2].count!=2||entry.first.input.chains[2].types[1]!=302)valid=false;}}});
        for(unsigned i=0;i<30000;++i)t.observe(command,contextInput(Operation::CmdDraw,i%8));
        done.store(true,std::memory_order_release);reader.join();const auto final=t.recordingContexts();
        require(valid&&final.count==8&&final.attempts==30000&&!final.contended&&!final.overflow,"concurrent cold reader observes only fully published immutable bounded payloads without contending on transaction");
        for(unsigned i=0;i<final.count;++i)require(final.entries[i].count==3750,"repeat counters remain complete after reader and producer join");
    }
    for(unsigned i=0;i<static_cast<unsigned>(Operation::Count);++i)require(std::strcmp(operationName(static_cast<Operation>(i)),"InvalidOperation"),"every finite operation has a diagnostic name");
    for(unsigned i=0;i<=static_cast<unsigned>(RecordingPhase::Retired);++i)require(std::strcmp(phaseName(static_cast<RecordingPhase>(i)),"InvalidPhase"),"every finite phase has a diagnostic name");
}

void compatibleMirror(const char* vs,const char* ps,const char* probe){
    for(unsigned mode=0;mode<9;++mode){Fixture f(false,3,1,vs,ps,probe);f.observeDevice();require(f.transaction.configure(f.clone,f.config()),"configured real owned mirror");
        f.driver.command=obj<VkCommandBuffer>(44);f.driver.pool=obj<VkCommandPool>(55);require(f.transaction.adoptRecording(TransactionTestAccess::recording()),"same-callback original recording ticket");f.arm();
        require(f.transaction.beginRenderPass(f.driver.command,f.driver.pass,VK_SUBPASS_CONTENTS_INLINE)&&f.transaction.nextSubpass(f.driver.command),"whole original pass reaches actual target subpass1");
        auto ticket=TransactionTestAccess::target(f.source,1,1);TransactionTestAccess::restore(ticket,Id{1,20,777},{16,32,64,128,256});
        if(mode==4)TransactionTestAccess::restore(ticket,Id{1,999,777},{16,32,64,128,256});
        if(mode==5)TransactionTestAccess::restore(ticket,Id{1,20,777},{16,32,64,128});
        const bool adopted=f.transaction.adoptTargetState(ticket);require(adopted==(mode!=4&&mode!=5),"opaque restore uses Core identity and exact full original dynamic count");
        if(adopted&&mode!=1){require(f.clone.populateMirror(f.set0).status==frame_pixel_ssbo_owned::Status::Ok,"copy before first descriptor bind after buffer-reset lease");auto fresh=ticket;
            if(mode==2)TransactionTestAccess::restore(fresh,Id{1,20,777},{16,32,64,128,257});
            if(mode==3)TransactionTestAccess::restore(fresh,Id{1,20,778},{16,32,64,128,256});
            if(mode==6)fresh=TransactionTestAccess::target(Cohort{Id{1,1001,2},f.source.view,f.source.memory,f.source.layout,f.source.extent},1,1);
            if(mode!=8)require(f.transaction.targetStateCurrent(fresh)==(mode!=2&&mode!=3&&mode!=6),"post-copy same-callback proof rejects full-offset drift/layout generation/source changes");}
        f.driver.reenter=mode==7;
        const auto result=f.transaction.drawIndexed(f.driver.command,17,2,4,-3,9);const bool replaced=mode==0||mode==7;
        require((result==DrawResult::ReplacedOriginal)==replaced,"no owned command without complete populated immutable mirror and fresh exact target state");
        if(replaced){const auto& c=f.driver.commands;require(c.size()==8&&c[3].op==Recorder::Op::Sets&&c[3].object==900&&c[4].op==Recorder::Op::Pipeline&&c[5].op==Recorder::Op::DrawIndexed&&c[5].args==std::array<std::uint32_t,5>{17,2,4,std::uint32_t(-3),9},"one mirror/probe and exact unchanged original indexed draw");
            require(c[6].op==Recorder::Op::Pipeline&&c[6].object==60&&c[7].op==Recorder::Op::Sets&&c[7].object==70,"immediate original pipeline then unconditional full native Set0 restoration even after reentry");}
        if(mode==0){require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal&&f.transaction.refusal()==Reason::None,"later same-target original draw is untouched after descriptor-equivalent restoration");
            require(f.transaction.nextSubpass(f.driver.command),"later original inline subpass allowed");require(f.transaction.drawIndexed(f.driver.command,6,1,0,0,0)==DrawResult::ForwardOriginal,"later-subpass original draw untouched");f.transaction.endRenderPass(f.driver.command);const auto ended=f.driver.commands.size();
            require(f.transaction.beginRenderPass(f.driver.command,obj<VkRenderPass>(999),VK_SUBPASS_CONTENTS_INLINE)==false&&f.transaction.refusal()==Reason::None,"unrelated later whole native pass retains no target authority");require(f.transaction.draw(f.driver.command,3,1,0,0)==DrawResult::ForwardOriginal,"later pass native draw not substituted");f.transaction.endRenderPass(f.driver.command);
            require(f.driver.commands.size()==ended&&f.ownerDriver.copies==1,"one selected observer, one copy, one reset and one HOST barrier for full reusable recording");f.transaction.endCommand(f.driver.command,VK_SUCCESS);f.driver.status=VK_SUCCESS;f.submitOriginal();RecordView view;require(!f.transaction.publish(f.readback(),view),"mirror cohort cannot publish before exact generation retirement");f.retire();require(f.transaction.publish(f.readback(),view)&&view.execution==1,"same final owned fence and source cohort required for mirror publication");}
        f.release();
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
int main(int argc,char** argv){
    if(argc==5&&!std::strcmp(argv[1],"mirror")){compatibleMirror(argv[2],argv[3],argv[4]);std::cout<<"compatible mirror transaction checks="<<checks<<" owner="<<owned_fixture::checks<<'\n';return 0;}
    if(argc==2 && !std::strcmp(argv[1],"contexts")){recordingContexts();std::cout<<"recording context checks="<<checks<<'\n';return 0;}
    if(argc==2 && !std::strcmp(argv[1],"subpasses")){multipleSubpassRecordings();std::cout<<"subpass transaction checks="<<checks<<'\n';return 0;}
    if(argc==2 && !std::strcmp(argv[1],"priorities")){originalQueuePriorities();std::cout<<"queue priority checks="<<checks<<'\n';return 0;}
    if(argc!=1)return 2;
    serial(true);serial(false);boundedRefusals();ticketsAndFences();boundedNativeBatches();transactionEpochReuse();rejectSharedReferencedOwner();sourceCohortMismatch();adoptionAndQueueHistory();precedingNativeDraws();retirementOperations();originalDeviceDestroyEntry();originalQueuePriorities();multipleSubpassRecordings();reentryAndConcurrency();
    std::cout<<"ssbo_draw_transaction checks="<<checks<<" owned_fixture_checks="<<owned_fixture::checks<<'\n';}
