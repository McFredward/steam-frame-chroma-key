#include "pixel_ssbo_receiver_bridge.h"
#include "pixel_capture_tracker.h"

namespace frame_pixel_ssbo_receiver {
namespace clone=frame_pixel_ssbo_clone;
namespace {template<class T> T native(std::uint64_t value) noexcept {
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}}
const char* refusalName(Refusal r) noexcept {
    switch(r){
#define NAME(a,b) case Refusal::a:return b;
    NAME(None,"none") NAME(Device,"device") NAME(Queue,"queue") NAME(Shader,"shader")
    NAME(Clone,"clone") NAME(Configure,"configure") NAME(ColdCancelled,"cold_cancelled")
    NAME(Recording,"recording") NAME(Target,"target") NAME(CopyCohort,"copy_cohort")
    NAME(SubmitShape,"submit_shape") NAME(Execution,"execution") NAME(DescriptorMutation,"descriptor_mutation")
    NAME(UnknownRoute,"unknown_route") NAME(Release,"release") NAME(Record,"invalid_original_invocation_record")
#undef NAME
    }return "unknown_bridge_refusal";
}
void Bridge::refuse(Refusal r) noexcept {unsigned empty=0;reason_.compare_exchange_strong(empty,unsigned(r),std::memory_order_acq_rel);}
bool Bridge::usable() const noexcept {return !reason_.load(std::memory_order_acquire)&&ready_.load(std::memory_order_acquire);}
bool Bridge::matches(VkCommandBuffer c) const noexcept {return c&&!retired_.load(std::memory_order_acquire)&&adopted_.load(std::memory_order_acquire)&&adoptedCommand_.load(std::memory_order_relaxed)==clone::handle(c);}
void Bridge::deviceCreated(VkDevice d,const VkDeviceCreateInfo& ci,PFN_vkGetDeviceProcAddr g,
        const VkPhysicalDeviceProperties& p,const VkPhysicalDeviceMemoryProperties& m) noexcept {
    device_=d;gdpa_=g;properties_=p;memory_=m;
    queueCreateInfos_.store(ci.queueCreateInfoCount);
    if(ci.queueCreateInfoCount==1&&ci.pQueueCreateInfos){queueCount_.store(ci.pQueueCreateInfos[0].queueCount);queueFamily_.store(ci.pQueueCreateInfos[0].queueFamilyIndex);}
    if(!transaction_.deviceCreated(d,ci,VK_SUCCESS))refuse(Refusal::Device);
}
void Bridge::queueRetrieved(std::uint32_t family,std::uint32_t index,VkQueue q,bool ordinary) noexcept {
    retrievedFamily_.store(family);retrievedIndex_.store(index);retrievedQueue_.store(clone::handle(q));
    if(!ordinary||!transaction_.queueRetrieved(device_,family,index,q)){refuse(Refusal::Queue);return;}queueKnown_.store(true,std::memory_order_release);
}
void Bridge::shaderCreated(const VkShaderModuleCreateInfo& ci,frame_pixel_ssbo_journal::Journal& journal) noexcept {
    if(ci.codeSize!=6620)return;
    try{std::lock_guard<std::mutex> lock(cold_);if(profile_.load(std::memory_order_acquire))return;
        clone::ShaderDefinition original;clone::Refusal why;
        if(!clone::captureShader(ci,clone::ShaderRole::Fragment,original,why))return; // An unrelated same-sized shader is not the target.
        if(!frame_pixel_ssbo_profile::transform(original,frame_pixel_ssbo_profile::Election::Sparse32,artifact_,why)||!journal.authorizeProbeInterface(artifact_.profile)){refuse(Refusal::Shader);return;}
        profile_.store(true,std::memory_order_release);
    }catch(...){refuse(Refusal::Shader);}
}
void Bridge::prepare(const frame_pixel_ssbo_journal::Ready& input) noexcept {
    try{std::lock_guard<std::mutex> lock(cold_);if(!pendingReady_&&!ready_.load()){pending_=input;pendingReady_=true;}}catch(...){refuse(Refusal::Clone);}
}
void Bridge::tick(frame_pixel_ssbo_journal::Journal& journal) noexcept {
    try{std::lock_guard<std::mutex> lock(cold_);
        if(!pendingReady_)return;
        const auto& input=pending_;
        if(ready_.load(std::memory_order_acquire)||reason_.load(std::memory_order_acquire)||!profile_.load(std::memory_order_acquire))return;
        if(!queueKnown_.load(std::memory_order_acquire))return; // Queue lookup may legally occur after pipeline creation.
        if(!input.recipe.set0.probeInterface.sameProfile(artifact_.profile)){refuse(Refusal::Shader);return;}
        const auto epoch=cancellationEpoch_.load(std::memory_order_acquire);
        if(!journal.current(input.pipeline)){refuse(Refusal::ColdCancelled);return;}
        originalPipeline_.store(input.recipe.provenance[5].value);originalLayout_.store(input.recipe.provenance[1].value);originalPass_.store(input.recipe.provenance[2].value);
        preparing_.store(true,std::memory_order_release);
        frame_pixel_ssbo_owned::Preparation p;p.device=device_;p.downstream=gdpa_;p.limits=properties_.limits;p.memory=memory_;
        p.recipe=&input.recipe;p.fragment=artifact_.words.data();p.fragmentBytes=artifact_.bytes;
        frame_receiver::Sha256 digest;digest.update(p.fragment,p.fragmentBytes);p.fragmentSha256=digest.finish();
        const auto result=clone_.prepare(p);cloneStatus_.store(unsigned(result.status));cloneStage_.store(unsigned(result.stage));cloneDriver_.store(result.driver);
        if(result.status!=frame_pixel_ssbo_owned::Status::Ok){preparing_.store(false,std::memory_order_release);refuse(Refusal::Clone);return;}
        tx::Configuration config;
#define LOAD(field,name) config.dispatch.field=reinterpret_cast<PFN_vk##name>(gdpa_(device_,"vk" #name));
        LOAD(barrier,CmdPipelineBarrier) LOAD(fill,CmdFillBuffer) LOAD(pipeline,CmdBindPipeline) LOAD(sets,CmdBindDescriptorSets)
        LOAD(draw,CmdDraw) LOAD(drawIndexed,CmdDrawIndexed) LOAD(createFence,CreateFence) LOAD(destroyFence,DestroyFence)
        LOAD(resetFence,ResetFences) LOAD(fenceStatus,GetFenceStatus) LOAD(submit,QueueSubmit) LOAD(invalidate,InvalidateMappedMemoryRanges)
#undef LOAD
        if(epoch!=cancellationEpoch_.load(std::memory_order_acquire)||reason_.load(std::memory_order_acquire)){clone_.release();preparing_.store(false,std::memory_order_release);refuse(Refusal::ColdCancelled);return;}
        if(!transaction_.configure(clone_,config)){clone_.release();preparing_.store(false,std::memory_order_release);refuse(Refusal::Configure);return;}
        if(epoch!=cancellationEpoch_.load(std::memory_order_acquire)||reason_.load(std::memory_order_acquire)){transaction_.release();preparing_.store(false,std::memory_order_release);refuse(Refusal::ColdCancelled);return;}
        ready_.store(true,std::memory_order_release);preparing_.store(false,std::memory_order_release);
    }catch(...){preparing_.store(false,std::memory_order_release);refuse(Refusal::Clone);}
}
void Bridge::definitionRetiring(frame_pixel_ssbo_journal::ObjectKind kind,std::uint64_t value) noexcept {
    using K=frame_pixel_ssbo_journal::ObjectKind;
    const bool relevant=(kind==K::Pipeline&&value==originalPipeline_.load())||
        (kind==K::Layout&&value==originalLayout_.load())||(kind==K::RenderPass&&value==originalPass_.load());
    if(kind==K::Pipeline||kind==K::Layout||kind==K::RenderPass)cancellationEpoch_.fetch_add(1,std::memory_order_acq_rel);
    if(!retired_.load(std::memory_order_acquire)&&(preparing_.load(std::memory_order_acquire)||relevant))refuse(Refusal::ColdCancelled);
}
void Bridge::resourceRetiring(tx::Resource kind,std::uint64_t value) noexcept {
    if(retired_.load(std::memory_order_acquire)||!adopted_.load(std::memory_order_acquire))return;
    resourceEpoch_.fetch_add(1,std::memory_order_acq_rel);
    if(!sourceBound_.load(std::memory_order_acquire))return;
    const unsigned index=kind==tx::Resource::Image?0u:kind==tx::Resource::View?1u:kind==tx::Resource::Memory?2u:3u;
    if(index<3&&value==selectedSourceValues_[index].load(std::memory_order_relaxed))
        transaction_.resourceRetiring(kind,{clone::handle(device_),value,selectedSourceGenerations_[index].load(std::memory_order_relaxed)});
}
void Bridge::descriptorMutation(VkDescriptorSet set) noexcept {
    if(retired_.load(std::memory_order_acquire)||!adopted_.load(std::memory_order_acquire))return;
    descriptorEpoch_.fetch_add(1,std::memory_order_acq_rel);
    if(targetBound_.load(std::memory_order_acquire)&&(!set||clone::handle(set)==selectedSet_.load(std::memory_order_acquire))){refuse(Refusal::DescriptorMutation);transaction_.unknownRoute(transaction_.command());}
}
void Bridge::descriptorPoolRetiring(frame_pixel_capture::LiveTracker& tracker,std::uint64_t tag,VkDescriptorPool pool) noexcept {
    if(retired_.load(std::memory_order_acquire)||!adopted_.load(std::memory_order_acquire))return;
    // Also cover a pool operation racing the opaque target snapshot. There is
    // no selected-set authority yet, so the target callback checks this epoch.
    descriptorEpoch_.fetch_add(1,std::memory_order_acq_rel);
    if(!targetBound_.load(std::memory_order_acquire))return;
    const auto selected=native<VkDescriptorSet>(selectedSet_.load(std::memory_order_relaxed));
    bool contains=false;
    if(!tracker.ssboPoolContainsSet(tag,pool,selected,contains)||contains)descriptorMutation(selected);
}
void Bridge::learn(const frame_pixel_capture::Candidate& c) noexcept {
    if(!usable()||!c.command||!c.pool||adopted_.load(std::memory_order_acquire))return;
    std::uint64_t empty=0;if(learnedCommand_.compare_exchange_strong(empty,clone::handle(c.command),std::memory_order_acq_rel))learnedPool_.store(clone::handle(c.pool),std::memory_order_release);
}
void Bridge::forget(VkCommandBuffer c,VkCommandPool p) noexcept {
    if(adopted_.load(std::memory_order_acquire))return;
    if((c&&clone::handle(c)==learnedCommand_.load())||(!c&&p&&clone::handle(p)==learnedPool_.load())){learnedCommand_.store(0,std::memory_order_release);learnedPool_.store(0,std::memory_order_release);}
}
void Bridge::begin(frame_pixel_capture::LiveTracker& tracker,std::uint64_t tag,VkCommandBuffer c,bool armed) noexcept {
    if(!armed||!usable()||adopted_.load(std::memory_order_acquire)||clone::handle(c)!=learnedCommand_.load(std::memory_order_acquire))return;
    tx::Transaction::RecordingTicket ticket;
    if(!tracker.ssboRecordingTicket(tag,c,ticket)||!transaction_.adoptRecording(ticket)||!transaction_.arm(c)){refuse(Refusal::Recording);return;}
    adoptedCommand_.store(clone::handle(c),std::memory_order_relaxed);adoptedPool_.store(clone::handle(transaction_.pool()),std::memory_order_relaxed);commandGeneration_.store(transaction_.generation());poolGeneration_.store(transaction_.poolGeneration());adopted_.store(true,std::memory_order_release);
}
void Bridge::pipeline(VkCommandBuffer c,VkPipeline p) noexcept {if(matches(c))transaction_.pipelineBound(c,p);}
void Bridge::beginPass(VkCommandBuffer c,VkRenderPass p,VkSubpassContents contents) noexcept {if(matches(c))transaction_.beginRenderPass(c,p,contents);}
void Bridge::endPass(VkCommandBuffer c) noexcept {if(matches(c))transaction_.endRenderPass(c);}
void Bridge::endCommand(VkCommandBuffer c,VkResult r) noexcept {if(matches(c))transaction_.endCommand(c,r);}
bool Bridge::draw(frame_pixel_capture::LiveTracker& tracker,std::uint64_t tag,VkCommandBuffer c,
        std::uint32_t n,std::uint32_t instances,std::uint32_t first,std::uint32_t firstInstance,std::int32_t offset,bool indexed) noexcept {
    if(!matches(c))return false;
    tx::Transaction::TargetTicket ticket;
    const auto descriptorEpoch=descriptorEpoch_.load(std::memory_order_acquire),resourceEpoch=resourceEpoch_.load(std::memory_order_acquire);
    if(usable()&&tracker.ssboTargetTicket(tag,c,*clone_.preparedRecipe(),ticket)){
        if(!transaction_.adoptTargetState(ticket))refuse(Refusal::Target);
        else {selectedSet_.store(clone::handle(transaction_.descriptorSet()),std::memory_order_relaxed);targetBound_.store(true,std::memory_order_release);
            if(descriptorEpoch!=descriptorEpoch_.load(std::memory_order_acquire))refuse(Refusal::DescriptorMutation);}
    }
    if(!usable()){transaction_.unknownRoute(c);return false;}
    const auto result=indexed?transaction_.drawIndexed(c,n,instances,first,offset,firstInstance):transaction_.draw(c,n,instances,first,firstInstance);
    if(result==tx::DrawResult::ReplacedOriginal){
        const auto source=transaction_.selectedSource();const std::array<tx::Id,3> identities{{source.image,source.view,source.memory}};
        for(unsigned i=0;i<3;++i){selectedSourceValues_[i].store(identities[i].value,std::memory_order_relaxed);selectedSourceGenerations_[i].store(identities[i].generation,std::memory_order_relaxed);}
        sourceBound_.store(true,std::memory_order_release);
        if(resourceEpoch!=resourceEpoch_.load(std::memory_order_acquire))refuse(Refusal::CopyCohort);
        drawKind_.store(indexed?2u:1u);drawArgs_[0].store(n);drawArgs_[1].store(instances);drawArgs_[2].store(first);drawArgs_[3].store(firstInstance);drawArgs_[4].store(static_cast<std::uint32_t>(offset));
        drawOrdinal_.store(transaction_.selectedOrdinal());replaced_.store(true,std::memory_order_release);return true;}return false;
}
void Bridge::unknown(VkCommandBuffer c) noexcept {if(matches(c)){transaction_.unknownRoute(c);refuse(Refusal::UnknownRoute);}}
bool Bridge::deferCopy(const frame_pixel_capture::Candidate& c) noexcept {
    learn(c);
    // A refused bridge leaves the established raw diagnostic available, but it
    // cannot publish an SSBO/source pairing. A ready bridge waits for next Begin.
    return !reason_.load(std::memory_order_acquire)&&(!matches(c.command)||transaction_.generation()!=c.commandGeneration);
}
tx::Cohort Bridge::cohort(const frame_pixel_capture::Candidate& c) const noexcept {
    const auto id=clone::handle(device_);const auto identity=[id](frame_bindings::Identity x){return tx::Id{id,x.handle,x.generation};};
    return {identity(c.binding.image.identity),identity(c.binding.view.identity),identity(c.binding.image.memory),c.binding.imageLayout,{c.binding.image.extent.width,c.binding.image.extent.height}};
}
void Bridge::copyRecorded(const frame_pixel_capture::Candidate& c) noexcept {
    if(!matches(c.command)||transaction_.generation()!=c.commandGeneration||transaction_.poolGeneration()!=c.poolGeneration||transaction_.pool()!=c.pool){refuse(Refusal::CopyCohort);return;}
    source_.source=cohort(c);source_.command=c.command;source_.pool=c.pool;source_.commandGeneration=c.commandGeneration;source_.poolGeneration=c.poolGeneration;source_.execution=0;
    copy_.store(true,std::memory_order_release);
}
tx::Transaction::SubmitTicket Bridge::beforeSubmit(VkQueue q,std::uint32_t count,const VkSubmitInfo* info) noexcept {
    if(!adopted_.load(std::memory_order_acquire)||retired_.load(std::memory_order_acquire))return {};
    if(count>32||(!info&&count)){refuse(Refusal::SubmitShape);return {};}
    for(std::uint32_t i=0;i<count;++i){if(info[i].commandBufferCount>64||(!info[i].pCommandBuffers&&info[i].commandBufferCount)){refuse(Refusal::SubmitShape);return {};}
        for(std::uint32_t j=0;j<info[i].commandBufferCount;++j)if(matches(info[i].pCommandBuffers[j])){
            const auto ticket=transaction_.beforeSubmit(q,count,info);const auto observed=transaction_.submitObservation();submitRoute_.store(1);
            submitBatches_.store(observed.batches);submitBatch_.store(observed.batch);submitCommands_.store(observed.commandCount);submitNext_.store(observed.nextType);submitOccurrences_.store(observed.selectedOccurrences);return ticket;}}
    return {};
}
void Bridge::afterSubmit(const tx::Transaction::SubmitTicket& ticket,VkQueue,std::uint32_t count,const VkSubmitInfo* info,VkResult result) noexcept {
    if(!adopted_.load(std::memory_order_acquire)||!info||count>32)return;
    unsigned copies=0;for(std::uint32_t i=0;i<count;++i){if(info[i].commandBufferCount>64||(!info[i].pCommandBuffers&&info[i].commandBufferCount)){refuse(Refusal::SubmitShape);return;}
        for(std::uint32_t j=0;j<info[i].commandBufferCount;++j)copies+=matches(info[i].pCommandBuffers[j]);}
    if(!copies)return;
    if(copies!=1){refuse(Refusal::SubmitShape);return;}
    if(result!=VK_SUCCESS){transaction_.afterSubmit(ticket,result);refuse(Refusal::Execution);return;}
    if(!copy_.load(std::memory_order_acquire)){transaction_.afterSubmit(ticket,result);refuse(Refusal::CopyCohort);return;}
    const auto previous=sourceExecutions_.fetch_add(1,std::memory_order_acq_rel);if(previous==UINT64_MAX){refuse(Refusal::Execution);return;}
    // This counter observes ORIGINAL submits containing the recorded GPU copy,
    // including serial replays. It is independent from B's fence marker count.
    if(!transaction_.afterSubmit(ticket,result)||transaction_.execution()!=previous+1){refuse(Refusal::Execution);return;}
    pairedExecution_.store(previous+1,std::memory_order_release);
}
void Bridge::unsupportedSubmit(VkQueue,std::uint32_t count,const VkSubmitInfo2* info) noexcept {
    if(!adopted_.load(std::memory_order_acquire)||!info||count>32)return;
    for(std::uint32_t i=0;i<count;++i){if(info[i].commandBufferInfoCount>64){refuse(Refusal::SubmitShape);return;}
        for(std::uint32_t j=0;j<info[i].commandBufferInfoCount;++j)if(matches(info[i].pCommandBufferInfos[j].commandBuffer)){submitRoute_.store(2);submitBatches_.store(count);submitBatch_.store(i);submitCommands_.store(info[i].commandBufferInfoCount);submitNext_.store(info[i].pNext?static_cast<std::uint32_t>(static_cast<const VkBaseInStructure*>(info[i].pNext)->sType):UINT32_MAX);submitOccurrences_.store(1);refuse(Refusal::SubmitShape);transaction_.unknownRoute(transaction_.command());return;}}
}
Bridge::RetirementTicket Bridge::beforeRetirement(tx::Retirement r,VkCommandBuffer c,VkCommandPool p) noexcept {
    if(r==tx::Retirement::DestroyPool)forget(VK_NULL_HANDLE,p);
    if(!adopted_.load(std::memory_order_acquire)||retired_.load(std::memory_order_acquire))return {};
    const bool poolOperation=r==tx::Retirement::ResetPool||r==tx::Retirement::DestroyPool;
    if(poolOperation?(!p||clone::handle(p)!=adoptedPool_.load(std::memory_order_relaxed)):!matches(c))return {};
    return {transaction_.beforeRetirement(r,c,p),true};
}
void Bridge::afterRetirement(const RetirementTicket& t,VkResult r) noexcept {
    if(t.selected&&!retired_.load(std::memory_order_acquire)&&transaction_.afterRetirement(t.native,r))retired_.store(true,std::memory_order_release);
}
bool Bridge::publish(const frame_pixels::View& view,tx::RecordView& out) noexcept {
    out={};if(!retired_.load(std::memory_order_acquire)||!usable()||!copy_.load(std::memory_order_acquire)||!view.generationRetired||view.commandGeneration!=source_.commandGeneration||
        clone::handle(view.source)!=source_.source.image.value||view.sourceGeneration!=source_.source.image.generation||
        view.layout.extent.width!=source_.source.extent.width||view.layout.extent.height!=source_.source.extent.height)return false;
    const auto observed=sourceExecutions_.load(std::memory_order_acquire),paired=pairedExecution_.load(std::memory_order_acquire);
    if(!observed||observed!=paired){refuse(Refusal::Execution);return false;}
    auto metadata=source_;metadata.execution=paired;
    if(!transaction_.publish(metadata,out))return false;
    std::array<std::uint32_t,4> header{};if(out.bytes!=128||!out.record){refuse(Refusal::Record);out={};return false;}
    std::memcpy(header.data(),out.record,16);
    if(header!=std::array<std::uint32_t,4>{1,UINT32_C(0x5353424f),1,32}){refuse(Refusal::Record);out={};return false;}return true;
}
bool Bridge::release(VkResult result) noexcept {
    transaction_.afterDeviceWaitIdle(result);
    transaction_.deviceDestroying(device_);
    if(!transaction_.release()){refuse(Refusal::Release);return false;}
    // A cold prepare may fail before configure takes ownership of this clone.
    if(clone_.ready()&&clone_.release().status!=frame_pixel_ssbo_owned::Status::Ok){refuse(Refusal::Release);return false;}return true;
}
Snapshot Bridge::snapshot() const noexcept {
    Snapshot s;s.refusal=Refusal(reason_.load(std::memory_order_acquire));s.transaction=unsigned(transaction_.refusal());
    s.cloneStatus=cloneStatus_.load();s.cloneStage=cloneStage_.load();s.cloneDriver=cloneDriver_.load();
    s.queueCreateInfos=queueCreateInfos_.load();s.queueCount=queueCount_.load();s.queueFamily=queueFamily_.load();
    s.queueKnown=queueKnown_.load();s.retrievedFamily=retrievedFamily_.load();s.retrievedIndex=retrievedIndex_.load();s.retrievedQueue=retrievedQueue_.load();
    s.profile=profile_.load();s.ready=ready_.load();s.learned=learnedCommand_.load()!=0;s.adopted=adopted_.load();s.replaced=replaced_.load();s.copy=copy_.load();
    s.drawKind=drawKind_.load();s.submitRoute=submitRoute_.load();for(unsigned i=0;i<5;++i)s.drawArgs[i]=drawArgs_[i].load();
    s.submit={submitBatches_.load(),submitBatch_.load(),submitCommands_.load(),submitNext_.load(),submitOccurrences_.load()};
    s.commandGeneration=commandGeneration_.load();s.poolGeneration=poolGeneration_.load();s.drawOrdinal=drawOrdinal_.load();
    s.sourceExecutions=sourceExecutions_.load();s.pairedExecution=pairedExecution_.load();s.cancellationEpoch=cancellationEpoch_.load();return s;
}
} // namespace frame_pixel_ssbo_receiver
