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
enum class HookEvent {Begin,Next,End,NativeIndexed,OwnedIndexed,OriginalPipeline,OwnedPipeline,HostBarrier,DescriptorSets,DynamicState,OriginalSubmit,MarkerSubmit,Unknown};
struct HookCalls {
    unsigned creates=0,begins=0,nexts=0,ends=0,nativeIndexed=0,ownedIndexed=0,commandBegins=0,commandEnds=0,idles=0,destroys=0;
    std::array<unsigned,4> coreRoute{},khrRoute{};
    unsigned sets=0,dynamics=0,submits=0,markers=0,unknowns=0;
    std::array<unsigned,unsigned(frame_pixel_ssbo_transaction::Operation::Count)> unknownCalls{};
    VkResult createResult=VK_SUCCESS;
    const VkRenderPassCreateInfo2* createInfo=nullptr;
    const VkRenderPassBeginInfo* beginInfo=nullptr;
    const VkSubpassBeginInfo* subBegin=nullptr;
    const VkSubpassEndInfo* subEnd=nullptr;
    VkCommandBuffer command{};
    VkDevice dispatchDevice{};VkQueue queue{};std::uint32_t dynamicOffset=0;
    std::vector<HookEvent> events;
} hookCalls;
unsigned ownedResets=0,ownedFences=0,availableRP2Routes=3;
unsigned ownedPassCount=1,ownedTargetSubpass=0,ownedLegacyPasses=0,ownedPasses2=0,ownedPasses2Core=0,ownedPasses2KHR=0;
void emulateShader(){const std::array<std::uint32_t,4> header{{1,0x5353424f,1,32}};std::memcpy(owned_fixture::driver->mapped.data(),header.data(),16);}
VKAPI_ATTR void VKAPI_CALL hookFill(VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,std::uint32_t){++ownedResets;}
VKAPI_ATTR void VKAPI_CALL hookSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,std::uint32_t,std::uint32_t,const VkDescriptorSet*,std::uint32_t,const std::uint32_t*){}
VKAPI_ATTR void VKAPI_CALL hookDraw(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){emulateShader();}
VKAPI_ATTR VkResult VKAPI_CALL hookFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){++ownedFences;*out=h<VkFence>(9000);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL hookDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){--ownedFences;}
VKAPI_ATTR VkResult VKAPI_CALL hookResetFence(VkDevice,std::uint32_t,const VkFence*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookFenceStatus(VkDevice,VkFence){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL hookSubmit(VkQueue queue,std::uint32_t count,const VkSubmitInfo* submit,VkFence fence){
    require(queue==hookCalls.queue&&count==1&&submit&&!submit->commandBufferCount&&fence,"owned completion marker follows original selected queue submission");
    ++hookCalls.markers;hookCalls.events.push_back(HookEvent::MarkerSubmit);return VK_SUCCESS;
}
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
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++ownedPasses2;++ownedPasses2Core;return hookOwnedPass(d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPass2KHR(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++ownedPasses2;++ownedPasses2KHR;return hookOwnedPass(d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL hookOwnedPipelineCreate(VkDevice d,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){
    require(count==1&&ci->subpass==ownedTargetSubpass,"owned graphics pipeline preserves exact original nonzero target subpass");
    return owned_fixture::createPipeline(d,cache,count,ci,a,out);
}
VkResult hookCreatePass2Route(bool khr,VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* allocator,VkRenderPass* out){
    ++(khr?hookCalls.khrRoute:hookCalls.coreRoute)[0];
    require(d==hookCalls.dispatchDevice&&!allocator&&ci==hookCalls.createInfo,"original RP2 creation arguments forwarded unchanged");
    ++hookCalls.creates;if(hookCalls.createResult==VK_SUCCESS)*out=h<VkRenderPass>(29+hookCalls.creates);return hookCalls.createResult;
}
void hookBegin2Route(bool khr,VkCommandBuffer c,const VkRenderPassBeginInfo* ci,const VkSubpassBeginInfo* begin){
    ++(khr?hookCalls.khrRoute:hookCalls.coreRoute)[1];
    require(c==hookCalls.command&&ci==hookCalls.beginInfo&&begin==hookCalls.subBegin,"original Begin2 arguments forwarded unchanged");
    ++hookCalls.begins;hookCalls.events.push_back(HookEvent::Begin);
}
void hookNext2Route(bool khr,VkCommandBuffer c,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end){
    ++(khr?hookCalls.khrRoute:hookCalls.coreRoute)[2];
    require(c==hookCalls.command&&begin==hookCalls.subBegin&&end==hookCalls.subEnd,"original Next2 arguments forwarded unchanged");
    ++hookCalls.nexts;hookCalls.events.push_back(HookEvent::Next);
}
void hookEnd2Route(bool khr,VkCommandBuffer c,const VkSubpassEndInfo* end){
    ++(khr?hookCalls.khrRoute:hookCalls.coreRoute)[3];
    require(c==hookCalls.command&&end==hookCalls.subEnd,"original End2 arguments forwarded unchanged");
    ++hookCalls.ends;hookCalls.events.push_back(HookEvent::End);
}
VKAPI_ATTR VkResult VKAPI_CALL hookCreatePass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){return hookCreatePass2Route(false,d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL hookCreatePass2KHR(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){return hookCreatePass2Route(true,d,ci,a,out);}
VKAPI_ATTR void VKAPI_CALL hookBegin2(VkCommandBuffer c,const VkRenderPassBeginInfo* ci,const VkSubpassBeginInfo* begin){hookBegin2Route(false,c,ci,begin);}
VKAPI_ATTR void VKAPI_CALL hookBegin2KHR(VkCommandBuffer c,const VkRenderPassBeginInfo* ci,const VkSubpassBeginInfo* begin){hookBegin2Route(true,c,ci,begin);}
VKAPI_ATTR void VKAPI_CALL hookNext2(VkCommandBuffer c,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end){hookNext2Route(false,c,begin,end);}
VKAPI_ATTR void VKAPI_CALL hookNext2KHR(VkCommandBuffer c,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end){hookNext2Route(true,c,begin,end);}
VKAPI_ATTR void VKAPI_CALL hookEnd2(VkCommandBuffer c,const VkSubpassEndInfo* end){hookEnd2Route(false,c,end);}
VKAPI_ATTR void VKAPI_CALL hookEnd2KHR(VkCommandBuffer c,const VkSubpassEndInfo* end){hookEnd2Route(true,c,end);}
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
    require(c==hookCalls.command&&(point==VK_PIPELINE_BIND_POINT_GRAPHICS||point==VK_PIPELINE_BIND_POINT_COMPUTE),"pipeline command retains exact native command/bind point");
    if(point==VK_PIPELINE_BIND_POINT_COMPUTE){++hookCalls.unknowns;++hookCalls.unknownCalls[unsigned(frame_pixel_ssbo_transaction::Operation::CmdBindPipeline)];}
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
VKAPI_ATTR void VKAPI_CALL hookNativeSets(VkCommandBuffer c,VkPipelineBindPoint point,VkPipelineLayout layout,std::uint32_t first,std::uint32_t count,const VkDescriptorSet* sets,std::uint32_t offsets,const std::uint32_t* values){
    require(c==hookCalls.command&&(point==VK_PIPELINE_BIND_POINT_GRAPHICS||point==VK_PIPELINE_BIND_POINT_COMPUTE)&&layout==pl&&!first&&count==1&&sets&&*sets==ds&&offsets==1&&values&&*values==hookCalls.dynamicOffset,"complete original Set0 bind forwards exact descriptor and dynamic offset");
    ++hookCalls.sets;hookCalls.events.push_back(HookEvent::DescriptorSets);
    if(point==VK_PIPELINE_BIND_POINT_COMPUTE){++hookCalls.unknowns;++hookCalls.unknownCalls[unsigned(frame_pixel_ssbo_transaction::Operation::CmdBindDescriptorSets)];}
}
void hookDynamic(VkCommandBuffer c){require(c==hookCalls.command,"dynamic state remains on exact original command");++hookCalls.dynamics;hookCalls.events.push_back(HookEvent::DynamicState);}
VKAPI_ATTR void VKAPI_CALL hookViewport(VkCommandBuffer c,std::uint32_t first,std::uint32_t count,const VkViewport* v){require(!first&&count==1&&v&&v->width==32,"original viewport arguments preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookScissor(VkCommandBuffer c,std::uint32_t first,std::uint32_t count,const VkRect2D* v){require(!first&&count==1&&v&&v->extent.width==32,"original scissor arguments preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookSampleEnable(VkCommandBuffer c,VkBool32 value){require(value==VK_FALSE,"original sample-locations enable preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookSamples(VkCommandBuffer c,const VkSampleLocationsInfoEXT* value){require(value&&value->sType==VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,"original sample-locations arguments preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookCull(VkCommandBuffer c,VkCullModeFlags value){require(value==VK_CULL_MODE_NONE,"original cull mode preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookFront(VkCommandBuffer c,VkFrontFace value){require(value==VK_FRONT_FACE_CLOCKWISE,"original front face preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookBias(VkCommandBuffer c,float constant,float clamp,float slope){require(constant==0.25f&&clamp==0.5f&&slope==1.0f,"original depth bias preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookBlend(VkCommandBuffer c,const float* value){require(value&&value[0]==0.25f&&value[3]==1.0f,"original blend constants preserved");hookDynamic(c);}
VKAPI_ATTR void VKAPI_CALL hookDepthBounds(VkCommandBuffer c,float low,float high){require(low==0.0f&&high==1.0f,"original depth bounds preserved");hookDynamic(c);}
VKAPI_ATTR VkResult VKAPI_CALL hookNativeSubmit(VkQueue queue,std::uint32_t count,const VkSubmitInfo* submit,VkFence fence){
    require(queue==hookCalls.queue&&count==1&&submit&&submit->commandBufferCount==1&&submit->pCommandBuffers&&*submit->pCommandBuffers==hookCalls.command&&!fence,"original selected submit forwarded unchanged exactly once");
    ++hookCalls.submits;hookCalls.events.push_back(HookEvent::OriginalSubmit);return VK_SUCCESS;
}
using TestOperation=frame_pixel_ssbo_transaction::Operation;
void hookUnknown(VkCommandBuffer c,TestOperation operation){require(c==hookCalls.command,"unknown callback keeps original command");++hookCalls.unknowns;++hookCalls.unknownCalls[unsigned(operation)];hookCalls.events.push_back(HookEvent::Unknown);}
VKAPI_ATTR void VKAPI_CALL hookShaders(VkCommandBuffer c,std::uint32_t n,const VkShaderStageFlagBits* stages,const VkShaderEXT* shaders){require(n==1&&stages&&*stages==VK_SHADER_STAGE_VERTEX_BIT&&shaders&&*shaders==h<VkShaderEXT>(400),"original shader-object arguments preserved");hookUnknown(c,TestOperation::CmdBindShadersEXT);}
VKAPI_ATTR void VKAPI_CALL hookDispatch(VkCommandBuffer c,std::uint32_t x,std::uint32_t y,std::uint32_t z){require(x==2&&y==3&&z==4,"original dispatch counts preserved");hookUnknown(c,TestOperation::CmdDispatch);}
VKAPI_ATTR void VKAPI_CALL hookDispatchIndirect(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset){require(buffer==h<VkBuffer>(401)&&offset==16,"original indirect dispatch arguments preserved");hookUnknown(c,TestOperation::CmdDispatchIndirect);}
void hookBase(bool khr,VkCommandBuffer c,std::uint32_t bx,std::uint32_t by,std::uint32_t bz,std::uint32_t x,std::uint32_t y,std::uint32_t z){require(bx==1&&by==2&&bz==3&&x==4&&y==5&&z==6,"original base dispatch arguments preserved");hookUnknown(c,khr?TestOperation::CmdDispatchBaseKHR:TestOperation::CmdDispatchBase);}
VKAPI_ATTR void VKAPI_CALL hookBaseCore(VkCommandBuffer c,std::uint32_t a,std::uint32_t b,std::uint32_t d,std::uint32_t x,std::uint32_t y,std::uint32_t z){hookBase(false,c,a,b,d,x,y,z);}
VKAPI_ATTR void VKAPI_CALL hookBaseKHR(VkCommandBuffer c,std::uint32_t a,std::uint32_t b,std::uint32_t d,std::uint32_t x,std::uint32_t y,std::uint32_t z){hookBase(true,c,a,b,d,x,y,z);}
VKAPI_ATTR void VKAPI_CALL hookQuery(VkCommandBuffer c,VkQueryPool pool,std::uint32_t query,VkQueryControlFlags flags){require(pool==h<VkQueryPool>(402)&&query==3&&flags==VK_QUERY_CONTROL_PRECISE_BIT,"original query arguments preserved");hookUnknown(c,TestOperation::CmdBeginQuery);}
VKAPI_ATTR void VKAPI_CALL hookQueryIndexed(VkCommandBuffer c,VkQueryPool pool,std::uint32_t query,VkQueryControlFlags flags,std::uint32_t index){require(pool==h<VkQueryPool>(402)&&query==3&&flags==VK_QUERY_CONTROL_PRECISE_BIT&&index==2,"original indexed query arguments preserved");hookUnknown(c,TestOperation::CmdBeginQueryIndexedEXT);}
void hookIndirectCall(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,std::uint32_t count,std::uint32_t stride,bool indexed){require(buffer==h<VkBuffer>(401)&&offset==16&&count==2&&stride==32,"original indirect draw arguments preserved");hookUnknown(c,indexed?TestOperation::CmdDrawIndexedIndirect:TestOperation::CmdDrawIndirect);}
VKAPI_ATTR void VKAPI_CALL hookIndirect(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,std::uint32_t n,std::uint32_t s){hookIndirectCall(c,b,o,n,s,false);}
VKAPI_ATTR void VKAPI_CALL hookIndexedIndirect(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,std::uint32_t n,std::uint32_t s){hookIndirectCall(c,b,o,n,s,true);}
void hookIndirectCountCall(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkBuffer counts,VkDeviceSize countOffset,std::uint32_t count,std::uint32_t stride,TestOperation operation){require(buffer==h<VkBuffer>(401)&&offset==16&&counts==h<VkBuffer>(403)&&countOffset==8&&count==2&&stride==32,"original indirect-count draw arguments preserved");hookUnknown(c,operation);}
#define INDIRECT_COUNT_HOOK(name,op) VKAPI_ATTR void VKAPI_CALL name(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkBuffer cb,VkDeviceSize co,std::uint32_t n,std::uint32_t s){hookIndirectCountCall(c,b,o,cb,co,n,s,TestOperation::op);}
INDIRECT_COUNT_HOOK(hookCount,CmdDrawIndirectCount) INDIRECT_COUNT_HOOK(hookCountKHR,CmdDrawIndirectCountKHR)
INDIRECT_COUNT_HOOK(hookIndexedCount,CmdDrawIndexedIndirectCount) INDIRECT_COUNT_HOOK(hookIndexedCountKHR,CmdDrawIndexedIndirectCountKHR)
#undef INDIRECT_COUNT_HOOK
VKAPI_ATTR void VKAPI_CALL hookSecondary(VkCommandBuffer c,std::uint32_t count,const VkCommandBuffer* commands){require(count==1&&commands&&*commands==h<VkCommandBuffer>(404),"original secondary command list preserved");hookUnknown(c,TestOperation::CmdExecuteCommands);}
void hookRendering(VkCommandBuffer c,const VkRenderingInfo* info,bool khr){require(info&&info->sType==VK_STRUCTURE_TYPE_RENDERING_INFO&&info->layerCount==1,"original dynamic rendering begin preserved");hookUnknown(c,khr?TestOperation::CmdBeginRenderingKHR:TestOperation::CmdBeginRendering);}
VKAPI_ATTR void VKAPI_CALL hookRenderingCore(VkCommandBuffer c,const VkRenderingInfo* i){hookRendering(c,i,false);}
VKAPI_ATTR void VKAPI_CALL hookRenderingKHR(VkCommandBuffer c,const VkRenderingInfo* i){hookRendering(c,i,true);}
VKAPI_ATTR void VKAPI_CALL hookEndRenderingCore(VkCommandBuffer c){hookUnknown(c,TestOperation::CmdEndRendering);}
VKAPI_ATTR void VKAPI_CALL hookEndRenderingKHR(VkCommandBuffer c){hookUnknown(c,TestOperation::CmdEndRenderingKHR);}
VkResult hookSubmit2Call(VkQueue queue,std::uint32_t count,const VkSubmitInfo2* info,VkFence fence,bool khr){require(queue==hookCalls.queue&&count==1&&info&&info->commandBufferInfoCount==1&&info->pCommandBufferInfos&&info->pCommandBufferInfos->commandBuffer==hookCalls.command&&!fence,"original submit2 pointer and command unchanged");hookUnknown(hookCalls.command,khr?TestOperation::QueueSubmit2KHR:TestOperation::QueueSubmit2);return VK_ERROR_OUT_OF_HOST_MEMORY;}
VKAPI_ATTR VkResult VKAPI_CALL hookSubmit2Core(VkQueue q,std::uint32_t c,const VkSubmitInfo2* i,VkFence f){return hookSubmit2Call(q,c,i,f,false);}
VKAPI_ATTR VkResult VKAPI_CALL hookSubmit2KHR(VkQueue q,std::uint32_t c,const VkSubmitInfo2* i,VkFence f){return hookSubmit2Call(q,c,i,f,true);}
VKAPI_ATTR VkResult VKAPI_CALL hookIdle(VkDevice d){require(d==hookCalls.dispatchDevice,"actual destroy callback passes original device to idle");++hookCalls.idles;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL hookDestroy(VkDevice d,const VkAllocationCallbacks* a){require(d==hookCalls.dispatchDevice&&!a,"original DestroyDevice forwarded");++hookCalls.destroys;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGdpa(VkDevice,const char* name){
#define ENTRY(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
#define RP2_ENTRY(n,f,route) if(!std::strcmp(name,"vk" #n))return (availableRP2Routes&route)?reinterpret_cast<PFN_vkVoidFunction>(f):nullptr;
    RP2_ENTRY(CreateRenderPass2,hookCreatePass2,1) RP2_ENTRY(CreateRenderPass2KHR,hookCreatePass2KHR,2)
    RP2_ENTRY(CmdBeginRenderPass2,hookBegin2,1) RP2_ENTRY(CmdBeginRenderPass2KHR,hookBegin2KHR,2)
    RP2_ENTRY(CmdNextSubpass2,hookNext2,1) RP2_ENTRY(CmdNextSubpass2KHR,hookNext2KHR,2)
    RP2_ENTRY(CmdEndRenderPass2,hookEnd2,1) RP2_ENTRY(CmdEndRenderPass2KHR,hookEnd2KHR,2)
    RP2_ENTRY(CmdDispatchBase,hookBaseCore,1) RP2_ENTRY(CmdDispatchBaseKHR,hookBaseKHR,2)
    RP2_ENTRY(CmdBeginRendering,hookRenderingCore,1) RP2_ENTRY(CmdBeginRenderingKHR,hookRenderingKHR,2)
    RP2_ENTRY(CmdEndRendering,hookEndRenderingCore,1) RP2_ENTRY(CmdEndRenderingKHR,hookEndRenderingKHR,2)
    RP2_ENTRY(QueueSubmit2,hookSubmit2Core,1) RP2_ENTRY(QueueSubmit2KHR,hookSubmit2KHR,2)
    RP2_ENTRY(CmdDrawIndirectCount,hookCount,1) RP2_ENTRY(CmdDrawIndirectCountKHR,hookCountKHR,2)
    RP2_ENTRY(CmdDrawIndexedIndirectCount,hookIndexedCount,1) RP2_ENTRY(CmdDrawIndexedIndirectCountKHR,hookIndexedCountKHR,2)
#undef RP2_ENTRY
    ENTRY(CmdBeginRenderPass,hookBegin1) ENTRY(CmdNextSubpass,hookNext1) ENTRY(CmdEndRenderPass,hookEnd1)
    ENTRY(CmdDrawIndexed,hookIndexed) ENTRY(CmdBindPipeline,hookPipeline) ENTRY(CmdBindDescriptorSets,hookNativeSets) ENTRY(QueueSubmit,hookNativeSubmit)
    ENTRY(CmdSetViewport,hookViewport) ENTRY(CmdSetScissor,hookScissor) ENTRY(CmdSetSampleLocationsEnableEXT,hookSampleEnable) ENTRY(CmdSetSampleLocationsEXT,hookSamples)
    ENTRY(CmdSetCullMode,hookCull) ENTRY(CmdSetFrontFace,hookFront) ENTRY(CmdSetDepthBias,hookBias) ENTRY(CmdSetBlendConstants,hookBlend) ENTRY(CmdSetDepthBounds,hookDepthBounds)
    ENTRY(CmdBindShadersEXT,hookShaders) ENTRY(CmdDispatch,hookDispatch) ENTRY(CmdDispatchIndirect,hookDispatchIndirect)
    ENTRY(CmdBeginQuery,hookQuery) ENTRY(CmdBeginQueryIndexedEXT,hookQueryIndexed) ENTRY(CmdDrawIndirect,hookIndirect) ENTRY(CmdDrawIndexedIndirect,hookIndexedIndirect) ENTRY(CmdExecuteCommands,hookSecondary)
    ENTRY(BeginCommandBuffer,hookCommandBegin) ENTRY(EndCommandBuffer,hookCommandEnd)
    ENTRY(DeviceWaitIdle,hookIdle) ENTRY(DestroyDevice,hookDestroy)
#undef ENTRY
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookOwnedProc(VkDevice d,const char* name){
#define ENTRY(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    ENTRY(CmdDrawIndexed,hookOwnedIndexed) ENTRY(CmdBindPipeline,hookPipeline) ENTRY(CmdPipelineBarrier,hookBarrier)
    ENTRY(CreateDescriptorSetLayout,hookSetLayout) ENTRY(CmdFillBuffer,hookFill) ENTRY(CmdBindDescriptorSets,hookSets) ENTRY(CmdDraw,hookDraw)
    ENTRY(CreateRenderPass,hookOwnedPass1) ENTRY(CreateRenderPass2,hookOwnedPass2) ENTRY(CreateRenderPass2KHR,hookOwnedPass2KHR) ENTRY(CreateGraphicsPipelines,hookOwnedPipelineCreate)
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
    int key{};Dispatchable fakeDevice{&key},fakeCommand{&key},fakeQueue{&key};
    VkDevice dispatchDevice=reinterpret_cast<VkDevice>(&fakeDevice);
    VkCommandBuffer command=reinterpret_cast<VkCommandBuffer>(&fakeCommand);
    VkQueue queueHandle=reinterpret_cast<VkQueue>(&fakeQueue);
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
    HookFixture(unsigned subpasses=1,unsigned selected=0,unsigned routes=3){
        hookCalls={};ownedResets=ownedFences=0;availableRP2Routes=routes;hookCalls.command=command;hookCalls.dispatchDevice=dispatchDevice;hookCalls.queue=queueHandle;hookCalls.dynamicOffset=native->dynamicOffset;owned_fixture::driver=&driver;
        driver.expectedCount=1;driver.expectedFragmentBytes=frame_pixel_ssbo_clone::ProbeInterface::Sparse32Bytes;
        ownedPassCount=subpasses;ownedTargetSubpass=selected;ownedLegacyPasses=ownedPasses2=ownedPasses2Core=ownedPasses2KHR=0;
        shape.nativeSet=sl;shape.graphics.layout=pl;shape.graphics.renderPass=h<VkRenderPass>(30);shape.stages[0].module=vs;shape.stages[1].module=fs;shape.push={VK_SHADER_STAGE_VERTEX_BIT,0,16};
        shape.graphics.subpass=selected;
        for(unsigned i=0;i<5;++i)setDefinitions[i]={frame_pixel_uniform::UniformBindings[i],i==1?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_ALL,nullptr};
        setDefinitions.back()={9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};shape.set.bindingCount=6;shape.set.pBindings=setDefinitions.data();
        queue.queueCount=1;queue.queueFamilyIndex=3;queue.pQueuePriorities=&priority;shape.device.queueCreateInfoCount=1;shape.device.pQueueCreateInfos=&queue;
        auto deviceContext=std::make_unique<Device>();context=deviceContext.get();context->native=h<VkDevice>(1);context->id=device;context->gdpa=hookGdpa;
        loadRenderPass2Dispatch(*context,dispatchDevice);
        context->CmdBeginRenderPass=hookBegin1;context->CmdNextSubpass=hookNext1;context->CmdEndRenderPass=hookEnd1;
        context->BeginCommandBuffer=hookCommandBegin;context->EndCommandBuffer=hookCommandEnd;context->CmdDrawIndexed=hookIndexed;context->CmdBindPipeline=hookPipeline;context->CmdBindDescriptorSets=hookNativeSets;context->QueueSubmit=hookNativeSubmit;
#define LOAD_RECORDING(name) context->name=reinterpret_cast<PFN_vk##name>(hookGdpa(dispatchDevice,"vk" #name));
        loadRecordingAliasDispatch(*context,dispatchDevice);
        LOAD_RECORDING(CmdBindShadersEXT) LOAD_RECORDING(CmdDispatch) LOAD_RECORDING(CmdDispatchIndirect) LOAD_RECORDING(CmdBeginQuery) LOAD_RECORDING(CmdBeginQueryIndexedEXT)
        LOAD_RECORDING(CmdDrawIndirect) LOAD_RECORDING(CmdDrawIndexedIndirect) LOAD_RECORDING(CmdExecuteCommands)
#undef LOAD_RECORDING
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
        context->bridge.deviceCreated(h<VkDevice>(1),shape.device,hookOwnedProc,properties,limits.p.memory);context->bridge.queueRetrieved(3,0,queueHandle);
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
        require(ownedPasses2==1&&!ownedLegacyPasses&&ownedPasses2Core==unsigned(!khr)&&ownedPasses2KHR==unsigned(khr),"original exact core/KHR RP2 creation route retained by owned cold factory");
        session().armed=true;
        hookExport<PFN_vkBeginCommandBuffer>(dispatchDevice,"vkBeginCommandBuffer")(command,&native->beginInfo);
        require(context->bridge.snapshot().adopted&&ownedResets==1,"actual successful Begin hook adopts and GPU-resets exact fresh recording");bindTarget();
        hookExport<PFN_vkCmdBindPipeline>(dispatchDevice,"vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        hookCalls.events.clear();
    }
};
void exportedExactAliasRoutes(){
    for(unsigned available:{1u,2u,3u})for(bool khr:{false,true}){
        if(!(available&(khr?2u:1u)))continue;
        HookFixture fixture(1,0,available);
        for(const char* operation:{"CreateRenderPass2","CmdBeginRenderPass2","CmdNextSubpass2","CmdEndRenderPass2"}){
            const std::string core=std::string("vk")+operation,khrName=core+"KHR";
            const auto c=frame_pixel_vkGetDeviceProcAddr(fixture.dispatchDevice,core.c_str());
            const auto k=frame_pixel_vkGetDeviceProcAddr(fixture.dispatchDevice,khrName.c_str());
            require(bool(c)==bool(available&1)&&bool(k)==bool(available&2),"missing exact original alias never fabricated from available sibling");
            if(c&&k)require(c!=k,"core and KHR queries retain distinct production entrypoints");
        }
        require(bool(fixture.context->CreateRenderPass2)==bool(available&1)&&bool(fixture.context->CreateRenderPass2KHR)==bool(available&2),"production dispatch loader retains independent exact creation pointers");
        require(bool(fixture.context->CmdBeginRenderPass2)==bool(available&1)&&bool(fixture.context->CmdBeginRenderPass2KHR)==bool(available&2)&&
                bool(fixture.context->CmdNextSubpass2)==bool(available&1)&&bool(fixture.context->CmdNextSubpass2KHR)==bool(available&2)&&
                bool(fixture.context->CmdEndRenderPass2)==bool(available&1)&&bool(fixture.context->CmdEndRenderPass2KHR)==bool(available&2),"production dispatch loader does not fall back between command aliases");
        auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.createPass(khr);begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
        auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;
        auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
        hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
        hookExport<PFN_vkCmdBeginRenderPass2>(fixture.dispatchDevice,khr?"vkCmdBeginRenderPass2KHR":"vkCmdBeginRenderPass2")(fixture.command,&begin,&subBegin);
        hookExport<PFN_vkCmdNextSubpass2>(fixture.dispatchDevice,khr?"vkCmdNextSubpass2KHR":"vkCmdNextSubpass2")(fixture.command,&subBegin,&subEnd);
        hookExport<PFN_vkCmdEndRenderPass2>(fixture.dispatchDevice,khr?"vkCmdEndRenderPass2KHR":"vkCmdEndRenderPass2")(fixture.command,&subEnd);
        for(unsigned i=0;i<4;++i)require(hookCalls.coreRoute[i]==unsigned(!khr)&&hookCalls.khrRoute[i]==unsigned(khr),"requested original core/KHR route forwarded exactly once; nonnull sibling never called");
    }
}
void invokeUnknown(HookFixture& fixture,TestOperation operation){
    const auto c=fixture.command;const auto d=fixture.dispatchDevice;const auto buffer=h<VkBuffer>(401);
#define CALL(op,type,...) case TestOperation::op:hookExport<type>(d,"vk" #op)(__VA_ARGS__);break;
    switch(operation){
    CALL(CmdBindPipeline,PFN_vkCmdBindPipeline,c,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline)
    CALL(CmdBindDescriptorSets,PFN_vkCmdBindDescriptorSets,c,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,1,&fixture.native->dynamicOffset)
    case TestOperation::CmdBindShadersEXT:{const VkShaderStageFlagBits stage=VK_SHADER_STAGE_VERTEX_BIT;const auto shader=h<VkShaderEXT>(400);hookExport<PFN_vkCmdBindShadersEXT>(d,"vkCmdBindShadersEXT")(c,1,&stage,&shader);break;}
    CALL(CmdDispatch,PFN_vkCmdDispatch,c,2,3,4)
    CALL(CmdDispatchIndirect,PFN_vkCmdDispatchIndirect,c,buffer,16)
    CALL(CmdDispatchBase,PFN_vkCmdDispatchBase,c,1,2,3,4,5,6)
    CALL(CmdDispatchBaseKHR,PFN_vkCmdDispatchBaseKHR,c,1,2,3,4,5,6)
    CALL(CmdBeginQuery,PFN_vkCmdBeginQuery,c,h<VkQueryPool>(402),3,VK_QUERY_CONTROL_PRECISE_BIT)
    CALL(CmdBeginQueryIndexedEXT,PFN_vkCmdBeginQueryIndexedEXT,c,h<VkQueryPool>(402),3,VK_QUERY_CONTROL_PRECISE_BIT,2)
    CALL(CmdDrawIndirect,PFN_vkCmdDrawIndirect,c,buffer,16,2,32)
    CALL(CmdDrawIndexedIndirect,PFN_vkCmdDrawIndexedIndirect,c,buffer,16,2,32)
    CALL(CmdDrawIndirectCount,PFN_vkCmdDrawIndirectCount,c,buffer,16,h<VkBuffer>(403),8,2,32)
    CALL(CmdDrawIndirectCountKHR,PFN_vkCmdDrawIndirectCountKHR,c,buffer,16,h<VkBuffer>(403),8,2,32)
    CALL(CmdDrawIndexedIndirectCount,PFN_vkCmdDrawIndexedIndirectCount,c,buffer,16,h<VkBuffer>(403),8,2,32)
    CALL(CmdDrawIndexedIndirectCountKHR,PFN_vkCmdDrawIndexedIndirectCountKHR,c,buffer,16,h<VkBuffer>(403),8,2,32)
    case TestOperation::CmdExecuteCommands:{const auto child=h<VkCommandBuffer>(404);hookExport<PFN_vkCmdExecuteCommands>(d,"vkCmdExecuteCommands")(c,1,&child);break;}
    case TestOperation::CmdBeginRendering:case TestOperation::CmdBeginRenderingKHR:{auto rendering=info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);rendering.layerCount=1;
        hookExport<PFN_vkCmdBeginRendering>(d,operation==TestOperation::CmdBeginRendering?"vkCmdBeginRendering":"vkCmdBeginRenderingKHR")(c,&rendering);break;}
    CALL(CmdEndRendering,PFN_vkCmdEndRendering,c)
    CALL(CmdEndRenderingKHR,PFN_vkCmdEndRenderingKHR,c)
    case TestOperation::QueueSubmit2:case TestOperation::QueueSubmit2KHR:{auto cb=info<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);cb.commandBuffer=c;auto submit=info<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);submit.commandBufferInfoCount=1;submit.pCommandBufferInfos=&cb;
        const auto result=hookExport<PFN_vkQueueSubmit2>(d,operation==TestOperation::QueueSubmit2?"vkQueueSubmit2":"vkQueueSubmit2KHR")(fixture.queueHandle,1,&submit,VK_NULL_HANDLE);
        require(result==VK_ERROR_OUT_OF_HOST_MEMORY,"unsupported observer submit2 still preserves exact original result");break;}
    default:require(false,"known finite exported unknown operation fixture");
    }
#undef CALL
}
void exactRecordingAliases(){
    const std::array<std::array<TestOperation,2>,6> pairs{{
        {{TestOperation::CmdDispatchBase,TestOperation::CmdDispatchBaseKHR}},{{TestOperation::CmdBeginRendering,TestOperation::CmdBeginRenderingKHR}},
        {{TestOperation::CmdEndRendering,TestOperation::CmdEndRenderingKHR}},{{TestOperation::QueueSubmit2,TestOperation::QueueSubmit2KHR}},
        {{TestOperation::CmdDrawIndirectCount,TestOperation::CmdDrawIndirectCountKHR}},{{TestOperation::CmdDrawIndexedIndirectCount,TestOperation::CmdDrawIndexedIndirectCountKHR}}}};
    const std::array<const char*,6> names{{"vkCmdDispatchBase","vkCmdBeginRendering","vkCmdEndRendering","vkQueueSubmit2","vkCmdDrawIndirectCount","vkCmdDrawIndexedIndirectCount"}};
    for(unsigned available:{1u,2u,3u})for(unsigned selected=0;selected<2;++selected){if(!(available&(1u<<selected)))continue;
        for(unsigned i=0;i<pairs.size();++i){HookFixture fixture(1,0,available);const std::string khr=std::string(names[i])+"KHR";
            const auto core=frame_pixel_vkGetDeviceProcAddr(fixture.dispatchDevice,names[i]),extension=frame_pixel_vkGetDeviceProcAddr(fixture.dispatchDevice,khr.c_str());
            require(bool(core)==bool(available&1)&&bool(extension)==bool(available&2),"exact recording alias absence never fabricated from other nonnull entry");
            if(core&&extension)require(core!=extension,"recording core/KHR entrypoint wrappers remain distinct");
            invokeUnknown(fixture,pairs[i][selected]);
            require(hookCalls.unknownCalls[unsigned(pairs[i][selected])]==1&&!hookCalls.unknownCalls[unsigned(pairs[i][1-selected])],"exact queried recording alias forwards once without calling nonnull sibling");
        }
    }
}
void nativeDynamicState(HookFixture& fixture){
    const auto d=fixture.dispatchDevice;const auto c=fixture.command;
#define NATIVE(name,type,...) do {const auto native=frame_pixel_vkGetDeviceProcAddr(d,"vk" #name);require(native==hookGdpa(d,"vk" #name),"unmodified dynamic command remains the exact native pointer");reinterpret_cast<type>(native)(__VA_ARGS__);}while(false)
    const VkViewport viewport{0,0,32,32,0,1};const VkRect2D rect{{0,0},{32,32}};const float blend[4]{0.25f,0.5f,0.75f,1.0f};auto locations=info<VkSampleLocationsInfoEXT>(VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT);
    NATIVE(CmdSetViewport,PFN_vkCmdSetViewport,c,0,1,&viewport);NATIVE(CmdSetScissor,PFN_vkCmdSetScissor,c,0,1,&rect);
    NATIVE(CmdSetSampleLocationsEnableEXT,PFN_vkCmdSetSampleLocationsEnableEXT,c,VK_FALSE);NATIVE(CmdSetSampleLocationsEXT,PFN_vkCmdSetSampleLocationsEXT,c,&locations);
    NATIVE(CmdSetCullMode,PFN_vkCmdSetCullMode,c,VK_CULL_MODE_NONE);NATIVE(CmdSetFrontFace,PFN_vkCmdSetFrontFace,c,VK_FRONT_FACE_CLOCKWISE);
    NATIVE(CmdSetDepthBias,PFN_vkCmdSetDepthBias,c,0.25f,0.5f,1.0f);NATIVE(CmdSetBlendConstants,PFN_vkCmdSetBlendConstants,c,blend);NATIVE(CmdSetDepthBounds,PFN_vkCmdSetDepthBounds,c,0.0f,1.0f);
#undef NATIVE
}
void recordingSerializationBounds(){
    namespace tx=frame_pixel_ssbo_transaction;tx::RecordingContext context;context.input.operation=tx::Operation::CmdDrawIndexedIndirectCountKHR;context.input.source=tx::ContextSource::RenderPassDecoder;
    context.phase=tx::RecordingPhase::TargetSubpassBeforeDraw;context.before=context.after=tx::Reason::DeviceDestroying;
    context.sequence=context.transactionEpoch=context.device=context.command=context.pool=context.commandGeneration=context.poolGeneration=context.drawOrdinal=UINT64_MAX;
    context.currentSubpass=context.targetSubpass=context.subpassCount=UINT32_MAX;context.rejected=context.metadataTruncated=true;
    context.input.detailReason=context.input.structureType=context.input.index=context.input.chain=context.input.contents=context.input.flags=UINT32_MAX;context.input.object=UINT64_MAX;context.input.targetPipeline=true;
    for(auto& chain:context.input.chains){chain.rootType=chain.termination=UINT32_MAX;chain.types.fill(UINT32_MAX);chain.count=chain.types.size();}
    std::string line;require(recordingContextLine(context,UINT32_MAX,UINT64_MAX,UINT64_MAX,true,line)&&line.size()<=2048,"max-width three-chain first-refusal event fits exact 2KiB line guard");
    require(line.find("4294967295,4294967295")!=std::string::npos&&line.find("ssbo_recording_first_refusal")!=std::string::npos,"all bounded headers and independent first-refusal event serialized");
    tx::RecordingContexts contexts;contexts.count=1;contexts.entries[0].first=context;contexts.entries[0].count=1;contexts.entries[0].lastSequence=context.sequence;
    FILE* output=std::tmpfile();require(output,"cold fixture output opened");RecordingReportProgress progress;
    recordingStatus(contexts,fileno(output),progress);const auto first=std::ftell(output);require(first>0&&first<=2048,"first context emits one bounded line");
    contexts.entries[0].count=UINT64_MAX;recordingStatus(contexts,fileno(output),progress);require(std::ftell(output)==first,"repeat-count changes never emit per-poll lines");
    contexts.count=2;contexts.entries[1]=contexts.entries[0];contexts.entries[1].first.input.operation=tx::Operation::CmdDispatch;
    contexts.firstRefusalAvailable=true;contexts.firstRefusal=context;contexts.overflow=1;contexts.attempts=UINT64_MAX;
    recordingStatus(contexts,fileno(output),progress);const auto after=std::ftell(output);require(after>first&&progress.contexts==2&&progress.firstRefusal&&progress.overflow,"new context, independently reserved first failure and overflow emit despite unchanged refusal state");
    recordingStatus(contexts,fileno(output),progress);require(std::ftell(output)==after,"all subsequent unchanged polls remain silent");std::fclose(output);
    contexts.count=contexts.entries.size();for(auto& entry:contexts.entries){entry.first=context;entry.count=entry.lastSequence=UINT64_MAX;}
    std::ostringstream metadata;recordingMetadataJson(metadata,contexts);
    require(metadata.str().size()<=6144,"max-width 64-entry snapshot repeats plus reserved first-refusal metadata fit 6KiB envelope");
    std::ostringstream envelope;envelope<<"{\"unchanged_raw_metadata_budget\":\""<<std::string(8192,' ')<<"\",\"native_ssbo_control\":{\"available\":false";recordingMetadataJson(envelope,contexts);envelope<<"}}\n";
    require(envelope.str().size()<=16384,"new worst-case recording metadata preserves 8KiB for unchanged snapshot fields within original 16KiB cap");
    require(metadata.str().find("\"target_pipeline\":true")!=std::string::npos,"exact target-pipeline diagnostic survives first-refusal snapshot output");
}
void wholeRecording(const char* vertex,const char* fragment,bool khr,unsigned extensions){
    HookFixture fixture(2,0);fixture.preparePrivate(vertex,fragment,khr);
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
    auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    auto attachments=info<VkRenderPassAttachmentBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);const auto view=h<VkImageView>(405);attachments.attachmentCount=1;attachments.pAttachments=&view;
    auto group=info<VkDeviceGroupRenderPassBeginInfo>(VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO);group.deviceMask=1;
    auto samples=info<VkRenderPassSampleLocationsBeginInfoEXT>(VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT);
    VkAttachmentSampleLocationsEXT initial{};initial.attachmentIndex=0;initial.sampleLocationsInfo.pNext=reinterpret_cast<const void*>(1);initial.sampleLocationsInfo.pSampleLocations=reinterpret_cast<const VkSampleLocationEXT*>(1);
    samples.attachmentInitialSampleLocationsCount=1;samples.pAttachmentInitialSampleLocations=&initial;
    if(extensions){begin.pNext=&attachments;if(extensions>1)attachments.pNext=&group;if(extensions>2)group.pNext=&samples;}
    hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
    hookExport<PFN_vkCmdBeginRenderPass2>(fixture.dispatchDevice,khr?"vkCmdBeginRenderPass2KHR":"vkCmdBeginRenderPass2")(fixture.command,&begin,&subBegin);
    hookExport<PFN_vkCmdBindDescriptorSets>(fixture.dispatchDevice,"vkCmdBindDescriptorSets")(fixture.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds,1,&fixture.native->dynamicOffset);nativeDynamicState(fixture);
    hookExport<PFN_vkCmdDrawIndexed>(fixture.dispatchDevice,"vkCmdDrawIndexed")(fixture.command,6,1,2,-3,4);
    hookExport<PFN_vkCmdNextSubpass2>(fixture.dispatchDevice,khr?"vkCmdNextSubpass2KHR":"vkCmdNextSubpass2")(fixture.command,&subBegin,&subEnd);
    hookExport<PFN_vkCmdEndRenderPass2>(fixture.dispatchDevice,khr?"vkCmdEndRenderPass2KHR":"vkCmdEndRenderPass2")(fixture.command,&subEnd);
    hookExport<PFN_vkEndCommandBuffer>(fixture.dispatchDevice,"vkEndCommandBuffer")(fixture.command);
    const auto before=fixture.context->bridge.snapshot();require(before.replaced&&before.transaction==0&&before.refusal==frame_pixel_ssbo_receiver::Refusal::None,"entire exported Begin/bind/nine dynamics/indexed/Next/End sequence retains strict exact original target authority");
    require(hookCalls.sets==1&&hookCalls.dynamics==9&&hookCalls.ownedIndexed==1&&!hookCalls.nativeIndexed,"native Set0 and all nine original dynamic setters preserved; target draw replaced exactly once");
    require(!before.recordingContexts.firstRefusalAvailable,"supported full chain does not fabricate unknown recording refusal");
    auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&fixture.command;
    require(hookExport<PFN_vkQueueSubmit>(fixture.dispatchDevice,"vkQueueSubmit")(fixture.queueHandle,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS,"actual exported original QueueSubmit result preserved");
    require(hookCalls.submits==1&&hookCalls.markers==1,"matching entire recording submits original once then one owned fence marker");
    const auto original=std::find(hookCalls.events.begin(),hookCalls.events.end(),HookEvent::OriginalSubmit);require(original!=hookCalls.events.end()&&original+1!=hookCalls.events.end()&&original[1]==HookEvent::MarkerSubmit,"owned marker follows unchanged entire original submit");
    // This callback fixture intentionally has no raw image reader/GPU copy.
    // It must refuse pairing rather than turn fake dispatch into source proof.
    require(fixture.context->bridge.snapshot().refusal==frame_pixel_ssbo_receiver::Refusal::CopyCohort,"whole callback fixture cannot publish without independently recorded raw copy");
}
void wholeUnknownCensus(const char* vertex,const char* fragment,bool afterDraw){
    namespace tx=frame_pixel_ssbo_transaction;HookFixture fixture(2,0);fixture.preparePrivate(vertex,fragment,true);
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
    auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
    hookExport<PFN_vkCmdBeginRenderPass2KHR>(fixture.dispatchDevice,"vkCmdBeginRenderPass2KHR")(fixture.command,&begin,&subBegin);
    if(afterDraw)hookExport<PFN_vkCmdDrawIndexed>(fixture.dispatchDevice,"vkCmdDrawIndexed")(fixture.command,6,1,2,-3,4);
    invokeUnknown(fixture,TestOperation::CmdBeginQuery);
    const auto first=fixture.context->bridge.snapshot();require(first.recordingContexts.firstRefusalAvailable,"exact first rejected callback retained independently");const auto& failure=first.recordingContexts.firstRefusal;
    require(failure.input.operation==TestOperation::CmdBeginQuery&&failure.rejected&&failure.input.source==tx::ContextSource::Wrapper&&failure.after==tx::Reason::UnknownRoute,"first refusal names exact query wrapper and resulting transaction reason");
    require(failure.phase==(afterDraw?tx::RecordingPhase::TargetSubpassAfterDraw:tx::RecordingPhase::TargetSubpassBeforeDraw),"first rejection phase distinguishes before and after exact selected draw");
    require(failure.command==frame_pixel_ssbo_clone::handle(fixture.command)&&failure.commandGeneration==first.commandGeneration&&failure.poolGeneration==first.poolGeneration,"first rejection belongs to exact adopted original command/pool cohort");
    const std::array<TestOperation,21> later{{TestOperation::CmdBindPipeline,TestOperation::CmdBindDescriptorSets,TestOperation::CmdBindShadersEXT,TestOperation::CmdDispatch,TestOperation::CmdDispatchIndirect,TestOperation::CmdDispatchBase,TestOperation::CmdDispatchBaseKHR,TestOperation::CmdBeginQueryIndexedEXT,TestOperation::CmdDrawIndirect,TestOperation::CmdDrawIndexedIndirect,TestOperation::CmdExecuteCommands,TestOperation::CmdDrawIndirectCount,TestOperation::CmdDrawIndirectCountKHR,TestOperation::CmdDrawIndexedIndirectCount,TestOperation::CmdDrawIndexedIndirectCountKHR,TestOperation::CmdBeginRendering,TestOperation::CmdBeginRenderingKHR,TestOperation::CmdEndRendering,TestOperation::CmdEndRenderingKHR,TestOperation::QueueSubmit2,TestOperation::QueueSubmit2KHR}};
    for(const auto operation:later)invokeUnknown(fixture,operation);
    const auto last=fixture.context->bridge.snapshot();require(last.refusal==frame_pixel_ssbo_receiver::Refusal::UnknownRoute&&last.recordingContexts.firstRefusal.sequence==failure.sequence&&last.recordingContexts.firstRefusal.input.operation==TestOperation::CmdBeginQuery,"later failures never overwrite exact first rejection");
    for(const auto operation:later){require(hookCalls.unknownCalls[unsigned(operation)]==1,"every distinct later unsupported original forwarded once");bool found=false;for(unsigned i=0;i<last.recordingContexts.count;++i)found|=last.recordingContexts.entries[i].first.input.operation==operation;require(found,"all later distinct unsupported callbacks retained after sticky first refusal");}
}
void headerFirstFailure(const char* vertex,const char* fragment,unsigned route){
    namespace tx=frame_pixel_ssbo_transaction;HookFixture fixture(2,0);fixture.preparePrivate(vertex,fragment,route==2);
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
    VkBaseInStructure third{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr},second{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&third},first{VK_STRUCTURE_TYPE_APPLICATION_INFO,&second};begin.pNext=&first;
    auto subBegin=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);subBegin.contents=VK_SUBPASS_CONTENTS_INLINE;auto subEnd=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    hookCalls.beginInfo=&begin;hookCalls.subBegin=&subBegin;hookCalls.subEnd=&subEnd;
    if(!route)hookExport<PFN_vkCmdBeginRenderPass>(fixture.dispatchDevice,"vkCmdBeginRenderPass")(fixture.command,&begin,subBegin.contents);
    else hookExport<PFN_vkCmdBeginRenderPass2>(fixture.dispatchDevice,route==2?"vkCmdBeginRenderPass2KHR":"vkCmdBeginRenderPass2")(fixture.command,&begin,&subBegin);
    const auto state=fixture.context->bridge.snapshot();require(state.recordingContexts.firstRefusalAvailable,"first unsupported recording header retained");const auto& failure=state.recordingContexts.firstRefusal;
    const auto operation=route==0?TestOperation::CmdBeginRenderPass:route==1?TestOperation::CmdBeginRenderPass2:TestOperation::CmdBeginRenderPass2KHR;
    require(failure.input.operation==operation&&failure.input.source==tx::ContextSource::RenderPassDecoder&&failure.phase==tx::RecordingPhase::RecordingOutsidePass,"exact Begin alias and pre-pass failure phase retained");
    require(failure.input.detailReason==unsigned(recording::Reason::UnknownExtension)&&failure.input.structureType==VK_STRUCTURE_TYPE_APPLICATION_INFO&&failure.input.index==0&&failure.input.chain==0,"public decoder refusal identifies exact first unknown header");
    const auto& chain=failure.input.chains[0];require(chain.rootType==VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO&&chain.count==3&&chain.types[0]==VK_STRUCTURE_TYPE_APPLICATION_INFO&&chain.types[1]==VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO&&chain.types[2]==VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,"complete later header census survives first unsupported header without unknown payload decoding");
    hookExport<PFN_vkCmdDrawIndexed>(fixture.dispatchDevice,"vkCmdDrawIndexed")(fixture.command,6,1,2,-3,4);
    require(hookCalls.begins==1&&hookCalls.nativeIndexed==1&&!hookCalls.ownedIndexed,"rejected metadata forwards original Begin and indexed draw once, never substitutes probe");
}
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
    bool feature=false,overflow=false;std::uint64_t reasons=0;unsigned recipes=0,sets=0,contextsCount=0,creations=0,graphics=0;std::uint32_t skips=0;
    for(unsigned i=0;i<2;++i)definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,graphics,overflow);
    const auto before=std::ftell(output);require(before>0&&before<65536,"bounded grouped production status output");
    definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,graphics,overflow);
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
void exactGraphicsReporting(){
    HookFixture fixture;
    for(unsigned i=0;i<9;++i)
        fixture.context->definitions.createPipeline(h<VkPipeline>(0x700+i),fixture.shape.graphics,VK_SUCCESS);
    FILE* output=std::tmpfile();require(output!=nullptr,"private exact-target status output");
    bool feature=false,overflow=false;std::uint64_t reasons=0;unsigned recipes=0,sets=0,contextsCount=0,creations=0,graphics=0;std::uint32_t skips=0;
    for(unsigned i=0;i<2;++i)definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,graphics,overflow);
    const auto before=std::ftell(output);
    require(graphics==8&&overflow&&before>0,"complete exact target bank and explicit overflow reported by actual cold writer");
    definitionStatus(*fixture.context,fileno(output),feature,reasons,recipes,sets,skips,contextsCount,creations,graphics,overflow);
    require(std::ftell(output)==before,"complete target observations are emitted once across repeated cold polls");
    require(!std::fseek(output,0,SEEK_SET),"read exact target reports from start");
    std::string text(static_cast<std::size_t>(before),'\0');
    require(std::fread(text.data(),1,text.size(),output)==text.size(),"complete exact target JSON status read");std::fclose(output);
    unsigned amount=0;std::size_t cursor=0;
    while((cursor=text.find("\"event\":\"ssbo_definition_graphics_state\"",cursor))!=std::string::npos){++amount;++cursor;}
    require(amount==8&&text.find("\"graphics_index\":7")!=std::string::npos,"same-shaped native creates keep all eight separately identified receipts");
    require(text.find("\"graphics_observation_overflow\":true")!=std::string::npos,"exact-target overflow cannot silently disappear in grouped refusal reports");
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
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
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
    auto begin=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);begin.renderPass=fixture.shape.graphics.renderPass;begin.framebuffer=h<VkFramebuffer>(300);begin.renderArea={{0,0},{32,32}};
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
    if(argc==2&&!std::strcmp(argv[1],"--recording-public")){exactRecordingAliases();recordingSerializationBounds();std::cout<<"{\"scope\":\"exact recording alias forwarding and bounded cold context output\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;}
    if(argc==4&&!std::strcmp(argv[1],"--recording-only")){for(bool khr:{false,true})for(unsigned extensions=0;extensions<4;++extensions)wholeRecording(argv[2],argv[3],khr,extensions);for(bool after:{false,true})wholeUnknownCensus(argv[2],argv[3],after);for(unsigned route=0;route<3;++route)headerFirstFailure(argv[2],argv[3],route);std::cout<<"{\"scope\":\"entire actual recording callback path, fake original dispatch\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;}

    if(argc==2&&!std::strcmp(argv[1],"--aliases-only")){
        exportedExactAliasRoutes();
        std::cout<<"{\"scope\":\"exact exported RP2 core/KHR API routes, fake downstream only\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;
    }
    if(argc==4&&!std::strcmp(argv[1],"--subpasses-only")){
        for(unsigned count:{2u,3u})for(unsigned route=0;route<3;++route)multipleSubpassHooks(argv[2],argv[3],count,route);
        for(unsigned route:{1u,2u})for(unsigned mutation:{1u,2u})multipleSubpassHooks(argv[2],argv[3],3,route,mutation);
        std::cout<<"{\"scope\":\"actual exported multi-subpass Snapshot hooks, fake downstream only\",\"checks\":"<<checks.load()<<",\"native_authority\":false}\n";return 0;
    }
    require(argc==1||argc==3,"optional private original VS/PS arguments");
    exportedExactAliasRoutes();exportedCreation();exportedObservationFailure();exactGraphicsReporting();
    if(argc==3)for(bool khr:{false,true})for(unsigned mutation=0;mutation<=5;++mutation)privateHooks(argv[1],argv[2],mutation,khr);
    std::cout<<"{\"scope\":\"actual SnapshotTU exported GDPA RP2/KHR hooks, fake downstream only\",\"checks\":"<<checks.load()
        <<",\"private_pins_tested\":"<<(argc==3?"true":"false")<<",\"native_admission_proven\":false,\"passthrough_authorized\":false}\n";
}
