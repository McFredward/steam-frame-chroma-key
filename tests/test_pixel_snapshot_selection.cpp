// Causal production LiveTracker controls for immutable capture selection.
// Synthetic original shader metadata is pinned only inside this offline fixture;
// no native shader, Vulkan execution, or Frame association is claimed.
#include "layer/pixel_snapshot_selection.h"
#include <cstdlib>
#include <iostream>
#include <memory>

namespace frame_pixel_capture {
struct TestAccess {
    static void pinSyntheticModule(LiveTracker& tracker,std::uint64_t device,VkShaderModule module) {
        auto* shader=tracker.shaders_.find(device,frame_bindings::handle(module));
        if(!shader || !shader->complete)std::abort();
        shader->hash=DistortionFragmentHash;
    }
};
}
namespace {
using namespace frame_pixel_capture;
using frame_pixel_selection::matchesSubmit;
using frame_pixel_selection::matchesRetirement;
unsigned checks=0;
void require(bool yes,const char* reason) {
    ++checks;if(!yes){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}
}
template<class T>T h(std::uint64_t value) {
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T>T info(VkStructureType type) { T value{};value.sType=type;return value; }
constexpr std::uint64_t device=1;
const auto sl=h<VkDescriptorSetLayout>(10);
const auto pl=h<VkPipelineLayout>(20);
const auto descriptorPool=h<VkDescriptorPool>(30);
const auto ds=h<VkDescriptorSet>(40);
const auto pool=h<VkCommandPool>(50);
const auto cb=h<VkCommandBuffer>(60);
const auto image=h<VkImage>(70);
const auto memory=h<VkDeviceMemory>(80);
const auto view=h<VkImageView>(90);
const auto vs=h<VkShaderModule>(100),fs=h<VkShaderModule>(101);
const auto pipeline=h<VkPipeline>(110);
const std::uint32_t ownedCode[]={0x07230203,0x00010000,0,8,0,0x00010000};

struct Fixture {
    LiveTracker tracker;
    VkCommandPoolCreateInfo poolInfo=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    VkCommandBufferAllocateInfo allocation=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    VkCommandBufferBeginInfo beginInfo=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    Fixture() {
        require(tracker.observe([&](auto& core) {
            auto im=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
            im.imageType=VK_IMAGE_TYPE_2D;im.format=VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
            im.extent={1152,4608,1};im.mipLevels=im.arrayLayers=1;im.samples=VK_SAMPLE_COUNT_1_BIT;
            im.tiling=VK_IMAGE_TILING_OPTIMAL;im.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            im.sharingMode=VK_SHARING_MODE_EXCLUSIVE;core.createImage(device,image,im);
            auto mem=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            mem.allocationSize=32*1024*1024;core.allocateMemory(device,memory,mem);core.bindImageMemory(device,image,memory,0);
            auto vi=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=im.format;
            vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};core.createView(device,view,vi);
            VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
            auto layout=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
            layout.bindingCount=1;layout.pBindings=&binding;core.createSetLayout(device,sl,layout);
            auto pipelineLayout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
            pipelineLayout.setLayoutCount=1;pipelineLayout.pSetLayouts=&sl;core.createPipelineLayout(device,pl,pipelineLayout);
            auto dp=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            dp.maxSets=1;core.createPool(device,descriptorPool,dp);
            auto sets=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            sets.descriptorPool=descriptorPool;sets.descriptorSetCount=1;sets.pSetLayouts=&sl;core.allocateSets(device,sets,&ds);
            VkDescriptorImageInfo ii{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            write.dstSet=ds;write.dstBinding=9;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&ii;
            core.updateSets(device,1,&write,0,nullptr);
        }),"seed actual bounded Core resource/descriptor graph");
        auto shader=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        shader.codeSize=sizeof(ownedCode);shader.pCode=ownedCode;
        require(tracker.createShader(device,vs,shader) && tracker.createShader(device,fs,shader),"observe original synthetic shader modules");
        TestAccess::pinSyntheticModule(tracker,device,fs);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[0].pName="VS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;stages[1].pName="PS_DISTORT";
        auto graphics=info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        graphics.stageCount=2;graphics.pStages=stages;graphics.layout=pl;
        require(tracker.createPipeline(device,pipeline,graphics),"observe eligible fixture pipeline");
        poolInfo.queueFamilyIndex=3;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        createPoolAndCommand();begin();
    }
    void createPoolAndCommand() {
        require(tracker.createCommandPool(device,pool,poolInfo),"observe ordinary native command pool");
        require(tracker.allocateCommands(device,allocation,&cb),"observe primary native allocation");
    }
    void begin() {
        require(tracker.beginCommand(device,cb,beginInfo,true),"native successful reusable begin creates recording generation");
        require(tracker.bindPipeline(device,cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline),"bind observed graphics pipeline");
        require(tracker.observe([&](auto& core){core.bindSets(device,cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds,0,nullptr);}),"bind original resource set");
    }
    Candidate capture() {
        Candidate selected;
        require(tracker.beginRenderPass(device,cb,VK_SUBPASS_CONTENTS_INLINE),"begin actual tracked primary pass");
        require(tracker.draw(device,cb),"eligible draw selects exact resource state");
        require(tracker.endRenderPass(device,cb,selected),"end native pass freezes candidate identity");
        require(tracker.endCommand(device,cb,true),"native successful command end");
        require(selected.command==cb && selected.pool==pool && selected.commandGeneration && selected.poolGeneration && selected.serial,
                "selected candidate has original complete command/pool identities");
        return selected;
    }
};

void discardedBeforeFirstSubmit() {
    auto f=std::make_unique<Fixture>();
    const auto original=f->capture();
    const auto retirement=f->tracker.prepareRetirement(device,cb);
    require(matchesRetirement(original,retirement),"pre-reset ticket belongs to original unsubmitted capture");
    require(f->tracker.resetCommand(device,cb),"native discards original recording before any submit");
    f->begin();const auto replacement=f->capture();
    require(original.command==replacement.command && original.commandGeneration!=replacement.commandGeneration && original.serial!=replacement.serial,
            "same API handle now owns observably different recording and acquisition serial");
    const auto submit=f->tracker.prepareSubmit(device,cb);
    require(submit.eligible && f->tracker.acceptSubmit(submit,true),"new native recording has legitimate accepted submit");
    require(!matchesSubmit(original,submit),"accepted replacement cannot complete the discarded original capture");
    require(matchesSubmit(replacement,submit),"same accepted ticket belongs only to replacement candidate");
    const auto currentRetirement=f->tracker.prepareRetirement(device,cb);
    require(!matchesRetirement(original,currentRetirement),"replacement retirement cannot unlock original capture");
    require(matchesRetirement(replacement,currentRetirement),"replacement retirement belongs to replacement");
    require(matchesRetirement(original,retirement),"immutable old pre-call retirement remains original");
    require(!f->tracker.poisoned(),"ordinary reset/reuse is not lost tracker state");
}

void delayedNativeSubmitReturn() {
    auto f=std::make_unique<Fixture>();const auto original=f->capture();
    const auto before=f->tracker.prepareSubmit(device,cb);
    const auto retireBefore=f->tracker.prepareRetirement(device,cb);
    require(before.eligible && matchesSubmit(original,before),"save exact original ticket before native queue call");
    // Model the valid native-completion race: another thread sees its native
    // fence complete and resets before the original QueueSubmit call returns.
    require(f->tracker.resetCommand(device,cb),"completed original generation may reset before delayed return");
    f->begin();const auto replacement=f->capture();
    require(f->tracker.acceptSubmit(before,true),"pre-call original ticket remains accepted after native completion/reset race");
    require(matchesSubmit(original,before),"original selection accepts its old immutable ticket after reset");
    require(!matchesSubmit(replacement,before),"old accepted ticket never completes replacement selection");
    require(matchesRetirement(original,retireBefore),"original pre-call retirement survives reset race");
    require(!matchesRetirement(replacement,retireBefore),"old retirement never retires newer generation");
    require(!f->tracker.acceptSubmit(before,true),"repeated acceptance cannot duplicate original completion authority");
}

void poolRetirementAndRecreation() {
    auto f=std::make_unique<Fixture>();const auto original=f->capture();
    const auto before=f->tracker.prepareRetirement(device,cb);
    require(matchesRetirement(original,before),"old pool command retirement initially matches");
    require(f->tracker.resetCommandPool(device,pool),"native pool reset discards every recording");
    f->begin();const auto afterReset=f->capture();
    const auto resetTicket=f->tracker.prepareRetirement(device,cb);
    require(afterReset.poolGeneration==original.poolGeneration && afterReset.commandGeneration!=original.commandGeneration,
            "pool reset keeps pool lifetime but changes recording generation");
    require(!matchesRetirement(original,resetTicket) && matchesRetirement(afterReset,resetTicket),"pool-reset replacement cannot retire old recording");
    require(f->tracker.destroyCommandPool(device,pool),"native pool destruction retires original allocation lifetime");
    require(!f->tracker.prepareRetirement(device,cb).eligible,"destroyed pool no longer offers a retirement ticket");
    f->createPoolAndCommand();f->begin();const auto replacement=f->capture();
    const auto replacementTicket=f->tracker.prepareRetirement(device,cb);
    require(replacement.pool==original.pool && replacement.poolGeneration!=original.poolGeneration,
            "recycled numeric pool handle has a genuinely new lifetime generation");
    require(!matchesRetirement(original,replacementTicket) && matchesRetirement(replacement,replacementTicket),"recycled pool and command cannot retire historical copy");
    auto transplanted=before;transplanted.poolGeneration=replacementTicket.poolGeneration;
    require(!matchesRetirement(original,transplanted),"even matching command scalars cannot substitute another pool generation");
}

void boundedMismatchControls() {
    auto f=std::make_unique<Fixture>();const auto candidate=f->capture();
    const auto submit=f->tracker.prepareSubmit(device,cb);
    const auto retirement=f->tracker.prepareRetirement(device,cb);
    require(matchesSubmit(candidate,submit) && matchesRetirement(candidate,retirement),"controls start from real production tickets");
    for(unsigned i=0;i<7;++i) {
        auto changed=submit;
        switch(i) {
        case 0:changed.device++;break;case 1:changed.command=h<VkCommandBuffer>(999);break;
        case 2:changed.commandGeneration++;break;case 3:changed.serial++;break;
        case 4:changed.queueFamily++;break;case 5:changed.eligible=false;break;case 6:changed.commandGeneration=0;break;
        }
        require(!matchesSubmit(candidate,changed),"mismatched submission never reaches selected readback");
    }
    for(unsigned i=0;i<7;++i) {
        auto changed=retirement;
        switch(i) {
        case 0:changed.device++;break;case 1:changed.command=h<VkCommandBuffer>(999);break;
        case 2:changed.commandGeneration++;break;case 3:changed.pool=h<VkCommandPool>(999);break;
        case 4:changed.poolGeneration++;break;case 5:changed.eligible=false;break;case 6:changed.poolGeneration=0;break;
        }
        require(!matchesRetirement(candidate,changed),"mismatched retirement never unlocks selected readback");
    }
    require(!matchesSubmit(Candidate{},submit) && !matchesRetirement(Candidate{},retirement),"empty selection cannot consume valid current tickets");
}
}
int main() {
    discardedBeforeFirstSubmit();delayedNativeSubmitReturn();poolRetirementAndRecreation();boundedMismatchControls();
    std::cout<<"{\"checks\":"<<checks<<",\"causal_same_handle_reuse\":true,\"delayed_original_submit_race\":true,\"pool_retirement\":true,\"native_gpu_or_frame_tested\":false}\n";
}
