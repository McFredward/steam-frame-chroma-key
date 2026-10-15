// Bounded admission/failure controls plus real, owned Vulkan source readback.
// Only the fixture waits/allocates; production record/queue paths do neither.
#include "layer/pixel_readback.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace frame_pixels;
static unsigned checks = 0;
static void require(bool yes, const char* message) {
    ++checks;
    if (!yes) throw std::runtime_error(message);
}
static void vkcheck(VkResult r) {
    if (r != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(r));
}
template<class T> static T handle(std::uintptr_t n) { return (T)n; }
static Config small(Mode mode = Mode::NativeP010) {
    Config c{}; c.extent = {8, 6}; c.mode = mode;
    c.format = mode == Mode::NativeP010 ? NativeP010 : VK_FORMAT_R8G8B8A8_UNORM;
    return c;
}
static Source source(const Config& c) {
    Source s{}; s.image = handle<VkImage>(41); s.generation = 9;
    s.format = c.format; s.extent = {c.extent.width,c.extent.height,1};
    s.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    s.mipLevels = s.arrayLayers = 1; s.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return s;
}
static Admission admission() {
    Admission a{}; a.commandGeneration = 19;
    a.complete = a.primary = a.outsideRenderPass = a.afterMatchingDistortDraw = true;
    a.queueOwnsImage = a.beforeExternalRelease = a.ordinaryUnprotected = true;
    a.oneTimeSubmit = a.noSimultaneousUse = true;
    return a;
}

struct Fake {
    static Fake* active;
    VkResult createResult = VK_SUCCESS, submitResult = VK_SUCCESS, fenceResult = VK_NOT_READY;
    VkResult invalidateResult = VK_SUCCESS;
    VkDeviceSize requested = 0, requirements = 0;
    unsigned created = 0, destroyed = 0, freed = 0, unmapped = 0, barriers = 0, copies = 0;
    unsigned submits = 0, polls = 0, invalidates = 0;
    std::uint32_t copyCount = 0;
    std::array<VkBufferImageCopy, 2> copyRegions{};
    std::array<VkImageMemoryBarrier, 2> imageBarriers{};
    VkBufferMemoryBarrier hostBarrier{};
    std::array<std::uint8_t, 4096> bytes{};
    bool badSubmit = false, fenceUnsignaled = false;
    const char* missing = nullptr;
    Fake() { active = this; }
    static VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice, const VkBufferCreateInfo* i, const VkAllocationCallbacks*, VkBuffer* out) {
        auto& f = *active; f.requested = i->size; ++f.created;
        if (f.createResult == VK_SUCCESS) *out = handle<VkBuffer>(51);
        return f.createResult;
    }
    static VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks*) { ++active->destroyed; }
    static VKAPI_ATTR void VKAPI_CALL requirementsFn(VkDevice, VkBuffer, VkMemoryRequirements* out) {
        *out = {active->requirements ? active->requirements : active->requested, 16, 1};
    }
    static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* out) { *out=handle<VkDeviceMemory>(52); return VK_SUCCESS; }
    static VKAPI_ATTR void VKAPI_CALL free(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) { ++active->freed; }
    static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }
    static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** out) { *out=active->bytes.data(); return VK_SUCCESS; }
    static VKAPI_ATTR void VKAPI_CALL unmap(VkDevice, VkDeviceMemory) { ++active->unmapped; }
    static VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice, const VkFenceCreateInfo* i, const VkAllocationCallbacks*, VkFence* out) { active->fenceUnsignaled=i->flags==0; *out=handle<VkFence>(53); return VK_SUCCESS; }
    static VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}
    static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t bc, const VkBufferMemoryBarrier* b, uint32_t ic, const VkImageMemoryBarrier* i) {
        auto& f=*active;
        if (f.barriers<2 && ic==1) f.imageBarriers[f.barriers]=*i;
        if (bc==1) f.hostBarrier=*b;
        require(src == (f.barriers ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),"all native sampling stages precede copy");
        require(dst == (f.barriers ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT),"restore covers all native sampling stages and host");
        ++f.barriers;
    }
    static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer, VkImage, VkImageLayout l, VkBuffer, uint32_t n, const VkBufferImageCopy* p) {
        auto& f=*active; ++f.copies; f.copyCount=n;
        require(l==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && n<=2,"copy source layout/count");
        for (unsigned j=0;j<n;++j) f.copyRegions[j]=p[j];
    }
    static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue, uint32_t n, const VkSubmitInfo* info, VkFence fence) {
        auto& f=*active; ++f.submits;
        f.badSubmit = n!=0 || info!=nullptr || fence!=handle<VkFence>(53);
        return f.submitResult;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL fenceStatus(VkDevice, VkFence) { ++active->polls; return active->fenceResult; }
    static VKAPI_ATTR VkResult VKAPI_CALL invalidate(VkDevice, uint32_t n, const VkMappedMemoryRange* p) {
        ++active->invalidates; require(n==1 && p->size==VK_WHOLE_SIZE && p->offset==0,"whole allocation invalidation");
        return active->invalidateResult;
    }
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice, const char* n) {
        if (active->missing && !std::strcmp(active->missing,n)) return nullptr;
#define PROC(name, fn) if (!std::strcmp(n,#name)) return reinterpret_cast<PFN_vkVoidFunction>(&fn);
        PROC(vkCreateBuffer,createBuffer) PROC(vkDestroyBuffer,destroyBuffer)
        PROC(vkGetBufferMemoryRequirements,requirementsFn) PROC(vkAllocateMemory,allocate)
        PROC(vkFreeMemory,free) PROC(vkBindBufferMemory,bind) PROC(vkMapMemory,map)
        PROC(vkUnmapMemory,unmap) PROC(vkCreateFence,createFence) PROC(vkDestroyFence,destroyFence)
        PROC(vkCmdPipelineBarrier,barrier) PROC(vkCmdCopyImageToBuffer,copy)
        PROC(vkQueueSubmit,submit) PROC(vkGetFenceStatus,fenceStatus)
        PROC(vkInvalidateMappedMemoryRanges,invalidate)
#undef PROC
        return nullptr;
    }
    static VkPhysicalDeviceMemoryProperties memory(bool coherent=true) {
        VkPhysicalDeviceMemoryProperties p{}; p.memoryTypeCount=1;
        p.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (coherent ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0);
        return p;
    }
    static Status prepare(Readback& r, bool coherent=true, Config c=small()) { return r.prepare(handle<VkDevice>(1),resolve,memory(coherent),c); }
};
Fake* Fake::active = nullptr;

