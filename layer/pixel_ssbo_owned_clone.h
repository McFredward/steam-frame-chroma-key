#pragma once

// Cold owned-object construction only. Nothing in this component is linked to
// the receiver, records a command, waits for a queue, or authorizes activation.
#include "pixel_ssbo_clone_contract.h"

namespace frame_pixel_ssbo_owned {
constexpr VkDeviceSize RecordBytes=128, MaximumAllocation=8u*1024u*1024u;
constexpr std::size_t MaximumProbeBytes=32u*1024u;
enum class Status : std::uint8_t {Ok,InvalidInput,MissingProc,Limits,VulkanFailure,UnsafeToRelease,AlreadyPrepared};
enum class Stage : std::uint8_t {None,Set0,Set1,Layout,RenderPass,Vertex,Fragment,Pipeline,Buffer,Allocate,Bind,Map,Flush,Pool,Set};
struct Result {Status status=Status::Ok;Stage stage=Stage::None;VkResult driver=VK_SUCCESS;};
struct Preparation {
    VkDevice device=VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr downstream=nullptr;
    // Actual queried properties of this device, supplied by the cold caller.
    VkPhysicalDeviceLimits limits{};
    VkPhysicalDeviceMemoryProperties memory{};
    const frame_pixel_ssbo_clone::Recipe* recipe=nullptr;
    // Explicit private transformer artifact and its independently pinned digest.
    // This validates artifact transport, not native pipeline compatibility.
    // For count1 the recipe's separately verified interface additionally pins
    // this exact canonical original-invocation SSBO artifact, not an arbitrary
    // caller-supplied digest. Original array3 API behavior remains unchanged.
    const std::uint32_t* fragment=nullptr;
    std::size_t fragmentBytes=0;
    std::array<std::uint8_t,32> fragmentSha256{};
};
struct ReleaseProof {bool allRecordingsRetired=false,deviceQuiescentOrLost=false;};

// One externally synchronized cold owner. Set markReferenced BEFORE exposing
// these handles to any recorded command. Even a completed fence does not retire
// a reusable recording. No implicit destructor calls Vulkan on an unknown thread.
class Clone {
public:
    Clone() noexcept=default;
    Clone(const Clone&)=delete;
    Clone& operator=(const Clone&)=delete;
    ~Clone()=default; // Explicit release with the required lifetime proof.
    Result prepare(const Preparation&) noexcept;
    Result release(ReleaseProof={}) noexcept;
    bool markReferenced() noexcept {if(!ready_)return false;referenced_=true;return true;}
    bool ready() const noexcept{return ready_;}
    bool referenced() const noexcept{return referenced_;}
    VkPipeline pipeline() const noexcept{return ready_?pipeline_:VK_NULL_HANDLE;}
    VkPipelineLayout layout() const noexcept{return ready_?layout_:VK_NULL_HANDLE;}
    VkDescriptorSet set1() const noexcept{return ready_?set_:VK_NULL_HANDLE;}
    VkBuffer recordBuffer() const noexcept{return ready_?buffer_:VK_NULL_HANDLE;}
    const void* mappedRecord() const noexcept{return ready_?mapped_:nullptr;}
    bool hostCoherent() const noexcept{return ready_ && coherent_;}
    VkDeviceMemory recordMemory() const noexcept{return ready_?memory_:VK_NULL_HANDLE;}
    VkDeviceSize allocationBytes() const noexcept{return ready_?allocationBytes_:0;}
    // Immutable facts actually used to construct these owned objects, not a
    // second caller recipe or a native lifetime/compatibility lease.
    const frame_pixel_ssbo_clone::Recipe* preparedRecipe() const noexcept{return ready_?&recipe_:nullptr;}
private:
    struct Procs {
#define PROC(name) PFN_vk##name name=nullptr;
        PROC(CreateDescriptorSetLayout) PROC(DestroyDescriptorSetLayout)
        PROC(CreatePipelineLayout) PROC(DestroyPipelineLayout)
        PROC(CreateRenderPass) PROC(CreateRenderPass2) PROC(DestroyRenderPass)
        PROC(CreateShaderModule) PROC(DestroyShaderModule)
        PROC(CreateGraphicsPipelines) PROC(DestroyPipeline)
        PROC(CreateBuffer) PROC(DestroyBuffer) PROC(GetBufferMemoryRequirements)
        PROC(AllocateMemory) PROC(FreeMemory) PROC(BindBufferMemory)
        PROC(MapMemory) PROC(UnmapMemory) PROC(FlushMappedMemoryRanges)
        PROC(CreateDescriptorPool) PROC(DestroyDescriptorPool)
        PROC(AllocateDescriptorSets) PROC(UpdateDescriptorSets)
#undef PROC
    } procs_{};
    frame_pixel_ssbo_clone::Recipe recipe_{};
    void destroyUnreferenced() noexcept;
    VkDevice device_=VK_NULL_HANDLE;
    VkDescriptorSetLayout set0_=VK_NULL_HANDLE,set1_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkRenderPass renderPass_=VK_NULL_HANDLE;
    VkShaderModule vertex_=VK_NULL_HANDLE,fragment_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkDescriptorPool pool_=VK_NULL_HANDLE;
    VkDescriptorSet set_=VK_NULL_HANDLE;
    VkBuffer buffer_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    void* mapped_=nullptr;
    VkDeviceSize allocationBytes_=0;
    bool ready_=false,referenced_=false,coherent_=false;
};
} // namespace frame_pixel_ssbo_owned
