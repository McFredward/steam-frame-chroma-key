// Reuse CI builders and spies, without invoking their inherited test suites.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main inherited_clone_controls_not_run
#include "test_pixel_ssbo_clone_contract.cpp"
#undef main
#pragma GCC diagnostic pop
#include "../layer/native_presenter_receiver.h"
#include "../layer/pixel_snapshot_layer.cpp"
#include <dirent.h>

namespace receiver_test {
namespace r=frame_native_presenter_receiver;
constexpr const char* Capture="0123456789abcdef0123456789abcdef";
struct Directory {
    std::string path; int fd=-1;
    Directory(){char name[]="/tmp/ghvr-presenter-fixture-XXXXXX";const auto* made=mkdtemp(name);require(made,"owned temporary directory");path=made;fd=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);require(fd>=0,"private directory descriptor");}
    ~Directory(){if(fd>=0){auto* stream=fdopendir(dup(fd));if(stream){while(auto* entry=readdir(stream))if(std::strcmp(entry->d_name,".")&&std::strcmp(entry->d_name,".."))unlinkat(fd,entry->d_name,0);closedir(stream);}close(fd);}rmdir(path.c_str());}
    void file(const char* name,const void* data,std::size_t size,mode_t mode=0600){unlinkat(fd,name,0);const int out=openat(fd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);require(out>=0,"create owned fixture file");require(!fchmod(out,mode)&&frame_pixel_io::writeAll(out,data,size),"write exact owned bytes");close(out);}
    void marker(){file("synthetic-presenter.enable",r::EnableMagic,std::strlen(r::EnableMagic));}
    std::string read(const char* name){const int in=openat(fd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(in>=0,"read owned receipt");char data[2048]{};const auto count=::read(in,data,sizeof(data));close(in);require(count>0&&count<static_cast<ssize_t>(sizeof(data)),"bounded status receipt");return {data,static_cast<std::size_t>(count)};}
};
std::string status(bool maximum=false){Directory d;r::Source source;r::Snapshot s;s.observedDevice=17;s.observedApi=VK_API_VERSION_1_1;s.activeDevices=1;
    if(maximum){s.revision=s.pipelineAttempts=s.pipelineReplacements=s.observedDevice=UINT64_MAX;s.observedApi=s.activeDevices=UINT32_MAX;s.primitiveReason=frame_native_presenter::Reason::UnexpectedHandle;s.definitionHistoryFailed=true;s.graphicsGate=frame_native_presenter::GraphicsGate::Multisample;s.attachmentFormat=VK_FORMAT_MAX_ENUM;s.unsupportedDynamic=VK_DYNAMIC_STATE_MAX_ENUM;}
    require(r::writeStatusCold(d.fd,Capture,source,s,maximum?32:1,maximum),"actual producer writes bounded JSON");return d.read(r::StatusFile);}
void publicFiles(){
    {Directory d;r::Source s;require(!s.prepareCold(-1)&&s.reason()==r::SourceReason::Directory,"invalid directory refused");}
    {Directory d;fchmod(d.fd,0755);r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Directory,"shared directory refused");}
    {Directory d;r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"missing explicit opt-in stays off");d.marker();require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"failed preparation cannot resurrect");}
    for(unsigned variant=0;variant<6;++variant){Directory d;d.marker();
        if(variant==0)d.file("synthetic-presenter.enable","GHVR_SYNTHETIC_PRESENTER/2\n",25);
        if(variant==1)require(!fchmodat(d.fd,"synthetic-presenter.enable",0644,0),"fixture marker permission mutation succeeds");
        if(variant==2){require(!renameat(d.fd,"synthetic-presenter.enable",d.fd,"elsewhere"),"move marker");require(!symlinkat("elsewhere",d.fd,"synthetic-presenter.enable"),"symlink marker");}
        if(variant==3)require(!linkat(d.fd,"synthetic-presenter.enable",d.fd,"second",0),"hardlinked marker");
        if(variant==4){unlinkat(d.fd,"synthetic-presenter.enable",0);require(!mkfifoat(d.fd,"synthetic-presenter.enable",0600),"FIFO marker");}
        if(variant==5)d.file("synthetic-presenter.enable",r::EnableMagic,std::strlen(r::EnableMagic)-1);
        r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==r::SourceReason::Marker,"noncanonical marker safely refused");}
    for(unsigned variant=0;variant<5;++variant){Directory d;d.marker();std::array<unsigned char,frame_native_presenter::GuardedFragmentBytes> bad{};d.file("synthetic-presenter.spv",bad.data(),bad.size());
        if(variant==1)require(!fchmodat(d.fd,"synthetic-presenter.spv",0644,0),"fixture artifact permission mutation succeeds");
        if(variant==2)require(!linkat(d.fd,"synthetic-presenter.spv",d.fd,"second",0),"hardlinked artifact");
        if(variant==3){require(!renameat(d.fd,"synthetic-presenter.spv",d.fd,"elsewhere"),"move artifact");require(!symlinkat("elsewhere",d.fd,"synthetic-presenter.spv"),"artifact symlink");}
        if(variant==4)d.file("synthetic-presenter.spv",bad.data(),4);
        r::Source s;require(!s.prepareCold(d.fd)&&s.reason()==(variant==0?r::SourceReason::Pin:r::SourceReason::Module),"noncanonical artifact refused before use");}
    auto text=status();require(text.find("\"source_reason\":\"disabled\"")!=std::string::npos&&text.find("\"diagnostic_atomics_not_source_cohort\":true")!=std::string::npos&&text.find("\"passthrough_authorized\":false")!=std::string::npos,"status keeps diagnostic and authority boundaries");
    require(status(true).size()<1024,"maximum width status fits envelope");
    {Directory d;r::Source s;r::Snapshot snap;require(!r::writeStatusCold(d.fd,"short",s,snap,1,false)&&!r::writeStatusCold(d.fd,Capture,s,snap,0,false)&&!r::writeStatusCold(d.fd,Capture,s,snap,33,false),"invalid status identity/budget refused");
        d.file("victim","keep",4);require(!symlinkat("victim",d.fd,r::StatusFile),"malicious final symlink");require(!r::writeStatusCold(d.fd,Capture,s,snap,1,false)&&d.read("victim")=="keep","status never follows final symlink");unlinkat(d.fd,r::StatusFile,0);
        d.file("synthetic-presenter-status.tmp","busy",4);require(!r::writeStatusCold(d.fd,Capture,s,snap,1,false)&&d.read("synthetic-presenter-status.tmp")=="busy","status cannot overwrite foreign preexisting temporary file");}
    {r::Counters c;c.deviceCreated(object<VkDevice>(7),VK_API_VERSION_1_1);frame_native_presenter::Result good;good.replaced=true;c.attempted(good);frame_native_presenter::Result bad;bad.reason=frame_native_presenter::Reason::Graphics;c.attempted(bad);c.historyFailed();c.deviceDestroyed();c.deviceDestroyed();const auto s=c.snapshot();require(s.pipelineAttempts==2&&s.pipelineReplacements==1&&s.activeDevices==0&&s.observedDevice==7&&s.definitionHistoryFailed&&s.primitiveReason==frame_native_presenter::Reason::Graphics,"bounded counters preserve coarse cold lifecycle");}
}