static void geometryControls() {
    Layout l{}; Config c{}; c.extent={1152,4608};
    require(Readback::describe(c,l),"native geometry accepted");
    require(l.bytes==15925248 && l.planeCount==2,"native total bytes");
    require(l.planes[1].bufferOffset==10616832,"native chroma offset");
    require(l.planes[1].imageExtent.width==576 && l.planes[1].imageExtent.height==2304,"native half-size chroma extent");
    require(l.planes[0].imageSubresource.aspectMask==VK_IMAGE_ASPECT_PLANE_0_BIT && l.planes[1].imageSubresource.aspectMask==VK_IMAGE_ASPECT_PLANE_1_BIT,"native plane aspects");
    for (auto extent : {VkExtent2D{1,2},VkExtent2D{2,1},VkExtent2D{0,2},VkExtent2D{2,0},VkExtent2D{0xffffffffu,0xffffffffu},VkExtent2D{8192,8192}}) {
        c.extent=extent; require(!Readback::describe(c,l),"invalid geometry refused");
    }
    c.extent={2,2}; require(Readback::describe(c,l) && l.bytes==12 && l.planes[1].bufferOffset==8,"smallest even geometry aligned");
    c.format=VK_FORMAT_R8G8B8A8_UNORM; require(!Readback::describe(c,l),"RGBA not admitted in native mode");
    c.mode=Mode::OwnedRgbaFixture; require(Readback::describe(c,l) && l.bytes==16,"explicit owned generic fixture mode");
}

