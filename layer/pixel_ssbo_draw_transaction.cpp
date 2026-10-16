#include "pixel_ssbo_draw_transaction.h"

namespace frame_pixel_ssbo_transaction {
namespace {
template<class T>T handle(std::uint64_t value) noexcept {T out{};std::memcpy(&out,&value,sizeof(out));return out;}
bool valid(Id value,std::uint64_t device) noexcept {return value.device==device && value.value && value.generation;}
}
const char* reasonName(Reason value) noexcept {
    switch(value){
#define NAME(x,s) case Reason::x:return s;
    NAME(None,"none") NAME(NotConfigured,"not_configured") NAME(InvalidConfiguration,"invalid_configuration")
    NAME(Contention,"contention") NAME(Identity,"identity") NAME(GenerationWrap,"generation_wrap")
    NAME(Begin,"begin") NAME(NotArmed,"not_armed") NAME(MissingReset,"missing_reset") NAME(Scope,"scope")
    NAME(Prefix,"prefix") NAME(Pipeline,"pipeline") NAME(Source,"source") NAME(EmptyDraw,"empty_draw")
    NAME(LaterDraw,"later_draw") NAME(Subpass,"subpass") NAME(Secondary,"secondary") NAME(UnknownRoute,"unknown_route")
    NAME(CommandEnd,"command_end") NAME(SubmitShape,"submit_shape") NAME(SubmitExtension,"submit_extension") NAME(SubmitBounds,"submit_bounds") NAME(UnknownQueue,"unknown_queue")
    NAME(Overlap,"overlap") NAME(NativeSubmit,"native_submit") NAME(MarkerSubmit,"marker_submit")
    NAME(Fence,"fence") NAME(NotRetired,"not_retired") NAME(CohortMismatch,"cohort_mismatch")
    NAME(SourceRetired,"source_retired") NAME(UnsafeRelease,"unsafe_release") NAME(AlreadyArmed,"already_armed") NAME(DeviceDestroying,"device_destroying")
#undef NAME
    }return "unknown_transaction_refusal";
}
std::uint64_t Transaction::reserveEpoch() noexcept {
    auto value=epochs_.load(std::memory_order_relaxed);
    while(value && value!=UINT64_MAX){if(epochs_.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;}
    return 0; // Sticky process-lifetime exhaustion; never wrap into an old epoch.
}
Transaction::Transaction() noexcept:epoch_(reserveEpoch()) {if(!epoch_)refuse(Reason::GenerationWrap);}
Transaction::Guard::Guard(Transaction& owner) noexcept:owner_(owner),acquired_(!owner.lock_.test_and_set(std::memory_order_acquire)) {
    if(!acquired_)owner_.refuse(Reason::Contention);
}
Transaction::Guard::~Guard(){if(acquired_)owner_.lock_.clear(std::memory_order_release);}
bool Transaction::refuse(Reason value) noexcept {
    auto expected=Reason::None;reason_.compare_exchange_strong(expected,value,std::memory_order_acq_rel);return false;
}
bool Transaction::advance(std::uint64_t& value) noexcept {return value!=UINT64_MAX?++value!=0:refuse(Reason::GenerationWrap);}
bool Transaction::same(Id a,Id b) noexcept{return a.device==b.device && a.value==b.value && a.generation && a.generation==b.generation;}
bool Transaction::same(const Cohort& a,const Cohort& b) noexcept {
    return same(a.image,b.image) && same(a.view,b.view) && same(a.memory,b.memory) && a.layout==b.layout &&
        a.extent.width && a.extent.height && a.extent.width==b.extent.width && a.extent.height==b.extent.height;
}
bool Transaction::matches(VkCommandBuffer command) const noexcept{return device_ && command_ && command==command_;}
bool Transaction::active(VkCommandBuffer command) noexcept {
    return refusal()==Reason::None && matches(command) && recording_ && !retired_;
}
bool Transaction::deviceCreated(VkDevice device,const VkDeviceCreateInfo& input,VkResult nativeResult) noexcept {
    Guard guard(*this);if(!guard || nativeResult!=VK_SUCCESS)return false;
    if(device_ || !device || input.sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO || input.flags ||
       input.queueCreateInfoCount!=1 || !input.pQueueCreateInfos)return refuse(Reason::InvalidConfiguration);
    const auto& q=input.pQueueCreateInfos[0];
    if(q.sType!=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO || q.flags || q.queueCount!=1 ||
       !q.pQueuePriorities || q.queueFamilyIndex==VK_QUEUE_FAMILY_IGNORED || !std::isfinite(q.pQueuePriorities[0]) ||
       q.pQueuePriorities[0]<0 || q.pQueuePriorities[0]>1)return refuse(Reason::InvalidConfiguration);
    if(q.pNext){
        const auto* node=static_cast<const VkBaseInStructure*>(q.pNext);
        if(node->sType!=VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR || node->pNext)
            return refuse(Reason::InvalidConfiguration);
        const auto priority=reinterpret_cast<const VkDeviceQueueGlobalPriorityCreateInfoKHR*>(node)->globalPriority;
        if(priority!=VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR && priority!=VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR &&
           priority!=VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR && priority!=VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR)
            return refuse(Reason::InvalidConfiguration);
        // The ORIGINAL successful create already selected this scheduling
        // priority. It changes neither queue identity nor marker ordering; do
        // not reject an ordinary high-priority compositor queue or rewrite it.
    }
    device_=device;family_=q.queueFamilyIndex;return epoch_!=0;
}
bool Transaction::queueRetrieved(VkDevice device,std::uint32_t family,std::uint32_t index,VkQueue queue) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!device_ || device!=device_ || family!=family_ || index || !queue || (queue_ && queue_!=queue))return refuse(Reason::UnknownQueue);
    queue_=queue;return true;
}
bool Transaction::configure(frame_pixel_ssbo_owned::Clone& clone,const Configuration& config) noexcept {
    Guard guard(*this);if(!guard)return false;
    const auto* prepared=clone.preparedRecipe();
    if(!prepared)return refuse(Reason::InvalidConfiguration);
    const auto& recipe=*prepared;const auto& p=config.dispatch;const auto device=frame_pixel_ssbo_clone::handle(device_);
    if(refusal()!=Reason::None || configured_ || !device_ || !queue_ || family_==VK_QUEUE_FAMILY_IGNORED || !clone.ready() ||
       clone.referenced() || !recipe.definitionComplete || !recipe.set0.complete || !recipe.layout.complete ||
       !recipe.renderPass.complete || recipe.runtimeActivationAuthorized || recipe.nativeLifetimeProven ||
       !p.barrier || !p.fill || !p.pipeline || !p.sets || !p.draw || !p.drawIndexed || !p.createFence || !p.destroyFence ||
       !p.resetFence || !p.fenceStatus || !p.submit || !p.invalidate)return refuse(Reason::InvalidConfiguration);
    for(const auto& id:recipe.provenance)if(!valid(id,device))return refuse(Reason::Identity);
    // The prepared Recipe is the immutable snapshot ACTUALLY used to build
    // this Clone, not a different caller-passed bag of all-true facts. It is CPU
    // definition history, still NOT native object lifetime or frame/eye proof.
    VkFenceCreateInfo create{};create.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;VkFence created=VK_NULL_HANDLE;
    const auto result=p.createFence(device_,&create,nullptr,&created);
    if(result!=VK_SUCCESS || !created)return refuse(Reason::Fence); // failed output is undefined, never destroy it
    config_=config;clone_=&clone;fence_=created;set0_=recipe.set0;prefix_=recipe.layout;provenance_=recipe.provenance;
    original_=handle<VkPipeline>(provenance_[5].value);originalLayout_=handle<VkPipelineLayout>(provenance_[1].value);
    pass_=handle<VkRenderPass>(provenance_[2].value);configured_=true;return true;
}
bool Transaction::poolCreated(VkCommandPool pool,std::uint32_t family,VkResult result) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!device_)return refuse(Reason::NotConfigured);
    if(result!=VK_SUCCESS)return false;
    if(pool_ || !pool || family!=family_)return refuse(Reason::Identity);
    pool_=pool;return advance(poolGeneration_);
}
bool Transaction::commandAllocated(VkCommandPool pool,VkCommandBuffer command,VkCommandBufferLevel level,VkResult result) noexcept {
    Guard guard(*this);if(!guard)return false;if(result!=VK_SUCCESS)return false;
    if(!device_ || pool!=pool_ || !pool_ || command_ || !command || level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        return refuse(Reason::Identity);
    command_=command;return true;
}
bool Transaction::beginSucceeded(VkCommandBuffer command,const VkCommandBufferBeginInfo& begin,VkResult result) noexcept {
    Guard guard(*this);if(!guard)return false;if(result!=VK_SUCCESS)return false;
    if(command!=command_ || !command_ || recording_ || armed_ || begin.sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO ||
       begin.pNext || begin.pInheritanceInfo || (begin.flags&~VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))return refuse(Reason::Begin);
    if(!advance(generation_))return false;
    recording_=true;retired_=false;inPass_=false;scopeEnded_=false;selected_=false;
    current_=VK_NULL_HANDLE;currentSet0_=VK_NULL_HANDLE;setIdentity_={};currentSource_={};selectedSource_={};
    drawOrdinal_=selectedOrdinal_=0;prefixValid_=false;matchingPass_=false;cleanHistory_=true;return true;
}
bool Transaction::adoptRecording(const RecordingTicket& ticket) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!configured_ || armed_ || !ticket.tracker_ || ticket.device_!=frame_pixel_ssbo_clone::handle(device_) ||
       !ticket.command_ || !ticket.pool_ || !ticket.commandGeneration_ || !ticket.poolGeneration_ || ticket.family_!=family_ ||
       (ticket.flags_&~VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) ||
       (adoptedTracker_ && adoptedTracker_!=ticket.tracker_) ||
       (command_==ticket.command_ && generation_>=ticket.commandGeneration_))return refuse(Reason::Identity);
    adoptedTracker_=ticket.tracker_;command_=ticket.command_;pool_=ticket.pool_;
    generation_=ticket.commandGeneration_;poolGeneration_=ticket.poolGeneration_;
    recording_=true;retired_=false;inPass_=matchingPass_=false;scopeEnded_=selected_=false;
    current_=VK_NULL_HANDLE;currentSet0_=VK_NULL_HANDLE;setIdentity_={};currentSource_={};selectedSource_={};
    drawOrdinal_=selectedOrdinal_=0;prefixValid_=false;cleanHistory_=true;return true;
}
bool Transaction::adoptTargetState(const TargetTicket& ticket) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!configured_ || !armed_ || !active(ticket.command_) || !inPass_ || !matchingPass_ || selected_ ||
       !adoptedTracker_ || ticket.tracker_!=adoptedTracker_ || ticket.device_!=frame_pixel_ssbo_clone::handle(device_) ||
       ticket.commandGeneration_!=generation_ || ticket.pool_!=pool_ || ticket.poolGeneration_!=poolGeneration_ ||
       ticket.ordinal_!=drawOrdinal_+1 || !same(ticket.pipeline_,provenance_[5]) || !same(ticket.layout_,provenance_[1]) ||
       !valid(ticket.set0_,ticket.device_) || !valid(ticket.source_.image,ticket.device_) ||
       !valid(ticket.source_.view,ticket.device_) || !valid(ticket.source_.memory,ticket.device_) ||
       ticket.source_.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || !same(ticket.source_,ticket.source_))return refuse(Reason::Identity);
    current_=original_;currentSet0_=handle<VkDescriptorSet>(ticket.set0_.value);setIdentity_=ticket.set0_;
    currentSource_=ticket.source_;prefixValid_=true;return true;
}
bool Transaction::arm(VkCommandBuffer command) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(armed_)return refuse(Reason::AlreadyArmed);
    if(!configured_ || !active(command) || inPass_ || scopeEnded_ || drawOrdinal_ || !cleanHistory_ || selected_ || !generation_)return refuse(Reason::NotArmed);
    if(!clone_->ready() || clone_->referenced() || !clone_->markReferenced())return refuse(Reason::InvalidConfiguration);
    armed_=true;
    // This GPU reset is inside the ORIGINAL reusable recording, before its
    // render pass. It therefore executes on EVERY serial replay; a CPU memset
    // or per-recording initialization cannot accidentally authorize old data.
    VkBufferMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;before.buffer=clone_->recordBuffer();before.size=frame_pixel_ssbo_owned::RecordBytes;
    before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    config_.dispatch.barrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&before,0,nullptr);
    config_.dispatch.fill(command,clone_->recordBuffer(),0,frame_pixel_ssbo_owned::RecordBytes,0);
    auto after=before;after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    config_.dispatch.barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,0,nullptr,1,&after,0,nullptr);reset_=true;return refusal()==Reason::None;
}
bool Transaction::beginRenderPass(VkCommandBuffer command,VkRenderPass pass,VkSubpassContents contents) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!active(command))return false;
    if(inPass_){if(armed_)return refuse(Reason::Scope);cleanHistory_=false;return false;}
    inPass_=true;
    if(!armed_){cleanHistory_=false;return false;}
    if(!reset_)return refuse(Reason::MissingReset);
    if(selected_ || scopeEnded_)return refuse(Reason::Scope);
    matchingPass_=pass==pass_ && contents==VK_SUBPASS_CONTENTS_INLINE;
    return matchingPass_;
}
void Transaction::pipelineBound(VkCommandBuffer command,VkPipeline pipeline) noexcept {
    Guard guard(*this);if(!guard || !active(command))return;current_=pipeline;
}
void Transaction::set0Bound(VkCommandBuffer command,VkPipelineLayout layout,std::uint32_t first,std::uint32_t count,
                           const VkDescriptorSet* sets,std::uint32_t dynamicOffsets,Id descriptorIdentity) noexcept {
    Guard guard(*this);if(!guard || !active(command))return;
    if(!configured_){currentSet0_=VK_NULL_HANDLE;setIdentity_={};return;}
    if(layout!=originalLayout_ || first || count!=1 || !sets || !sets[0] || dynamicOffsets || !valid(descriptorIdentity,frame_pixel_ssbo_clone::handle(device_)) ||
       descriptorIdentity.value!=frame_pixel_ssbo_clone::handle(sets[0])){prefixValid_=false;return;}
    currentSet0_=sets[0];setIdentity_=descriptorIdentity;prefixValid_=true;
}
void Transaction::pushConstants(VkCommandBuffer command,VkPipelineLayout layout,VkShaderStageFlags stages,
                               std::uint32_t offset,std::uint32_t bytes,const void* data) noexcept {
    Guard guard(*this);if(!guard || !active(command))return;
    if(!configured_)return;
    if(layout!=originalLayout_ || !data || !bytes || bytes%4 || offset%4 || offset>4096 || bytes>4096-offset || !stages){prefixValid_=false;return;}
    VkShaderStageFlags covered=0;
    for(std::uint32_t i=0;i<prefix_.count;++i){const auto& r=prefix_.ranges[i];
        if(offset>=r.offset && bytes<=r.size && offset-r.offset<=r.size-bytes)covered|=r.stageFlags;}
    if((covered&stages)!=stages)prefixValid_=false;
}
void Transaction::sourceBound(VkCommandBuffer command,const Cohort& source) noexcept {
    Guard guard(*this);if(!guard || !active(command))return;
    const auto device=frame_pixel_ssbo_clone::handle(device_);
    if(!valid(source.image,device) || !valid(source.view,device) || !valid(source.memory,device) ||
       !source.extent.width || !source.extent.height || source.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){refuse(Reason::Source);return;}
    if(selected_ && !same(source,selectedSource_)){refuse(Reason::Source);return;}currentSource_=source;
}
DrawResult Transaction::draw(VkCommandBuffer command,std::uint32_t vertices,std::uint32_t instances,
                            std::uint32_t firstVertex,std::uint32_t firstInstance) noexcept {
    return replace(command,vertices,instances,firstVertex,firstInstance,0,false);
}
DrawResult Transaction::drawIndexed(VkCommandBuffer command,std::uint32_t indices,std::uint32_t instances,
                                   std::uint32_t firstIndex,std::int32_t vertexOffset,std::uint32_t firstInstance) noexcept {
    return replace(command,indices,instances,firstIndex,firstInstance,vertexOffset,true);
}
DrawResult Transaction::replace(VkCommandBuffer command,std::uint32_t vertices,std::uint32_t instances,
                               std::uint32_t firstVertex,std::uint32_t firstInstance,std::int32_t vertexOffset,bool indexed) noexcept {
    Guard guard(*this);if(!guard || !active(command))return DrawResult::ForwardOriginal;
    if(!advance(drawOrdinal_))return DrawResult::ForwardOriginal;
    if(selected_){refuse(Reason::LaterDraw);return DrawResult::ForwardOriginal;}
    if(!armed_)return DrawResult::ForwardOriginal;
    if(!reset_){refuse(Reason::MissingReset);return DrawResult::ForwardOriginal;}
    if(!inPass_){refuse(Reason::Scope);return DrawResult::ForwardOriginal;}
    // Preceding ordinary unrelated native draws are preserved, including the
    // real receiver target at ordinal 4. Only the exact target profile is
    // replaced; ANY subsequent draw invalidates publication.
    if(current_!=original_ || !matchingPass_)return DrawResult::ForwardOriginal;
    if(!vertices || !instances){refuse(Reason::EmptyDraw);return DrawResult::ForwardOriginal;}
    if(!currentSet0_ || !prefixValid_){refuse(Reason::Prefix);return DrawResult::ForwardOriginal;}
    if(!same(currentSource_,currentSource_)){refuse(Reason::Source);return DrawResult::ForwardOriginal;}
    if(!clone_->ready() || !clone_->referenced()){refuse(Reason::InvalidConfiguration);return DrawResult::ForwardOriginal;}
    selected_=true;selectedOrdinal_=drawOrdinal_;selectedSource_=currentSource_;
    const auto set=clone_->set1();
    config_.dispatch.sets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,clone_->layout(),1,1,&set,0,nullptr);
    config_.dispatch.pipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,clone_->pipeline());
    if(indexed)config_.dispatch.drawIndexed(command,vertices,instances,firstVertex,vertexOffset,firstInstance);
    else config_.dispatch.draw(command,vertices,instances,firstVertex,firstInstance);
    // MUST be the next command even if a downstream callback reveals reentry or
    // poisons publication. Restoring is cleanup, never conditional authorization.
    config_.dispatch.pipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original_);
    return DrawResult::ReplacedOriginal;
}
void Transaction::endRenderPass(VkCommandBuffer command) noexcept {
    Guard guard(*this);if(!guard || !active(command))return;
    if(!armed_){inPass_=false;scopeEnded_=true;return;}
    if(!inPass_){refuse(Reason::Scope);return;}
    inPass_=false;matchingPass_=false;
    if(selected_){
        scopeEnded_=true;
        if(!clone_->ready()){refuse(Reason::InvalidConfiguration);return;}
        VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        host.buffer=clone_->recordBuffer();host.size=frame_pixel_ssbo_owned::RecordBytes;
        config_.dispatch.barrier(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    }
}
void Transaction::endCommand(VkCommandBuffer command,VkResult result) noexcept {
    Guard guard(*this);if(!guard || !matches(command))return;
    if(!armed_){recording_=false;inPass_=false;return;}
    if(result!=VK_SUCCESS || !recording_ || inPass_ || !scopeEnded_ || !selected_){refuse(Reason::CommandEnd);return;}
    recording_=false;executable_=true;
}
void Transaction::unknown(VkCommandBuffer command,Reason why) noexcept {
    Guard guard(*this);if(guard && matches(command) && !retired_){cleanHistory_=false;if(armed_)refuse(why);}
}
void Transaction::laterDraw(VkCommandBuffer command) noexcept {
    Guard guard(*this);if(!guard || !matches(command) || retired_)return;
    if(advance(drawOrdinal_) && selected_)refuse(Reason::LaterDraw);
}
void Transaction::nextSubpass(VkCommandBuffer c) noexcept{unknown(c,Reason::Subpass);}
void Transaction::executeSecondary(VkCommandBuffer c) noexcept{unknown(c,Reason::Secondary);}
void Transaction::unknownRoute(VkCommandBuffer c) noexcept{unknown(c,Reason::UnknownRoute);}
void Transaction::resourceRetiring(Resource kind,Id id) noexcept {
    Guard guard(*this);if(!guard || !device_)return;
    for(const auto& source:{currentSource_,selectedSource_}){
        const bool hit=(kind==Resource::Image && same(id,source.image)) || (kind==Resource::View && same(id,source.view)) ||
            (kind==Resource::Memory && same(id,source.memory));
        if(hit){if(armed_)refuse(Reason::SourceRetired);else cleanHistory_=false;return;}
    }
    if(kind==Resource::DescriptorSet && same(id,setIdentity_)){if(armed_)refuse(Reason::Identity);else cleanHistory_=false;return;}
    // Shader/SetLayout/PipelineLayout provenance is consumed definition history,
    // not live borrowed storage. Never confuse equal raw handles of DIFFERENT
    // Vulkan resource kinds. Original pipeline/renderpass remain dependencies.
    if((kind==Resource::Pipeline && same(id,provenance_[5])) || (kind==Resource::RenderPass && same(id,provenance_[2]))){
        if(armed_)refuse(Reason::Identity);else cleanHistory_=false;
    }
}
bool Transaction::fenceComplete() noexcept {
    if(!submitted_ || !pending_)return complete_;
    const auto result=config_.dispatch.fenceStatus(device_,fence_);
    if(result==VK_NOT_READY)return false;
    if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;return refuse(Reason::Fence);}
    pending_=false;complete_=true;return true;
}
Transaction::SubmitTicket Transaction::beforeSubmit(VkQueue queue,std::uint32_t count,const VkSubmitInfo* submits) noexcept {
    Guard guard(*this);SubmitTicket ticket;if(!guard || !configured_)return ticket;
    quiescent_=false; // ANY observed new native submission invalidates an earlier idle observation.
    if(refusal()!=Reason::None)return ticket;
    if(queue!=queue_){refuse(Reason::UnknownQueue);return ticket;}
    if(!armed_)return ticket;
    // Inspect the ORIGINAL untouched batch, without allocating/copying its
    // arrays. Unrelated buffers remain native; only a single occurrence of the
    // selected original recording can authorize the one owned completion marker.
    submitObservation_={};submitObservation_.batches=count;
    if(count>32){refuse(Reason::SubmitBounds);return ticket;}
    if(count && !submits){refuse(Reason::SubmitShape);return ticket;}
    std::uint32_t occurrences=0;
    for(std::uint32_t i=0;i<count;++i){const auto& info=submits[i];
        submitObservation_.batch=i;submitObservation_.commandCount=info.commandBufferCount;
        if(info.sType!=VK_STRUCTURE_TYPE_SUBMIT_INFO){refuse(Reason::SubmitShape);return ticket;}
        if(info.commandBufferCount>64 || info.waitSemaphoreCount>64 || info.signalSemaphoreCount>64){refuse(Reason::SubmitBounds);return ticket;}
        if((info.commandBufferCount && !info.pCommandBuffers) ||
           (info.waitSemaphoreCount && (!info.pWaitSemaphores || !info.pWaitDstStageMask)) ||
           (info.signalSemaphoreCount && !info.pSignalSemaphores)){refuse(Reason::SubmitShape);return ticket;}
        if(info.pNext){
            const auto* base=static_cast<const VkBaseInStructure*>(info.pNext);
            submitObservation_.nextType=static_cast<std::uint32_t>(base->sType);
            if(base->sType!=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO || base->pNext){refuse(Reason::SubmitExtension);return ticket;}
            const auto* timeline=static_cast<const VkTimelineSemaphoreSubmitInfo*>(info.pNext);
            if(timeline->waitSemaphoreValueCount!=info.waitSemaphoreCount || timeline->signalSemaphoreValueCount!=info.signalSemaphoreCount ||
               (timeline->waitSemaphoreValueCount && !timeline->pWaitSemaphoreValues) ||
               (timeline->signalSemaphoreValueCount && !timeline->pSignalSemaphoreValues)){refuse(Reason::SubmitShape);return ticket;}
        }
        for(std::uint32_t n=0;n<info.commandBufferCount;++n)if(info.pCommandBuffers[n]==command_){
            ++occurrences;submitObservation_.selectedOccurrences=occurrences;
            if(occurrences>1){refuse(Reason::SubmitShape);return ticket;}
        }
    }
    if(!occurrences)return ticket; // A valid unrelated original submit is not optional-work failure.
    if(!executable_ || retired_ || pendingSubmit_ || pendingRetirement_){refuse(Reason::Overlap);return ticket;}
    if(pending_ && !fenceComplete()){if(refusal()==Reason::None)refuse(Reason::Overlap);return ticket;}
    if(!advance(submitNonce_))return ticket;
    pendingSubmit_=submitNonce_;ticket.owner_=this;ticket.epoch_=epoch_;ticket.generation_=generation_;ticket.nonce_=submitNonce_;return ticket;
}
bool Transaction::afterSubmit(const SubmitTicket& ticket,VkResult result) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(ticket.owner_!=this || ticket.epoch_!=epoch_ || ticket.generation_!=generation_ || !ticket.nonce_ || ticket.nonce_!=pendingSubmit_)
        return false; // Stale/foreign/replayed tickets cannot consume a newer admission.
    pendingSubmit_=0;
    if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;return refuse(Reason::NativeSubmit);}
    // An empty marker follows the UNMODIFIED native submit on the same original
    // externally synchronized queue. It waits for prior queue execution only;
    // this is not command retirement or a source video-frame association.
    if(refusal()!=Reason::None)return false;
    result=config_.dispatch.resetFence(device_,1,&fence_);
    if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;return refuse(Reason::Fence);}
    VkSubmitInfo marker{};marker.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
    result=config_.dispatch.submit(queue_,1,&marker,fence_);
    submitted_=true;complete_=false;quiescent_=false;
    if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;return refuse(Reason::MarkerSubmit);}
    pending_=true;return advance(serial_);
}
Transaction::RetirementTicket Transaction::beforeRetirement(Retirement operation,VkCommandBuffer command,VkCommandPool pool) noexcept {
    Guard guard(*this);RetirementTicket ticket;if(!guard || !device_ || !generation_ || retired_)return ticket;
    switch(operation){case Retirement::ResetCommand:case Retirement::BeginCommand:case Retirement::FreeCommand:
        case Retirement::ResetPool:case Retirement::DestroyPool:break;default:return ticket;}
    const bool poolOperation=operation==Retirement::ResetPool || operation==Retirement::DestroyPool;
    if((poolOperation && (pool!=pool_ || !pool_)) || (!poolOperation && !matches(command)))return ticket;
    if(pendingSubmit_ || pendingRetirement_){refuse(Reason::Overlap);return ticket;}
    if(!advance(retireNonce_))return ticket;
    ticket.owner_=this;ticket.epoch_=epoch_;ticket.generation_=generation_;ticket.poolGeneration_=poolGeneration_;
    ticket.nonce_=retireNonce_;ticket.operation_=operation;pendingRetirement_=retireNonce_;return ticket;
}
bool Transaction::afterRetirement(const RetirementTicket& ticket,VkResult result) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(ticket.owner_!=this || ticket.epoch_!=epoch_ || ticket.generation_!=generation_ || ticket.poolGeneration_!=poolGeneration_ ||
       !ticket.nonce_ || ticket.nonce_!=pendingRetirement_)return false;
    pendingRetirement_=0;if(result!=VK_SUCCESS)return false;
    // Only the saved OLD generation retires. Reset/Begin/Free/pool reuse cannot
    // be inferred by querying a replacement CB with the same numeric handle.
    retired_=true;recording_=false;executable_=false;
    if(!armed_){
        if(ticket.operation_==Retirement::FreeCommand || ticket.operation_==Retirement::DestroyPool)command_=VK_NULL_HANDLE;
        if(ticket.operation_==Retirement::DestroyPool)pool_=VK_NULL_HANDLE;
        if(ticket.operation_==Retirement::ResetPool || ticket.operation_==Retirement::DestroyPool)advance(poolGeneration_);
    }
    return true;
}
void Transaction::afterDeviceWaitIdle(VkResult result) noexcept {
    Guard guard(*this);if(!guard || !configured_)return;
    if(result==VK_SUCCESS)quiescent_=true;else if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;
}
bool Transaction::deviceDestroying(VkDevice device) noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!device_ || device!=device_)return refuse(Reason::Identity);
    if(!quiescent_ && !deviceLost_)return false;
    // This is the actual original DestroyDevice ENTRY, not an ordinary idle
    // notification. Vulkan requires external synchronization of the device and
    // all its queues, so no valid future submission/recording may follow. The
    // separate actual idle/lost observation rules out pending GPU references.
    // Retire only this exact adopted generation and invalidate publication.
    retired_=true;recording_=false;executable_=false;
    pendingSubmit_=pendingRetirement_=0;refuse(Reason::DeviceDestroying);return true;
}
bool Transaction::publish(const SourceReadbackMetadata& readback,RecordView& out) noexcept {
    Guard guard(*this);out={};if(!guard || !configured_ || refusal()!=Reason::None)return false;
    if(!retired_)return false; // Pending is not a permanent refusal; callers may retry after retirement.
    if(!submitted_ || !fenceComplete())return false;
    if(readback.command!=command_ || readback.pool!=pool_ || readback.commandGeneration!=generation_ ||
       readback.poolGeneration!=poolGeneration_ || readback.execution!=serial_ || !same(readback.source,selectedSource_))return refuse(Reason::CohortMismatch);
    if(!clone_->hostCoherent()){
        VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;range.memory=clone_->recordMemory();range.size=VK_WHOLE_SIZE;
        const auto result=config_.dispatch.invalidate(device_,1,&range);
        if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)deviceLost_=true;return refuse(Reason::Fence);}
    }
    out.record=clone_->mappedRecord();out.bytes=frame_pixel_ssbo_owned::RecordBytes;out.source=readback.source;
    out.commandGeneration=generation_;out.execution=serial_;return out.record!=nullptr;
}
bool Transaction::release() noexcept {
    Guard guard(*this);if(!guard)return false;
    if(!configured_)return true;
    // A separately configured observer must never free another recording's
    // Clone reference. The cold owner is externally synchronized and exclusive;
    // this check also catches accidental sequential double-controller use.
    if(!armed_ && clone_->referenced())return false;
    // Error/refusal never loosens lifetime. A completed marker alone leaves the
    // reusable recording referencing owned objects. Device idle alone does too.
    if(armed_ && (!retired_ || (!quiescent_ && !deviceLost_)))return false;
    const auto result=clone_->release({!armed_ || retired_,!armed_ || quiescent_ || deviceLost_});
    if(result.status!=frame_pixel_ssbo_owned::Status::Ok)return false;
    config_.dispatch.destroyFence(device_,fence_,nullptr);fence_=VK_NULL_HANDLE;configured_=false;return true;
}
} // namespace frame_pixel_ssbo_transaction
