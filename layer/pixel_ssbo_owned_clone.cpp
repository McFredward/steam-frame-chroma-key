#include "pixel_ssbo_owned_clone.h"

namespace frame_pixel_ssbo_owned {
namespace {
bool limitsPermit(const frame_pixel_ssbo_clone::Recipe& recipe,const VkPhysicalDeviceLimits& limits) noexcept {
    if(limits.maxBoundDescriptorSets<2 || limits.maxStorageBufferRange<RecordBytes ||
       !limits.maxDescriptorSetStorageBuffers || !limits.maxPerStageDescriptorStorageBuffers)return false;
    for(std::uint32_t i=0;i<recipe.layout.count;++i){const auto& r=recipe.layout.ranges[i];
        if(r.offset>limits.maxPushConstantsSize || r.size>limits.maxPushConstantsSize-r.offset)return false;
    }
    // Conservative cold budget: count all admitted Set0 resources, including
    // unused stage bindings. Refusing a larger envelope cannot alter native work.
    std::uint32_t storage=1,fragmentStorage=1,fragmentResources=1;
    for(std::uint32_t i=0;i<recipe.set0.count;++i){const auto& b=recipe.set0.bindings[i];
        const bool stored=b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        if(stored)storage+=b.descriptorCount;
        if(b.stageFlags&VK_SHADER_STAGE_FRAGMENT_BIT){fragmentResources+=b.descriptorCount;if(stored)fragmentStorage+=b.descriptorCount;}
    }
    return storage<=limits.maxDescriptorSetStorageBuffers && fragmentStorage<=limits.maxPerStageDescriptorStorageBuffers &&
        fragmentResources<=limits.maxPerStageResources;
}
bool validRecipe(const frame_pixel_ssbo_clone::Recipe& r) noexcept {
    return r.definitionComplete && r.feature.complete && r.feature.enabled && r.set0.complete &&
        r.layout.complete && r.renderPass.complete && r.renderPass.bounded() && r.renderPass.subpass(r.graphics.info.subpass) &&
        r.vertex.complete && r.fragment.complete && r.graphics.complete &&
        r.set0.count<=frame_pixel_ssbo_clone::MaxBindings && r.layout.count<=frame_pixel_ssbo_clone::MaxPushRanges &&
        r.vertex.bytes==9460 && r.fragment.bytes==6620 && !r.nativeCompatibilityProven &&
        !r.nativeLifetimeProven && !r.runtimeActivationAuthorized;
}
bool profileMatches(const Preparation& p) noexcept {
    namespace clone=frame_pixel_ssbo_clone;
    const auto& r=*p.recipe;
    if(!r.set0.probeInterface.matches(p.fragment,p.fragmentBytes))return false;
    VkDescriptorSetLayoutCreateInfo info{};clone::Set0Definition checked;clone::Refusal why;
    if(!r.set0.view(info) || !clone::captureSet0(info,checked,why,r.set0.probeInterface))return false;
    if(!r.set0.probeInterface.specializedCount1())return true;
    // Specialized bytes prove the probe interface, never the source identity or
    // native compatibility. Independently rehash both owned original modules.
    VkShaderModuleCreateInfo module{};module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    clone::ShaderDefinition original;
    module.codeSize=r.vertex.bytes;module.pCode=r.vertex.words.data();
    if(!clone::captureShader(module,clone::ShaderRole::Vertex,original,why))return false;
    module.codeSize=r.fragment.bytes;module.pCode=r.fragment.words.data();
    return clone::captureShader(module,clone::ShaderRole::Fragment,original,why);
}
}

Result Clone::prepare(const Preparation& input) noexcept {
    if(device_ || ready_)return {Status::AlreadyPrepared,Stage::None,VK_SUCCESS};
    if(!input.recipe)return {Status::InvalidInput,Stage::None,VK_SUCCESS};
    // One cold snapshot is the sole source for all validation and construction.
    // It remains owned by the prepared clone after caller storage is reused.
    const frame_pixel_ssbo_clone::Recipe snapshot=*input.recipe;
    Preparation p=input;p.recipe=&snapshot;
    if(!p.device || !p.downstream || !p.recipe || !validRecipe(*p.recipe) || !p.fragment ||
       p.fragmentBytes<20 || p.fragmentBytes>MaximumProbeBytes || p.fragmentBytes%4 || p.fragment[0]!=0x07230203u ||
       p.memory.memoryTypeCount==0 || p.memory.memoryTypeCount>VK_MAX_MEMORY_TYPES ||
       p.memory.memoryHeapCount==0 || p.memory.memoryHeapCount>VK_MAX_MEMORY_HEAPS)
        return {Status::InvalidInput,Stage::None,VK_SUCCESS};
    frame_receiver::Sha256 digest;digest.update(p.fragment,p.fragmentBytes);
    if(digest.finish()!=p.fragmentSha256)return {Status::InvalidInput,Stage::Fragment,VK_SUCCESS};
    if(!profileMatches(p))return {Status::InvalidInput,Stage::Fragment,VK_SUCCESS};
    if(!limitsPermit(*p.recipe,p.limits))return {Status::Limits,Stage::None,VK_SUCCESS};
    Procs functions{};
#define LOAD(name) functions.name=reinterpret_cast<PFN_vk##name>(p.downstream(p.device,"vk" #name));if(!functions.name)return {Status::MissingProc,Stage::None,VK_SUCCESS};
    LOAD(CreateDescriptorSetLayout) LOAD(DestroyDescriptorSetLayout)
    LOAD(CreatePipelineLayout) LOAD(DestroyPipelineLayout) LOAD(DestroyRenderPass)
    LOAD(CreateShaderModule) LOAD(DestroyShaderModule) LOAD(CreateGraphicsPipelines) LOAD(DestroyPipeline)
    LOAD(CreateBuffer) LOAD(DestroyBuffer) LOAD(GetBufferMemoryRequirements) LOAD(AllocateMemory) LOAD(FreeMemory)
    LOAD(BindBufferMemory) LOAD(MapMemory) LOAD(UnmapMemory) LOAD(FlushMappedMemoryRanges)
    LOAD(CreateDescriptorPool) LOAD(DestroyDescriptorPool) LOAD(AllocateDescriptorSets) LOAD(UpdateDescriptorSets)
#undef LOAD
    if(snapshot.renderPass.route2){
        functions.CreateRenderPass2=reinterpret_cast<PFN_vkCreateRenderPass2>(p.downstream(p.device,"vkCreateRenderPass2"));
        if(!functions.CreateRenderPass2)functions.CreateRenderPass2=reinterpret_cast<PFN_vkCreateRenderPass2>(p.downstream(p.device,"vkCreateRenderPass2KHR"));
        if(!functions.CreateRenderPass2)return {Status::MissingProc,Stage::RenderPass,VK_SUCCESS};
    }else{
        functions.CreateRenderPass=reinterpret_cast<PFN_vkCreateRenderPass>(p.downstream(p.device,"vkCreateRenderPass"));
        if(!functions.CreateRenderPass)return {Status::MissingProc,Stage::RenderPass,VK_SUCCESS};
    }
    recipe_=snapshot;device_=p.device;procs_=functions;
    const auto failure=[&](Stage stage,VkResult result) noexcept {
        destroyUnreferenced();return Result{Status::VulkanFailure,stage,result};
    };
    const auto invalid=[&](Stage stage) noexcept {
        destroyUnreferenced();return Result{Status::InvalidInput,stage,VK_SUCCESS};
    };
    const auto& r=recipe_;
    VkDescriptorSetLayoutCreateInfo set0Info{};
    if(!r.set0.view(set0Info))return invalid(Stage::Set0);
    auto result=procs_.CreateDescriptorSetLayout(device_,&set0Info,nullptr,&set0_);
    if(result!=VK_SUCCESS){set0_=VK_NULL_HANDLE;return failure(Stage::Set0,result);}
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo set1Info{};set1Info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set1Info.bindingCount=1;set1Info.pBindings=&binding;
    result=procs_.CreateDescriptorSetLayout(device_,&set1Info,nullptr,&set1_);
    if(result!=VK_SUCCESS){set1_=VK_NULL_HANDLE;return failure(Stage::Set1,result);}
    frame_pixel_ssbo_clone::LayoutView layoutView{};
    if(!r.layoutView(set0_,set1_,layoutView))return invalid(Stage::Layout);
    result=procs_.CreatePipelineLayout(device_,&layoutView.info,nullptr,&layout_);
    if(result!=VK_SUCCESS){layout_=VK_NULL_HANDLE;return failure(Stage::Layout,result);}
    if(r.renderPass.route2){
        frame_pixel_ssbo_clone::RenderPass2View renderView{};
        if(!r.renderPass.view2(renderView))return invalid(Stage::RenderPass);
        result=procs_.CreateRenderPass2(device_,&renderView.info,nullptr,&renderPass_);
    }else{
        frame_pixel_ssbo_clone::RenderPassView renderView{};
        if(!r.renderPass.view(renderView))return invalid(Stage::RenderPass);
        result=procs_.CreateRenderPass(device_,&renderView.info,nullptr,&renderPass_);
    }
    if(result!=VK_SUCCESS){renderPass_=VK_NULL_HANDLE;return failure(Stage::RenderPass,result);}
    VkShaderModuleCreateInfo module{};module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module.codeSize=r.vertex.bytes;module.pCode=r.vertex.words.data();
    result=procs_.CreateShaderModule(device_,&module,nullptr,&vertex_);
    if(result!=VK_SUCCESS){vertex_=VK_NULL_HANDLE;return failure(Stage::Vertex,result);}
    module.codeSize=p.fragmentBytes;module.pCode=p.fragment;
    result=procs_.CreateShaderModule(device_,&module,nullptr,&fragment_);
    if(result!=VK_SUCCESS){fragment_=VK_NULL_HANDLE;return failure(Stage::Fragment,result);}
    frame_pixel_ssbo_clone::OwnedObjects objects{vertex_,fragment_,layout_,renderPass_,true};
    frame_pixel_ssbo_clone::GraphicsView graphics{};frame_pixel_ssbo_clone::Refusal refusal{};
    if(!r.graphics.view(objects,graphics,refusal))return invalid(Stage::Pipeline);
    // Never borrow a native pipeline cache or allocator callback.
    result=procs_.CreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&graphics.info,nullptr,&pipeline_);
    // Unlike ordinary failed creates, this API can return a valid partial
    // pipeline even on failure; cleanup must retain that exceptional handle.
    if(result!=VK_SUCCESS)return failure(Stage::Pipeline,result);
    VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size=RecordBytes;buffer.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    result=procs_.CreateBuffer(device_,&buffer,nullptr,&buffer_);
    if(result!=VK_SUCCESS){buffer_=VK_NULL_HANDLE;return failure(Stage::Buffer,result);}
    VkMemoryRequirements requirements{};procs_.GetBufferMemoryRequirements(device_,buffer_,&requirements);
    if(requirements.size<RecordBytes || requirements.size>MaximumAllocation || !requirements.alignment ||
       (requirements.alignment&(requirements.alignment-1)) || !requirements.memoryTypeBits)return invalid(Stage::Allocate);
    std::uint32_t selected=UINT32_MAX;
    constexpr VkMemoryPropertyFlags ordinaryMemory=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    for(std::uint32_t i=0;i<p.memory.memoryTypeCount;++i){const auto& type=p.memory.memoryTypes[i];
        if(!(requirements.memoryTypeBits&(1u<<i)) || !(type.propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ||
           (type.propertyFlags&~ordinaryMemory) ||
           type.heapIndex>=p.memory.memoryHeapCount || p.memory.memoryHeaps[type.heapIndex].size<requirements.size)continue;
        if(selected==UINT32_MAX || (type.propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))selected=i;
        if(type.propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)break;
    }
    if(selected==UINT32_MAX)return invalid(Stage::Allocate);
    coherent_=(p.memory.memoryTypes[selected].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=selected;
    result=procs_.AllocateMemory(device_,&allocation,nullptr,&memory_);
    if(result!=VK_SUCCESS){memory_=VK_NULL_HANDLE;return failure(Stage::Allocate,result);}
    allocationBytes_=requirements.size;
    result=procs_.BindBufferMemory(device_,buffer_,memory_,0);
    if(result!=VK_SUCCESS)return failure(Stage::Bind,result);
    result=procs_.MapMemory(device_,memory_,0,VK_WHOLE_SIZE,0,&mapped_);
    if(result!=VK_SUCCESS){mapped_=nullptr;return failure(Stage::Map,result);}
    if(!mapped_)return invalid(Stage::Map);
    // Cold initialization is not a reset for later GPU executions. The recording
    // still requires its per-execution fill and shader/host dependencies.
    std::memset(mapped_,0,RecordBytes);
    if(!coherent_){VkMappedMemoryRange range{};range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=memory_;range.size=VK_WHOLE_SIZE;
        result=procs_.FlushMappedMemoryRanges(device_,1,&range);
        if(result!=VK_SUCCESS)return failure(Stage::Flush,result);
    }
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;
    result=procs_.CreateDescriptorPool(device_,&pool,nullptr,&pool_);
    if(result!=VK_SUCCESS){pool_=VK_NULL_HANDLE;return failure(Stage::Pool,result);}
    VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool=pool_;allocate.descriptorSetCount=1;allocate.pSetLayouts=&set1_;
    result=procs_.AllocateDescriptorSets(device_,&allocate,&set_);
    if(result!=VK_SUCCESS)return failure(Stage::Set,result);
    VkDescriptorBufferInfo record{buffer_,0,RecordBytes};
    VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet=set_;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&record;
    procs_.UpdateDescriptorSets(device_,1,&write,0,nullptr);
    ready_=true;return {};
}

void Clone::destroyUnreferenced() noexcept {
    if(!device_)return;
    if(pool_)procs_.DestroyDescriptorPool(device_,pool_,nullptr);
    if(mapped_)procs_.UnmapMemory(device_,memory_);
    if(buffer_)procs_.DestroyBuffer(device_,buffer_,nullptr);
    if(memory_)procs_.FreeMemory(device_,memory_,nullptr);
    if(pipeline_)procs_.DestroyPipeline(device_,pipeline_,nullptr);
    if(fragment_)procs_.DestroyShaderModule(device_,fragment_,nullptr);
    if(vertex_)procs_.DestroyShaderModule(device_,vertex_,nullptr);
    if(renderPass_)procs_.DestroyRenderPass(device_,renderPass_,nullptr);
    if(layout_)procs_.DestroyPipelineLayout(device_,layout_,nullptr);
    if(set1_)procs_.DestroyDescriptorSetLayout(device_,set1_,nullptr);
    if(set0_)procs_.DestroyDescriptorSetLayout(device_,set0_,nullptr);
    device_=VK_NULL_HANDLE;set0_=set1_=VK_NULL_HANDLE;layout_=VK_NULL_HANDLE;renderPass_=VK_NULL_HANDLE;
    vertex_=fragment_=VK_NULL_HANDLE;pipeline_=VK_NULL_HANDLE;pool_=VK_NULL_HANDLE;set_=VK_NULL_HANDLE;
    buffer_=VK_NULL_HANDLE;memory_=VK_NULL_HANDLE;mapped_=nullptr;allocationBytes_=0;
    ready_=referenced_=coherent_=false;procs_={};recipe_={};
}
Result Clone::release(ReleaseProof proof) noexcept {
    if(referenced_ && !(proof.allRecordingsRetired && proof.deviceQuiescentOrLost))
        return {Status::UnsafeToRelease,Stage::None,VK_SUCCESS};
    destroyUnreferenced();return {};
}
} // namespace frame_pixel_ssbo_owned