static void admissionControls() {
    Fake f; Readback r;
    require(Fake::prepare(r)==Status::Ok && f.fenceUnsignaled,"cold setup ready with own unsignaled fence");
    auto c=small(); const auto good=source(c); const auto gate=admission();
    const auto cb=handle<VkCommandBuffer>(61);
    View v; require(r.poll(v)==Status::NotReady && !v.data,"no bytes before recording");
    for(unsigned i=0;i<17;++i) {
        auto s=good;
        switch(i) {
        case 0:s.image=VK_NULL_HANDLE;break; case 1:s.generation=0;break;
        case 2:s.format=VK_FORMAT_R8G8B8A8_UNORM;break; case 3:s.extent.width+=2;break;
        case 4:s.extent.height+=2;break; case 5:s.extent.depth=2;break;
        case 6:s.type=VK_IMAGE_TYPE_3D;break; case 7:s.mipLevels=2;break;
        case 8:s.arrayLayers=2;break; case 9:s.samples=VK_SAMPLE_COUNT_2_BIT;break;
        case 10:s.flags=VK_IMAGE_CREATE_PROTECTED_BIT;break; case 11:s.flags=VK_IMAGE_CREATE_DISJOINT_BIT;break;
        case 12:s.flags=VK_IMAGE_CREATE_SPARSE_BINDING_BIT;break; case 13:s.usage=VK_IMAGE_USAGE_SAMPLED_BIT;break;
        case 14:s.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;break; case 15:s.layout=VK_IMAGE_LAYOUT_GENERAL;break;
        case 16:s.extent.width=7;break;
        }
        require(r.record(cb,s,gate)==Status::Refused && f.barriers==0 && f.copies==0,"invalid source emits no commands");
    }
    for(unsigned i=0;i<10;++i) {
        auto a=gate;
        switch(i) {
        case 0:a.commandGeneration=0;break;case 1:a.complete=false;break;case 2:a.primary=false;break;
        case 3:a.outsideRenderPass=false;break;case 4:a.afterMatchingDistortDraw=false;break;
        case 5:a.queueOwnsImage=false;break;case 6:a.beforeExternalRelease=false;break;case 7:a.ordinaryUnprotected=false;break;
        case 8:a.oneTimeSubmit=false;break;case 9:a.noSimultaneousUse=false;break;
        }
        require(r.record(cb,good,a)==Status::Refused && f.barriers==0,"unknown gate emits no commands");
    }
    require(r.record(VK_NULL_HANDLE,good,gate)==Status::Refused,"null command refused");
    require(r.record(cb,good,gate)==Status::Ok && f.barriers==2 && f.copies==1,"one copy enclosed by two barriers");
    require(f.copyCount==2 && f.copyRegions[1].bufferOffset==96,"both exact P010 planes copied");
    require(f.imageBarriers[0].oldLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && f.imageBarriers[0].newLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,"source transitions to transfer");
    require(f.imageBarriers[1].oldLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && f.imageBarriers[1].newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"source layout restored");
    require(f.hostBarrier.srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT && f.hostBarrier.dstAccessMask==VK_ACCESS_HOST_READ_BIT,"host visibility barrier");
    require(r.record(cb,good,gate)==Status::Refused && f.copies==1,"record one-shot");
    require(r.release()==Status::UnsafeToRelease && f.destroyed==0,"recorded work cannot be freed prematurely");
    require(r.submitCompletion(handle<VkQueue>(71),handle<VkCommandBuffer>(62),19,VK_SUCCESS)==Status::Refused && f.submits==0,"different submitted CB cannot complete copy");
    require(r.submitCompletion(handle<VkQueue>(71),cb,20,VK_SUCCESS)==Status::Refused && f.submits==0,"different command generation refused");
    require(r.submitCompletion(handle<VkQueue>(71),cb,19,VK_SUCCESS)==Status::Pending && f.submits==1 && !f.badSubmit,"own zero-count completion fence follows native success");
    require(r.poll(v)==Status::Pending && !v.data && f.invalidates==0,"unsignaled fence exposes no bytes");
    require(r.release()==Status::UnsafeToRelease,"pending copy remains owned");
    f.fenceResult=VK_SUCCESS;
    require(r.poll(v)==Status::Complete && v.data==f.bytes.data() && v.bytes==144,"bytes only after completion");
    require(v.sourceGeneration==9 && v.commandGeneration==19 && v.layout.extent.width==8,"recorded metadata published with bytes");
    require(f.invalidates==0,"coherent allocation needs no invalidate");
    require(r.release()==Status::Ok && f.destroyed==1 && f.freed==1 && f.unmapped==1,"completed resources safely released");
}

