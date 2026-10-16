#pragma once

// Optional manually armed receiver diagnostic foundation; never a production
// presenter or activation authority. Callbacks are synchronous public API events;
// an asynchronously replayed Candidate/Recipe is never a native lifetime lease.
#include "pixel_ssbo_owned_clone.h"
#include <atomic>

namespace frame_pixel_capture { class LiveTracker; }
namespace frame_pixel_ssbo_transaction {
struct TransactionTestAccess;
using Id = frame_pixel_ssbo_clone::Identity;
struct Cohort {
    Id image{}, view{}, memory{};
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkExtent2D extent{};
};
enum class Reason : std::uint8_t {
    None, NotConfigured, InvalidConfiguration, Contention, Identity, GenerationWrap,
    Begin, NotArmed, MissingReset, Scope, Prefix, Pipeline, Source, EmptyDraw,
    LaterDraw, Subpass, Secondary, UnknownRoute, CommandEnd, SubmitShape, SubmitExtension, SubmitBounds,
    UnknownQueue, Overlap, NativeSubmit, MarkerSubmit, Fence, NotRetired,
    CohortMismatch, SourceRetired, UnsafeRelease, AlreadyArmed, DeviceDestroying
};
const char* reasonName(Reason) noexcept;
enum class DrawResult : std::uint8_t { ForwardOriginal, ReplacedOriginal };
enum class Resource : std::uint8_t { Image, View, Memory, DescriptorSet, Pipeline, RenderPass };
enum class Retirement : std::uint8_t { ResetCommand, BeginCommand, FreeCommand, ResetPool, DestroyPool };
// Copied callback provenance only. No member grants command, object lifetime,
// source-frame or eye authority. Numeric refusal/token enums above stay stable.
enum class Operation : std::uint16_t {
    Unspecified,BeginCommandBuffer,EndCommandBuffer,CmdBindPipeline,CmdBindDescriptorSets,CmdPushConstants,
    CmdBeginRenderPass,CmdBeginRenderPass2,CmdBeginRenderPass2KHR,CmdNextSubpass,CmdNextSubpass2,CmdNextSubpass2KHR,
    CmdEndRenderPass,CmdEndRenderPass2,CmdEndRenderPass2KHR,CmdDraw,CmdDrawIndexed,CmdDrawIndirect,CmdDrawIndexedIndirect,
    CmdDrawIndirectCount,CmdDrawIndirectCountKHR,CmdDrawIndirectCountAMD,CmdDrawIndexedIndirectCount,CmdDrawIndexedIndirectCountKHR,CmdDrawIndexedIndirectCountAMD,
    CmdExecuteCommands,CmdBindShadersEXT,CmdDispatch,CmdDispatchIndirect,CmdDispatchBase,CmdDispatchBaseKHR,
    CmdBeginQuery,CmdBeginQueryIndexedEXT,CmdBeginRendering,CmdBeginRenderingKHR,CmdEndRendering,CmdEndRenderingKHR,
    QueueSubmit,QueueSubmit2,QueueSubmit2KHR,DescriptorMutation,DescriptorPoolRetiring,ResourceRetiring,DefinitionRetiring,
    ResetCommandBuffer,FreeCommandBuffers,ResetCommandPool,DestroyCommandPool,DeviceDestroying,CopyRecorded,TargetTicket,Count
};
const char* operationName(Operation) noexcept;
enum class ContextSource : std::uint8_t {Unspecified,Wrapper,RenderPassDecoder,Internal};
enum class RecordingPhase : std::uint8_t {Unavailable,RecordingOutsidePass,OtherPass,BeforeTargetSubpass,TargetSubpassBeforeDraw,TargetSubpassAfterDraw,AfterTargetSubpass,AfterPass,Executable,Retired};
const char* phaseName(RecordingPhase) noexcept;
constexpr std::size_t RecordingContextCapacity=64,CommandContextChains=3,CommandContextHeaders=16;
struct CommandContextChain {
    std::uint32_t rootType=UINT32_MAX;
    std::array<std::uint32_t,CommandContextHeaders> types{};
    std::uint32_t count=0,termination=0;
};
struct CommandContextInput {
    Operation operation=Operation::Unspecified;ContextSource source=ContextSource::Unspecified;
    std::uint32_t detailReason=0,structureType=UINT32_MAX,index=UINT32_MAX,chain=UINT32_MAX,contents=UINT32_MAX,flags=0;
    std::uint64_t object=0;
    bool targetPipeline=false;
    std::array<CommandContextChain,CommandContextChains> chains{};
};
struct RecordingContext {
    CommandContextInput input{};RecordingPhase phase=RecordingPhase::Unavailable;
    Reason before=Reason::None,after=Reason::None;
    std::uint64_t sequence=0,transactionEpoch=0,device=0,command=0,pool=0,commandGeneration=0,poolGeneration=0,drawOrdinal=0;
    std::uint32_t currentSubpass=0,targetSubpass=0,subpassCount=0;
    bool rejected=false,metadataTruncated=false;
};
struct RecordingContextEntry {RecordingContext first{};std::uint64_t count=0,lastSequence=0;};
struct RecordingContexts {
    std::array<RecordingContextEntry,RecordingContextCapacity> entries{};std::uint32_t count=0;
    std::uint64_t attempts=0,overflow=0;
    bool contended=false,firstRefusalAvailable=false;
    RecordingContext firstRefusal{};
};

struct Dispatch {
    PFN_vkCmdPipelineBarrier barrier=nullptr;
    PFN_vkCmdFillBuffer fill=nullptr;
    PFN_vkCmdBindPipeline pipeline=nullptr;
    PFN_vkCmdBindDescriptorSets sets=nullptr;
    PFN_vkCmdDraw draw=nullptr;
    PFN_vkCmdDrawIndexed drawIndexed=nullptr;
    PFN_vkCreateFence createFence=nullptr;
    PFN_vkDestroyFence destroyFence=nullptr;
    PFN_vkResetFences resetFence=nullptr;
    PFN_vkGetFenceStatus fenceStatus=nullptr;
    PFN_vkQueueSubmit submit=nullptr;
    PFN_vkInvalidateMappedMemoryRanges invalidate=nullptr;
};
// Cold known queue identity, observed from GetDeviceQueue/GetDeviceQueue2. One
// exact queue only. The integrator must preserve its native external lock across
// beforeSubmit -> original native submit -> afterSubmit. No queue is inferred.
struct Configuration { Dispatch dispatch{}; };
// Metadata from the INDEPENDENT source-copy owner after its own completed
// same-command readback. These scalars grant no command authorization. Matching
// object handles alone do not identify which reusable execution produced bytes.
struct SourceReadbackMetadata {Cohort source{};VkCommandBuffer command=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;
    std::uint64_t commandGeneration=0,poolGeneration=0,execution=0;};
struct SubmitObservation {std::uint32_t batches=0,batch=UINT32_MAX,commandCount=0,nextType=UINT32_MAX,selectedOccurrences=0;};
struct RecordView {const void* record=nullptr; VkDeviceSize bytes=0; Cohort source{};
    std::uint64_t commandGeneration=0, execution=0;
    // Diagnostic command/resource cohort only: never video frame/eye authority.
};

class Transaction {
    friend struct TransactionTestAccess;
public:
    // Native adoption is exclusively minted by the synchronous full live
    // tracker after a successful original Begin. Consume immediately in that
    // callback; never queue/replay this token or manufacture creation history.
    class RecordingTicket {
        friend class Transaction; friend class frame_pixel_capture::LiveTracker; friend struct TransactionTestAccess;
        const frame_pixel_capture::LiveTracker* tracker_=nullptr;
        std::uint64_t device_=0,commandGeneration_=0,poolGeneration_=0;
        VkCommandBuffer command_=VK_NULL_HANDLE;VkCommandPool pool_=VK_NULL_HANDLE;
        std::uint32_t family_=VK_QUEUE_FAMILY_IGNORED;VkCommandBufferUsageFlags flags_=0;
    public: RecordingTicket() noexcept=default;
    };
    // Snapshot of actual native draw state at the exact hook, NOT permission to
    // emit commands. The transaction independently checks recording, scope,
    // matching copied prefix, reset, owned references and original draw route.
    class TargetTicket {
        friend class Transaction; friend class frame_pixel_capture::LiveTracker; friend struct TransactionTestAccess;
        const frame_pixel_capture::LiveTracker* tracker_=nullptr;
        std::uint64_t device_=0,commandGeneration_=0,poolGeneration_=0,ordinal_=0;
        std::uint32_t subpass_=0;
        VkCommandBuffer command_=VK_NULL_HANDLE;VkCommandPool pool_=VK_NULL_HANDLE;
        Id pipeline_{},layout_{},set0_{};Cohort source_{};
    public: TargetTicket() noexcept=default;
    };
    // Tokens are minted BEFORE the original API call. Their private contents
    // cannot be replaced by a public eligible/all-true Admission structure.
    class SubmitTicket {friend class Transaction; const Transaction* owner_=nullptr;
        std::uint64_t epoch_=0,generation_=0,nonce_=0; public: SubmitTicket() noexcept=default;};
    class RetirementTicket {friend class Transaction; const Transaction* owner_=nullptr;
        std::uint64_t epoch_=0,generation_=0,poolGeneration_=0,nonce_=0;
        Retirement operation_=Retirement::ResetCommand; public: RetirementTicket() noexcept=default;};
    Transaction() noexcept;
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    ~Transaction()=default; // No implicit Vulkan calls or waits.
    // Retain actual public lifecycle history BEFORE a Clone exists. A literal
    // singleQueue=true is deliberately not an input: inspect the ORIGINAL
    // successful DeviceCreate queue create infos and GetDeviceQueue result.
    bool deviceCreated(VkDevice,const VkDeviceCreateInfo&,VkResult) noexcept;
    bool queueRetrieved(VkDevice,std::uint32_t family,std::uint32_t index,VkQueue) noexcept;
    bool configure(frame_pixel_ssbo_owned::Clone&,const Configuration&) noexcept;
    // Exactly one pool/primary CB is tracked. Generation numbers are assigned
    // internally on successful allocation/begin; equal reused handles are not
    // accepted as a later identity. Call original lifecycle APIs normally.
    bool poolCreated(VkCommandPool, std::uint32_t family, VkResult) noexcept;
    bool commandAllocated(VkCommandPool,VkCommandBuffer,VkCommandBufferLevel,VkResult) noexcept;
    bool beginSucceeded(VkCommandBuffer,const VkCommandBufferBeginInfo&,VkResult) noexcept;
    bool adoptRecording(const RecordingTicket&) noexcept;
    bool adoptTargetState(const TargetTicket&) noexcept;
    bool arm(VkCommandBuffer) noexcept;
    bool beginRenderPass(VkCommandBuffer,VkRenderPass,VkSubpassContents) noexcept;
    void pipelineBound(VkCommandBuffer,VkPipeline) noexcept;
    void set0Bound(VkCommandBuffer,VkPipelineLayout,std::uint32_t first,std::uint32_t count,
                   const VkDescriptorSet*,std::uint32_t dynamicOffsets,Id descriptorIdentity) noexcept;
    void pushConstants(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,std::uint32_t offset,
                       std::uint32_t bytes,const void*) noexcept;
    void sourceBound(VkCommandBuffer,const Cohort&) noexcept;
    // Invoke instead of forwarding one original direct vkCmdDraw. Forward the
    // native draw exactly once ONLY when ForwardOriginal is returned. On success
    // the only replacement commands are Set1/probe bind, exact original draw,
    // and the IMMEDIATE original-pipeline restore. Never duplicate that draw.
    DrawResult draw(VkCommandBuffer,std::uint32_t vertices,std::uint32_t instances,
                    std::uint32_t firstVertex,std::uint32_t firstInstance) noexcept;
    DrawResult drawIndexed(VkCommandBuffer,std::uint32_t indices,std::uint32_t instances,
                           std::uint32_t firstIndex,std::int32_t vertexOffset,std::uint32_t firstInstance) noexcept;
    // Invoke AFTER forwarding the original native EndRenderPass (host barrier is outside RP).
    void endRenderPass(VkCommandBuffer) noexcept;
    void endCommand(VkCommandBuffer,VkResult) noexcept;
    void laterDraw(VkCommandBuffer) noexcept;
    // Invoke AFTER the untouched original NextSubpass/NextSubpass2. Only
    // inline transitions inside the complete captured pass are admitted.
    bool nextSubpass(VkCommandBuffer,VkSubpassContents=VK_SUBPASS_CONTENTS_INLINE) noexcept;
    void executeSecondary(VkCommandBuffer) noexcept;
    void observe(VkCommandBuffer,const CommandContextInput&) noexcept;
    void unknownRoute(VkCommandBuffer,const CommandContextInput& = {}) noexcept;
    RecordingContexts recordingContexts() const noexcept;
    // Exact identity retirement notification at entry, before native destruction.
    void resourceRetiring(Resource,Id) noexcept;
    SubmitTicket beforeSubmit(VkQueue,std::uint32_t,const VkSubmitInfo*) noexcept;
    bool afterSubmit(const SubmitTicket&,VkResult originalNativeResult) noexcept;
    RetirementTicket beforeRetirement(Retirement,VkCommandBuffer,VkCommandPool=VK_NULL_HANDLE) noexcept;
    bool afterRetirement(const RetirementTicket&,VkResult originalNativeResult) noexcept;
    // Invoke after the native DeviceWaitIdle returned, or on terminal device
    // loss. It performs no speculative wait and never waits in a destructor.
    void afterDeviceWaitIdle(VkResult) noexcept;
    // Only at original vkDestroyDevice ENTRY, after native device/queues were
    // externally closed to future use and actual idle/lost was observed. This
    // explicit destruction event can retire the last reusable recording;
    // ordinary DeviceWaitIdle alone cannot. No Vulkan call is issued here.
    bool deviceDestroying(VkDevice) noexcept;
    bool publish(const SourceReadbackMetadata& matchingSourceReadback,RecordView&) noexcept;
    bool release() noexcept;
    Reason refusal() const noexcept {return reason_.load(std::memory_order_acquire);}
    // All following scalar diagnostics require the integrator's external synchronization.
    // They are not a coherent snapshot, an admission or native lifetime proof.
    std::uint64_t generation() const noexcept {return generation_;}
    bool retired() const noexcept {return retired_;}
    bool armed() const noexcept {return armed_;}
    VkCommandBuffer command() const noexcept {return command_;}
    VkCommandPool pool() const noexcept {return pool_;}
    VkDescriptorSet descriptorSet() const noexcept {return currentSet0_;}
    Cohort selectedSource() const noexcept {return selectedSource_;} // same original CB callback synchronization
    SubmitObservation submitObservation() const noexcept {return submitObservation_;}
    std::uint64_t drawOrdinal() const noexcept {return drawOrdinal_;}
    std::uint64_t selectedOrdinal() const noexcept {return selectedOrdinal_;}
    std::uint64_t execution() const noexcept {return serial_;}
    std::uint64_t poolGeneration() const noexcept {return poolGeneration_;}
    std::uint32_t targetSubpass() const noexcept {return targetSubpass_;}
    std::uint32_t subpassCount() const noexcept {return subpassCount_;}
    std::uint32_t currentSubpass() const noexcept {return currentSubpass_;}
private:
    inline static std::atomic<std::uint64_t> epochs_{1};
    static std::uint64_t reserveEpoch() noexcept;
    class Guard {Transaction& owner_; bool acquired_,firstRefusal_=false; public:
        explicit Guard(Transaction&) noexcept; ~Guard(); explicit operator bool() const noexcept{return acquired_;}
        bool firstRefusal() const noexcept{return firstRefusal_;}};
    class ContextScope {
        Transaction& owner_;RecordingContext context_{};bool enabled_=false;
    public:
        ContextScope(Transaction&,VkCommandBuffer,const CommandContextInput&,Operation,bool rejected=false) noexcept;
        ~ContextScope();
    };
    void retainContext(RecordingContext) noexcept;
    void firstContext(const RecordingContext&,bool ownsContention=false) noexcept;
    void contextContention(VkCommandBuffer,const CommandContextInput&,bool firstRefusal) noexcept;
    RecordingPhase contextPhase() const noexcept;
    DrawResult replace(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::int32_t,bool) noexcept;
    bool refuse(Reason) noexcept;
    bool matches(VkCommandBuffer) const noexcept;
    bool active(VkCommandBuffer) noexcept;
    bool advance(std::uint64_t&) noexcept;
    bool fenceComplete() noexcept;
    void unknown(VkCommandBuffer,Reason,const CommandContextInput& = {}) noexcept;
    static bool same(Id,Id) noexcept;
    static bool same(const Cohort&,const Cohort&) noexcept;
    Configuration config_{};
    VkDevice device_=VK_NULL_HANDLE;VkQueue queue_=VK_NULL_HANDLE;
    std::uint32_t family_=VK_QUEUE_FAMILY_IGNORED;
    frame_pixel_ssbo_owned::Clone* clone_=nullptr;
    frame_pixel_ssbo_clone::Set0Definition set0_{};
    frame_pixel_ssbo_clone::LayoutDefinition prefix_{};
    std::array<Id,6> provenance_{};
    VkFence fence_=VK_NULL_HANDLE;
    VkCommandPool pool_=VK_NULL_HANDLE; VkCommandBuffer command_=VK_NULL_HANDLE;
    VkPipeline original_=VK_NULL_HANDLE,current_=VK_NULL_HANDLE;
    VkPipelineLayout originalLayout_=VK_NULL_HANDLE; VkRenderPass pass_=VK_NULL_HANDLE;
    std::uint32_t targetSubpass_=0,subpassCount_=0,currentSubpass_=0;
    VkDescriptorSet currentSet0_=VK_NULL_HANDLE;
    Cohort currentSource_{},selectedSource_{};Id setIdentity_{};SubmitObservation submitObservation_{};
    std::uint64_t epoch_=0,generation_=0,poolGeneration_=0,serial_=0,submitNonce_=0,retireNonce_=0;
    std::uint64_t pendingSubmit_=0,pendingRetirement_=0,drawOrdinal_=0,selectedOrdinal_=0;
    bool configured_=false,recording_=false,armed_=false,reset_=false,inPass_=false;
    const frame_pixel_capture::LiveTracker* adoptedTracker_=nullptr;
    bool prefixValid_=false,matchingPass_=false,targetPass_=false;
    bool cleanHistory_=true;
    bool selected_=false,scopeEnded_=false,executable_=false,retired_=false;
    bool submitted_=false,pending_=false,complete_=false,quiescent_=false,deviceLost_=false;
    std::atomic_flag lock_=ATOMIC_FLAG_INIT;
    std::atomic<Reason> reason_{Reason::None};
    struct ContextSlot {RecordingContext first{};std::atomic<std::uint64_t> count{0},lastSequence{0};};
    // One producer holds Guard; consumers only read immutable published slots.
    // No consumer takes Guard, and no callback spins waiting for a reader.
    std::array<ContextSlot,RecordingContextCapacity> contexts_{};
    std::atomic<std::uint32_t> contextCount_{0};
    std::atomic<std::uint64_t> contextAttempts_{0},contextOverflow_{0};
    std::atomic<bool> contextContended_{false};
    RecordingContext firstContext_{};std::atomic<unsigned> firstContextState_{0};
};
} // namespace frame_pixel_ssbo_transaction
