#include "../layer/pixel_ssbo_owned_clone.h"
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace frame_pixel_ssbo_owned;
namespace {
unsigned checks=0;
void demand(bool ok,const char* message){++checks;if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
template<class T>T object(std::uint64_t n){T value{};std::memcpy(&value,&n,sizeof(value));return value;}
template<class T>std::uint64_t number(T value){return frame_pixel_ssbo_clone::handle(value);}
struct Driver {
    unsigned calls=0,failAt=0,destroys=0,flushes=0,updates=0;
    bool noncoherent=false,partialPipeline=false,missing=false,wrongRequirements=false;
    bool refuseMemoryType=false,oversize=false,missingPass2=false,aliasPass2=false,missingKhr2=false;
    bool mappedLive=false;
    unsigned expectedCount=3,expectedFragmentBytes=20,expectedSubpasses=1,expectedSubpass=UINT32_MAX,pass2Calls=0,corePass2Calls=0,khrPass2Calls=0,corePassLookups=0,khrPassLookups=0;
    std::uint64_t next=100;
    std::set<std::uint64_t> live;
    std::array<std::uint8_t,256> mapped{};
    std::vector<Stage> stages;
    VkDevice device=object<VkDevice>(1);
    VkDescriptorSetLayout set0{},set1{};VkPipelineLayout layout{};VkRenderPass pass{};
    VkShaderModule vertex{},fragment{};VkBuffer buffer{};VkDeviceMemory memory{};VkDescriptorPool pool{};
    template<class T>VkResult create(Stage s,T* out){
        ++calls;stages.push_back(s);*out=VK_NULL_HANDLE;
        if(calls==failAt){
            if(s==Stage::Pipeline && partialPipeline){*out=object<T>(++next);live.insert(number(*out));}
            else if(s!=Stage::Pipeline)*out=object<T>(0xdeadbeef); // Undefined failed output is not an owned object.
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        *out=object<T>(++next);live.insert(number(*out));return VK_SUCCESS;
    }
    VkResult step(Stage s){++calls;stages.push_back(s);return calls==failAt?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
    template<class T>void destroy(T obj){demand(live.erase(number(obj))==1,"destroy only an owned live object once");++destroys;}
}* driver=nullptr;
void common(VkDevice d,const VkAllocationCallbacks* a){demand(d==driver->device && !a,"downstream own device/null allocator");}

VKAPI_ATTR VkResult VKAPI_CALL createSet(VkDevice d,const VkDescriptorSetLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    common(d,a);demand(ci && ci->sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO && !ci->pNext && !ci->flags,"ordinary owned set layout");
    const bool first=!driver->set0;
    if(first)demand(ci->bindingCount==3 && ci->pBindings[2].binding==9 && ci->pBindings[2].descriptorCount==driver->expectedCount,"original Set0 descriptor count retained without native expansion");
    else demand(ci->bindingCount==1 && ci->pBindings[0].binding==0 && ci->pBindings[0].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,"owned Set1 record binding");
    auto result=driver->create(first?Stage::Set0:Stage::Set1,out);
    if(result==VK_SUCCESS)(first?driver->set0:driver->set1)=*out;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroySet(VkDevice d,VkDescriptorSetLayout obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice d,const VkPipelineLayoutCreateInfo* ci,const VkAllocationCallbacks* a,VkPipelineLayout* out){
    common(d,a);demand(ci->setLayoutCount==2 && ci->pSetLayouts[0]==driver->set0 && ci->pSetLayouts[1]==driver->set1,"layout uses own Set0/Set1 only");
    demand(ci->pushConstantRangeCount==1 && ci->pPushConstantRanges[0].size==16 && ci->pPushConstantRanges[0].stageFlags==VK_SHADER_STAGE_VERTEX_BIT,"original complete push range retained");
    auto result=driver->create(Stage::Layout,out);if(result==VK_SUCCESS)driver->layout=*out;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkPipelineLayout obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice d,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    common(d,a);demand(ci->subpassCount==driver->expectedSubpasses && ci->pSubpasses && ci->pSubpasses[0].colorAttachmentCount==1,"owned renderpass rebased view");
    auto result=driver->create(Stage::RenderPass,out);if(result==VK_SUCCESS)driver->pass=*out;return result;
}
VkResult createPass2Impl(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    common(d,a);++driver->pass2Calls;demand(ci->sType==VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 && !ci->pNext && ci->subpassCount==driver->expectedSubpasses,"owned RP2 preserves original route and whole cohort");
    demand(ci->pSubpasses[1].inputAttachmentCount==1 && ci->pSubpasses[1].pInputAttachments[0].aspectMask==VK_IMAGE_ASPECT_COLOR_BIT,"owned RP2 input aspect cannot be lost via legacy fallback");
    auto result=driver->create(Stage::RenderPass,out);if(result==VK_SUCCESS)driver->pass=*out;return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createPass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++driver->corePass2Calls;return createPass2Impl(d,ci,a,out);}
VKAPI_ATTR VkResult VKAPI_CALL createPass2KHR(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){++driver->khrPass2Calls;return createPass2Impl(d,ci,a,out);}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice d,VkRenderPass obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice d,const VkShaderModuleCreateInfo* ci,const VkAllocationCallbacks* a,VkShaderModule* out){
    common(d,a);const bool first=!driver->vertex;
    demand(ci->pCode && ci->pCode[0]==0x07230203u && ci->codeSize==(first?9460u:driver->expectedFragmentBytes),"only explicitly owned original VS/probe PS bytes");
    auto result=driver->create(first?Stage::Vertex:Stage::Fragment,out);
    if(result==VK_SUCCESS)(first?driver->vertex:driver->fragment)=*out;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL createPipeline(VkDevice d,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* ci,const VkAllocationCallbacks* a,VkPipeline* out){
    common(d,a);demand(!cache && count==1 && ci->layout==driver->layout && ci->renderPass==driver->pass,"owned pipeline dependencies/null cache");
    demand(ci->stageCount==2 && ci->pStages && ci->pStages[0].module==driver->vertex && ci->pStages[1].module==driver->fragment,"only owned shader handles reach pipeline");
    demand(std::string(ci->pStages[0].pName)=="VS_DISTORT" && std::string(ci->pStages[1].pName)=="PS_DISTORT","original entry names retained");
    demand(!ci->basePipelineHandle,"no native derivative handle borrowed");
    demand(driver->expectedSubpass==UINT32_MAX || ci->subpass==driver->expectedSubpass,"owned pipeline preserves explicitly expected original subpass index");
    return driver->create(Stage::Pipeline,out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice d,const VkBufferCreateInfo* ci,const VkAllocationCallbacks* a,VkBuffer* out){
    common(d,a);demand(ci->size==128 && ci->usage==(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT) && ci->sharingMode==VK_SHARING_MODE_EXCLUSIVE,"bounded real record/reset usage");
    auto result=driver->create(Stage::Buffer,out);if(result==VK_SUCCESS)driver->buffer=*out;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice d,VkBuffer obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR void VKAPI_CALL requirements(VkDevice d,VkBuffer b,VkMemoryRequirements* r){
    demand(d==driver->device && b==driver->buffer,"requirements for own buffer");*r={driver->oversize?MaximumAllocation+1:256,driver->wrongRequirements?3u:16u,driver->refuseMemoryType?2u:1u};
}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkMemoryAllocateInfo* ci,const VkAllocationCallbacks* a,VkDeviceMemory* out){
    common(d,a);demand(ci->allocationSize==256 && ci->memoryTypeIndex==0,"bounded host-visible allocation");
    auto result=driver->create(Stage::Allocate,out);if(result==VK_SUCCESS)driver->memory=*out;return result;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice d,VkDeviceMemory obj,const VkAllocationCallbacks* a){common(d,a);demand(!driver->mappedLive,"unmap successful mapping before free");driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize offset){
    demand(d==driver->device && b==driver->buffer && m==driver->memory && !offset,"own buffer/memory zero offset");return driver->step(Stage::Bind);
}
VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice d,VkDeviceMemory m,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** out){
    demand(d==driver->device && m==driver->memory && !offset && size==VK_WHOLE_SIZE && !flags,"whole own allocation map");
    const auto result=driver->step(Stage::Map);driver->mappedLive=result==VK_SUCCESS;
    *out=driver->mappedLive?driver->mapped.data():reinterpret_cast<void*>(1);return result;
}
VKAPI_ATTR void VKAPI_CALL unmap(VkDevice d,VkDeviceMemory m){demand(d==driver->device && m==driver->memory && driver->mappedLive,"unmap only a successful own mapping");driver->mappedLive=false;}
VKAPI_ATTR VkResult VKAPI_CALL flush(VkDevice d,std::uint32_t count,const VkMappedMemoryRange* ranges){
    demand(d==driver->device && count==1 && ranges->memory==driver->memory && !ranges->offset && ranges->size==VK_WHOLE_SIZE,"noncoherent whole allocation flush");
    ++driver->flushes;return driver->step(Stage::Flush);
}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice d,const VkDescriptorPoolCreateInfo* ci,const VkAllocationCallbacks* a,VkDescriptorPool* out){
    common(d,a);demand(ci->maxSets==1 && ci->poolSizeCount==1 && ci->pPoolSizes[0].descriptorCount==1,"one owned storage descriptor");
    auto result=driver->create(Stage::Pool,out);if(result==VK_SUCCESS)driver->pool=*out;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice d,VkDescriptorPool obj,const VkAllocationCallbacks* a){common(d,a);driver->destroy(obj);}
VKAPI_ATTR VkResult VKAPI_CALL allocateSet(VkDevice d,const VkDescriptorSetAllocateInfo* ci,VkDescriptorSet* out){
    demand(d==driver->device && ci->descriptorPool==driver->pool && ci->descriptorSetCount==1 && ci->pSetLayouts[0]==driver->set1,"allocate only own Set1");
    const auto result=driver->step(Stage::Set);*out=result==VK_SUCCESS?object<VkDescriptorSet>(900):VK_NULL_HANDLE;return result;
}
VKAPI_ATTR void VKAPI_CALL update(VkDevice d,std::uint32_t count,const VkWriteDescriptorSet* writes,std::uint32_t copyCount,const VkCopyDescriptorSet*){
    demand(d==driver->device && count==1 && !copyCount && writes->dstSet==object<VkDescriptorSet>(900) && writes->descriptorCount==1 && writes->pBufferInfo->buffer==driver->buffer && writes->pBufferInfo->range==128 && !writes->pBufferInfo->offset,"write exact own128B descriptor, no native updates");++driver->updates;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getProc(VkDevice d,const char* name){
    demand(d==driver->device,"resolve only downstream own device");if(driver->missing)return nullptr;
    if(!std::strcmp(name,"vkCreateRenderPass2")){++driver->corePassLookups;return driver->missingPass2 || driver->aliasPass2?nullptr:reinterpret_cast<PFN_vkVoidFunction>(createPass2);}
    if(!std::strcmp(name,"vkCreateRenderPass2KHR")){++driver->khrPassLookups;return driver->missingPass2 || driver->missingKhr2?nullptr:reinterpret_cast<PFN_vkVoidFunction>(createPass2KHR);}
#define MAP(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    MAP(CreateDescriptorSetLayout,createSet) MAP(DestroyDescriptorSetLayout,destroySet)
    MAP(CreatePipelineLayout,createLayout) MAP(DestroyPipelineLayout,destroyLayout)
    MAP(CreateRenderPass,createPass) MAP(DestroyRenderPass,destroyPass)
    MAP(CreateShaderModule,createShader) MAP(DestroyShaderModule,destroyShader)
    MAP(CreateGraphicsPipelines,createPipeline) MAP(DestroyPipeline,destroyPipeline)
    MAP(CreateBuffer,createBuffer) MAP(DestroyBuffer,destroyBuffer) MAP(GetBufferMemoryRequirements,requirements)
    MAP(AllocateMemory,allocate) MAP(FreeMemory,freeMemory) MAP(BindBufferMemory,bind)
    MAP(MapMemory,map) MAP(UnmapMemory,unmap) MAP(FlushMappedMemoryRanges,flush)
    MAP(CreateDescriptorPool,createPool) MAP(DestroyDescriptorPool,destroyPool)
    MAP(AllocateDescriptorSets,allocateSet) MAP(UpdateDescriptorSets,update)
#undef MAP
    return nullptr;
}

struct Fixture {
    // Synthetic driver-call fixture only; not a genuine SPIR-V/native recipe.
    // Pin/deep-copy validation is independently tested by the capture contract.
    frame_pixel_ssbo_clone::Recipe recipe{};
    std::array<std::uint32_t,5> fragment{{0x07230203u,0x10000u,0,1,0}};
    Preparation p{};
    Fixture(){
        recipe.definitionComplete=recipe.feature.complete=recipe.feature.enabled=true;
        recipe.set0.complete=recipe.layout.complete=recipe.renderPass.complete=recipe.vertex.complete=recipe.fragment.complete=recipe.graphics.complete=true;
        recipe.set0.count=3;
        recipe.set0.bindings[0]={4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        recipe.set0.bindings[1]={6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        recipe.set0.bindings[2]={9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        recipe.layout.count=1;recipe.layout.ranges[0]={VK_SHADER_STAGE_VERTEX_BIT,0,16};
        recipe.renderPass.attachmentCount=recipe.renderPass.colorCount=1;
        recipe.renderPass.attachments[0].format=VK_FORMAT_R32G32B32A32_SFLOAT;recipe.renderPass.attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
        recipe.renderPass.colors[0]={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        recipe.vertex.bytes=9460;recipe.vertex.words[0]=0x07230203u;recipe.fragment.bytes=6620;
        recipe.graphics.info.stageCount=2;
        for(unsigned i=0;i<2;++i){recipe.graphics.stageInfo[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;recipe.graphics.stageInfo[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;std::memcpy(recipe.graphics.entries[i].data(),i?"PS_DISTORT":"VS_DISTORT",11);}
        p.device=driver->device;p.downstream=getProc;p.recipe=&recipe;p.fragment=fragment.data();p.fragmentBytes=sizeof(fragment);
        frame_receiver::Sha256 hash;hash.update(fragment.data(),sizeof(fragment));p.fragmentSha256=hash.finish();
        p.limits.maxBoundDescriptorSets=2;p.limits.maxStorageBufferRange=128;p.limits.maxDescriptorSetStorageBuffers=8;
        p.limits.maxPerStageDescriptorStorageBuffers=8;p.limits.maxPerStageResources=128;p.limits.maxPushConstantsSize=128;
        p.memory.memoryTypeCount=p.memory.memoryHeapCount=1;p.memory.memoryHeaps[0].size=MaximumAllocation;
        p.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            (driver->noncoherent?VkMemoryPropertyFlags(0):VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    }
};
void happy(bool noncoherent){
    Driver state;driver=&state;state.noncoherent=noncoherent;Fixture f;Clone clone;
    demand(clone.prepare(f.p).status==Status::Ok && clone.ready(),"cold build succeeds");
    demand(state.updates==1 && state.flushes==(noncoherent?1u:0u),"actual coherent/noncoherent initialization paths");
    demand(clone.pipeline() && clone.layout() && clone.set1() && clone.recordBuffer() && clone.recordMemory() && clone.allocationBytes()==256,"publish complete own objects only");
    const auto* prepared=clone.preparedRecipe();demand(prepared && prepared!=&f.recipe,"prepared clone owns exact cold recipe snapshot");
    f.recipe.set0.bindings[2].descriptorCount=99;f.recipe.graphics.entries[0][0]='?';
    demand(prepared->set0.bindings[2].descriptorCount==3 && prepared->graphics.entries[0][0]=='V',"later caller overwrite cannot change actual constructed object facts");
    demand(!clone.hostCoherent()==noncoherent,"coherence reflects chosen memory");
    for(unsigned i=0;i<128;++i)demand(state.mapped[i]==0,"record initialized before publication");
    const auto calls=state.calls;demand(clone.prepare(f.p).status==Status::AlreadyPrepared && calls==state.calls,"no replacement over live allocation");
    demand(clone.markReferenced() && clone.referenced(),"recording lease acquired before use");
    const auto live=state.live;
    for(auto proof:{ReleaseProof{},ReleaseProof{true,false},ReleaseProof{false,true}}){
        demand(clone.release(proof).status==Status::UnsafeToRelease && state.live==live && clone.ready(),"fence/quiescence alone cannot retire reusable recording");
    }
    demand(clone.release({true,true}).status==Status::Ok && state.live.empty() && !clone.ready(),"retired/quiescent release frees every object");
    demand(!clone.markReferenced() && !clone.pipeline() && !clone.mappedRecord(),"released handles never published");
    demand(!clone.preparedRecipe(),"released recipe facts never published");
    demand(clone.release().status==Status::Ok && state.live.empty(),"release idempotent");
}
void failures(bool noncoherent){
    // Each native-call failure owns a different dependency prefix. Include the
    // graphics failure case where a driver legally returns a partial pipeline.
    const unsigned count=noncoherent?14:13;
    for(unsigned at=1;at<=count;++at){
        Driver state;driver=&state;state.noncoherent=noncoherent;state.failAt=at;state.partialPipeline=true;Fixture f;Clone clone;
        const auto result=clone.prepare(f.p);
        demand(result.status==Status::VulkanFailure && result.driver==VK_ERROR_OUT_OF_DEVICE_MEMORY,"preserve actual driver failure");
        demand(state.calls==at && result.stage==state.stages.back(),"no later creation after failed stage");
        demand(state.live.empty() && !clone.ready() && !clone.recordBuffer(),"partial failure releases all owned prefix handles");
        demand(clone.release().status==Status::Ok,"failed object remains safely releasable");
    }
}
void refusals(){
    for(unsigned mode=0;mode<12;++mode){Driver state;driver=&state;Fixture f;Clone clone;
        if(mode==0)f.p.fragmentSha256[0]^=1;
        if(mode==1)f.recipe.runtimeActivationAuthorized=true;
        if(mode==2)f.p.limits.maxBoundDescriptorSets=1;
        if(mode==3)f.p.limits.maxPushConstantsSize=8;
        if(mode==4)state.missing=true;
        if(mode==5)f.p.memory.memoryTypeCount=33;
        if(mode==6)f.p.fragmentBytes=MaximumProbeBytes+4;
        if(mode==7)f.recipe.set0.count=33;
        if(mode==8)f.recipe.set0.bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if(mode==8)f.p.limits.maxDescriptorSetStorageBuffers=1;
        if(mode==9)f.p.limits.maxPerStageDescriptorStorageBuffers=0;
        if(mode==10)f.p.limits.maxPerStageResources=2;
        if(mode==11)f.recipe.set0.bindings[2].descriptorCount=1;
        demand(clone.prepare(f.p).status!=Status::Ok && !state.calls && state.live.empty(),"invalid input/missing PFN/budget refuses before driver creation");
    }
    for(unsigned mode=0;mode<4;++mode){Driver state;driver=&state;Fixture f;Clone clone;
        state.wrongRequirements=mode==0;state.refuseMemoryType=mode==1;state.oversize=mode==2;
        if(mode==3)f.p.memory.memoryTypes[0].propertyFlags|=VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
        demand(clone.prepare(f.p).status==Status::InvalidInput && state.live.empty(),"invalid actual memory requirements refuse and clean dependencies");
        demand(state.calls==8,"unreviewed memory flags/requirements rejected before allocation");
    }
    {Driver state;driver=&state;Fixture f;Clone clone;
        demand(clone.prepare(f.p).status==Status::Ok,"unreferenced cold allocation created");
        demand(clone.release().status==Status::Ok && state.live.empty(),"never-recorded allocation needs no invented queue wait");}
}
void renderPassCohorts(){
    namespace c=frame_pixel_ssbo_clone;
    for(unsigned count:{2u,3u})for(unsigned mode=0;mode<7;++mode){Driver state;driver=&state;state.expectedSubpasses=count;state.expectedSubpass=1;state.aliasPass2=mode==3||mode==5;state.missingKhr2=mode==4||mode==6;Fixture f;Clone clone;
        auto& pass=f.recipe.renderPass;pass.subpassCount=count;for(unsigned i=1;i<count;++i)*pass.subpass(i)=static_cast<const c::SubpassDefinition&>(pass);
        pass.route2=mode!=0;pass.route2KHR=mode==2||mode==4||mode==5;pass.subpass(1)->inputCount=1;pass.subpass(1)->inputs[0]={0,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};pass.subpass(1)->inputAspects[0]=VK_IMAGE_ASPECT_COLOR_BIT;f.recipe.graphics.info.subpass=1;
        const auto result=clone.prepare(f.p);
        if(mode==3||mode==4)demand(result.status==Status::MissingProc && result.stage==Stage::RenderPass && !state.calls && state.live.empty(),"missing exact RP2 entry refuses despite a usable different-name PFN");
        else {demand(result.status==Status::Ok && clone.preparedRecipe()->renderPass.subpassCount==count && clone.preparedRecipe()->graphics.info.subpass==1,"complete owned cohort and selected index survive actual clone snapshot");demand(state.pass2Calls==(mode?1u:0u),"legacy vs core/KHR RP2 owned creation route exact");}
        const bool khr=mode==2||mode==4||mode==5;
        demand(state.corePassLookups==(mode&&!khr?1u:0u) && state.khrPassLookups==(khr?1u:0u),"only the exact original requested RP2 name is queried");
        demand(state.corePass2Calls==((mode==1||mode==6)?1u:0u) && state.khrPass2Calls==((mode==2||mode==5)?1u:0u),"only exact original core or KHR callable is executed");
        demand(clone.release().status==Status::Ok && state.live.empty(),"cohort owner success/refusal safely releases all children");
    }
}
std::vector<std::uint32_t> readShader(const char* path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);demand(bool(input),"explicit private shader readable");const auto bytes=input.tellg();
    demand(bytes>=20 && bytes<=std::streamoff(MaximumProbeBytes) && bytes%4==0,"bounded explicit shader extent");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4);input.seekg(0);
    demand(bool(input.read(reinterpret_cast<char*>(words.data()),bytes)),"complete explicit shader read");return words;
}
void count1Profile(const char* vsPath,const char* psPath,const char* probePath){
    const auto vs=readShader(vsPath),ps=readShader(psPath),probe=readShader(probePath);
    for(unsigned mode=0;mode<5;++mode){Driver state;driver=&state;state.expectedCount=1;state.expectedFragmentBytes=static_cast<unsigned>(probe.size()*4);Fixture f;Clone clone;
        frame_pixel_ssbo_clone::Refusal why;frame_pixel_ssbo_clone::ProbeInterface profile;
        demand(frame_pixel_ssbo_clone::ProbeInterface::count1(probe.data(),probe.size()*4,profile,why),"count1 independently pinned interface proof");
        f.recipe.set0.probeInterface=profile;f.recipe.set0.bindings[2].descriptorCount=1;
        std::copy(vs.begin(),vs.end(),f.recipe.vertex.words.begin());std::copy(ps.begin(),ps.end(),f.recipe.fragment.words.begin());
        f.p.fragment=probe.data();f.p.fragmentBytes=probe.size()*4;
        if(mode==1){f.p.fragment=f.fragment.data();f.p.fragmentBytes=sizeof(f.fragment);} // Self-consistent caller hash, wrong interface.
        if(mode==2)f.recipe.set0.probeInterface={};
        if(mode==3)f.recipe.fragment.words[7]^=1;
        if(mode==4)f.recipe.set0.bindings[2].descriptorCount=3;
        frame_receiver::Sha256 hash;hash.update(f.p.fragment,f.p.fragmentBytes);f.p.fragmentSha256=hash.finish();
        const auto result=clone.prepare(f.p);
        if(mode==0){demand(result.status==Status::Ok && clone.ready(),"count1 original-source plus exact probe cold construction succeeds");demand(clone.release().status==Status::Ok && state.live.empty(),"specialized clone frees complete cold resources");}
        else demand(result.status==Status::InvalidInput && !state.calls && !clone.ready(),"source/interface/descriptor mismatch refuses before every driver call");
    }
}
}
int main(int argc,char** argv){if(argc==2 && !std::strcmp(argv[1],"--renderpass-cohort-only")){renderPassCohorts();std::cout<<"{\"scope\":\"owned-renderpass-cohort-fake-dispatch\",\"checks\":"<<checks<<"}\n";return 0;}demand(argc==1 || argc==4,"optional private VS PS count1-probe paths");happy(false);happy(true);failures(false);failures(true);refusals();renderPassCohorts();if(argc==4)count1Profile(argv[1],argv[2],argv[3]);std::cout<<"owned cold-clone lifecycle: "<<checks<<" checks\n";}