static void concurrentRecordControl() {
    Fake f; Readback r; require(Fake::prepare(r)==Status::Ok,"concurrent record setup");
    std::array<Status,8> outcomes{}; std::array<std::thread,8> workers;
    std::atomic<bool> go{false};
    for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&,i] {
        while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        outcomes[i]=r.record(handle<VkCommandBuffer>(61+i),source(small()),admission());
    });
    go.store(true,std::memory_order_release);
    for(auto& worker:workers)worker.join();
    require(std::count(outcomes.begin(),outcomes.end(),Status::Ok)==1 && f.copies==1,"concurrent candidate threads record exactly one bounded source");
    require(r.release(true)==Status::Ok,"joined concurrent fixture quiescent cleanup");
}

static void retirementControls() {
    Fake f; Readback r; require(Fake::prepare(r,false)==Status::Ok,"retirement mode cold preparation");
    auto a=admission(); a.oneTimeSubmit=false; a.retirementRequired=true;
    const auto cb=handle<VkCommandBuffer>(61); const auto queue=handle<VkQueue>(71);
    require(r.retireCommand(cb,19,true)==Status::Refused,"cannot retire unrecorded generation");
    auto simultaneous=a; simultaneous.noSimultaneousUse=false;
    require(r.record(cb,source(small()),simultaneous)==Status::Refused && f.copies==0,"retirement alternative still refuses simultaneous use");
    require(r.record(cb,source(small()),a)==Status::Ok,"explicit retirement mode accepts reusable CB");
    require(r.submitCompletion(queue,cb,19,VK_SUCCESS)==Status::Pending,"first reusable submit owns one completion fence");
    f.fenceResult=VK_SUCCESS;
    View v;
    require(r.poll(v)==Status::Pending && !v.data && r.state()==State::AwaitingRetirement && f.invalidates==0,"first fence success without retirement never publishes or invalidates bytes");
    for(unsigned i=0;i<10;++i)require(r.poll(v)==Status::Pending && !v.data && f.polls==1 && f.invalidates==0,"unretired generation remains unreadable without fence polling churn");
    require(r.submitCompletion(queue,cb,19,VK_SUCCESS)==Status::Refused && f.submits==1,"native replay cannot rearm own completion fence");
    require(r.retireCommand(handle<VkCommandBuffer>(62),19,true)==Status::Refused,"foreign CB retirement refused");
    require(r.retireCommand(cb,20,true)==Status::Refused,"different generation retirement refused");
    require(r.retireCommand(cb,19,false)==Status::Refused,"failed native retirement refused");
    require(r.poll(v)==Status::Pending && !v.data,"wrong retire facts do not unlock CPU bytes");
    require(r.retireCommand(cb,19,true)==Status::Ok,"exact successfully retired recorded generation published");
    require(r.poll(v)==Status::Complete && v.data && f.invalidates==1,"fence and retirement jointly unlock background invalidation/read");
    require(!v.oneTimeSubmit && v.retirementRequired && v.generationRetired,"actual reusable admission facts accompany bytes");
    require(r.release()==Status::UnsafeToRelease && f.destroyed==0,"retirement staging retained through publication until shutdown proof");
    require(r.release(true)==Status::Ok && f.destroyed==1,"joined device-quiescent retirement cleanup");
    require(r.retireCommand(cb,19,true)==Status::Refused,"released generation cannot revive");
    { Fake other; Readback beforeFence; require(Fake::prepare(beforeFence)==Status::Ok,"early retirement control preparation");
      require(beforeFence.record(cb,source(small()),a)==Status::Ok,"early retirement recording");
      beforeFence.submitCompletion(queue,cb,19,VK_SUCCESS);
      require(beforeFence.retireCommand(cb,19,true)==Status::Ok,"native CB may retire before later zero-submit fence is signaled");
      require(beforeFence.poll(v)==Status::Pending && !v.data,"retirement without own fence success also exposes no bytes");
      other.fenceResult=VK_SUCCESS;
      require(beforeFence.poll(v)==Status::Complete && v.generationRetired,"later own fence completes both independent proofs");
      beforeFence.release(true);
    }
}

