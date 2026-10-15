// Temporary explicit-pattern carriage diagnostic. Only public Vulkan calls,
// with one owned raw P010 copy. No shader change, native executable detour,
// private memory read, PC sender modification or passthrough activation.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "pixel_capture_tracker.h"
#include "pixel_snapshot_selection.h"
#include "pixel_readback.h"
#include "pixel_snapshot_io.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#define PIXEL_EXPORT extern "C" __attribute__((visibility("default")))
namespace {
using frame_pixel_capture::LiveTracker;
using frame_pixel_capture::Candidate;
using frame_pixel_capture::SubmitTicket;
using frame_pixel_capture::RetirementTicket;
struct Session {
    frame_pixel_io::Config config;
    std::unique_ptr<LiveTracker> tracker;
    std::atomic<bool> armed{false}, deviceClaimed{false};
    bool allowed = false;
    explicit Session(bool inert=false) noexcept {
        if(inert)return;
        try {
        char path[4096]{};
        const auto count = readlink("/proc/self/exe", path, sizeof(path)-1);
        if (count <= 0) return;
        path[count] = 0;
        const auto* base = std::strrchr(path, '/');
        if (std::strcmp(base ? base+1 : path, "vrcompositor")) return;
        // Host fixtures may select their own private config. Deployment does
        // not add process/service environment or preload/audit libraries.
        const auto* overridePath = std::getenv("GHVR_PIXEL_SNAPSHOT_CONFIG");
        if (!frame_pixel_io::load(overridePath ? overridePath : frame_pixel_io::ConfigPath, config)) return;
        // One native process may claim a prepared run. A crash/reboot/restart
        // never re-arms an old capture, even if its temporary manifest remains.
        const int claim=frame_pixel_io::create(config,"launch.claim");
        if(claim<0)return;
        const bool claimed=frame_pixel_io::writeAll(claim,"GHVR_PIXEL_CLAIM/1\n",19);
        close(claim);if(!claimed)return;
        tracker = std::make_unique<LiveTracker>(); allowed = true;
        } catch(...) { allowed=false; tracker.reset(); }
    }
};
Session& session() {
    // A native process may exit without explicit Vulkan teardown. Keep cold
    // diagnostic state valid until process exit rather than race a live writer
    // against the C++ global-destructor order. Explicit device teardown still
    // joins writers and frees every owned GPU child.
    static Session inert(true);
    static auto* value = []() noexcept {try{return new Session();}catch(...){return static_cast<Session*>(nullptr);}}();
    return value ? *value : inert;
}
template<class T> void* dispatchKey(T value) { return *reinterpret_cast<void**>(value); }
struct Instance {
    VkInstance native{};
    PFN_vkGetInstanceProcAddr gipa{};
    PFN_GetPhysicalDeviceProcAddr gpdpa{};
};
struct Device {
    VkDevice native{};
    std::uint64_t id = 0;
    PFN_vkGetDeviceProcAddr gdpa{};
#define FIELD(name) PFN_vk##name name{};
    FIELD(DestroyDevice) FIELD(DeviceWaitIdle) FIELD(CreateShaderModule) FIELD(DestroyShaderModule)
    FIELD(CreateGraphicsPipelines) FIELD(DestroyPipeline) FIELD(CreateCommandPool)
    FIELD(AllocateCommandBuffers) FIELD(FreeCommandBuffers) FIELD(BeginCommandBuffer) FIELD(EndCommandBuffer)
    FIELD(ResetCommandBuffer) FIELD(ResetCommandPool) FIELD(DestroyCommandPool) FIELD(CmdBindPipeline)
    FIELD(CmdDraw) FIELD(CmdDrawIndexed) FIELD(CmdDrawIndirect) FIELD(CmdDrawIndexedIndirect)
    FIELD(CmdBindShadersEXT)
    FIELD(CmdExecuteCommands) FIELD(CmdBeginRenderPass) FIELD(CmdNextSubpass) FIELD(CmdEndRenderPass)
    FIELD(CmdBeginRenderPass2) FIELD(CmdNextSubpass2) FIELD(CmdEndRenderPass2)
    FIELD(CmdBeginRendering) FIELD(CmdEndRendering) FIELD(QueueSubmit) FIELD(QueueSubmit2)
#define BINDING_FIELD(name) FIELD(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#undef FIELD
    frame_pixels::Readback readback;
    Candidate candidate{};
    std::thread writer;
    std::atomic<bool> stop{false}, selected{false};
    bool ownsCapture = false;
};
struct Contexts {
    std::mutex mutex;
    std::unordered_map<void*, std::unique_ptr<Instance>> instances;
    std::unordered_map<void*, std::unique_ptr<Device>> devices;
};
Contexts& contexts() { static auto* value=new Contexts();return *value; }
std::atomic<std::uint64_t> nextDevice{0};
template<class T> Instance* instanceFor(T value) {
    if(!value)return nullptr;
    auto& c=contexts(); std::lock_guard<std::mutex> lock(c.mutex);
    const auto found = c.instances.find(dispatchKey(value));
    return found == c.instances.end() ? nullptr : found->second.get();
}
template<class T> Device* deviceFor(T value) {
    if(!value)return nullptr;
    auto& c=contexts(); std::lock_guard<std::mutex> lock(c.mutex);
    const auto found = c.devices.find(dispatchKey(value));
    return found == c.devices.end() ? nullptr : found->second.get();
}
template<class F> void observeBindings(F observer) {
    if (session().allowed) session().tracker->observe(observer);
}
void status(int fd, const char* name, int result = 0) {
    if (fd < 0) return;
    char bytes[256]{};
    const auto count=std::snprintf(bytes,sizeof(bytes),
        "{\"event\":\"%s\",\"vk_result\":%d,\"mask_enabled\":false,\"native_frame_association_proven\":false}\n",name,result);
    if(count>0 && static_cast<std::size_t>(count)<sizeof(bytes))frame_pixel_io::writeAll(fd,bytes,static_cast<std::size_t>(count));
}
void save(Device& d, const frame_pixels::View& view, int progress) {
    const auto& config = session().config;
    const auto bytes = static_cast<std::size_t>(view.bytes);
    const auto yBytes = static_cast<std::size_t>(view.layout.planes[1].bufferOffset);
    const int raw = frame_pixel_io::create(config, "snapshot.p010");
    if (raw < 0) { status(progress, "output_refused"); return; }
    const bool written = frame_pixel_io::writeAll(raw, view.data, bytes) && !fsync(raw);
    close(raw);
    if (!written) { status(progress, "output_failed"); return; }
    const auto& b = d.candidate.binding;
    const auto& conversion = b.view.conversion;
    std::ostringstream record;
    record << "{\"schema\":\"frame-decoded-p010-snapshot-v1\",\"format\":" << int(view.layout.format)
        << ",\"width\":" << view.layout.extent.width << ",\"height\":" << view.layout.extent.height
        << ",\"byte_length\":" << bytes << ",\"sha256\":\"" << frame_pixel_io::digest(view.data,bytes)
        << "\",\"immutable\":true,\"generation\":{\"capture_id\":\"" << config.captureId
        << "\",\"image_generation\":" << view.sourceGeneration << ",\"acquisition_id\":1,\"conversion_generation\":"
        << conversion.identity.generation << "},\"planes\":[{\"name\":\"Y\",\"offset\":0,\"length\":" << yBytes
        << ",\"sha256\":\"" << frame_pixel_io::digest(view.data,yBytes) << "\"},{\"name\":\"UV\",\"offset\":"
        << yBytes << ",\"length\":" << bytes-yBytes << ",\"sha256\":\""
        << frame_pixel_io::digest(view.data+yBytes,bytes-yBytes) << "\"}],\"conversion\":";
    if (conversion.complete && conversion.model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY &&
        conversion.range == VK_SAMPLER_YCBCR_RANGE_ITU_FULL &&
        conversion.components.r == VK_COMPONENT_SWIZZLE_IDENTITY && conversion.components.g == VK_COMPONENT_SWIZZLE_IDENTITY &&
        conversion.components.b == VK_COMPONENT_SWIZZLE_IDENTITY && conversion.components.a == VK_COMPONENT_SWIZZLE_IDENTITY &&
        (conversion.xChromaOffset==VK_CHROMA_LOCATION_MIDPOINT || conversion.xChromaOffset==VK_CHROMA_LOCATION_COSITED_EVEN) &&
        (conversion.yChromaOffset==VK_CHROMA_LOCATION_MIDPOINT || conversion.yChromaOffset==VK_CHROMA_LOCATION_COSITED_EVEN) &&
        (conversion.chromaFilter==VK_FILTER_LINEAR || conversion.chromaFilter==VK_FILTER_NEAREST)) {
        record << "{\"model\":\"rgb_identity\",\"range\":\"itu_full\",\"components\":{\"r\":\"r\",\"g\":\"g\",\"b\":\"b\",\"a\":\"a\"},"
               << "\"x_chroma_offset\":\"" << (conversion.xChromaOffset == VK_CHROMA_LOCATION_MIDPOINT ? "midpoint" : "cosited_even")
               << "\",\"y_chroma_offset\":\"" << (conversion.yChromaOffset == VK_CHROMA_LOCATION_MIDPOINT ? "midpoint" : "cosited_even")
               << "\",\"chroma_filter\":\"" << (conversion.chromaFilter == VK_FILTER_LINEAR ? "linear" : "nearest") << "\"}";
    } else record << "null";
    record << ",\"observed_vulkan_conversion\":{\"complete\":" << (conversion.complete ? "true" : "false")
        << ",\"model\":" << int(conversion.model) << ",\"range\":" << int(conversion.range)
        << ",\"components\":[" << int(conversion.components.r) << ',' << int(conversion.components.g) << ','
        << int(conversion.components.b) << ',' << int(conversion.components.a) << "],\"chroma_offsets\":["
        << int(conversion.xChromaOffset) << ',' << int(conversion.yChromaOffset) << "],\"chroma_filter\":" << int(conversion.chromaFilter)
        << "},\"acquisition\":{\"path\":\"same_primary_after_distortion_renderpass_gpu_plane_copy\",\"owned_fence_complete\":true,"
        << "\"one_time_submit\":" << (view.oneTimeSubmit ? "true" : "false")
        << ",\"retirement_required\":" << (view.retirementRequired ? "true" : "false")
        << ",\"generation_retired\":" << (view.generationRetired ? "true" : "false")
        << ",\"command_generation\":" << view.commandGeneration << ",\"view_generation\":"
        << b.view.identity.generation << ",\"queue_family\":" << d.candidate.queueFamily
        << "},\"source_frame_associated\":false,\"uv_owned\":false,\"passthrough_authorized\":false}\n";
    const int meta = frame_pixel_io::create(config, "snapshot.json");
    if (meta < 0) { status(progress, "metadata_refused"); return; }
    const auto text = record.str();
    const bool good = frame_pixel_io::writeAll(meta,text.data(),text.size()) && !fsync(meta);
    close(meta); status(progress,good ? "complete" : "metadata_failed");
}
void writer(Device* d) noexcept {
    const int progress = frame_pixel_io::create(session().config,"status.jsonl");
    if (progress < 0 || flock(progress,LOCK_EX|LOCK_NB)) { if(progress>=0)close(progress); return; }
    status(progress,"prepared");
    bool announced = false;
    try { while (!d->stop.load(std::memory_order_acquire)) {
        if (!session().armed.load(std::memory_order_acquire) && frame_pixel_io::armed(session().config)) {
            session().armed.store(true,std::memory_order_release); status(progress,"armed");
        }
        if (d->selected.load(std::memory_order_acquire)) {
            if (!announced) { status(progress,"commands_recorded"); announced=true; }
            frame_pixels::View view;
            const auto result = d->readback.poll(view);
            if (result == frame_pixels::Status::Complete) { save(*d,view,progress); break; }
            if (result == frame_pixels::Status::VulkanFailure) { status(progress,"gpu_failure",d->readback.lastResult()); break; }
        }
        if (!d->selected.load(std::memory_order_acquire) && session().tracker->poisoned()) {
            status(progress,"tracking_refused"); break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Background polling only; never restarts anything.
    }} catch(...) { status(progress,"writer_failed"); }
    fsync(progress); close(progress);
}
// Capture the OLD live generation before native reset/free can replace or
// recycle handles. A later equal raw handle is never retirement evidence.
RetirementTicket retiring(Device* d, VkCommandBuffer command, VkCommandPool pool = VK_NULL_HANDLE) {
    if (!d->selected.load(std::memory_order_acquire) || command != d->candidate.command) return {};
    const auto before = session().tracker->prepareRetirement(d->id,command);
    const auto& chosen = d->candidate;
    if (!frame_pixel_selection::matchesRetirement(chosen,before) ||
        (pool && (pool != before.pool || !session().tracker->ownsCommandPool(d->id,command,pool)))) return {};
    return before;
}
void retired(Device* d, const RetirementTicket& before, bool nativeSuccess) {
    if (before.eligible) d->readback.retireCommand(before.command,before.commandGeneration,nativeSuccess);
}
void afterRenderPass(Device* d, VkCommandBuffer command, bool dynamic) {
    if (!session().allowed) return;
    Candidate candidate;
    const bool ready = dynamic ? session().tracker->endRendering(d->id,command,candidate)
                               : session().tracker->endRenderPass(d->id,command,candidate);
    if (!ready || !d->ownsCapture || d->selected.load(std::memory_order_acquire) ||
        !session().armed.load(std::memory_order_acquire)) return;
    const auto& image = candidate.binding.image;
    // The offline recovery/PNG reader has the same independent 12M-pixel bound.
    // Reserve 64 MiB cold, but never save an extent that its reader cannot use.
    if (image.extent.width>8192 || image.extent.height>8192 ||
        std::uint64_t(image.extent.width)*image.extent.height>12000000) return;
    frame_pixels::Source source;
    source.image = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(image.identity.handle));
    source.generation=image.identity.generation; source.format=image.format; source.extent=image.extent;
    source.usage=image.usage; source.flags=image.flags; source.type=image.type; source.mipLevels=image.mipLevels;
    source.arrayLayers=image.arrayLayers; source.samples=image.samples; source.layout=candidate.binding.imageLayout;
    frame_pixels::Admission admission;
    admission.commandGeneration=candidate.commandGeneration; admission.complete=true; admission.primary=true;
    admission.outsideRenderPass=true; admission.afterMatchingDistortDraw=true; admission.queueOwnsImage=true;
    admission.beforeExternalRelease=true; admission.ordinaryUnprotected=true;
    admission.oneTimeSubmit=candidate.oneTimeSubmit;
    admission.noSimultaneousUse=candidate.noSimultaneousUse;
    admission.retirementRequired=candidate.retirementRequired;
    if (d->readback.record(command,source,admission) == frame_pixels::Status::Ok) {
        d->candidate=candidate; d->selected.store(true,std::memory_order_release);
    }
}
} // namespace

