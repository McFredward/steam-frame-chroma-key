#pragma once

// Optional diagnostic integration only. The generated original-output probe
// preserves native color; this is not a mask presenter or frame/eye authority.
#include "pixel_ssbo_definition_journal.h"
#include "pixel_ssbo_profile_transform.h"
#include "pixel_ssbo_draw_transaction.h"
#include "pixel_readback.h"
#include <mutex>

namespace frame_pixel_capture { class LiveTracker; struct Candidate; }
namespace frame_pixel_ssbo_receiver {
namespace tx=frame_pixel_ssbo_transaction;
enum class Refusal : unsigned {None,Device,Queue,Shader,Clone,Configure,ColdCancelled,
    Recording,Target,CopyCohort,SubmitShape,Execution,DescriptorMutation,UnknownRoute,Release,Record};
const char* refusalName(Refusal) noexcept;
struct Snapshot {
    Refusal refusal=Refusal::None;
    unsigned transaction=0,cloneStatus=0,cloneStage=0;int cloneDriver=0;
    std::uint32_t queueCreateInfos=0,queueCount=0,queueFamily=VK_QUEUE_FAMILY_IGNORED;
    bool profile=false,ready=false,learned=false,adopted=false,replaced=false,copy=false,queueKnown=false;
    std::uint32_t retrievedFamily=VK_QUEUE_FAMILY_IGNORED,retrievedIndex=UINT32_MAX;std::uint64_t retrievedQueue=0;
    unsigned drawKind=0,submitRoute=0;std::array<std::uint32_t,5> drawArgs{};
    tx::SubmitObservation submit{};std::uint64_t commandGeneration=0,poolGeneration=0,drawOrdinal=0;
    std::uint64_t sourceExecutions=0,pairedExecution=0,cancellationEpoch=0;
};
class Bridge {
public:
    Bridge() noexcept=default;
    Bridge(const Bridge&)=delete;Bridge& operator=(const Bridge&)=delete;
    ~Bridge()=default; // No implicit waits or Vulkan destruction.
    void deviceCreated(VkDevice,const VkDeviceCreateInfo&,PFN_vkGetDeviceProcAddr,
                       const VkPhysicalDeviceProperties&,const VkPhysicalDeviceMemoryProperties&) noexcept;
    void queueRetrieved(std::uint32_t,std::uint32_t,VkQueue,bool ordinary=true) noexcept;
    void shaderCreated(const VkShaderModuleCreateInfo&,frame_pixel_ssbo_journal::Journal&) noexcept;
    void prepare(const frame_pixel_ssbo_journal::Ready&) noexcept;
    void tick(frame_pixel_ssbo_journal::Journal&) noexcept;
    // Cancellation at original native destroy ENTRY, including cold preparation.
    void definitionRetiring(frame_pixel_ssbo_journal::ObjectKind,std::uint64_t) noexcept;
    void resourceRetiring(tx::Resource,std::uint64_t) noexcept;
    void descriptorMutation(VkDescriptorSet=VK_NULL_HANDLE) noexcept;
    void descriptorPoolRetiring(frame_pixel_capture::LiveTracker&,std::uint64_t,VkDescriptorPool) noexcept;
    void learn(const frame_pixel_capture::Candidate&) noexcept;
    void forget(VkCommandBuffer,VkCommandPool) noexcept;
    void begin(frame_pixel_capture::LiveTracker&,std::uint64_t,VkCommandBuffer,bool armed) noexcept;
    bool tracks(VkCommandBuffer c) const noexcept{return matches(c);}
    void pipeline(VkCommandBuffer,VkPipeline) noexcept;
    void beginPass(VkCommandBuffer,VkRenderPass,VkSubpassContents) noexcept;
    void endPass(VkCommandBuffer) noexcept;
    void endCommand(VkCommandBuffer,VkResult) noexcept;
    bool draw(frame_pixel_capture::LiveTracker&,std::uint64_t,VkCommandBuffer,
              std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::int32_t,bool) noexcept;
    void unknown(VkCommandBuffer) noexcept;
    bool deferCopy(const frame_pixel_capture::Candidate&) noexcept;
    void copyRecorded(const frame_pixel_capture::Candidate&) noexcept;
    tx::Transaction::SubmitTicket beforeSubmit(VkQueue,std::uint32_t,const VkSubmitInfo*) noexcept;
    void afterSubmit(const tx::Transaction::SubmitTicket&,VkQueue,std::uint32_t,const VkSubmitInfo*,VkResult) noexcept;
    void unsupportedSubmit(VkQueue,std::uint32_t,const VkSubmitInfo2*) noexcept;
    struct RetirementTicket {tx::Transaction::RetirementTicket native{};bool selected=false;};
    RetirementTicket beforeRetirement(tx::Retirement,VkCommandBuffer,VkCommandPool=VK_NULL_HANDLE) noexcept;
    void afterRetirement(const RetirementTicket&,VkResult) noexcept;
    bool publish(const frame_pixels::View&,tx::RecordView&) noexcept;
    bool release(VkResult actualIdleOrLost) noexcept;
    Snapshot snapshot() const noexcept;
private:
    bool usable() const noexcept;
    bool matches(VkCommandBuffer) const noexcept;
    void refuse(Refusal) noexcept;
    tx::Cohort cohort(const frame_pixel_capture::Candidate&) const noexcept;
    VkDevice device_=VK_NULL_HANDLE;PFN_vkGetDeviceProcAddr gdpa_=nullptr;
    VkPhysicalDeviceProperties properties_{};VkPhysicalDeviceMemoryProperties memory_{};
    std::mutex cold_;
    frame_pixel_ssbo_profile::Artifact artifact_{};
    frame_pixel_ssbo_owned::Clone clone_;
    tx::Transaction transaction_;
    tx::SourceReadbackMetadata source_{};
    frame_pixel_ssbo_journal::Ready pending_{};bool pendingReady_=false;
    std::atomic<std::uint64_t> learnedCommand_{0},learnedPool_{0},adoptedCommand_{0},adoptedPool_{0};
    std::atomic<std::uint64_t> originalPipeline_{0},originalLayout_{0},originalPass_{0};
    std::atomic<std::uint64_t> cancellationEpoch_{0},sourceExecutions_{0},pairedExecution_{0},selectedSet_{0},descriptorEpoch_{0},resourceEpoch_{0};
    std::array<std::atomic<std::uint64_t>,3> selectedSourceValues_{},selectedSourceGenerations_{};
    std::atomic<unsigned> drawKind_{0},submitRoute_{0};std::array<std::atomic<std::uint32_t>,5> drawArgs_{};
    std::atomic<std::uint32_t> submitBatches_{0},submitBatch_{UINT32_MAX},submitCommands_{0},submitNext_{UINT32_MAX},submitOccurrences_{0};
    std::atomic<std::uint64_t> commandGeneration_{0},poolGeneration_{0},drawOrdinal_{0};
    std::atomic<unsigned> reason_{0},cloneStatus_{0},cloneStage_{0};std::atomic<int> cloneDriver_{0};
    std::atomic<std::uint32_t> queueCreateInfos_{0},queueCount_{0},queueFamily_{VK_QUEUE_FAMILY_IGNORED},retrievedFamily_{VK_QUEUE_FAMILY_IGNORED},retrievedIndex_{UINT32_MAX};
    std::atomic<std::uint64_t> retrievedQueue_{0};
    std::atomic<bool> queueKnown_{false},profile_{false},ready_{false},preparing_{false},adopted_{false},replaced_{false},copy_{false},retired_{false},targetBound_{false},sourceBound_{false};
};
} // namespace frame_pixel_ssbo_receiver