static void failureControls() {
    { Fake f; Readback r; f.missing="vkQueueSubmit"; require(Fake::prepare(r)==Status::Refused && f.created==0,"missing next procedure refuses cold"); require(r.release()==Status::Ok,"failed cold setup safe release"); }
    { Fake f; Readback r; f.requirements=MaximumBytes+1; require(Fake::prepare(r)==Status::Refused && f.destroyed==1 && !f.freed,"oversized native allocation refused"); r.release(); }
    { Fake f; Readback r; f.createResult=VK_ERROR_OUT_OF_HOST_MEMORY; require(Fake::prepare(r)==Status::VulkanFailure,"cold Vulkan failure terminal"); r.release(); }
    { Fake f; Readback r; auto mem=Fake::memory(); mem.memoryTypes[0].propertyFlags=0; require(r.prepare(handle<VkDevice>(1),Fake::resolve,mem,small())==Status::Refused && f.destroyed==1,"host visible memory required"); r.release(); }
    for(unsigned fail=0;fail<4;++fail) {
        Fake f; Readback r; require(Fake::prepare(r,false)==Status::Ok,"noncoherent setup");
        auto cb=handle<VkCommandBuffer>(61); require(r.record(cb,source(small()),admission())==Status::Ok,"failure fixture recording");
        if(fail==0) {
            require(r.submitCompletion(handle<VkQueue>(71),cb,19,VK_ERROR_DEVICE_LOST)==Status::VulkanFailure && f.submits==0,"native failed submit unchanged and no diagnostic submit");
        } else {
            if(fail==1)f.submitResult=VK_ERROR_OUT_OF_HOST_MEMORY;
            const auto status=r.submitCompletion(handle<VkQueue>(71),cb,19,VK_SUCCESS);
            if(fail==1) require(status==Status::VulkanFailure,"own fence submission failure terminal");
            else {
                require(status==Status::Pending,"diagnostic pending");
                f.fenceResult=fail==2?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
                f.invalidateResult=VK_ERROR_MEMORY_MAP_FAILED;
                View v; require(r.poll(v)==Status::VulkanFailure && !v.data,"fence/invalidate failure exposes no bytes");
            }
        }
        require(r.state()==State::Failed && r.release()==Status::UnsafeToRelease && f.destroyed==0,"failed potentially pending copy retained");
        require(r.release(true)==Status::Ok && f.destroyed==1,"explicit device-quiescent proof permits cleanup");
    }
    { Fake f; Readback r; require(Fake::prepare(r,false)==Status::Ok,"noncoherent success prepare");
      auto cb=handle<VkCommandBuffer>(61); r.record(cb,source(small()),admission()); r.submitCompletion(handle<VkQueue>(71),cb,19,VK_SUCCESS);
      View v; require(r.poll(v)==Status::Pending && f.invalidates==0,"noncoherent invalidate not before fence");
      f.fenceResult=VK_SUCCESS; require(r.poll(v)==Status::Complete && f.invalidates==1 && v.data,"noncoherent complete invalidated once");
      require(r.poll(v)==Status::Complete && f.invalidates==1,"repeat polling immutable completed view"); r.release(); }
    { Fake f; Readback r; Config c{}; require(Fake::prepare(r,true,c)==Status::Ok && f.requested==MaximumBytes,"dynamic extent reserves fixed cold capacity");
      auto actual=small(); require(r.record(handle<VkCommandBuffer>(61),source(actual),admission())==Status::Ok && f.copyRegions[1].bufferOffset==96 && f.created==1,"dynamic live extent chosen without realloc");
      require(r.release()==Status::UnsafeToRelease,"dynamic pending resources not abandoned");
      require(r.release(true)==Status::Ok && f.destroyed==1 && f.unmapped==1,"joined terminal-device-lost/idle proof permits explicit child cleanup"); }
    { Fake f; Readback r; Config c{}; require(Fake::prepare(r,true,c)==Status::Ok,"dynamic negative setup");
      auto actual=small(); auto s=source(actual); s.extent={8192,8192,1};
      require(r.record(handle<VkCommandBuffer>(61),s,admission())==Status::Refused && f.barriers==0,"dynamic too-large source refused before commands");
      s.extent={7,6,1}; require(r.record(handle<VkCommandBuffer>(61),s,admission())==Status::Refused && f.barriers==0,"dynamic odd source refused"); r.release(); }
}