std::vector<std::uint32_t> loadWords(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"private pinned input opens");const auto bytes=f.tellg();require(bytes>0&&bytes<=65536&&bytes%4==0,"bounded private SPIR-V extent");std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),bytes);require(bool(f),"complete private SPIR-V input");return words;}
struct Native;
Native* active=nullptr;
VKAPI_ATTR void VKAPI_CALL noop(){}
VKAPI_ATTR void VKAPI_CALL aliasNoop(){}
PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice,const char*);
PFN_vkVoidFunction VKAPI_CALL gipa(VkInstance,const char*);
struct Native {
    struct Handle{void* key;};Handle instanceHandle{&instanceHandle},physicalHandle{&instanceHandle},deviceHandle{&deviceHandle};
    VkInstance instance=reinterpret_cast<VkInstance>(&instanceHandle);VkPhysicalDevice physical=reinterpret_cast<VkPhysicalDevice>(&physicalHandle);VkDevice device=reinterpret_cast<VkDevice>(&deviceHandle);
    Directory directory;Fixture fixture;std::vector<VkDescriptorSetLayoutBinding> full;
    VkAllocationCallbacks allocator{};VkPipelineCache cache=object<VkPipelineCache>(0x91);
    std::uint64_t nextOriginal=0x600,nextCandidate=0x800;
    unsigned deviceCalls=0,deviceDestroys=0,shaderCalls=0,ownedShaderCalls=0,ownedShaderDestroys=0,nativePipelineCalls=0,ownedPipelineCalls=0,originalDestroys=0,ownedDestroys=0,corePass=0,khrPass=0;
    VkResult nativeResult=VK_SUCCESS,ownedShaderResult=VK_SUCCESS,ownedPipelineResult=VK_SUCCESS;
    bool partialOwned=false,reuseOriginal=false,testNested=false,failDuringOwned=false;unsigned nestedChecks=0;
    const VkGraphicsPipelineCreateInfo* expected=nullptr;std::uint32_t expectedCount=0;
    frame_pixel_ssbo_journal::PipelineToken oldToken{};frame_pixel_ssbo_journal::Event reused{};
    std::vector<VkPipeline> originals,candidates;std::vector<VkShaderModule> ownedShaders;
    const char* missing=nullptr;
    Native(bool count1=false,std::uint32_t api=VK_API_VERSION_1_1){
        require(!active,"one owned fake dispatch driver at a time");active=this;fixture.colorBlend.blendEnable=VK_FALSE;
        if(count1){full.resize(29);for(unsigned i=0;i<29;++i)full[i]={i,i<8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<25?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_ALL,nullptr};fixture.set0.bindingCount=full.size();fixture.set0.pBindings=full.data();}
        auto& s=session();require(!s.tracker&& !s.presenterWriter.joinable(),"functional fixture has no LiveTracker or prior writer");
        if(s.config.directoryFd>=0){close(s.config.directoryFd);}s.config.directoryFd=dup(directory.fd);s.config.captureId=Capture;s.presenterWriterFailed.store(false);s.allowed=true;
        auto instanceRecord=std::make_unique<Instance>();instanceRecord->native=instance;instanceRecord->apiVersion=api;instanceRecord->gipa=gipa;
        {auto& c=contexts();std::lock_guard<std::mutex> lock(c.mutex);c.instances.emplace(dispatchKey(instance),std::move(instanceRecord));}
        VkLayerDeviceLink link{};link.pfnNextGetInstanceProcAddr=gipa;link.pfnNextGetDeviceProcAddr=gdpa;
        VkLayerDeviceCreateInfo layer{};layer.sType=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;layer.function=VK_LAYER_LINK_INFO;layer.u.pLayerInfo=&link;fixture.device.pNext=&layer;
        auto create=reinterpret_cast<PFN_vkCreateDevice>(frame_pixel_vkGetInstanceProcAddr(instance,"vkCreateDevice"));VkDevice out{};
        require(create&&create(physical,&fixture.device,&allocator,&out)==VK_SUCCESS&&out==device&&deviceCalls==1,"actual exported DeviceCreate forwards original once");fixture.device.pNext=nullptr;
        const auto* record=deviceFor(device);require(record&&record->instanceApiVersion==api&&!record->samplerPrepareAttempted&&!record->writer.joinable()&&!record->ownsCapture&&record->readback.state()==frame_pixels::State::Empty,"no diagnostic reader/sampler/recording preparation in functional mode");
    }
    ~Native(){if(deviceFor(device)){auto destroy=reinterpret_cast<PFN_vkDestroyDevice>(frame_pixel_vkGetDeviceProcAddr(device,"vkDestroyDevice"));destroy(device,&allocator);require(deviceDestroys==1&&!deviceFor(device)&&!session().presenterWriter.joinable(),"actual teardown forwards once, joins cold status writer and erases identity");}auto& c=contexts();c.instances.erase(dispatchKey(instance));session().allowed=false;active=nullptr;}
    template<class T>T proc(const char* name){auto result=frame_pixel_vkGetDeviceProcAddr(device,name);require(result!=nullptr,"actual exported cold entry resolves");return reinterpret_cast<T>(result);}
    void definitions(const std::vector<std::uint32_t>& vs,const std::vector<std::uint32_t>& ps){
        VkDescriptorSetLayout set{};require(proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&fixture.set0,&allocator,&set)==VK_SUCCESS&&set==fixture.nativeSet,"actual Set0 creation forwarded");
        VkPipelineLayout layout{};require(proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&fixture.layout,&allocator,&layout)==VK_SUCCESS&&layout==fixture.graphics.layout,"actual pipeline-layout creation forwarded");
        VkRenderPass pass{};require(proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device,&fixture.pass,&allocator,&pass)==VK_SUCCESS&&pass==fixture.graphics.renderPass,"actual renderpass creation forwarded");
        for(unsigned index=0;index<2;++index){auto shader=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);const auto& words=index?ps:vs;shader.pCode=words.data();shader.codeSize=words.size()*4;VkShaderModule output{};require(proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device,&shader,&allocator,&output)==VK_SUCCESS,"actual original shader callback forwards");fixture.stages[index?0:1].module=output;}
    }
    std::vector<VkPipeline> create(unsigned count=1){std::vector<VkGraphicsPipelineCreateInfo> cis(count,fixture.graphics);std::vector<VkPipeline> output(count);expected=cis.data();expectedCount=count;
        const auto result=proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(device,cache,count,cis.data(),&allocator,output.data());require(result==nativeResult,"native pipeline batch result retained");expected=nullptr;return output;}
};
void exact(VkDevice device,const VkAllocationCallbacks* allocator){require(active&&device==active->device&&allocator==&active->allocator,"exact downstream device/allocator unchanged");}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* allocator,VkDevice* output){require(active&&physical==active->physical&&ci==&active->fixture.device&&allocator==&active->allocator,"exact original DeviceCreate arguments");++active->deviceCalls;*output=active->device;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks* allocator){exact(device,allocator);++active->deviceDestroys;}
VKAPI_ATTR VkResult VKAPI_CALL createSet(VkDevice device,const VkDescriptorSetLayoutCreateInfo* ci,const VkAllocationCallbacks* allocator,VkDescriptorSetLayout* output){exact(device,allocator);require(ci==&active->fixture.set0,"unchanged original set CI");*output=active->fixture.nativeSet;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice device,const VkPipelineLayoutCreateInfo* ci,const VkAllocationCallbacks* allocator,VkPipelineLayout* output){exact(device,allocator);require(ci==&active->fixture.layout,"unchanged original layout CI");*output=active->fixture.graphics.layout;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice device,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* allocator,VkRenderPass* output){exact(device,allocator);require(ci==&active->fixture.pass,"unchanged original pass CI");*output=active->fixture.graphics.renderPass;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createPass2(VkDevice device,const VkRenderPassCreateInfo2*,const VkAllocationCallbacks* allocator,VkRenderPass* output){exact(device,allocator);++active->corePass;*output=object<VkRenderPass>(0x32);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createPass2KHR(VkDevice device,const VkRenderPassCreateInfo2*,const VkAllocationCallbacks* allocator,VkRenderPass* output){exact(device,allocator);++active->khrPass;*output=object<VkRenderPass>(0x33);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice device,const VkShaderModuleCreateInfo* ci,const VkAllocationCallbacks* allocator,VkShaderModule* output){exact(device,allocator);++active->shaderCalls;
    if(ci->codeSize==frame_native_presenter::GuardedFragmentBytes){++active->ownedShaderCalls;*output=object<VkShaderModule>(0x900+active->ownedShaderCalls);active->ownedShaders.push_back(*output);return active->ownedShaderResult;}
    *output=object<VkShaderModule>(active->shaderCalls==1?0x40:0x50);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice device,VkShaderModule shader,const VkAllocationCallbacks* allocator){exact(device,allocator);require(std::find(active->ownedShaders.begin(),active->ownedShaders.end(),shader)!=active->ownedShaders.end(),"only successfully owned candidate shader destroyed");++active->ownedShaderDestroys;}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice,VkPipeline,const VkAllocationCallbacks*);
VKAPI_ATTR VkResult VKAPI_CALL createPipelines(VkDevice device,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* cis,const VkAllocationCallbacks* allocator,VkPipeline* output){exact(device,allocator);require(cache==active->cache,"native cache exact");
    if(cis==active->expected){require(count==active->expectedCount,"native batch count preserved");++active->nativePipelineCalls;for(unsigned i=0;i<count;++i){output[i]=object<VkPipeline>(active->nextOriginal++);active->originals.push_back(output[i]);}return active->nativeResult;}
    ++active->ownedPipelineCalls;require(count==1&&cis&&active->expected,"one candidate from current original callback");const auto& a=active->expected[active->ownedPipelineCalls-1<active->expectedCount?active->ownedPipelineCalls-1:0];const auto& b=*cis;
    require(b.pNext==a.pNext&&b.flags==a.flags&&b.layout==a.layout&&b.renderPass==a.renderPass&&b.subpass==a.subpass&&b.pVertexInputState==a.pVertexInputState&&b.pViewportState==a.pViewportState&&b.pRasterizationState==a.pRasterizationState&&b.pMultisampleState==a.pMultisampleState&&b.pDepthStencilState==a.pDepthStencilState&&b.pColorBlendState==a.pColorBlendState&&b.pDynamicState==a.pDynamicState,"candidate preserves original complete fixed-state pointers and identities");
    for(unsigned i=0;i<2;++i)require(b.pStages[i].stage==a.pStages[i].stage&&b.pStages[i].pName==a.pStages[i].pName&&b.pStages[i].pSpecializationInfo==a.pStages[i].pSpecializationInfo&&b.pStages[i].module==(a.pStages[i].stage==VK_SHADER_STAGE_FRAGMENT_BIT?active->ownedShaders.back():a.pStages[i].module),"only exact original fragment module replaced");
    auto ready=std::make_unique<frame_pixel_ssbo_journal::Ready>();bool found=false;for(unsigned attempt=0;attempt<16&&deviceFor(device)->definitions.takeReady(*ready);++attempt){if(ready->pipeline.value==handle(active->originals[active->ownedPipelineCalls-1])){found=true;break;}}require(found,"current original is journaled before optional replacement");active->oldToken=ready->pipeline;
    if(active->testNested){auto unchanged=object<VkPipeline>(active->oldToken.value);const frame_native_presenter::Dispatch calls{createShader,destroyShader,createPipelines,destroyPipeline};
        const auto result=r::complete(device,cache,VK_API_VERSION_1_1,VK_SUCCESS,a,allocator,unchanged,ready->recipe,session().presenterSource,calls,{deviceFor(device),retirePresenterOriginal});
        require(!result.replaced&&result.reason==frame_native_presenter::Reason::Dispatch&&handle(unchanged)==active->oldToken.value,"nested optional replacement cannot borrow outer TLS retirement scope");++active->nestedChecks;}

    if(active->failDuringOwned)deviceFor(device)->definitionBoundary.invalidate();
    *output=(active->ownedPipelineResult==VK_SUCCESS||active->partialOwned)?object<VkPipeline>(active->nextCandidate++):VK_NULL_HANDLE;if(*output)active->candidates.push_back(*output);return active->ownedPipelineResult;}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* allocator){exact(device,allocator);
    if(std::find(active->originals.begin(),active->originals.end(),pipeline)!=active->originals.end()){
        ++active->originalDestroys;require(!deviceFor(device)->definitions.current(active->oldToken),"exact old generation retired BEFORE native original destroy");
        if(active->reuseOriginal)require(deviceFor(device)->definitions.createPipeline(pipeline,active->fixture.graphics,VK_SUCCESS,&active->reused),"simulated immediate same-raw-value reuse accepted as new generation");
    }else{require(std::find(active->candidates.begin(),active->candidates.end(),pipeline)!=active->candidates.end(),"only owned candidate cleanup or app-owned destruction");++active->ownedDestroys;}}
PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice,const char* name){if(active->missing&&!std::strcmp(name,active->missing))return nullptr;
#define PROC(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    PROC(DestroyDevice,destroyDevice) PROC(CreateDescriptorSetLayout,createSet) PROC(CreatePipelineLayout,createLayout) PROC(CreateRenderPass,createPass) PROC(CreateRenderPass2,createPass2) PROC(CreateRenderPass2KHR,createPass2KHR) PROC(CreateShaderModule,createShader) PROC(DestroyShaderModule,destroyShader) PROC(CreateGraphicsPipelines,createPipelines) PROC(DestroyPipeline,destroyPipeline)
#undef PROC
    return std::strstr(name,"KHR")?reinterpret_cast<PFN_vkVoidFunction>(aliasNoop):reinterpret_cast<PFN_vkVoidFunction>(noop);}
PFN_vkVoidFunction VKAPI_CALL gipa(VkInstance,const char* name){if(!std::strcmp(name,"vkCreateDevice"))return reinterpret_cast<PFN_vkVoidFunction>(createDevice);return gdpa(VK_NULL_HANDLE,name);}
void publicHooks(){Native d;
    std::vector<const char*> hot{"vkCmdDraw","vkCmdDrawIndexed","vkCmdBindPipeline","vkBeginCommandBuffer","vkEndCommandBuffer","vkResetCommandBuffer","vkQueueSubmit","vkQueueSubmit2","vkQueueSubmit2KHR","vkCmdBeginRenderPass2","vkCmdBeginRenderPass2KHR","vkCmdNextSubpass2KHR","vkDeviceWaitIdle","vkGetDeviceQueue","vkCmdSetViewport","vkCmdPipelineBarrier2KHR"};
#define BINDING_FIELD(n) hot.push_back("vk" #n);
#include "../layer/passive_binding_commands.inc"
#undef BINDING_FIELD
    for(const auto* name:hot){if(!std::strcmp(name,"vkCreateDescriptorSetLayout")||!std::strcmp(name,"vkDestroyDescriptorSetLayout")||!std::strcmp(name,"vkCreatePipelineLayout")||!std::strcmp(name,"vkDestroyPipelineLayout"))continue;const auto next=gdpa(d.device,name);require(frame_pixel_vkGetDeviceProcAddr(d.device,name)==next&&frame_pixel_vkGetInstanceProcAddr(d.instance,name)==next,"every hot device/instance query returns exact NEXT PFN without tracker");}
    d.missing="vkCmdDraw";require(!frame_pixel_vkGetDeviceProcAddr(d.device,"vkCmdDraw"),"missing native hot entry remains missing");d.missing=nullptr;
    auto ci=info<VkRenderPassCreateInfo2>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);VkRenderPass out{};
    d.proc<PFN_vkCreateRenderPass2>("vkCreateRenderPass2")(d.device,&ci,&d.allocator,&out);d.proc<PFN_vkCreateRenderPass2KHR>("vkCreateRenderPass2KHR")(d.device,&ci,&d.allocator,&out);
    require(d.corePass==1&&d.khrPass==1,"cold core/KHR dispatch preserves exact queried native route");
    d.missing="vkCreateRenderPass2KHR";require(!frame_pixel_vkGetDeviceProcAddr(d.device,"vkCreateRenderPass2KHR"),"missing cold alias never falls back to other nonnull route");d.missing=nullptr;
    session().allowed=false;require(frame_pixel_vkGetDeviceProcAddr(d.device,"vkCreateShaderModule")==gdpa(d.device,"vkCreateShaderModule"),"disabled session leaves native cold API unchanged");session().allowed=true;
}
void startupBoundary(){
    require(!presenterSessionSlot().load()&&!session().allowed&&!session().tracker&&session().config.directoryFd<0,"out-of-order Session access is inert without lazy preparation");
    require(!frame_pixel_vkGetDeviceProcAddr(VK_NULL_HANDLE,"vkCmdDraw")&&!presenterSessionSlot().load(),"early GPA cannot initiate source/config preparation");
    VkNegotiateLayerInterface negotiation{};negotiation.sType=LAYER_NEGOTIATE_INTERFACE_STRUCT;negotiation.loaderLayerInterfaceVersion=1;
    require(frame_pixel_vkNegotiateLoaderLayerInterfaceVersion(&negotiation)==VK_ERROR_INITIALIZATION_FAILED&&!presenterSessionSlot().load(),"invalid negotiation never prepares source");
    negotiation.loaderLayerInterfaceVersion=2;require(frame_pixel_vkNegotiateLoaderLayerInterfaceVersion(&negotiation)==VK_SUCCESS&&presenterSessionSlot().load(),"successful loader negotiation exclusively publishes cold session");
    auto* original=presenterSessionSlot().load();require(!original->allowed&&!original->tracker&&original->config.directoryFd<0,"non-vrcompositor test process cannot acquire production opt-in");
    require(frame_pixel_vkNegotiateLoaderLayerInterfaceVersion(&negotiation)==VK_SUCCESS&&presenterSessionSlot().load()==original,"repeated negotiation never creates a fresh opt-in epoch");
}
void deltaHooks(const char* vertexPath,const char* fragmentPath,const char* candidatePath){
    const auto vs=loadWords(vertexPath),ps=loadWords(fragmentPath),candidate=loadWords(candidatePath);
    Directory source;source.marker();source.file("synthetic-presenter.spv",candidate.data(),candidate.size()*4);require(session().presenterSource.prepareCold(source.fd),"new private delta prepares pinned source");
    {Native d(true);d.definitions(vs,ps);d.testNested=true;auto output=d.create(2);require(output==d.candidates&&d.nestedChecks==2&&d.originalDestroys==2,"nested refusal restores parent scope for all later matched views");}
    for(unsigned variant=0;variant<6;++variant){Native d;d.definitions(vs,ps);
        if(variant==0){d.fixture.attachments[0].format=VK_FORMAT_R8G8B8_UNORM;deviceFor(d.device)->definitions.destroyRenderPass(d.fixture.graphics.renderPass);require(deviceFor(d.device)->definitions.createRenderPass(d.fixture.graphics.renderPass,d.fixture.pass,VK_SUCCESS),"owned bad-format recipe observed");}
        if(variant==1)d.fixture.dynamics[0]=VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
        if(variant==2)observeDefinitions(deviceFor(d.device),[](auto&){throw 1;});
        if(variant==3)session().presenterWriterFailed.store(true);
        if(variant==4)d.fixture.colorBlend.blendEnable=VK_TRUE;
        if(variant==5){auto changed=vs;changed.back()^=1;deviceFor(d.device)->definitions.destroyShader(d.fixture.stages[1].module);auto ci=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);ci.pCode=changed.data();ci.codeSize=changed.size()*4;deviceFor(d.device)->definitions.createShader(d.fixture.stages[1].module,ci,VK_SUCCESS);}
        const auto output=d.create();require(output==d.originals&&!d.ownedPipelineCalls&&!d.originalDestroys,"finite diagnostic/history/pin refusal leaves native pipeline untouched");
        const auto diagnostic=session().presenterCounters.snapshot();
        if(variant==0)require(diagnostic.graphicsGate==frame_native_presenter::GraphicsGate::Format&&diagnostic.attachmentFormat==VK_FORMAT_R8G8B8_UNORM,"format gate preserves exact unsupported format");
        if(variant==1)require(diagnostic.graphicsGate==frame_native_presenter::GraphicsGate::Dynamic&&diagnostic.unsupportedDynamic==VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,"dynamic gate preserves exact unsupported state");
        if(variant==2)require(diagnostic.definitionHistoryFailed,"missed definition history is visible in cold status");
        if(variant==4)require(diagnostic.graphicsGate==frame_native_presenter::GraphicsGate::Blend,"blend refusal is distinguishable from format/dynamic");
    }
    {Native d;d.definitions(vs,ps);d.fixture.colorBlend.blendEnable=VK_TRUE;auto output=d.create();auto ready=std::make_unique<frame_pixel_ssbo_journal::Ready>();require(deviceFor(d.device)->definitions.takeReady(*ready),"unreplaced current original recipe remains admitted");
        const frame_native_presenter::Dispatch calls{createShader,destroyShader,createPipelines,destroyPipeline};const auto before=d.shaderCalls;
        const auto noOwner=r::complete(d.device,d.cache,VK_API_VERSION_1_1,VK_SUCCESS,d.fixture.graphics,&d.allocator,output[0],ready->recipe,session().presenterSource,calls,{});
        require(noOwner.reason==frame_native_presenter::Reason::Dispatch&&!noOwner.replaced&&d.shaderCalls==before,"caller cannot omit exact retirement owner/callback");
        const auto foreign=r::complete(object<VkDevice>(7),d.cache,VK_API_VERSION_1_1,VK_SUCCESS,d.fixture.graphics,&d.allocator,output[0],ready->recipe,session().presenterSource,calls,{deviceFor(d.device),retirePresenterOriginal});
        require(foreign.reason==frame_native_presenter::Reason::Identity&&!foreign.replaced&&d.shaderCalls==before&&!d.originalDestroys,"same numeric pipeline on a foreign device cannot borrow lifecycle authority");}
    require(!session().presenterWriter.joinable(),"all prior cold writers joined before budget test");
    session().presenterStatusUpdates=r::MaximumStatusUpdates;
    {Native d;{std::lock_guard<std::mutex> lock(session().presenterWriterMutex);if(session().presenterWriter.joinable())session().presenterWriter.join();}
        struct stat file{};require(fstatat(d.directory.fd,r::StatusFile,&file,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT,"new device lifetime cannot restart exhausted process-wide status budget");}
    require(session().presenterStatusUpdates==r::MaximumStatusUpdates,"status update budget persists across final device destruction");
    const auto receipt=status(true);require(receipt.find("\"graphics_gate\":\"multisample\"")!=std::string::npos&&receipt.find("\"attachment_format\":2147483647")!=std::string::npos&&receipt.find("\"unsupported_dynamic\":2147483647")!=std::string::npos,"actual maximal diagnostic fields stay inside 1KiB producer envelope");
}
void privateHooks(const char* vertexPath,const char* fragmentPath,const char* candidatePath){const auto vs=loadWords(vertexPath),ps=loadWords(fragmentPath),candidate=loadWords(candidatePath);
    Directory source;source.marker();source.file("synthetic-presenter.spv",candidate.data(),candidate.size()*4);require(session().presenterSource.prepareCold(source.fd),"real pinned guarded artifact cold-prepared");
    unlinkat(source.fd,"synthetic-presenter.enable",0);require(session().presenterSource.ready(),"cold artifact remains immutable after enable removal; not a live pipeline switch");
    {Native d(true);d.definitions(vs,ps);require(deviceFor(d.device)->presenterInterfaceAuthorized.load(),"original PS attests pending count1 layout before pipeline creation");d.reuseOriginal=true;const auto before=session().presenterCounters.snapshot();auto output=d.create(2);
        require(d.nativePipelineCalls==1&&d.ownedPipelineCalls==2&&d.originalDestroys==2&&d.ownedShaderCalls==2&&d.ownedShaderDestroys==2&&output==d.candidates,"all matched views replaced from same original batch immediately");
        const auto after=session().presenterCounters.snapshot();require(after.pipelineReplacements==before.pipelineReplacements+2,"replacement diagnostics count all views");
        require(deviceFor(d.device)->definitions.current({handle(d.device),d.reused.value,d.reused.generation,d.oldToken.journalEpoch}),"post-destroy recycled generation survives exact old retirement");
        auto ready=std::make_unique<frame_pixel_ssbo_journal::Ready>();require(!deviceFor(d.device)->definitions.lookupPipeline({handle(d.device),handle(output[0]),d.oldToken.generation},*ready),"replacement is never registered as stock-original shader");
        d.proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(d.device,output[0],&d.allocator);require(d.ownedDestroys==1,"returned replacement follows ordinary app-owned destruction");}
    for(unsigned variant=0;variant<7;++variant){Native d(false,variant==0?VK_API_VERSION_1_0:VK_API_VERSION_1_1);auto wrong=ps;if(variant==1)wrong.back()^=1;d.definitions(vs,wrong);
        if(variant==2)d.fixture.colorBlend.blendEnable=VK_TRUE;
        if(variant==3)d.nativeResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
        if(variant==4)d.ownedShaderResult=VK_ERROR_OUT_OF_HOST_MEMORY;
        if(variant==5){d.ownedPipelineResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;d.partialOwned=true;}
        if(variant==6)d.fixture.graphics.layout=object<VkPipelineLayout>(0xdead);
        const auto output=d.create();require(output[0]==d.originals[0]&&d.originalDestroys==0,"unmatched/unsupported/failed optional route preserves original native handle");
        if(variant==0||variant==1||variant==2||variant==3||variant==6)require(!d.ownedPipelineCalls,"early refusal never attempts candidate pipeline");
        if(variant==3)require(!d.ownedShaderCalls,"partial native batch never enters optional transformation");
        if(variant==4)require(!d.ownedShaderDestroys,"failed shader undefined output never destroyed");
        if(variant==5)require(d.ownedDestroys==1&&d.ownedShaderDestroys==1,"partial optional candidate cleaned without touching native result");}
}

} // namespace receiver_test
int main(int argc,char** argv){using namespace receiver_test;
    if(argc==2&&!std::strcmp(argv[1],"--presenter-status-json")){std::cout<<status();return 0;}
    if(argc==5&&!std::strcmp(argv[1],"--retirement-race")){
        startupBoundary();const auto vs=loadWords(argv[2]),ps=loadWords(argv[3]),candidate=loadWords(argv[4]);
        Directory source;source.marker();source.file("synthetic-presenter.spv",candidate.data(),candidate.size()*4);
        require(session().presenterSource.prepareCold(source.fd),"race control seals the actual pinned candidate");
        Native d;d.definitions(vs,ps);d.failDuringOwned=true;auto output=d.create();
        require(output==d.candidates&&d.originalDestroys==1,"in-flight observer failure cannot skip original retirement");
        require(deviceFor(d.device)->definitionBoundary.failed()&&session().presenterCounters.snapshot().definitionHistoryFailed,"failed history stays closed after mandatory retirement");
        const auto later=d.create();require(later.back()==d.originals.back()&&d.ownedPipelineCalls==1,"failed history admits no later replacement");
        std::cout<<"{\"scope\":\"cold-presenter-retirement-race\",\"checks\":"<<checks<<",\"headset_result\":false}\n";return 0;
    }
    if(argc==5&&!std::strcmp(argv[1],"--receiver-delta")){startupBoundary();deltaHooks(argv[2],argv[3],argv[4]);std::cout<<"{\"scope\":\"cold-presenter-receiver-delta\",\"checks\":"<<checks<<",\"headset_result\":false}\n";return 0;}
    require(argc==1||argc==4,"optional private VS PS guarded candidate paths");startupBoundary();publicFiles();publicHooks();if(argc==4)privateHooks(argv[1],argv[2],argv[3]);std::cout<<"{\"scope\":\"cold-presenter-receiver-public\",\"checks\":"<<checks<<",\"native_provider_verified\":false}\n";
}
