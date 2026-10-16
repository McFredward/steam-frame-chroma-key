#pragma once

// OFFLINE foundation only. Not linked to the receiver and not an activation
// policy. Callbacks below represent synchronously intercepted public API events;
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
    void nextSubpass(VkCommandBuffer) noexcept;
    void executeSecondary(VkCommandBuffer) noexcept;
    void unknownRoute(VkCommandBuffer) noexcept;
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
private:
    inline static std::atomic<std::uint64_t> epochs_{1};
    static std::uint64_t reserveEpoch() noexcept;
    class Guard {Transaction& owner_; bool acquired_; public:
        explicit Guard(Transaction&) noexcept; ~Guard(); explicit operator bool() const noexcept{return acquired_;}};
    DrawResult replace(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::int32_t,bool) noexcept;
    bool refuse(Reason) noexcept;
    bool matches(VkCommandBuffer) const noexcept;
    bool active(VkCommandBuffer) noexcept;
    bool advance(std::uint64_t&) noexcept;
    bool fenceComplete() noexcept;
    void unknown(VkCommandBuffer,Reason) noexcept;
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
    VkDescriptorSet currentSet0_=VK_NULL_HANDLE;
    Cohort currentSource_{},selectedSource_{};Id setIdentity_{};SubmitObservation submitObservation_{};
    std::uint64_t epoch_=0,generation_=0,poolGeneration_=0,serial_=0,submitNonce_=0,retireNonce_=0;
    std::uint64_t pendingSubmit_=0,pendingRetirement_=0,drawOrdinal_=0,selectedOrdinal_=0;
    bool configured_=false,recording_=false,armed_=false,reset_=false,inPass_=false;
    const frame_pixel_capture::LiveTracker* adoptedTracker_=nullptr;
    bool prefixValid_=false,matchingPass_=false;
    bool cleanHistory_=true;
    bool selected_=false,scopeEnded_=false,executable_=false,retired_=false;
    bool submitted_=false,pending_=false,complete_=false,quiescent_=false,deviceLost_=false;
    std::atomic_flag lock_=ATOMIC_FLAG_INIT;
    std::atomic<Reason> reason_{Reason::None};
};
} // namespace frame_pixel_ssbo_transaction