struct OwnedGpu {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkCommandPool pool{}; std::uint32_t family{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties properties{};
    struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{}; bool coherent=false; };
    struct Image { VkImage handle{}; VkDeviceMemory memory{}; };
    ~OwnedGpu() {
        if(device) { vkDeviceWaitIdle(device); vkDestroyCommandPool(device,pool,nullptr); vkDestroyDevice(device,nullptr); }
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    void init() {
        VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ci.pApplicationInfo=&app;
        vkcheck(vkCreateInstance(&ci,nullptr,&instance));
        std::uint32_t n=0; vkcheck(vkEnumeratePhysicalDevices(instance,&n,nullptr)); require(n!=0,"real Vulkan device exists");
        std::vector<VkPhysicalDevice> devices(n); vkcheck(vkEnumeratePhysicalDevices(instance,&n,devices.data())); physical=devices[0];
        vkGetPhysicalDeviceProperties(physical,&properties); vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr); std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
        bool found=false; for(unsigned i=0;i<n;++i) if(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family=i; found=true; break; }
        require(found,"real graphics queue exists");
        float priority=1; VkDeviceQueueCreateInfo q{}; q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; q.queueFamilyIndex=family; q.queueCount=1; q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{}; dc.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dc.queueCreateInfoCount=1; dc.pQueueCreateInfos=&q;
        vkcheck(vkCreateDevice(physical,&dc,nullptr,&device)); vkGetDeviceQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo pc{}; pc.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pc.queueFamilyIndex=family; pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkcheck(vkCreateCommandPool(device,&pc,nullptr,&pool));
    }
    unsigned type(std::uint32_t bits, VkMemoryPropertyFlags flags) {
        for(unsigned i=0;i<memory.memoryTypeCount;++i) if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Required fixture memory unavailable");
    }
    Buffer buffer(VkDeviceSize bytes,VkBufferUsageFlags usage) {
        Buffer b{}; VkBufferCreateInfo c{}; c.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; c.size=bytes; c.usage=usage;
        vkcheck(vkCreateBuffer(device,&c,nullptr,&b.handle)); VkMemoryRequirements r{}; vkGetBufferMemoryRequirements(device,b.handle,&r);
        auto t=type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); b.coherent=(memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        VkMemoryAllocateInfo a{}; a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; a.allocationSize=r.size; a.memoryTypeIndex=t;
        vkcheck(vkAllocateMemory(device,&a,nullptr,&b.memory)); vkcheck(vkBindBufferMemory(device,b.handle,b.memory,0));
        vkcheck(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped)); return b;
    }
    void release(Buffer b) { vkUnmapMemory(device,b.memory); vkDestroyBuffer(device,b.handle,nullptr); vkFreeMemory(device,b.memory,nullptr); }
    Image image(const Config& c) {
        Image i{}; VkImageCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; info.imageType=VK_IMAGE_TYPE_2D;
        info.format=c.format; info.extent={c.extent.width,c.extent.height,1}; info.mipLevels=info.arrayLayers=1;
        info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        vkcheck(vkCreateImage(device,&info,nullptr,&i.handle)); VkMemoryRequirements r{}; vkGetImageMemoryRequirements(device,i.handle,&r);
        VkMemoryAllocateInfo a{}; a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; a.allocationSize=r.size; a.memoryTypeIndex=type(r.memoryTypeBits,0);
        vkcheck(vkAllocateMemory(device,&a,nullptr,&i.memory)); vkcheck(vkBindImageMemory(device,i.handle,i.memory,0)); return i;
    }
    void release(Image i) { vkDestroyImage(device,i.handle,nullptr); vkFreeMemory(device,i.memory,nullptr); }
    bool supports(const Config& c) {
        VkImageFormatProperties p{};
        return vkGetPhysicalDeviceImageFormatProperties(physical,c.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,0,&p)==VK_SUCCESS
            && p.maxExtent.width>=c.extent.width && p.maxExtent.height>=c.extent.height;
    }
    VkCommandBuffer command() {
        VkCommandBufferAllocateInfo a{}; a.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; a.commandPool=pool; a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount=1;
        VkCommandBuffer cb{}; vkcheck(vkAllocateCommandBuffers(device,&a,&cb)); return cb;
    }
};

