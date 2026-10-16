// Actual Snapshot translation-unit exports with bounded fake downstream calls.
// Optional private shaders prepare the real cold Bridge, never a native lease.
#include "layer/pixel_ssbo_receiver_bridge.h"
#include <algorithm>
#include <set>
#include <fstream>
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
#include "layer/pixel_snapshot_layer.cpp"

namespace {
enum class HookEvent {Begin,Next,End,NativeIndexed,OwnedIndexed,OriginalPipeline,OwnedPipeline,HostBarrier};
struct HookCalls {
    unsigned creates=0,begins=0,nexts=0,ends=0,nativeIndexed=0,ownedIndexed=0,commandBegins=0,commandEnds=0,idles=0,destroys=0;
    VkResult createResult=VK_SUCCESS;
    const VkRenderPassCreateInfo2* createInfo=nullptr;
    const VkRenderPassBeginInfo* beginInfo=nullptr;
    const VkSubpassBeginInfo* subBegin=nullptr;
    const VkSubpassEndInfo* subEnd=nullptr;
    VkCommandBuffer command{};
    VkDevice dispatchDevice{};
    std::vector<HookEvent> events;
} hookCalls;
unsigned ownedResets=0,ownedFences=0;
unsigned ownedPassCount=1,ownedTargetSubpass=0,ownedLegacyPasses=0,ownedPasses2=0;
void emulateShader(){const std::array<std::uint32_t,4> header{{1,0x5353424f,1,32}};std::memcpy(owned_fixture::driver->mapped.data(),header.data(),16);}
VKAPI_ATTR void VKAPI_CALL hookFill(VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,std::uint32_t){++ownedResets;}
VKAPI_ATTR void VKAPI_CALL hookSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,std::uint32_t,std::uint32_t,const VkDescriptorSet*,std::uint32_t,const std::uint32_t*){}
VKAPI_ATTR void VKAPI_CALL hookDraw(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){emulateShader();}
VKAPI_ATTR VkResult VKAPI_CALL hookFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){++ownedFences;*out=h<VkFence>(9000);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL hookDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){--ownedFences;}
VKAPI_ATTR VkResult VKAPI_CALL hookResetFence(VkDevice,std::uint32_t,const VkFence*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookFenceStatus(VkDevice,VkFence){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookSubmit(VkQueue,std::uint32_t,const VkSubmitInfo*,VkFence){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookInvalidate(VkDevice,std::uint32_t,const VkMappedMemoryRange*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookSetLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    owned_fixture::common(d,a);const bool first=!owned_fixture::driver->set0;
    require(first?ci->bindingCount==6:ci->bindingCount==1,"full original six-binding prefix and own storage Set1");
    const auto result=owned_fixture::driver->create(first?frame_pixel_ssbo_owned::Stage::Set0:frame_pixel_ssbo_owned::Stage::Set1,out);
    if(result==VK_SUCCESS)(first?owned_fixture::driver->set0:owned_fixture::driver->set1)=*out;
    return result;
}
template<class Info>VkResult hookOwnedPass(VkDevice d,const Info* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    owned_fixture::common(d,a);require(ci->subpassCount==ownedPassCount&&ci->pSubpasses,"owned factory preserves complete exact original pass count");
    for(unsigned i=0;i<ci->subpassCount;++i)require(ci->pSubpasses[i].colorAttachmentCount==1,"owned factory retains every original subpass color definition");
    const auto result=owned_fixture::driver->create(frame_pixel_ssbo_owned::Stage::RenderPass,out);
    if(result==VK_SUCCESS)owned_fixture::driver->pass=*out;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPass1(VkDevice d,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++ownedLegacyPasses;return hookOwnedPass(d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++ownedPasses2;return hookOwnedPass(d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPipelineCreate(VkDevice d,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){
    require(count==1&&ci->subpass==ownedTargetSubpass,"owned graphics pipeline preserves exact original nonzero target subpass");
    return owned_fixture::createPipeline(d,cache,count,ci,a,out);
}
VKAPI_ATTR VkResult VKAPI_CALL hookCreatePass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* allocator,VkRenderPass* out){
    require(d==hookCalls.dispatchDevice&&!allocator&&ci==hookCalls.createInfo,"original RP2 creation arguments forwarded unchanged");
    ++hookCalls.creates;if(hookCalls.createResult==VK_SUCCESS)*out=h<VkRenderPass>(29+hookCalls.creates);return hookCalls.createResult;
}
VKAPI_ATTR void VKAPI_CALL hookBegin2(VkCommandBuffer c,const VkRenderPassBeginInfo* ci,const VkSubpassBeginInfo* begin){
    require(c==hookCalls.command&&ci==hookCalls.beginInfo&&begin==hookCalls.subBegin,"original Begin2 arguments forwarded unchanged");
    ++hookCalls.begins;hookCalls.events.push_back(HookEvent::Begin);
}
VKAPI_ATTR void VKAPI_CALL hookNext2(VkCommandBuffer c,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end){
    require(c==hookCalls.command&&begin==hookCalls.subBegin&&end==hookCalls.subEnd,"original Next2 arguments forwarded unchanged");
    ++hookCalls.nexts;hookCalls.events.push_back(HookEvent::Next);
}
VKAPI_ATTR void VKAPI_CALL hookEnd2(VkCommandBuffer c,const VkSubpassEndInfo* end){
    require(c==hookCalls.command&&end==hookCalls.subEnd,"original End2 arguments forwarded unchanged");
    ++hookCalls.ends;hookCalls.events.push_back(HookEvent::End);
}
VKAPI_ATTR void VKAPI_CALL hookBegin1(VkCommandBuffer c,const VkRenderPassBeginInfo* ci,VkSubpassContents contents){
    require(contents==hookCalls.subBegin->contents,"original legacy contents preserved");hookBegin2(c,ci,hookCalls.subBegin);
}
VKAPI_ATTR void VKAPI_CALL hookNext1(VkCommandBuffer c,VkSubpassContents contents){
    require(contents==hookCalls.subBegin->contents,"original legacy Next contents preserved");hookNext2(c,hookCalls.subBegin,hookCalls.subEnd);
}
VKAPI_ATTR void VKAPI_CALL hookEnd1(VkCommandBuffer c){hookEnd2(c,hookCalls.subEnd);}
VKAPI_ATTR void VKAPI_CALL hookIndexed(VkCommandBuffer c,std::uint32_t count,std::uint32_t instances,std::uint32_t first,std::int32_t offset,std::uint32_t firstInstance){
    require(c==hookCalls.command&&count==6&&instances==1&&first==2&&offset==-3&&firstInstance==4,"native indexed original arguments preserved");
    ++hookCalls.nativeIndexed;hookCalls.events.push_back(HookEvent::NativeIndexed);
}
VKAPI_ATTR void VKAPI_CALL hookOwnedIndexed(VkCommandBuffer c,std::uint32_t count,std::uint32_t instances,std::uint32_t first,std::int32_t offset,std::uint32_t firstInstance){
    require(c==hookCalls.command&&count==6&&instances==1&&first==2&&offset==-3&&firstInstance==4,"instrumented original indexed arguments preserved");
    ++hookCalls.ownedIndexed;hookCalls.events.push_back(HookEvent::OwnedIndexed);emulateShader();
}
VKAPI_ATTR void VKAPI_CALL hookPipeline(VkCommandBuffer c,VkPipelineBindPoint point,VkPipeline value){
    require(c==hookCalls.command&&point==VK_PIPELINE_BIND_POINT_GRAPHICS,"pipeline command retains exact native command");
    hookCalls.events.push_back(value==pipeline?HookEvent::OriginalPipeline:HookEvent::OwnedPipeline);
}
VKAPI_ATTR void VKAPI_CALL hookBarrier(VkCommandBuffer c,VkPipelineStageFlags,VkPipelineStageFlags dst,VkDependencyFlags,
        std::uint32_t,const VkMemoryBarrier*,std::uint32_t,const VkBufferMemoryBarrier*,std::uint32_t,const VkImageMemoryBarrier*){
    require(c==hookCalls.command,"owned barrier stays in exact native command");
    if(dst==VK_PIPELINE_STAGE_HOST_BIT)hookCalls.events.push_back(HookEvent::HostBarrier);
}
VKAPI_ATTR VkResult VKAPI_CALL hookCommandBegin(VkCommandBuffer c,const VkCommandBufferBeginInfo* info){
    require(c==hookCalls.command&&info&&info->sType==VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,"original command Begin forwarded");++hookCalls.commandBegins;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL hookCommandEnd(VkCommandBuffer c){require(c==hookCalls.command,"original command End forwarded");++hookCalls.commandEnds;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookIdle(VkDevice d){require(d==hookCalls.dispatchDevice,"actual destroy callback passes original device to idle");++hookCalls.idles;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL hookDestroy(VkDevice d,const VkAllocationCallbacks* a){require(d==hookCalls.dispatchDevice&&!a,"original DestroyDevice forwarded");++hookCalls.destroys;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGdpa(VkDevice,const char* name){
#define ENTRY(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    ENTRY(CreateRenderPass2,hookCreatePass2) ENTRY(CreateRenderPass2KHR,hookCreatePass2)
    ENTRY(CmdBeginRenderPass2,hookBegin2) ENTRY(CmdBeginRenderPass2KHR,hookBegin2)
    ENTRY(CmdNextSubpass2,hookNext2) ENTRY(CmdNextSubpass2KHR,hookNext2)
    ENTRY(CmdEndRenderPass2,hookEnd2) ENTRY(CmdEndRenderPass2KHR,hookEnd2)
    ENTRY(CmdBeginRenderPass,hookBegin1) ENTRY(CmdNextSubpass,hookNext1) ENTRY(CmdEndRenderPass,hookEnd1)
    ENTRY(CmdDrawIndexed,hookIndexed) ENTRY(CmdBindPipeline,hookPipeline)
    ENTRY(BeginCommandBuffer,hookCommandBegin) ENTRY(EndCommandBuffer,hookCommandEnd)
    ENTRY(DeviceWaitIdle,hookIdle) ENTRY(DestroyDevice,hookDestroy)
#undef ENTRY
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookOwnedProc(VkDevice d,const char* name){
#define ENTRY(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    ENTRY(CmdDrawIndexed,hookOwnedIndexed) ENTRY(CmdBindPipeline,hookPipeline) ENTRY(CmdPipelineBarrier,hookBarrier)
    ENTRY(CreateDescriptorSetLayout,hookSetLayout) ENTRY(CmdFillBuffer,hookFill) ENTRY(CmdBindDescriptorSets,hookSets) ENTRY(CmdDraw,hookDraw)
    ENTRY(CreateRenderPass,hookOwnedPass1) ENTRY(CreateRenderPass2,hookOwnedPass2) ENTRY(CreateRenderPass2KHR,hookOwnedPass2) ENTRY(CreateGraphicsPipelines,hookOwnedPipelineCreate)
    ENTRY(CreateFence,hookFence) ENTRY(DestroyFence,hookDestroyFence) ENTRY(ResetFences,hookResetFence) ENTRY(GetFenceStatus,hookFenceStatus)
    ENTRY(QueueSubmit,hookSubmit) ENTRY(InvalidateMappedMemoryRanges,hookInvalidate)
#undef ENTRY
    return owned_fixture::getProc(d,name);
}
template<class T>T hookExport(VkDevice d,const char* name){
    const auto pointer=frame_pixel_vkGetDeviceProcAddr(d,name);require(pointer!=nullptr,"actual exported GDPA provides native available hook");
    require(pointer!=hookGdpa(d,name),"actual GDPA returns production interception, not downstream bypass");return reinterpret_cast<T>(pointer);
}
struct HookFixture {
    struct Dispatchable {void* key;};
    int key{};Dispatchable fakeDevice{&key},fakeCommand{&key};
    VkDevice dispatchDevice=reinterpret_cast<VkDevice>(&fakeDevice);
    VkCommandBuffer command=reinterpret_cast<VkCommandBuffer>(&fakeCommand);
    owned_fixture::Driver driver;
    std::unique_ptr<Fixture> native=std::make_unique<Fixture>(true,0,VK_COMMAND_BUFFER_LEVEL_PRIMARY,0,true);
    std::unique_ptr<LiveTracker> previous;
    journal_fixture::Fixture shape;
    Device* context=nullptr;
    VkAttachmentDescription2 attachment=info<VkAttachmentDescription2>(VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2);
    VkAttachmentReference2 color=info<VkAttachmentReference2>(VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2);
    VkSubpassDescription2 subpass=info<VkSubpassDescription2>(VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2);
    std::array<VkSubpassDescription2,3> allSubpasses{};
    VkRenderPassCreateInfo2 pass=info<VkRenderPassCreateInfo2>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);
    std::array<VkDescriptorSetLayoutBinding,6> setDefinitions{};
    float priority=1;VkDeviceQueueCreateInfo queue=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    HookFixture(unsigned subpasses=1,unsigned selected=0){
        hookCalls={};ownedResets=ownedFences=0;hookCalls.command=command;hookCalls.dispatchDevice=dispatchDevice;owned_fixture::driver=&driver;
        driver.expectedCount=1;driver.expectedFragmentBytes=frame_pixel_ssbo_clone::ProbeInterface::Sparse32Bytes;
        ownedPassCount=subpasses;ownedTargetSubpass=selected;ownedLegacyPasses=ownedPasses2=0;
        shape.nativeSet=sl;shape.graphics.layout=pl;shape.graphics.renderPass=h<VkRenderPass>(30);shape.stages[0].module=vs;shape.stages[1].module=fs;shape.push={VK_SHADER_STAGE_VERTEX_BIT,0,16};
        shape.graphics.subpass=selected;
        for(unsigned i=0;i<5;++i)setDefinitions[i]={frame_pixel_uniform::UniformBindings[i],i==1?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_ALL,nullptr};
        setDefinitions.back()={9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};shape.set.bindingCount=6;shape.set.pBindings=setDefinitions.data();
        queue.queueCount=1;queue.queueFamilyIndex=3;queue.pQueuePriorities=&priority;shape.device.queueCreateInfoCount=1;shape.device.pQueueCreateInfos=&queue;
        auto deviceContext=std::make_unique<Device>();context=deviceContext.get();context->native=h<VkDevice>(1);context->id=device;context->gdpa=hookGdpa;
        context->CreateRenderPass2=hookCreatePass2;context->CmdBeginRenderPass2=hookBegin2;context->CmdNextSubpass2=hookNext2;context->CmdEndRenderPass2=hookEnd2;
        context->CmdBeginRenderPass=hookBegin1;context->CmdNextSubpass=hookNext1;context->CmdEndRenderPass=hookEnd1;
        context->BeginCommandBuffer=hookCommandBegin;context->EndCommandBuffer=hookCommandEnd;context->CmdDrawIndexed=hookIndexed;context->CmdBindPipeline=hookPipeline;
        context->DeviceWaitIdle=hookIdle;context->DestroyDevice=hookDestroy;
        require(contexts().devices.emplace(&key,std::move(deviceContext)).second,"install one isolated fake native dispatch context");
        previous=std::move(session().tracker);session().tracker.reset(&native->tracker);session().allowed=true;session().armed=false;
        shape.initialize(context->definitions);
        require(native->tracker.allocateCommands(device,native->allocate,&command),"observe dispatchable command allocation in known original pool");
        require(native->tracker.beginCommand(device,command,native->beginInfo,true),"observe original dispatchable recording");bindTarget();
        attachment.format=shape.attachment.format;attachment.samples=shape.attachment.samples;attachment.loadOp=shape.attachment.loadOp;attachment.storeOp=shape.attachment.storeOp;
        attachment.initialLayout=shape.attachment.initialLayout;attachment.finalLayout=shape.attachment.finalLayout;
        color.attachment=0;color.layout=shape.color.layout;color.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
        require(subpasses>0&&subpasses<=allSubpasses.size()&&selected<subpasses,"bounded original hook pass cohort");
        for(unsigned i=0;i<subpasses;++i)allSubpasses[i]=subpass;
        pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=subpasses;pass.pSubpasses=allSubpasses.data();
    }
    ~HookFixture(){
        if(context){
            hookExport<PFN_vkDestroyDevice>(dispatchDevice,"vkDestroyDevice")(dispatchDevice,nullptr);
            require(hookCalls.destroys==1&&driver.live.empty()&&!ownedFences,"actual native destroy entry releases all owned children exactly once");
        }
        require(session().tracker.release()==&native->tracker,"release borrowed tracker before owned fixture teardown");
        session().tracker=std::move(previous);session().allowed=false;session().armed=false;
    }
    void bindTarget(){
        require(native->tracker.bindPipeline(device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline),"bind chosen source pipeline on actual dispatchable command");
        require(native->tracker.observe([&](auto& core){core.bindSets(device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds,1,&native->dynamicOffset);}),"bind complete source prefix on dispatchable command");
    }
    VkRenderPass createPass(bool khr){
        hookCalls.createInfo=&pass;VkRenderPass result{};
        const auto before=hookCalls.creates;
        const auto original=hookExport<PFN_vkCreateRenderPass2>(dispatchDevice,khr?"vkCreateRenderPass2KHR":"vkCreateRenderPass2");
        const auto status=original(dispatchDevice,&pass,nullptr,&result);
        require(hookCalls.creates==before+1&&status==hookCalls.createResult,"original Create2 invoked exactly once with unchanged result");return result;
    }
    void preparePrivate(const char* vertexPath,const char* fragmentPath,bool khr){
        owned_fixture::Fixture limits;VkPhysicalDeviceProperties properties{};properties.limits=limits.p.limits;
        context->bridge.deviceCreated(h<VkDevice>(1),shape.device,hookOwnedProc,properties,limits.p.memory);context->bridge.queueRetrieved(3,0,h<VkQueue>(2));
        require(native->tracker.ssboDeviceCreated(device,h<VkDevice>(1)),"tracker retains original mock device namespace");
        require(context->definitions.createSet(sl,shape.set,VK_SUCCESS)&&context->definitions.createLayout(pl,shape.layout,VK_SUCCESS),"same original cold Set0/push definitions");
        shape.graphics.renderPass=createPass(khr);require(context->definitions.inspectStats().live[3]==1,"actual exported Create2 records complete original pass");
        require(native->tracker.destroyPipeline(device,pipeline)&&native->tracker.destroyShader(device,vs)&&native->tracker.destroyShader(device,fs),"retire synthetic shader seed before private originals");
        require(native->tracker.observe([&](auto& core){core.destroyPipelineLayout(device,pl);core.createPipelineLayout(device,pl,shape.layout);}),"same Core layout generation follows original definition");
        const auto vertex=journal_fixture::readShader(vertexPath),fragment=journal_fixture::readShader(fragmentPath);
        auto module=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);module.codeSize=fragment.size()*4;module.pCode=fragment.data();context->bridge.shaderCreated(module,context->definitions);
        module.codeSize=vertex.size()*4;module.pCode=vertex.data();require(context->definitions.createShader(vs,module,VK_SUCCESS)&&native->tracker.createShader(device,vs,module),"private original vertex observed, no test pin bypass");
        module.codeSize=fragment.size()*4;module.pCode=fragment.data();require(context->definitions.createShader(fs,module,VK_SUCCESS)&&native->tracker.createShader(device,fs,module),"private original fragment observed, no test pin bypass");
        frame_pixel_ssbo_journal::Event event;
        require(context->definitions.createPipeline(pipeline,shape.graphics,VK_SUCCESS,&event)&&native->tracker.createPipeline(device,pipeline,shape.graphics),"actual RP2 definition supports exact private pinned recipe");
        require(native->tracker.ssboPipelineCreated(device,pipeline,{1,event.value,event.generation}),"associate exact original pipeline generation");bindTarget();
        Candidate learned;require(native->tracker.beginRenderPass(device,command,VK_SUBPASS_CONTENTS_INLINE),"original metadata pass begins");
        for(unsigned i=0;i<shape.graphics.subpass;++i)require(native->tracker.nextSubpass(device,command,VK_SUBPASS_CONTENTS_INLINE),"original learned target subpass reached");
        require(native->tracker.draw(device,command),"actual original target metadata draw");
        for(unsigned i=shape.graphics.subpass+1;i<pass.subpassCount;++i)require(native->tracker.nextSubpass(device,command,VK_SUBPASS_CONTENTS_INLINE),"remaining original learned subpass observed");
        require(native->tracker.endRenderPass(device,command,learned),"completed whole original metadata pass selects next recording");
        context->bridge.learn(learned);context->bridge.tick(context->definitions);require(context->bridge.snapshot().ready,"real cold bridge ready from actual original private definitions");
        require(ownedPasses2==1&&!ownedLegacyPasses,"original RP2 creation route retained by owned cold factory");
        session().armed=true;
        hookExport<PFN_vkBeginCommandBuffer>(dispatchDevice,"vkBeginCommandBuffer")(command,&native->beginInfo);
        require(context->bridge.snapshot().adopted&&ownedResets==1,"actual successful Begin hook adopts and GPU-resets exact fresh recording");bindTarget();
        hookExport<PFN_vkCmdBindPipeline>(dispatchDevice,"vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        hookCalls.events.clear();
    }
};
void exportedCreation(){
    HookFixture fixture;
    require(frame_pixel_vkGetDeviceProcAddr(VK_NULL_HANDLE,"vkCreateRenderPass2")==nullptr,"null native device never fabricates dispatch");
    require(frame_pixel_vkGetDeviceProcAddr(fixture.dispatchDevice,"vkMissingRP2FixtureFunction")==nullptr,"unknown native procedure stays absent");
    const auto first=fixture.createPass(false),second=fixture.createPass(true);require(first!=second&&fixture.context->definitions.inspectStats().live[3]==2,"core2 and KHR creation both reach retained production journal");
    hookCalls.createResult=VK_ERROR_OUT_OF_HOST_MEMORY;fixture.createPass(false);
    require(fixture.context->definitions.inspectStats().live[3]==2,"failed native creation never inserts an original pass");
    hookCalls.createResult=VK_SUCCESS;VkBaseInStructure extension{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr};fixture.pass.pNext=&extension;
    fixture.createPass(true);const auto stats=fixture.context->definitions.inspectStats();
    require(stats.live[3]==2&&stats.firstRefusal.contract.reason==frame_pixel_ssbo_clone::Reason::Extension,"successful unsupported RP2 still forwards native success but refuses only observer definition");
    for(unsigned i=0;i<9;++i)fixture.context->definitions.unsupported(frame_pixel_ssbo_journal::ObjectKind::Layout,0x1000+i,1000+i);
    FILE* output=std::tmpfile();require(output!=nullptr,"private bounded status fixture output");
    bool feature=false,overflow=false;std::uint64_t reasons=0;unsigned recipes=0,sets=0,contextsCount=0,creations=0;std::uint32_t skips=0;
    for(unsigned i=0;i<2;++i)definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,overflow);
    const auto before=std::ftell(output);require(before>0&&before<65536,"bounded grouped production status output");
    definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,overflow);
    require(std::ftell(output)==before,"unchanged repeated status poll produces no recurring diagnostic output");
    require(!std::fseek(output,0,SEEK_SET),"read retained production status from start");
    std::string text(static_cast<std::size_t>(before),'\0');require(std::fread(text.data(),1,text.size(),output)==text.size(),"complete actual production JSON status read");std::fclose(output);
    unsigned reports=0;std::size_t position=0;
    while(position<text.size()){
        const auto end=text.find('\n',position);require(end!=std::string::npos&&end-position<=8192,"each production status JSON record stays bounded and newline terminated");
        const auto line=text.substr(position,end-position);
        if(line.find("\"event\":\"ssbo_definition_refusals\"")!=std::string::npos){
            ++reports;require(line.find("\"inventory\":[")!=std::string::npos&&line.find("\"set_fact_count\":")!=std::string::npos&&line.find("\"set_fact_capacity\":")!=std::string::npos,"keyed failure receipt retains separate instance and semantic-bank occupancy");
            unsigned grouped=0;std::size_t cursor=0;while((cursor=line.find("\"context_index\":",cursor))!=std::string::npos){++grouped;++cursor;}
            require(grouped>0&&grouped<=8,"failure contexts grouped in at most eight entries per bounded report");
        }
        position=end+1;
    }
    require(reports==3&&contextsCount==11,"ten creation contexts plus next-poll NoReady retained without historical refusal masking");
}
void exportedObservationFailure(){
    HookFixture fixture;fixture.createPass(false);const auto live=fixture.context->definitions.inspectStats().live[3];
    observeDefinitions(fixture.context,[](auto&){throw std::runtime_error("owned observation failure control");});
    require(fixture.context->definitionBoundary.failed()&&fixture.context->bridge.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory,"actual caught definition observation failure permanently invalidates Bridge admission");
    fixture.createPass(true);
    require(hookCalls.creates==2&&fixture.context->definitions.inspectStats().live[3]==live,"native original creation still forwarded once after observer failure without fabricated history");
}
void privateHooks(const char* vertex,const char* fragment,unsigned mutation,bool khr){
    HookFixture fixture;fixture.preparePrivate(vertex,fragment,khr);
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;
    auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;
    auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    VkBaseInStructure extension{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr};
    if(mutation==1)begin.pNext=&extension;
    if(mutation==2)subBegin.pNext=&extension;
    if(mutation==3)subEnd.pNext=&extension;
    if(mutation==5){
        observeDefinitions(fixture.context,[](auto&){throw std::runtime_error("owned post-adoption observation failure control");});
        fixture.createPass(khr);
        require(fixture.context->definitions.inspectStats().live[3]==1,"failed observation cannot insert later native creation into retained old history");
    }
    hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
    hookExport<PFN_vkCmdBeginRenderPass2>(fixture.dispatchDevice,khr?"vkCmdBeginRenderPass2KHR":"vkCmdBeginRenderPass2")(fixture.command,&begin,&subBegin);
    if(mutation==4)hookExport<PFN_vkCmdNextSubpass2>(fixture.dispatchDevice,khr?"vkCmdNextSubpass2KHR":"vkCmdNextSubpass2")(fixture.command,&subBegin,&subEnd);
    hookExport<PFN_vkCmdDrawIndexed>(fixture.dispatchDevice,"vkCmdDrawIndexed")(fixture.command,6,1,2,-3,4);
    hookExport<PFN_vkCmdEndRenderPass2>(fixture.dispatchDevice,khr?"vkCmdEndRenderPass2KHR":"vkCmdEndRenderPass2")(fixture.command,&subEnd);
    require(hookCalls.begins==1&&hookCalls.ends==1&&hookCalls.nexts==unsigned(mutation==4),"each original scope callback invoked exactly once");
    hookExport<PFN_vkEndCommandBuffer>(fixture.dispatchDevice,"vkEndCommandBuffer")(fixture.command);
    const auto state=fixture.context->bridge.snapshot();
    if(!mutation){
        require(state.replaced&&state.refusal==frame_pixel_ssbo_receiver::Refusal::None&&!state.transaction,"ordinary core2/KHR reaches exact transaction Begin/End, no blanket unknown route");
        require(hookCalls.ownedIndexed==1&&!hookCalls.nativeIndexed,"instrumented exact original draw issued once, never duplicated by native wrapper");
        const auto& events=hookCalls.events;const auto draw=std::find(events.begin(),events.end(),HookEvent::OwnedIndexed);
        require(draw!=events.end()&&draw+1!=events.end()&&*(draw+1)==HookEvent::OriginalPipeline,"original pipeline restore is immediate command after instrumented indexed draw");
        require(events.size()>=2&&events[events.size()-2]==HookEvent::End&&events.back()==HookEvent::HostBarrier,"original End2 executes before transaction emits outside-pass HOST barrier");
    }else{
        if(mutation==5)require(state.refusal==frame_pixel_ssbo_receiver::Refusal::DefinitionHistory,"missing definition history refuses future intervention on already-adopted generation");
        else if(mutation==4)require(state.refusal==frame_pixel_ssbo_receiver::Refusal::None&&state.transaction==unsigned(frame_pixel_ssbo_transaction::Reason::Subpass),"known Next2 beyond captured single-subpass count invalidates exact generation");
        else require(state.refusal==frame_pixel_ssbo_receiver::Refusal::UnknownRoute&&state.transaction==unsigned(frame_pixel_ssbo_transaction::Reason::UnknownRoute),"unsupported pNext invalidates exact transaction generation");
        require(hookCalls.ownedIndexed==unsigned(mutation==3)&&hookCalls.nativeIndexed==unsigned(mutation!=3),"refused route preserves exactly one original indexed operation");
        require(std::find(hookCalls.events.begin(),hookCalls.events.end(),HookEvent::HostBarrier)==hookCalls.events.end(),"refused End/Next/Begin never emits publication completion barrier");
        frame_pixel_ssbo_transaction::RecordView record;require(!fixture.context->bridge.publish({},record)&&!record.record,"unsupported generation cannot publish output");
    }
}
void multipleSubpassHooks(const char* vertex,const char* fragment,unsigned count,unsigned route,unsigned mutation=0){
    HookFixture fixture(count,1);fixture.preparePrivate(vertex,fragment,route==2);
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;
    auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;
    auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);VkBaseInStructure extension{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr};
    hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
    if(route==0)hookExport<PFN_vkCmdBeginRenderPass>(fixture.dispatchDevice,"vkCmdBeginRenderPass")(fixture.command,&begin,subBegin.contents);
    else hookExport<PFN_vkCmdBeginRenderPass2>(fixture.dispatchDevice,route==2?"vkCmdBeginRenderPass2KHR":"vkCmdBeginRenderPass2")(fixture.command,&begin,&subBegin);
    const auto next=[&]{
        if(route==0)hookExport<PFN_vkCmdNextSubpass>(fixture.dispatchDevice,"vkCmdNextSubpass")(fixture.command,subBegin.contents);
        else hookExport<PFN_vkCmdNextSubpass2>(fixture.dispatchDevice,route==2?"vkCmdNextSubpass2KHR":"vkCmdNextSubpass2")(fixture.command,&subBegin,&subEnd);
    };
    if(mutation==1)subBegin.pNext=&extension;
    if(mutation==2)subEnd.pNext=&extension;
    next();subBegin.pNext=nullptr;subEnd.pNext=nullptr;
    hookExport<PFN_vkCmdDrawIndexed>(fixture.dispatchDevice,"vkCmdDrawIndexed")(fixture.command,6,1,2,-3,4);
    require(std::find(hookCalls.events.begin(),hookCalls.events.end(),HookEvent::HostBarrier)==hookCalls.events.end(),"selected subpass must not emit a HOST/copy barrier while whole original pass remains open");
    for(unsigned i=2;i<count;++i)next();
    require(hookCalls.nexts==count-1&&std::find(hookCalls.events.begin(),hookCalls.events.end(),HookEvent::HostBarrier)==hookCalls.events.end(),"every original Next transition is forwarded once without early source readback");
    if(route==0)hookExport<PFN_vkCmdEndRenderPass>(fixture.dispatchDevice,"vkCmdEndRenderPass")(fixture.command);
    else hookExport<PFN_vkCmdEndRenderPass2>(fixture.dispatchDevice,route==2?"vkCmdEndRenderPass2KHR":"vkCmdEndRenderPass2")(fixture.command,&subEnd);
    hookExport<PFN_vkEndCommandBuffer>(fixture.dispatchDevice,"vkEndCommandBuffer")(fixture.command);
    const auto state=fixture.context->bridge.snapshot();const auto& events=hookCalls.events;
    require(hookCalls.begins==1&&hookCalls.ends==1,"original complete multi-subpass Begin/End each forwarded exactly once");
    if(!mutation){
        require(state.replaced&&state.refusal==frame_pixel_ssbo_receiver::Refusal::None&&!state.transaction,"actual exported legacy/Core2/KHR reaches exact configured nonzero-subpass transaction");
        require(hookCalls.ownedIndexed==1&&!hookCalls.nativeIndexed,"nonzero-subpass original indexed draw never duplicated");
        const auto draw=std::find(events.begin(),events.end(),HookEvent::OwnedIndexed);
        require(draw!=events.end()&&draw+1!=events.end()&&*(draw+1)==HookEvent::OriginalPipeline,"immediate original pipeline restoration survives actual Next2 route");
        require(events.size()>=2&&events[events.size()-2]==HookEvent::End&&events.back()==HookEvent::HostBarrier,"HOST/source work follows final whole-pass original End only");
    }else{
        require(state.refusal==frame_pixel_ssbo_receiver::Refusal::UnknownRoute&&!hookCalls.ownedIndexed&&hookCalls.nativeIndexed==1,"unknown Next2 child extension refuses only observer and preserves one original draw");
        require(std::find(events.begin(),events.end(),HookEvent::HostBarrier)==events.end(),"unsupported Next2 metadata never produces HOST completion publication");
    }
}
}
int main(int argc,char** argv){
    if(argc==4&&!std::strcmp(argv[1],"--subpasses-only")){
        for(unsigned count:{2u,3u})for(unsigned route=0;route<3;++route)multipleSubpassHooks(argv[2],argv[3],count,route);
        for(unsigned route:{1u,2u})for(unsigned mutation:{1u,2u})multipleSubpassHooks(argv[2],argv[3],3,route,mutation);
        std::cout<<"{\"scope\":\"actual exported multi-subpass Snapshot hooks, fake downstream only\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;
    }
    require(argc==1||argc==3,"optional private original VS/PS arguments");
    exportedCreation();exportedObservationFailure();
    if(argc==3)for(bool khr:{false,true})for(unsigned mutation=0;mutation<=5;++mutation)privateHooks(argv[1],argv[2],mutation,khr);
    std::cout<<"{\"scope\":\"actual SnapshotTU exported GDPA RP2/KHR hooks, fake downstream only\",\"checks\":"<<checks.load()
        <<",\"private_pins_tested\":"<<(argc==3?"true":"false")<<",\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";
}
