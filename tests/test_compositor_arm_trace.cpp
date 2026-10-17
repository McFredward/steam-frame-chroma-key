// Exercise the actual raw-mode producer gates and existing background writer.
// Public scalar/fake-provider fixtures are not native import or GPU evidence.
#define FRAME_MASK_COMPOSITOR_RAW 1
#define FRAME_MASK_PRIVATE_VULKAN_EXPORTS 1
#include "../layer/passive_pipeline_trace.cpp"
#include <fstream>
#include <sys/wait.h>

namespace {
unsigned checks=0;
void require_at(bool ok,unsigned line) {++checks;if(!ok){std::fprintf(stderr,"arm trace check %u failed at line %u\n",checks,line);std::abort();}}
#define require(ok) require_at(bool(ok),__LINE__)
std::string read_file(const std::string& path) {
    std::ifstream input(path);return std::string(std::istreambuf_iterator<char>(input),{});
}
template<class T> T handle(std::uint64_t value) { return (T)(std::uintptr_t)value; }
struct Results {
    frame_compositor_journal::Receipt receipt;
    std::atomic<unsigned> received{0};
    static void receive(const frame_compositor_journal::Receipt& receipt,void* context) noexcept {
        auto& result=*static_cast<Results*>(context);
        result.receipt=receipt;result.received.store(1,std::memory_order_release);
    }
};
unsigned drawCalls=0,executeCalls=0,submitCalls=0,submit2Calls=0;
VkResult nextResult=VK_SUCCESS;
VKAPI_ATTR void VKAPI_CALL nativeDraw(VkCommandBuffer,std::uint32_t count,std::uint32_t instances,
                                     std::uint32_t first,std::uint32_t instance) {
    require(count==5 && instances==6 && first==7 && instance==8);++drawCalls;
}
VKAPI_ATTR void VKAPI_CALL nativeExecute(VkCommandBuffer,std::uint32_t count,const VkCommandBuffer* children) {
    require(count==1 && children && children[0]);++executeCalls;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeSubmit(VkQueue,std::uint32_t count,const VkSubmitInfo* info,VkFence fence) {
    require(count==1 && info && info->commandBufferCount==1 && info->pCommandBuffers && fence==handle<VkFence>(91));
    ++submitCalls;return nextResult;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeSubmit2(VkQueue,std::uint32_t count,const VkSubmitInfo2* info,VkFence fence) {
    require(count==1 && info && info->commandBufferInfoCount==1 && info->pCommandBufferInfos && fence==handle<VkFence>(92));
    ++submit2Calls;return nextResult;
}
void wait_replayed(Capture& c) {
    for(unsigned i=0;i<2000;++i) {
        const auto counters=c.bindings.transportCounters();
        if(counters.published==counters.replayed && c.bindings.empty())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(false);
}
void wait_optional_output_drained(EventRing& events,Core& core,
                                  std::atomic<bool>* waiting=nullptr) {
    // Test-side pacing only. A fixed sleep cannot guarantee that a scheduled
    // background writer has consumed a finite batch. Render hooks never wait.
    for(unsigned i=0;i<2000;++i) {
        if(events.empty() || !core.enabled())return;
        if(waiting)waiting->store(true,std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::fprintf(stderr,"optional output did not drain: emitted=%llu dropped=%llu enabled=%u\n",
        static_cast<unsigned long long>(events.emitted()),
        static_cast<unsigned long long>(events.dropped()),unsigned(core.enabled()));
    require(false);
}
void delayed_optional_consumer_pacing() {
    const std::uint32_t code=0x07230203;
    {
        // Reproduce the old failure mechanism without scheduler or wall-clock
        // assumptions: all 2200 calls run while the consumer is paused. Waiting
        // after that finite burst cannot turn rejected events into accepted ones.
        EventRing events;Core core(events,true);
        for(unsigned i=0;i<2200;++i)core.shaderCreated(1,10,&code,sizeof(code));
        require(events.emitted()==EventCapacity && events.dropped()==2200-EventCapacity);
        Event event;while(events.pop(event)){}
        require(events.empty() && !events.finished() && core.enabled());
    }
    EventRing events;Core core(events,true);
    std::atomic<bool> releaseConsumer{false},waiting{false},producerDone{false};
    unsigned consumed=0;
    std::thread consumer([&]{
        while(!releaseConsumer.load(std::memory_order_acquire))std::this_thread::yield();
        Event event;
        while(core.enabled() || !events.empty()) {
            if(events.pop(event))++consumed;
            else std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    std::thread producer([&]{
        for(unsigned i=0;i<2200 && core.enabled();++i) {
            core.shaderCreated(1,10,&code,sizeof(code));
            if(i%16==15)wait_optional_output_drained(events,core,&waiting);
        }
        producerDone.store(true,std::memory_order_release);
    });
    for(unsigned i=0;i<1000 && !waiting.load(std::memory_order_acquire);++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(waiting.load(std::memory_order_acquire));
    // The consumer is still explicitly paused, so this proves the producer
    // waits for progress instead of relying on how long a sleep took.
    require(!producerDone.load(std::memory_order_acquire) && events.emitted()==16 && !events.dropped());
    releaseConsumer.store(true,std::memory_order_release);
    producer.join();consumer.join();
    require(producerDone.load(std::memory_order_acquire) && events.finished() && !core.enabled());
    require(consumed==EventLimit && events.empty() && !events.dropped());
}
void footer_capacity(Capture& c) {
    // Formatting bounds only: deliberately impossible maximum counters are
    // not native observations or admission evidence.
    c.probesTimeDeferred=c.probesGeneral=c.probesDistortion=UINT64_MAX;
    c.probeClassificationsAdmitted=c.probeClassificationsOmitted=UINT64_MAX;c.encounteredProbeReasons=UINT64_MAX;
    for(auto& reason:c.probeReasons)reason={UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
    c.probeDenialCount=c.probeDenials.size();
    for(unsigned i=0;i<c.probeDenials.size();++i) {
        auto& sample=c.probeDenials[i];auto& d=sample.diagnostics;
        sample.order=sample.period=sample.replayNs=sample.pipeline=sample.pipelineGeneration=UINT64_MAX;
        sample.samplingClass=UINT32_MAX;d.reason=static_cast<frame_bindings::TargetProbeReason>(i);
        d.reasonMask=d.requestedCommand=d.requestedLayout=d.observedTargetTag=d.observedTargetCookie=d.observedTargetDevice=UINT64_MAX;
        d.binding=d.examinedImageBindings=d.currentTargetErrors=d.observations=d.descriptorStateFlags=UINT32_MAX;
        for(auto* id:{&d.command,&d.layout,&d.commandLayout,&d.boundSet,&d.set,&d.setLayout,&d.setDeclaredLayout,&d.pool,
                     &d.currentTargetImage,&d.currentTargetView,&d.currentTargetMemory,&d.descriptorView,
                     &d.candidateView,&d.candidateImage,&d.candidateMemory,&d.descriptorConversion})*id={UINT64_MAX,UINT64_MAX};
        for(auto* chain:{&d.descriptorViewChain,&d.descriptorImageChain}) {
            chain->count=UINT32_MAX;chain->errorFlags=UINT32_MAX;
            // Keep the ten-digit formatter bound without an out-of-range C++
            // enum load, which UBSan correctly rejects even in synthetic data.
            for(auto& type:chain->types)type=VK_STRUCTURE_TYPE_MAX_ENUM;
        }
    }
    char buffer[FooterReserve];std::size_t used=0;
    require(c.serializeProbeDiagnostics(buffer,sizeof(buffer),used));
    require(used+8192<FooterReserve); // Reserve unchanged common footer fields too.
    std::printf("bounded target diagnostic maximum formatter: %zu bytes +8192 common reserve\n",used);
    char shortBuffer[64];used=0;
    require(!c.serializeProbeDiagnostics(shortBuffer,sizeof(shortBuffer),used) && used<sizeof(shortBuffer));
}
}
int main(int argc,char** argv) {
    const unsigned char scope[64]={'F','C','M','S','M','P','0','4',4,0,0,0,64,0,0,0,32,0,0,0,16,0,0,0,16,0,0,0,32,0,0,0,0,16,0,0,127,0,0,0,0,135,147,3,2,0,0,0,16,0,0,0,2,0,0,0,32,0,0,0,16,0,0,0};
    require(sizeof(frame_compositor_trace_sampling_scope)==sizeof(scope) && !std::memcmp(scope,frame_compositor_trace_sampling_scope,sizeof(scope)));
    const std::string mode=argc==1?"armed":argv[1];
    const bool armed_run=mode!="unarmed",deadline_run=mode=="deadline";
    const bool byte_cap=mode=="byte-cap",event_cap=mode=="event-cap",failed_writer=mode=="failed-writer";
    const bool public_run=mode=="public-draw";
    if(event_cap)delayed_optional_consumer_pacing();
    char pattern[]="/tmp/frame-raw-arm-trace-XXXXXX";const auto directory=mkdtemp(pattern);require(directory);
    const std::string root(directory),journal=root+"/journal.jsonl",marker=root+"/capture-arm.marker";
    auto& c=capture();c.bindingsEnabled=true;c.core.enable(true);c.importProcessAdmitted=true;c.importJournalOpen=true;
    Results results;
    require(c.importReceipts.initialize_before_threads(true));
    require(c.importReceipts.register_receipts_before_threads({Results::receive,&results}));
    const auto started=std::chrono::steady_clock::now();
    std::thread writer([&]{c.write(failed_writer ? root+"/missing/journal.jsonl" : journal,marker);});
    for(unsigned i=0;i<1000 && !c.importWriterReady;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(failed_writer) {
        writer.join();require(c.importWriterReady==2 && !c.lifecycleReady() && !c.core.enabled());
        observeBindings([&](auto&){require(false);});
        require(!frame_compositor_journal::request(1,handle<VkImage>(20),handle<VkImageView>(22),1,2));
        require(c.bindings.transportCounters().published==0 && !c.importRequestPublished);
        require(!rmdir(root.c_str()));
        std::printf("PASS: %u production raw-mode failed-writer admission controls; no native admission or GPU proof\n",checks);return 0;
    }
    require(c.importWriterReady==1 && !c.bindingsArmed && !c.drawsReady());
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={4,4,1};
    image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    observeBindings([&](auto& bindings){bindings.createImage(1,handle<VkImage>(2),image);});
    // The entire ring/snapshot capacity stays reserved before the signal.
    for(unsigned i=0;i<2400;++i)recordedDraw(1,handle<VkCommandBuffer>(3),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    require(!c.bindingsArmed && c.bindingSnapshots==0 && c.events.emitted()==0 && !c.events.dropped());
    require(c.rawSampling.draws.sampled()==0 && c.snapshotsDeferredBeforeImport==0);
    if(armed_run) {
        const int fd=open(marker.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);require(fd>=0);
        require(write(fd,frame_compositor_arm::Contents,sizeof(frame_compositor_arm::Contents)-1)==sizeof(frame_compositor_arm::Contents)-1);
        require(!close(fd));
        for(unsigned i=0;i<1000 && !c.bindingsArmed;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(c.bindingsArmed && c.drawsReady() && c.events.emitted()==1);
        recordedDraw(1,handle<VkCommandBuffer>(3),1);
        require(c.bindingSnapshots==0 && c.events.emitted()==2);
        // Actual forwarding hooks must retain identical native inputs/results
        // after thousands of repeats and command reset/begin cycles.
        int dispatchKeyStorage=0;
        struct Dispatch {void* key;};
        Dispatch primary{&dispatchKeyStorage},secondary{&dispatchKeyStorage},queueObject{&dispatchKeyStorage};
        const auto command=reinterpret_cast<VkCommandBuffer>(&primary),child=reinterpret_cast<VkCommandBuffer>(&secondary);
        const auto queue=reinterpret_cast<VkQueue>(&queueObject);
        auto device=std::make_unique<Device>();device->id=1;device->CmdDraw=nativeDraw;
        device->CmdExecuteCommands=nativeExecute;device->QueueSubmit=nativeSubmit;device->QueueSubmit2=nativeSubmit2;
        devices[&dispatchKeyStorage]=std::move(device);
        const std::uint32_t code=0x07230203;c.core.shaderCreated(1,10,&code,sizeof(code));
        Stage stage{};stage.module=10;stage.stage=VK_SHADER_STAGE_FRAGMENT_BIT;stage.complete=true;copyEntry(stage.entry,"main");
        c.core.pipelineCreated(1,12,11,&stage,1,true,true);
        c.core.commandAllocated(1,number(command),13);c.core.commandAllocated(1,number(child),13);
        c.core.commandBegin(1,number(child));c.core.bind(1,number(child),12);c.core.commandEnd(1,number(child));
        VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&command;
        VkCommandBufferSubmitInfo commandInfo{};commandInfo.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;commandInfo.commandBuffer=command;
        VkSubmitInfo2 info2{};info2.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2;info2.commandBufferInfoCount=1;info2.pCommandBufferInfos=&commandInfo;
        observeBindings([&](auto& b){b.allocateCommands(1,handle<VkCommandPool>(13),1,&command);b.beginCommand(1,command);});
        wait_replayed(c);
        constexpr unsigned repeats=6000;
        for(unsigned i=0;i<repeats;++i) {
            c.core.commandReset(1,number(command),false);c.core.commandBegin(1,number(command));c.core.bind(1,number(command),12);
            traceCmdDraw(command,5,6,7,8);traceCmdExecuteCommands(command,1,&child);c.core.commandEnd(1,number(command));
            require(traceQueueSubmit(queue,1,&info,handle<VkFence>(91))==VK_SUCCESS);
            require(traceQueueSubmit2(queue,1,&info2,handle<VkFence>(92))==VK_SUCCESS);
            if(i%128==127)wait_replayed(c);
        }
        require(drawCalls==repeats && executeCalls==repeats && submitCalls==repeats && submit2Calls==repeats);
        nextResult=VK_ERROR_DEVICE_LOST;
        require(traceQueueSubmit(queue,1,&info,handle<VkFence>(91))==nextResult);
        require(traceQueueSubmit2(queue,1,&info2,handle<VkFence>(92))==nextResult);nextResult=VK_SUCCESS;
        require(c.rawSampling.draws.sampled()==32 && c.rawSampling.draws.omitted()==repeats+1-32);
        require(c.rawSampling.executes.sampled()==16 && c.rawSampling.executes.omitted()==repeats-16);
        require(c.rawSampling.submits.sampled()==16 && c.rawSampling.submits.omitted()==2*repeats+2-16);
        require(c.bindingSnapshots==0 && c.snapshotsDeferredBeforeImport==repeats+1);
        require(c.core.enabled() && c.lifecycleReady() && !c.events.finished() && !c.events.dropped());
        if(byte_cap || event_cap) {
            // Exercise residual optional-output caps using actual Core producers.
            // These are deliberate trace limits, never permission to close the
            // lifecycle owner or to hide its independent metadata loss counters.
            std::array<Stage,StageCapacity> stages;
            for(auto& value:stages){value=stage;copyEntry(value.entry,"entry_0123456789012345678901234567890123456789");}
            for(unsigned i=0;i<2200 && c.core.enabled();++i) {
                c.core.shaderCreated(1,10,&code,sizeof(code));
                if(byte_cap) {
                    c.core.pipelineDestroyed(1,12);
                    c.core.pipelineCreated(1,12,11,stages.data(),stages.size(),true,true);
                }
                // These batches emit at most 48 records into the 256-slot ring.
                // A real consumer acknowledgement prevents fixture-induced loss
                // even when another parallel CI test deschedules the writer.
                if(i%16==15)wait_optional_output_drained(c.events,c.core);
            }
            for(unsigned i=0;i<2000 && c.core.enabled();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(c.core.enabled() || !c.lifecycleReady())
                std::fprintf(stderr,"optional cap state: emitted=%llu dropped=%llu enabled=%u lifecycle=%u writer=%u\n",
                    static_cast<unsigned long long>(c.events.emitted()),
                    static_cast<unsigned long long>(c.events.dropped()),unsigned(c.core.enabled()),
                    unsigned(c.lifecycleReady()),c.importWriterReady.load());
            require(!c.core.enabled() && c.lifecycleReady());
            require(c.events.finished()==event_cap && !c.events.dropped());
        }
        // The production request bridge opens snapshots only after this real
        // owning journal's tagged image -> memory -> bind -> view publication.
        const std::uint64_t tag=UINT64_C(0x880000001),cookie=UINT64_C(0x990000002);
        VkExternalMemoryImageCreateInfo external{};external.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;image.pNext=&external;
        VkImportMemoryFdInfoKHR imported{};imported.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        imported.fd=19;imported.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkMemoryDedicatedAllocateInfo dedicated{};dedicated.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image=handle<VkImage>(20);imported.pNext=&dedicated;
        VkMemoryAllocateInfo memory{};memory.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory.allocationSize=4096;memory.memoryTypeIndex=2;memory.pNext=&imported;
        VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image=handle<VkImage>(20);view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        observeBindings([&](auto& b){require(b.createImageTagged(1,handle<VkImage>(20),image,tag));});
        observeBindings([&](auto& b){b.allocateMemory(1,handle<VkDeviceMemory>(21),memory);b.bindImageMemory(1,handle<VkImage>(20),handle<VkDeviceMemory>(21),0);});
        observeBindings([&](auto& b){require(b.createViewTagged(1,handle<VkImageView>(22),view,tag));});
        require(frame_compositor_journal::request(1,handle<VkImage>(20),handle<VkImageView>(22),tag,cookie));
        require(c.importRequestPublished);
        for(unsigned i=0;i<2000 && !results.received.load(std::memory_order_acquire);++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(results.received.load(std::memory_order_acquire)==1);
        require(results.receipt.current_after_drain && results.receipt.resource.resourceComplete && c.importReceipts.validate(results.receipt));
        require(results.receipt.resource.invocationTag==tag && results.receipt.resource.cookie==cookie);
        require(!results.receipt.resource.sourceFrameAssociated && !results.receipt.resource.gpuConsumedCoherent);
        require(c.importReceipts.counters().delivered==1 && !c.importReceipts.notification_loss());
        if(!byte_cap && !event_cap) {
            const auto setLayout=handle<VkDescriptorSetLayout>(40);const auto set=handle<VkDescriptorSet>(41);
            const auto pool=handle<VkDescriptorPool>(42);
            VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
            VkDescriptorSetLayoutCreateInfo sli{};sli.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            sli.bindingCount=1;sli.pBindings=&binding;
            VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount=1;layout.pSetLayouts=&setLayout;
            VkDescriptorPoolCreateInfo pci{};pci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pci.maxSets=1;
            VkDescriptorSetAllocateInfo sai{};sai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            sai.descriptorPool=pool;sai.descriptorSetCount=1;sai.pSetLayouts=&setLayout;
            VkDescriptorImageInfo di{VK_NULL_HANDLE,handle<VkImageView>(22),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;
            write.dstBinding=9;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&di;
            observeBindings([&](auto& b){
                b.createSetLayout(1,setLayout,sli);b.createPipelineLayout(1,handle<VkPipelineLayout>(11),layout);
                b.createPool(1,pool,pci);b.allocateSets(1,sai,&set);b.updateSets(1,1,&write,0,nullptr);
                b.beginCommand(1,command);
                b.bindSets(1,command,VK_PIPELINE_BIND_POINT_GRAPHICS,handle<VkPipelineLayout>(11),0,1,&set,0,nullptr);
            });
            const auto uiImage=handle<VkImage>(50);const auto uiView=handle<VkImageView>(51);const auto uiMemory=handle<VkDeviceMemory>(52);
            auto uiImageInfo=image;uiImageInfo.pNext=nullptr;auto uiMemoryInfo=memory;uiMemoryInfo.pNext=nullptr;
            auto uiViewInfo=view;uiViewInfo.image=uiImage;
            observeBindings([&](auto& b){b.createImage(1,uiImage,uiImageInfo);b.allocateMemory(1,uiMemory,uiMemoryInfo);
                b.bindImageMemory(1,uiImage,uiMemory,0);b.createView(1,uiView,uiViewInfo);});
            di.imageView=uiView;observeBindings([&](auto& b){b.updateSets(1,1,&write,0,nullptr);});
            c.core.commandBegin(1,number(command));c.core.bind(1,number(command),12);
            for(unsigned i=0;i<80;++i) {
                // Advance the actual permit issuer with controlled public
                // owner-clock values; no test-only runtime getter or delay.
                c.rawSampling.general.issue((100+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                c.rawSampling.classifications.issue((100+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                recordedDraw(1,command,1);
            }
            wait_replayed(c);
            require(c.bindingSnapshots==0 && c.probesFiltered==80 && !c.probesUnknown);
            if(public_run) {
                // Reproduce the hardware blind spot: the selected import is
                // retired while a different, live public descriptor is drawn.
                // Such observations must remain explicitly unassociated.
                observeBindings([&](auto& b){b.destroyView(1,handle<VkImageView>(22));
                    b.destroyImage(1,handle<VkImage>(20));b.freeMemory(1,handle<VkDeviceMemory>(21));});
                c.rawSampling.general.issue(300*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                c.rawSampling.classifications.issue(300*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                recordedDraw(1,command,1);wait_replayed(c);
                require(c.bindingSnapshots==0 && c.publicDrawSnapshots==0 && c.probesUnknown==1);
                require(c.publicDrawSnapshotsQuotaOmitted==0); // The public quota is still available.
                copyEntry(stage.entry,"PS_DISTORT");c.core.pipelineDestroyed(1,12);
                c.core.pipelineCreated(1,12,11,&stage,1,true,true);c.core.bind(1,number(command),12);
                for(unsigned i=0;i<20;++i) {
                    c.rawSampling.distortion.issue((400+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                    c.rawSampling.classifications.issue((400+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                    recordedDraw(1,command,1);
                }
                wait_replayed(c);
                require(c.probesUnknown==21 && c.probesMatched==0 && c.bindingSnapshots==16);
                require(c.publicDrawSnapshots==16 && c.exactTargetSnapshots==0 && c.publicDrawSnapshotsQuotaOmitted==4);
                for(unsigned i=0;i<16;++i) {
                    require(c.bindingReady[i] && c.bindingResults[i].sourceComplete && !c.bindingResults[i].gpuConsumedCoherent);
                    require(c.bindingResults[i].bindings[5].view.identity.handle==number(uiView));
                }
                // Other pipeline names cannot spend the public diagnostic
                // allowance. Their strict retired-target refusal is preserved.
                copyEntry(stage.entry,"PS_MAIN");c.core.pipelineDestroyed(1,12);
                c.core.pipelineCreated(1,12,11,&stage,1,true,true);c.core.bind(1,number(command),12);
                c.rawSampling.general.issue(500*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                c.rawSampling.classifications.issue(500*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                recordedDraw(1,command,1);wait_replayed(c);
                require(c.bindingSnapshots==16 && c.publicDrawSnapshots==16 && c.probesUnknown==22);
            } else {
            di.imageView=handle<VkImageView>(22);observeBindings([&](auto& b){b.updateSets(1,1,&write,0,nullptr);});
            for(unsigned i=0;i<80;++i) {
                c.rawSampling.general.issue((200+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                c.rawSampling.classifications.issue((200+i)*frame_compositor_sampling::ProbeSchedule::PeriodNs);
                recordedDraw(1,command,1);
            }
            wait_replayed(c);
            require(c.bindingSnapshots==32 && c.rawSampling.draws.sampled()==32 && c.rawSampling.probes.sampled()==160);
            require(c.probesMatched==32 && c.probesQuotaOmitted==48 && c.probesFiltered==80 && c.probesUnknown==0);
            // Native distortion entry selection spends its separate permit;
            // it does not grant an association or consume the generic permit.
            copyEntry(stage.entry,"PS_DISTORT");c.core.pipelineDestroyed(1,12);
            c.core.pipelineCreated(1,12,11,&stage,1,true,true);c.core.bind(1,number(command),12);
            c.rawSampling.general.issue(301*frame_compositor_sampling::ProbeSchedule::PeriodNs);
            c.rawSampling.distortion.issue(300*frame_compositor_sampling::ProbeSchedule::PeriodNs);
            c.rawSampling.classifications.issue(300*frame_compositor_sampling::ProbeSchedule::PeriodNs);
            recordedDraw(1,command,1);wait_replayed(c);
            require(c.probesDistortion==1 && c.rawSampling.general.available() && !c.rawSampling.distortion.available());
            require(c.probesQuotaOmitted==49 && c.rawSampling.probes.sampled()==161 && c.bindingSnapshots==32);
            require(c.probeReasons[static_cast<unsigned>(frame_bindings::TargetProbeReason::SnapshotQuota)].count==49);
            }
            require(c.lifecycleReady() && !c.events.finished() && !c.events.dropped());
            // A forked child cannot reopen/reset budgets or touch the copied
            // owning writer. Parent admission and counters stay unchanged.
            preparedImportCapture.store(&c,std::memory_order_release);
            const auto childPID=fork();require(childPID>=0);
            if(!childPID) {
                frame_compositor_journal::retire_child_atomically();
                const auto before=c.rawSampling.draws.omitted();
                recordedDraw(1,command,1);observeBindings([&](auto&){_exit(2);});
                _exit(!c.lifecycleReady() && c.rawSampling.draws.omitted()==before ? 0 : 3);
            }
            int status=0;require(waitpid(childPID,&status,0)==childPID && WIFEXITED(status) && WEXITSTATUS(status)==0);
            require(c.lifecycleReady() && c.core.enabled());
        }
        devices.clear();
    }
    if(!deadline_run)c.stop=true;
    writer.join();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    if(deadline_run)require(elapsed>=119000 && elapsed<125000);
    const auto text=read_file(journal);
    require(text.size()<1048576 && text.find("\"binding_arm_required\":true")!=std::string::npos);
    require(text.find("\"binding_ready_at_header\":false")!=std::string::npos);
    require(text.find("\"raw_trace_sampling\":{\"intentional\":true")!=std::string::npos);
    require(text.find("\"target_descriptor_set\":0,\"target_bindings\":[0,2,3,4,6,9,25,31]")!=std::string::npos);
    require(text.find("\"deadline_seconds\":120")!=std::string::npos);
    require(text.find("\"target_diagnostics_version\":2")!=std::string::npos);
    require(text.find("\"public_draw_snapshot_limit\":16")!=std::string::npos);
    require(text.find("\"target_snapshots_require_target_match\":true")!=std::string::npos);
    require(text.find("\"probe_period_ns\":60000000")!=std::string::npos);
    require(text.find("\"target_probe_diagnostics\":{")!=std::string::npos);
    if(public_run) {
        require(text.find("\"snapshot_provenance\":\"unassociated_public_draw\",\"target_matched\":false")!=std::string::npos);
        require(text.find("\"snapshots\":16,\"quota_omitted\":4,\"exact_target_snapshots\":0")!=std::string::npos);
        require(text.find("\"operation\":\"destroy_image_call\"")!=std::string::npos);
        require(text.find("\"operation\":\"destroy_view_call\"")!=std::string::npos);
        require(text.find("\"operation\":\"free_memory_call\"")!=std::string::npos);
        require(text.find("\"descriptor_view_chain\":{\"observed\":false,\"complete\":false")!=std::string::npos);
    }
    require((text.find("\"byte_cap_reached\":true")!=std::string::npos)==byte_cap);
    require((text.find("\"event\":\"bindings_armed\"")!=std::string::npos)==armed_run);
    require((text.find("\"event\":\"draw_recorded\"")!=std::string::npos)==armed_run);
    require(!c.lifecycleReady() && c.events.closed() && c.bindings.finished());
    const auto published=c.bindings.transportCounters().published;
    observeBindings([&](auto& b){b.destroyImage(1,handle<VkImage>(20));});
    require(c.bindings.transportCounters().published==published);
    if(armed_run)require(text.find("\"import_request_published\":true")!=std::string::npos);
    else require(text.find("\"published\":1,\"replayed\":1")!=std::string::npos);
    footer_capacity(c);
    if(argc>2) {std::ofstream output(argv[2]);output<<text;require(output.good());}
    require(!unlink(journal.c_str()));if(armed_run)require(!unlink(marker.c_str()));require(!rmdir(root.c_str()));
    std::printf("PASS: %u production raw-mode writer/provider controls (%s, %lld ms); no native admission or GPU proof\n",checks,mode.c_str(),static_cast<long long>(elapsed));
}