static void transition(VkCommandBuffer cb,VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags srcAccess,VkAccessFlags dstAccess,VkPipelineStageFlags src,VkPipelineStageFlags dst) {
    VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; b.srcAccessMask=srcAccess; b.dstAccessMask=dstAccess;
    b.oldLayout=before; b.newLayout=after; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cb,src,dst,0,0,nullptr,0,nullptr,1,&b);
}
static void realCopy(OwnedGpu& g,Mode mode,bool retirement=false) {
    auto c=small(mode);
    if(mode==Mode::NativeP010)c.extent={1152,4608};
    Layout l{}; require(Readback::describe(c,l),"real fixture layout valid");
    auto image=g.image(c); auto upload=g.buffer(l.bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT); auto verify=g.buffer(l.bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(l.bytes));
    for(std::size_t i=0;i<expected.size();++i)expected[i]=std::uint8_t(i*31u+7u);
    if(mode==Mode::NativeP010) for(std::size_t i=0;i<expected.size();i+=2) { const auto value=std::uint16_t(((i*17u+83u)&1023u)<<6); expected[i]=value&255; expected[i+1]=value>>8; }
    std::memcpy(upload.mapped,expected.data(),expected.size());
    if(!upload.coherent) { VkMappedMemoryRange r{}; r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; r.memory=upload.memory; r.size=VK_WHOLE_SIZE; vkcheck(vkFlushMappedMemoryRanges(g.device,1,&r)); }
    Config preparation=c;
    if(mode==Mode::NativeP010)preparation.extent={0,0};
    Readback reader; require(reader.prepare(g.device,vkGetDeviceProcAddr,g.memory,preparation)==Status::Ok,"real cold preparation including dynamic native capacity");
    View view; require(reader.poll(view)==Status::NotReady && !view.data,"real no bytes before submitted completion");
    auto cb=g.command(); VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags=retirement?0:VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkcheck(vkBeginCommandBuffer(cb,&begin));
    transition(cb,image.handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBufferToImage(cb,upload.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,l.planeCount,l.planes.data());
    transition(cb,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    auto s=source(c); s.image=image.handle;
    auto gate=admission(); gate.oneTimeSubmit=!retirement; gate.retirementRequired=retirement;
    require(reader.record(cb,s,gate)==Status::Ok,"real recording uses backend copy with actual admitted flags");
    // A SECOND independent readback immediately uses the restored source layout
    // and verifies original contents, not just the diagnostic staging buffer.
    transition(cb,image.handle,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(cb,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,verify.handle,l.planeCount,l.planes.data());
    transition(cb,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkBufferMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_HOST_READ_BIT; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.buffer=verify.handle; b.size=l.bytes;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    vkcheck(vkEndCommandBuffer(cb));
    VkFence original{}; VkFenceCreateInfo fc{}; fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; vkcheck(vkCreateFence(g.device,&fc,nullptr,&original));
    VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount=1; submit.pCommandBuffers=&cb;
    const auto native=vkQueueSubmit(g.queue,1,&submit,original); vkcheck(native);
    require(reader.submitCompletion(g.queue,cb,19,native)==Status::Pending,"real independent zero-submit completion fence");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    Status status{};
    if(retirement) {
        // Test-only waits prove valid non-simultaneous native replay. Production
        // readback hooks contain no wait and receive only successful retirement.
        vkcheck(vkWaitForFences(g.device,1,&original,VK_TRUE,10000000000ULL));
        do { status=reader.poll(view); std::this_thread::yield(); }
        while(status==Status::Pending && reader.state()!=State::AwaitingRetirement && std::chrono::steady_clock::now()<deadline);
        require(reader.state()==State::AwaitingRetirement && !view.data,"real first GPU fence ready still hides unretired mapped source");
        // Make the final execution observably different: publishing an earlier
        // copy would now fail byte equality, even if its first fence succeeded.
        for(std::size_t i=0;i<expected.size();++i)expected[i]^=std::uint8_t(mode==Mode::NativeP010 ? ((i&1u)?0x55u:0xc0u) : 0x55u);
        std::memcpy(upload.mapped,expected.data(),expected.size());
        if(!upload.coherent) { VkMappedMemoryRange r{}; r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; r.memory=upload.memory; r.size=VK_WHOLE_SIZE; vkcheck(vkFlushMappedMemoryRanges(g.device,1,&r)); }
        VkFence replay{}; vkcheck(vkCreateFence(g.device,&fc,nullptr,&replay));
        const auto replayResult=vkQueueSubmit(g.queue,1,&submit,replay); vkcheck(replayResult);
        require(reader.submitCompletion(g.queue,cb,19,replayResult)==Status::Refused,"real native second execution does not rearm diagnostic fence");
        require(reader.poll(view)==Status::Pending && !view.data,"real replay cannot expose mutable staging");
        vkcheck(vkWaitForFences(g.device,1,&replay,VK_TRUE,10000000000ULL));
        require(reader.retireCommand(cb,20,true)==Status::Refused && reader.retireCommand(cb,19,false)==Status::Refused,"real wrong or failed retirement does not publish");
        require(reader.poll(view)==Status::Pending && !view.data,"real completed second submission alone still insufficient");
        const auto resetResult=vkResetCommandBuffer(cb,0); vkcheck(resetResult);
        require(reader.retireCommand(cb,19,resetResult==VK_SUCCESS)==Status::Ok,"real successful original native reset retires final reusable generation");
        vkDestroyFence(g.device,replay,nullptr);
    }
    do { status=reader.poll(view); if(status==Status::Pending) std::this_thread::yield(); }
    while(status==Status::Pending && std::chrono::steady_clock::now()<deadline);
    require(status==Status::Complete && view.data && view.bytes==l.bytes,"real asynchronous completion returns bytes only after required proofs");
    require(view.oneTimeSubmit==!retirement && view.retirementRequired==retirement && view.generationRetired==retirement,"real view describes actual recording and retirement mode");
    require(view.layout.extent.width==c.extent.width && view.layout.extent.height==c.extent.height,"real view contains actual recorded extent");
    require(!std::memcmp(view.data,expected.data(),expected.size()),"all real readback bytes match original source");
    if(!verify.coherent) { VkMappedMemoryRange r{}; r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; r.memory=verify.memory; r.size=VK_WHOLE_SIZE; vkcheck(vkInvalidateMappedMemoryRanges(g.device,1,&r)); }
    require(!std::memcmp(verify.mapped,expected.data(),expected.size()),"source remains byte-for-byte unchanged after copy");
    require(vkGetFenceStatus(g.device,original)==VK_SUCCESS,"original native fence semantics preserved");
    if(retirement) {
        require(reader.release()==Status::UnsafeToRelease,"real retirement output retains allocation until shutdown");
        vkcheck(vkDeviceWaitIdle(g.device)); // Fixture-only explicit shutdown proof.
        require(reader.release(true)==Status::Ok,"real retired allocation released with joined idle proof");
    } else require(reader.release()==Status::Ok,"real one-time completion permits nonblocking cleanup");
    vkDestroyFence(g.device,original,nullptr); vkFreeCommandBuffers(g.device,g.pool,1,&cb);
    g.release(verify); g.release(upload); g.release(image);
}
int main(int argc,char** argv) {
    try {
        const std::string mode = argc>1 ? argv[1] : "all";
        if(mode!="all" && mode!="controls" && mode!="gpu")throw std::runtime_error("Expected all, controls, or gpu");
        if(mode!="gpu") { geometryControls(); admissionControls(); failureControls(); concurrentRecordControl(); retirementControls(); }
        if(mode=="controls") { std::cout << "{\"controls\":" << checks << ",\"real_gpu_tested\":false}\n"; return 0; }
        const auto controlCount=checks;
        OwnedGpu gpu; gpu.init();
        realCopy(gpu,Mode::OwnedRgbaFixture);
        auto nativeConfig=small(); nativeConfig.extent={1152,4608};
        const bool p010=gpu.supports(nativeConfig);
        if(p010)realCopy(gpu,Mode::NativeP010,true);
        else realCopy(gpu,Mode::OwnedRgbaFixture,true);
        std::cout << "{\"controls\":" << controlCount << ",\"total_checks\":" << checks
                  << ",\"device\":\"" << gpu.properties.deviceName << "\",\"real_rgba_readback\":true,\"real_source_unchanged\":true,\"native_fence_preserved\":true,\"real_p010_readback\":" << (p010?"true":"false")
                  << ",\"real_p010_reusable_native_executions\":" << (p010?2:0) << ",\"real_retirement_after_native_reset\":true,\"p010_test_extent\":[1152,4608],\"p010_dynamic_capacity\":67108864,\"p010_skip\":\"" << (p010?"":"driver format/usage unsupported; no P010 GPU proof claimed")
                  << "\",\"frame_hardware_tested\":false,\"native_source_association_proved\":false}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "pixel readback test failed: " << e.what() << '\n'; return 1; }
}