PIXEL_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL frame_pixel_vkGetInstanceProcAddr(VkInstance,const char*);
PIXEL_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL frame_pixel_vkGetDeviceProcAddr(VkDevice,const char*);
namespace {
VKAPI_ATTR VkResult VKAPI_CALL traceCreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* alloc,VkInstance* out) {
    auto* link=static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while(link && (link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || link->function!=VK_LAYER_LINK_INFO))
        link=static_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
    if(!link || !link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    std::unique_ptr<Instance> record;
    try{record=std::make_unique<Instance>();}catch(...){return VK_ERROR_OUT_OF_HOST_MEMORY;}
    record->gipa=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    record->gpdpa=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    auto next=reinterpret_cast<PFN_vkCreateInstance>(record->gipa(VK_NULL_HANDLE,"vkCreateInstance"));
    link->u.pLayerInfo=link->u.pLayerInfo->pNext;
    const auto result=next(info,alloc,out);
    if(result==VK_SUCCESS){
        record->native=*out;
        const auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(record->gipa(*out,"vkDestroyInstance"));
        try{auto& c=contexts();std::lock_guard<std::mutex> lock(c.mutex);c.instances.emplace(dispatchKey(*out),std::move(record));}
        catch(...){destroy(*out,alloc);*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyInstance(VkInstance instance,const VkAllocationCallbacks* alloc) {
    auto* i=instanceFor(instance);auto next=reinterpret_cast<PFN_vkDestroyInstance>(i->gipa(instance,"vkDestroyInstance"));
    const auto key=dispatchKey(instance);next(instance,alloc);auto& c=contexts();std::lock_guard<std::mutex> lock(c.mutex);c.instances.erase(key);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* alloc,VkDevice* out) {
    auto* i=instanceFor(physical);auto* link=static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while(link && (link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || link->function!=VK_LAYER_LINK_INFO))
        link=static_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
    if(!i || !link || !link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    std::unique_ptr<Device> d;
    try{d=std::make_unique<Device>();}catch(...){return VK_ERROR_OUT_OF_HOST_MEMORY;}
    d->gdpa=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto next=reinterpret_cast<PFN_vkCreateDevice>(link->u.pLayerInfo->pfnNextGetInstanceProcAddr(i->native,"vkCreateDevice"));
    link->u.pLayerInfo=link->u.pLayerInfo->pNext;
    const auto result=next(physical,info,alloc,out);if(result!=VK_SUCCESS)return result;
    d->native=*out;d->id=++nextDevice;
#define LOAD(name) d->name=reinterpret_cast<PFN_vk##name>(d->gdpa(*out,"vk" #name));
    LOAD(DestroyDevice) LOAD(DeviceWaitIdle) LOAD(CreateShaderModule) LOAD(DestroyShaderModule)
    LOAD(CreateGraphicsPipelines) LOAD(DestroyPipeline) LOAD(CreateCommandPool)
    LOAD(AllocateCommandBuffers) LOAD(FreeCommandBuffers) LOAD(BeginCommandBuffer) LOAD(EndCommandBuffer)
    LOAD(ResetCommandBuffer) LOAD(ResetCommandPool) LOAD(DestroyCommandPool) LOAD(CmdBindPipeline)
    LOAD(CmdDraw) LOAD(CmdDrawIndexed) LOAD(CmdDrawIndirect) LOAD(CmdDrawIndexedIndirect) LOAD(CmdExecuteCommands)
    LOAD(CmdBindShadersEXT)
    LOAD(CmdBeginRenderPass) LOAD(CmdNextSubpass) LOAD(CmdEndRenderPass) LOAD(CmdBeginRenderPass2) LOAD(CmdNextSubpass2) LOAD(CmdEndRenderPass2)
    LOAD(CmdBeginRendering) LOAD(CmdEndRendering) LOAD(QueueSubmit) LOAD(QueueSubmit2)
#define BINDING_FIELD(name) LOAD(name)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#undef LOAD
#define ALIAS(name,suffix) if(!d->name)d->name=reinterpret_cast<PFN_vk##name>(d->gdpa(*out,"vk" #name suffix));
    ALIAS(CmdBeginRenderPass2,"KHR") ALIAS(CmdNextSubpass2,"KHR") ALIAS(CmdEndRenderPass2,"KHR")
    ALIAS(CmdBeginRendering,"KHR") ALIAS(CmdEndRendering,"KHR") ALIAS(QueueSubmit2,"KHR")
#undef ALIAS
#define BINDING_ALIAS(alias,name) if(!d->name)d->name=reinterpret_cast<PFN_vk##name>(d->gdpa(*out,alias));
#include "passive_binding_aliases.inc"
#undef BINDING_ALIAS
    bool ordinary=info->queueCreateInfoCount>0 && info->queueCreateInfoCount<=32;
    for(std::uint32_t n=0;ordinary && n<info->queueCreateInfoCount;++n)ordinary=info->pQueueCreateInfos[n].flags==0;
    auto* deviceExtension=static_cast<const VkBaseInStructure*>(info->pNext);
    for(unsigned budget=0;ordinary && deviceExtension && budget<32;++budget,deviceExtension=deviceExtension->pNext)
        if(deviceExtension->sType==VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO)ordinary=false;
    if(deviceExtension)ordinary=false;
    // Publish forwarding context BEFORE preparing resources or starting a
    // writer. A map allocation failure must not destroy a joinable std::thread
    // or strand a successfully created native device.
    Device* published=d.get();const auto destroy=d->DestroyDevice;
    try{auto& c=contexts();std::lock_guard<std::mutex> lock(c.mutex);c.devices.emplace(dispatchKey(*out),std::move(d));}
    catch(...){destroy(*out,alloc);*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    if(session().allowed && ordinary && !session().deviceClaimed.exchange(true)) {
        VkPhysicalDeviceMemoryProperties properties{};
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(i->gipa(i->native,"vkGetPhysicalDeviceMemoryProperties"))(physical,&properties);
        if(published->readback.prepare(*out,published->gdpa,properties,{})==frame_pixels::Status::Ok) {
            published->ownsCapture=true;
            try{published->writer=std::thread(writer,published);}
            catch(...){published->ownsCapture=false;published->readback.release();}
        }
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyDevice(VkDevice device,const VkAllocationCallbacks* alloc) {
    auto* d=deviceFor(device);d->stop.store(true,std::memory_order_release);if(d->writer.joinable())d->writer.join();
    // Explicit shutdown only, never a rendering/recording callback. The added
    // completion fence also belongs to this queue, so native fence waits alone
    // cannot be substituted for quiescing our own children before destruction.
    const auto state=d->readback.state();
    if(state!=frame_pixels::State::Empty && state!=frame_pixels::State::Released) {
        const auto idle=d->DeviceWaitIdle(device);
        d->readback.release(idle==VK_SUCCESS || idle==VK_ERROR_DEVICE_LOST);
    }
    const auto key=dispatchKey(device);d->DestroyDevice(device,alloc);
    auto& c=contexts();std::lock_guard<std::mutex> lock(c.mutex);c.devices.erase(key);
}
#include "passive_binding_hooks.inc"
VKAPI_ATTR VkResult VKAPI_CALL traceCreateShaderModule(VkDevice device,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks* alloc,VkShaderModule* out) {
    auto* d=deviceFor(device);const auto result=d->CreateShaderModule(device,info,alloc,out);
    if(result==VK_SUCCESS)session().tracker->createShader(d->id,*out,*info);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyShaderModule(VkDevice device,VkShaderModule value,const VkAllocationCallbacks* alloc) {
    auto* d=deviceFor(device);session().tracker->destroyShader(d->id,value);d->DestroyShaderModule(device,value,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks* alloc,VkPipeline* out) {
    auto* d=deviceFor(device);const auto result=d->CreateGraphicsPipelines(device,cache,count,info,alloc,out);
    // Partial success handles are not an admitted diagnostic pipeline.
    if(result==VK_SUCCESS && count<=256)for(std::uint32_t n=0;n<count;++n)session().tracker->createPipeline(d->id,out[n],info[n]);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyPipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* alloc) {
    auto* d=deviceFor(device);session().tracker->destroyPipeline(d->id,pipeline);d->DestroyPipeline(device,pipeline,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL traceCreateCommandPool(VkDevice device,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks* alloc,VkCommandPool* out) {
    auto* d=deviceFor(device);const auto result=d->CreateCommandPool(device,info,alloc,out);
    if(result==VK_SUCCESS)session().tracker->createCommandPool(d->id,*out,*info);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceDestroyCommandPool(VkDevice device,VkCommandPool pool,const VkAllocationCallbacks* alloc) {
    auto* d=deviceFor(device);
    const auto before=d->selected.load(std::memory_order_acquire)
        ? retiring(d,d->candidate.command,pool) : RetirementTicket{};
    d->DestroyCommandPool(device,pool,alloc);
    retired(d,before,true);
    session().tracker->destroyCommandPool(d->id,pool);
}
VKAPI_ATTR VkResult VKAPI_CALL traceResetCommandPool(VkDevice device,VkCommandPool pool,VkCommandPoolResetFlags flags) {
    auto* d=deviceFor(device);
    const auto before=d->selected.load(std::memory_order_acquire)
        ? retiring(d,d->candidate.command,pool) : RetirementTicket{};
    const auto result=d->ResetCommandPool(device,pool,flags);
    retired(d,before,result==VK_SUCCESS);
    if(result==VK_SUCCESS)session().tracker->resetCommandPool(d->id,pool);
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceAllocateCommandBuffers(VkDevice device,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out) {
    auto* d=deviceFor(device);const auto result=d->AllocateCommandBuffers(device,info,out);
    if(result==VK_SUCCESS)session().tracker->allocateCommands(d->id,*info,out);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceFreeCommandBuffers(VkDevice device,VkCommandPool pool,std::uint32_t count,const VkCommandBuffer* commands) {
    auto* d=deviceFor(device);
    RetirementTicket before;
    if(d->selected.load(std::memory_order_acquire) && count<=512)
        for(std::uint32_t n=0;n<count;++n)
            if(commands[n]==d->candidate.command)before=retiring(d,commands[n],pool);
    d->FreeCommandBuffers(device,pool,count,commands);
    retired(d,before,true);
    session().tracker->freeCommands(d->id,pool,count,commands);
}
VKAPI_ATTR VkResult VKAPI_CALL traceBeginCommandBuffer(VkCommandBuffer command,const VkCommandBufferBeginInfo* info) {
    auto* d=deviceFor(command);
    const auto before=retiring(d,command);
    const auto result=d->BeginCommandBuffer(command,info);
    retired(d,before,result==VK_SUCCESS);
    session().tracker->beginCommand(d->id,command,*info,result==VK_SUCCESS);return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceEndCommandBuffer(VkCommandBuffer command) {
    auto* d=deviceFor(command);const auto result=d->EndCommandBuffer(command);
    session().tracker->endCommand(d->id,command,result==VK_SUCCESS);return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceResetCommandBuffer(VkCommandBuffer command,VkCommandBufferResetFlags flags) {
    auto* d=deviceFor(command);
    const auto before=retiring(d,command);
    const auto result=d->ResetCommandBuffer(command,flags);
    retired(d,before,result==VK_SUCCESS);
    if(result==VK_SUCCESS)session().tracker->resetCommand(d->id,command);
    return result;
}
VKAPI_ATTR void VKAPI_CALL traceCmdBindPipeline(VkCommandBuffer command,VkPipelineBindPoint point,VkPipeline pipeline) {
    auto* d=deviceFor(command);session().tracker->bindPipeline(d->id,command,point,pipeline);d->CmdBindPipeline(command,point,pipeline);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBindShadersEXT(VkCommandBuffer command,std::uint32_t count,const VkShaderStageFlagBits* stages,const VkShaderEXT* shaders) {
    auto* d=deviceFor(command);session().tracker->unknownCommand(d->id,command);
    d->CmdBindShadersEXT(command,count,stages,shaders);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDraw(VkCommandBuffer command,std::uint32_t vertices,std::uint32_t instances,std::uint32_t firstVertex,std::uint32_t firstInstance) {
    auto* d=deviceFor(command);if(vertices && instances)session().tracker->draw(d->id,command);
    d->CmdDraw(command,vertices,instances,firstVertex,firstInstance);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndexed(VkCommandBuffer command,std::uint32_t indices,std::uint32_t instances,std::uint32_t firstIndex,std::int32_t vertexOffset,std::uint32_t firstInstance) {
    auto* d=deviceFor(command);if(indices && instances)session().tracker->draw(d->id,command);
    d->CmdDrawIndexed(command,indices,instances,firstIndex,vertexOffset,firstInstance);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndirect(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,std::uint32_t count,std::uint32_t stride) {
    auto* d=deviceFor(command);session().tracker->unknownCommand(d->id,command);d->CmdDrawIndirect(command,buffer,offset,count,stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdDrawIndexedIndirect(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,std::uint32_t count,std::uint32_t stride) {
    auto* d=deviceFor(command);session().tracker->unknownCommand(d->id,command);d->CmdDrawIndexedIndirect(command,buffer,offset,count,stride);
}
VKAPI_ATTR void VKAPI_CALL traceCmdExecuteCommands(VkCommandBuffer command,std::uint32_t count,const VkCommandBuffer* children) {
    auto* d=deviceFor(command);session().tracker->unknownCommand(d->id,command);d->CmdExecuteCommands(command,count,children);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBeginRenderPass(VkCommandBuffer command,const VkRenderPassBeginInfo* info,VkSubpassContents contents) {
    auto* d=deviceFor(command);session().tracker->beginRenderPass(d->id,command,contents);d->CmdBeginRenderPass(command,info,contents);
}
VKAPI_ATTR void VKAPI_CALL traceCmdNextSubpass(VkCommandBuffer command,VkSubpassContents contents) {
    auto* d=deviceFor(command);session().tracker->nextSubpass(d->id,command,contents);d->CmdNextSubpass(command,contents);
}
VKAPI_ATTR void VKAPI_CALL traceCmdEndRenderPass(VkCommandBuffer command) {
    auto* d=deviceFor(command);d->CmdEndRenderPass(command);afterRenderPass(d,command,false);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBeginRenderPass2(VkCommandBuffer command,const VkRenderPassBeginInfo* info,const VkSubpassBeginInfo* begin) {
    auto* d=deviceFor(command);session().tracker->beginRenderPass(d->id,command,begin->contents);d->CmdBeginRenderPass2(command,info,begin);
}
VKAPI_ATTR void VKAPI_CALL traceCmdNextSubpass2(VkCommandBuffer command,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end) {
    auto* d=deviceFor(command);session().tracker->nextSubpass(d->id,command,begin->contents);d->CmdNextSubpass2(command,begin,end);
}
VKAPI_ATTR void VKAPI_CALL traceCmdEndRenderPass2(VkCommandBuffer command,const VkSubpassEndInfo* end) {
    auto* d=deviceFor(command);d->CmdEndRenderPass2(command,end);afterRenderPass(d,command,false);
}
VKAPI_ATTR void VKAPI_CALL traceCmdBeginRendering(VkCommandBuffer command,const VkRenderingInfo* info) {
    auto* d=deviceFor(command);session().tracker->beginRendering(d->id,command,*info);d->CmdBeginRendering(command,info);
}
VKAPI_ATTR void VKAPI_CALL traceCmdEndRendering(VkCommandBuffer command) {
    auto* d=deviceFor(command);d->CmdEndRendering(command);afterRenderPass(d,command,true);
}
template<class Info,class Count,class Command> SubmitTicket ticket(Device* d,std::uint32_t count,const Info* info,Count commandCount,Command command) {
    if(!d->ownsCapture || !d->selected.load(std::memory_order_acquire) || d->readback.state()!=frame_pixels::State::Recorded || count>32)return {};
    for(std::uint32_t i=0;i<count;++i) {
        if(commandCount(info[i])>64)return {};
        for(std::uint32_t j=0;j<commandCount(info[i]);++j)
            if(command(info[i],j)==d->candidate.command) {
                const auto before=session().tracker->prepareSubmit(d->id,d->candidate.command);
                if(frame_pixel_selection::matchesSubmit(d->candidate,before))return before;
                // Consume/refuse a ticket belonging to a later native recording;
                // its work cannot establish completion of the discarded copy.
                session().tracker->acceptSubmit(before,false);
                return {};
            }
    }
    return {};
}
void submitted(Device* d,VkQueue queue,const SubmitTicket& before,VkResult nativeResult) {
    if(!frame_pixel_selection::matchesSubmit(d->candidate,before))return;
    const bool accepted=session().tracker->acceptSubmit(before,nativeResult==VK_SUCCESS);
    if(accepted || nativeResult!=VK_SUCCESS)
        d->readback.submitCompletion(queue,before.command,before.commandGeneration,nativeResult);
}
VKAPI_ATTR VkResult VKAPI_CALL traceQueueSubmit(VkQueue queue,std::uint32_t count,const VkSubmitInfo* info,VkFence fence) {
    auto* d=deviceFor(queue);const auto before=ticket(d,count,info,[](const auto& i){return i.commandBufferCount;},[](const auto& i,std::uint32_t n){return i.pCommandBuffers[n];});
    const auto result=d->QueueSubmit(queue,count,info,fence);submitted(d,queue,before,result);return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traceQueueSubmit2(VkQueue queue,std::uint32_t count,const VkSubmitInfo2* info,VkFence fence) {
    auto* d=deviceFor(queue);const auto before=ticket(d,count,info,[](const auto& i){return i.commandBufferInfoCount;},[](const auto& i,std::uint32_t n){return i.pCommandBufferInfos[n].commandBuffer;});
    const auto result=d->QueueSubmit2(queue,count,info,fence);submitted(d,queue,before,result);return result;
}
PFN_vkVoidFunction intercept(const char* name,PFN_vkVoidFunction native) {
    if(!native || !session().allowed)return native;
#define HOOK(method) if(!std::strcmp(name,"vk" #method))return reinterpret_cast<PFN_vkVoidFunction>(trace##method);
    HOOK(CreateShaderModule) HOOK(DestroyShaderModule) HOOK(CreateGraphicsPipelines) HOOK(DestroyPipeline)
    HOOK(CreateCommandPool) HOOK(DestroyCommandPool) HOOK(ResetCommandPool)
    HOOK(AllocateCommandBuffers) HOOK(FreeCommandBuffers) HOOK(BeginCommandBuffer) HOOK(EndCommandBuffer) HOOK(ResetCommandBuffer)
    HOOK(CmdBindPipeline) HOOK(CmdDraw) HOOK(CmdDrawIndexed) HOOK(CmdDrawIndirect) HOOK(CmdDrawIndexedIndirect) HOOK(CmdExecuteCommands)
    HOOK(CmdBindShadersEXT)
    HOOK(CmdBeginRenderPass) HOOK(CmdNextSubpass) HOOK(CmdEndRenderPass)
    HOOK(CmdBeginRenderPass2) HOOK(CmdNextSubpass2) HOOK(CmdEndRenderPass2) HOOK(CmdBeginRendering) HOOK(CmdEndRendering)
    HOOK(QueueSubmit) HOOK(QueueSubmit2)
#define BINDING_FIELD(method) HOOK(method)
#include "passive_binding_commands.inc"
#undef BINDING_FIELD
#undef HOOK
#define ALIAS(alias,method) if(!std::strcmp(name,alias))return reinterpret_cast<PFN_vkVoidFunction>(trace##method);
#define BINDING_ALIAS(alias,method) ALIAS(alias,method)
#include "passive_binding_aliases.inc"
#undef BINDING_ALIAS
    ALIAS("vkCmdBeginRenderPass2KHR",CmdBeginRenderPass2) ALIAS("vkCmdNextSubpass2KHR",CmdNextSubpass2) ALIAS("vkCmdEndRenderPass2KHR",CmdEndRenderPass2)
    ALIAS("vkCmdBeginRenderingKHR",CmdBeginRendering) ALIAS("vkCmdEndRenderingKHR",CmdEndRendering) ALIAS("vkQueueSubmit2KHR",QueueSubmit2)
#undef ALIAS
    return native;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL physicalProc(VkInstance instance,const char* name) {
    auto* i=instanceFor(instance);return i && i->gpdpa ? i->gpdpa(instance,name) : nullptr;
}
} // namespace

PIXEL_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL frame_pixel_vkGetDeviceProcAddr(VkDevice device,const char* name) {
    auto* d=deviceFor(device);if(!d || !name)return nullptr;
    if(!std::strcmp(name,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(frame_pixel_vkGetDeviceProcAddr);
    const auto native=d->gdpa(device,name);if(!native)return nullptr;
    if(!std::strcmp(name,"vkDestroyDevice"))return reinterpret_cast<PFN_vkVoidFunction>(traceDestroyDevice);
    return intercept(name,native);
}
PIXEL_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL frame_pixel_vkGetInstanceProcAddr(VkInstance instance,const char* name) {
    if(!name)return nullptr;
    if(!std::strcmp(name,"vkGetInstanceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(frame_pixel_vkGetInstanceProcAddr);
    if(!std::strcmp(name,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(frame_pixel_vkGetDeviceProcAddr);
    if(!std::strcmp(name,"vkCreateInstance"))return reinterpret_cast<PFN_vkVoidFunction>(traceCreateInstance);
    auto* i=instanceFor(instance);if(!i)return nullptr;
    const auto native=i->gipa(instance,name);if(!native)return nullptr;
    if(!std::strcmp(name,"vkDestroyInstance"))return reinterpret_cast<PFN_vkVoidFunction>(traceDestroyInstance);
    if(!std::strcmp(name,"vkCreateDevice"))return reinterpret_cast<PFN_vkVoidFunction>(traceCreateDevice);
    if(!std::strcmp(name,"vkDestroyDevice"))return reinterpret_cast<PFN_vkVoidFunction>(traceDestroyDevice);
    return intercept(name,native);
}
PIXEL_EXPORT VKAPI_ATTR VkResult VKAPI_CALL frame_pixel_vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if(!info || info->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT || info->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    info->loaderLayerInterfaceVersion=std::min(2u,info->loaderLayerInterfaceVersion);
    info->pfnGetInstanceProcAddr=frame_pixel_vkGetInstanceProcAddr;info->pfnGetDeviceProcAddr=frame_pixel_vkGetDeviceProcAddr;
    info->pfnGetPhysicalDeviceProcAddr=physicalProc;return VK_SUCCESS;
}
