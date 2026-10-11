// VK_LAYER_MRCHROMA — chroma-key alpha injection for Steam Frame vrlink.
//
// Loaded as an implicit Vulkan layer, but refuses to load (negotiation fails,
// the loader skips it) in any process whose executable is not in the allow
// list (default: "vrlink").  Inside vrlink it:
//   mode=log    : records how frames flow (images, exports, passes, submits)
//   mode=rect   : writes alpha=0 inside a fixed rectangle of each target image
//   mode=chroma : writes alpha from a YCbCr-distance chroma key
// Config is re-read live from ~/.config/mrchroma/mrchroma.conf.

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "mrchroma_shaders.h" // generated: SPIR-V arrays

#define LAYER_NAME "VK_LAYER_MRCHROMA"
#define EXPORT extern "C" __attribute__((visibility("default")))

enum Role { ROLE_NONE, ROLE_VRLINK, ROLE_COMPOSITOR };
static Role g_role = ROLE_NONE;
struct DeviceData;
static void compositorSummary(DeviceData *d);

// ---------------------------------------------------------------- logging
static FILE *g_log = nullptr;
static std::mutex g_logMutex;
static double nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static void LOG(const char *fmt, ...) {
    std::lock_guard<std::mutex> l(g_logMutex);
    if (!g_log) {
        char p[256];
        snprintf(p, sizeof p, "/tmp/mrchroma-%d.log", getpid());
        g_log = fopen(p, "w");
        if (!g_log) return;
        setvbuf(g_log, nullptr, _IOLBF, 0);
    }
    fprintf(g_log, "[%.3f] ", nowMs());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
}

// ---------------------------------------------------------------- config
enum Mode { MODE_OFF = 0, MODE_LOG, MODE_RECT, MODE_CHROMA };
struct Config {
    Mode mode = MODE_LOG;
    float key[3] = {0.f, 1.f, 0.f};   // key colour, sRGB 0..1
    float threshold = 0.18f;           // CbCr distance fully keyed below this
    float softness = 0.10f;            // ramp width above threshold
    float spill = 0.5f;                // green spill suppression 0..1
    float rect[4] = {0.30f, 0.30f, 0.70f, 0.70f}; // x0 y0 x1 y1 (normalized)
    int minWidth = 1024;               // only process images at least this wide
    int target = -1;                   // -1 = auto; otherwise index into candidate list
    int logEvents = 4000;              // detailed event budget in log mode
    bool invert = false;               // rect mode: key outside instead of inside
    // compositor role: replace a named compositor shader with a patched SPIR-V file
    std::vector<std::pair<std::string, std::string>> overrides;
};
static Config g_cfg;
static std::mutex g_cfgMutex;
static time_t g_cfgMtime = 0;
static std::string cfgPath() {
    const char *e = getenv("MRCHROMA_CONFIG");
    if (e) return e;
    const char *h = getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/mrchroma/mrchroma.conf";
}
static void loadConfig(bool force) {
    struct stat st;
    std::string p = cfgPath();
    if (stat(p.c_str(), &st) != 0) return;
    if (!force && st.st_mtime == g_cfgMtime) return;
    g_cfgMtime = st.st_mtime;
    FILE *f = fopen(p.c_str(), "r");
    if (!f) return;
    Config c;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char k[64], v[160];
        if (line[0] == '#') continue;
        if (sscanf(line, " %63[^= ] = %159[^\n]", k, v) != 2) continue;
        std::string K = k;
        if (K == "mode") {
            if (!strncmp(v, "off", 3)) c.mode = MODE_OFF;
            else if (!strncmp(v, "log", 3)) c.mode = MODE_LOG;
            else if (!strncmp(v, "rect", 4)) c.mode = MODE_RECT;
            else if (!strncmp(v, "chroma", 6)) c.mode = MODE_CHROMA;
        } else if (K == "key") sscanf(v, "%f %f %f", &c.key[0], &c.key[1], &c.key[2]);
        else if (K == "threshold") c.threshold = atof(v);
        else if (K == "softness") c.softness = atof(v);
        else if (K == "spill") c.spill = atof(v);
        else if (K == "rect") sscanf(v, "%f %f %f %f", &c.rect[0], &c.rect[1], &c.rect[2], &c.rect[3]);
        else if (K == "min_width") c.minWidth = atoi(v);
        else if (K == "target") c.target = atoi(v);
        else if (K == "log_events") c.logEvents = atoi(v);
        else if (K == "invert") c.invert = atoi(v) != 0;
        else if (K == "override") {
            char a[128], b[256];
            if (sscanf(v, "%127s %255s", a, b) == 2) c.overrides.emplace_back(a, b);
        }
    }
    fclose(f);
    std::lock_guard<std::mutex> l(g_cfgMutex);
    g_cfg = c;
    LOG("config: mode=%d key=%.2f,%.2f,%.2f thr=%.3f soft=%.3f spill=%.2f rect=%.2f,%.2f,%.2f,%.2f minw=%d target=%d",
        c.mode, c.key[0], c.key[1], c.key[2], c.threshold, c.softness, c.spill, c.rect[0], c.rect[1], c.rect[2],
        c.rect[3], c.minWidth, c.target);
}
static Config cfg() {
    std::lock_guard<std::mutex> l(g_cfgMutex);
    return g_cfg;
}

// ---------------------------------------------------------------- dispatch
template <typename T> static void *key(T h) { return *(void **)h; }

struct InstanceData {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
};

struct ImageInfo {
    VkImageCreateInfo ci;
    bool exportable = false;   // created with external memory
    bool exported = false;     // memory was exported via vkGetMemoryFdKHR
    bool imported = false;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint64_t writes = 0;       // render-pass/copy/blit writes observed
    uint64_t lastFrameWritten = 0;
    int id = 0;
};
struct ViewInfo { VkImage image; VkImageViewCreateInfo ci; };
struct RPInfo { std::vector<VkAttachmentDescription> att; };
struct FBInfo { std::vector<VkImageView> views; uint32_t w, h, layers; VkRenderPass rp; };

// Per-target keying resources (one per image+layer, created lazily)
struct KeyTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t w = 0, h = 0;
};

struct DeviceData {
    VkDevice device;
    VkPhysicalDevice phys;
    InstanceData *inst;
    PFN_vkGetDeviceProcAddr gdpa;
#define DF(n) PFN_vk##n n = nullptr;
    DF(DestroyDevice) DF(CreateImage) DF(DestroyImage) DF(BindImageMemory) DF(BindImageMemory2) DF(AllocateMemory)
    DF(GetMemoryFdKHR) DF(CreateImageView) DF(DestroyImageView) DF(CreateFramebuffer) DF(DestroyFramebuffer)
    DF(CreateRenderPass) DF(CreateRenderPass2) DF(CmdBeginRenderPass) DF(CmdBeginRenderPass2) DF(CmdEndRenderPass)
    DF(CmdEndRenderPass2) DF(CmdBeginRendering) DF(CmdEndRendering) DF(CmdCopyImage) DF(CmdBlitImage)
    DF(QueueSubmit) DF(QueueSubmit2) DF(CreateSwapchainKHR) DF(BeginCommandBuffer) DF(EndCommandBuffer)
    DF(CmdPipelineBarrier) DF(CmdBindPipeline) DF(CmdDraw) DF(CmdBindDescriptorSets) DF(CmdPushConstants)
    DF(CreateShaderModule) DF(DestroyShaderModule) DF(CreatePipelineLayout) DF(CreateGraphicsPipelines)
    DF(CreateDescriptorSetLayout) DF(CreateDescriptorPool) DF(AllocateDescriptorSets) DF(UpdateDescriptorSets)
    DF(CmdSetViewport) DF(CmdSetScissor)
    DF(CreateQueryPool) DF(CmdWriteTimestamp) DF(GetQueryPoolResults) DF(CmdResetQueryPool)
    DF(CmdClearColorImage) DF(CmdResolveImage) DF(CmdDispatch) DF(CmdCopyBufferToImage)
    DF(CreateSampler) DF(CreateComputePipelines) DF(CreateSamplerYcbcrConversion)
#undef DF
    std::mutex m;
    std::unordered_map<VkImage, ImageInfo> images;
    std::unordered_map<VkDeviceMemory, VkImage> memToImage;
    std::unordered_set<VkDeviceMemory> exportMem, importMem;
    std::unordered_map<VkImageView, ViewInfo> views;
    std::unordered_map<VkRenderPass, RPInfo> rps;
    std::unordered_map<VkFramebuffer, FBInfo> fbs;
    std::unordered_map<VkCommandBuffer, VkFramebuffer> cbActiveFb;
    std::unordered_map<VkCommandBuffer, VkRenderPass> cbActiveRp;
    int nextImageId = 1;
    uint64_t submits = 0, frame = 0;
    int events = 0;
    double lastSummary = 0;

    // keying resources
    bool keyInit = false, keyFailed = false;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, KeyTarget> keyTargets; // (image ^ layer)
    std::unordered_map<uint64_t, VkPipeline> pipeCache;  // format|layout -> pipeline (keyed by rp)
    uint64_t keyedPasses = 0;

    // compositor role
    std::unordered_map<VkShaderModule, std::string> modName;
    std::unordered_set<VkSampler> ycbcrSamplers;
    std::unordered_set<VkDescriptorSetLayout> ycbcrDsl;
    std::unordered_set<VkPipelineLayout> ycbcrPl;
    std::unordered_map<VkPipeline, std::string> pipeDesc;
    std::unordered_map<VkPipeline, uint64_t> binds;
};

static std::mutex g_mapMutex;
static std::unordered_map<void *, InstanceData *> g_instances;
static std::unordered_map<void *, DeviceData *> g_devices;
static InstanceData *getInst(void *k) { std::lock_guard<std::mutex> l(g_mapMutex); return g_instances[k]; }
static DeviceData *getDev(void *k) { std::lock_guard<std::mutex> l(g_mapMutex); return g_devices[k]; }

static bool wantEvent(DeviceData *d) {
    Config c = cfg();
    if (c.mode == MODE_OFF) return false;
    if (d->events >= c.logEvents) return false;
    d->events++;
    return true;
}

// ---------------------------------------------------------------- instance
static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a,
                                                     VkInstance *out) {
    auto *chain = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    auto create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult r = create(ci, a, out);
    if (r != VK_SUCCESS) return r;
    auto *id = new InstanceData{};
    id->instance = *out;
    id->gipa = gipa;
    id->DestroyInstance = (PFN_vkDestroyInstance)gipa(*out, "vkDestroyInstance");
    id->GetPhysicalDeviceMemoryProperties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(*out, "vkGetPhysicalDeviceMemoryProperties");
    id->GetPhysicalDeviceFormatProperties =
        (PFN_vkGetPhysicalDeviceFormatProperties)gipa(*out, "vkGetPhysicalDeviceFormatProperties");
    {
        std::lock_guard<std::mutex> l(g_mapMutex);
        g_instances[key(*out)] = id;
    }
    loadConfig(true);
    LOG("CreateInstance app=%s engine=%s api=0x%x", ci->pApplicationInfo && ci->pApplicationInfo->pApplicationName ?
        ci->pApplicationInfo->pApplicationName : "?", ci->pApplicationInfo && ci->pApplicationInfo->pEngineName ?
        ci->pApplicationInfo->pEngineName : "?", ci->pApplicationInfo ? ci->pApplicationInfo->apiVersion : 0);
    for (uint32_t i = 0; i < ci->enabledExtensionCount; i++) LOG("  inst ext %s", ci->ppEnabledExtensionNames[i]);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance inst, const VkAllocationCallbacks *a) {
    InstanceData *id;
    {
        std::lock_guard<std::mutex> l(g_mapMutex);
        id = g_instances[key(inst)];
        g_instances.erase(key(inst));
    }
    id->DestroyInstance(inst, a);
    delete id;
}

// ---------------------------------------------------------------- device
static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo *ci,
                                                   const VkAllocationCallbacks *a, VkDevice *out) {
    auto *chain = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    InstanceData *id = getInst(key(phys));
    auto create = (PFN_vkCreateDevice)gipa(id ? id->instance : VK_NULL_HANDLE, "vkCreateDevice");
    VkResult r = create(phys, ci, a, out);
    if (r != VK_SUCCESS) return r;
    auto *d = new DeviceData{};
    d->device = *out;
    d->phys = phys;
    d->inst = id;
    d->gdpa = gdpa;
#define DF(n) d->n = (PFN_vk##n)gdpa(*out, "vk" #n);
    DF(DestroyDevice) DF(CreateImage) DF(DestroyImage) DF(BindImageMemory) DF(BindImageMemory2) DF(AllocateMemory)
    DF(GetMemoryFdKHR) DF(CreateImageView) DF(DestroyImageView) DF(CreateFramebuffer) DF(DestroyFramebuffer)
    DF(CreateRenderPass) DF(CreateRenderPass2) DF(CmdBeginRenderPass) DF(CmdBeginRenderPass2) DF(CmdEndRenderPass)
    DF(CmdEndRenderPass2) DF(CmdBeginRendering) DF(CmdEndRendering) DF(CmdCopyImage) DF(CmdBlitImage)
    DF(QueueSubmit) DF(QueueSubmit2) DF(CreateSwapchainKHR) DF(BeginCommandBuffer) DF(EndCommandBuffer)
    DF(CmdPipelineBarrier) DF(CmdBindPipeline) DF(CmdDraw) DF(CmdBindDescriptorSets) DF(CmdPushConstants)
    DF(CreateShaderModule) DF(DestroyShaderModule) DF(CreatePipelineLayout) DF(CreateGraphicsPipelines)
    DF(CreateDescriptorSetLayout) DF(CreateDescriptorPool) DF(AllocateDescriptorSets) DF(UpdateDescriptorSets)
    DF(CmdSetViewport) DF(CmdSetScissor) DF(CreateQueryPool) DF(CmdWriteTimestamp) DF(GetQueryPoolResults)
    DF(CmdResetQueryPool) DF(CmdClearColorImage) DF(CmdResolveImage) DF(CmdDispatch) DF(CmdCopyBufferToImage)
    DF(CreateSampler) DF(CreateComputePipelines) DF(CreateSamplerYcbcrConversion)
#undef DF
    if (!d->QueueSubmit2) d->QueueSubmit2 = (PFN_vkQueueSubmit2)gdpa(*out, "vkQueueSubmit2KHR");
    if (!d->CmdBeginRendering) d->CmdBeginRendering = (PFN_vkCmdBeginRendering)gdpa(*out, "vkCmdBeginRenderingKHR");
    if (!d->CmdEndRendering) d->CmdEndRendering = (PFN_vkCmdEndRendering)gdpa(*out, "vkCmdEndRenderingKHR");
    if (!d->CreateRenderPass2) d->CreateRenderPass2 = (PFN_vkCreateRenderPass2)gdpa(*out, "vkCreateRenderPass2KHR");
    {
        std::lock_guard<std::mutex> l(g_mapMutex);
        g_devices[key(*out)] = d;
    }
    LOG("CreateDevice ok, queues=%u", ci->queueCreateInfoCount);
    for (uint32_t i = 0; i < ci->enabledExtensionCount; i++) LOG("  dev ext %s", ci->ppEnabledExtensionNames[i]);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice dev, const VkAllocationCallbacks *a) {
    DeviceData *d;
    {
        std::lock_guard<std::mutex> l(g_mapMutex);
        d = g_devices[key(dev)];
        g_devices.erase(key(dev));
    }
    LOG("DestroyDevice (keyed passes=%llu)", (unsigned long long)d->keyedPasses);
    d->DestroyDevice(dev, a);
    delete d;
}

// ---------------------------------------------------------------- tracking
static const void *findPNext(const void *p, VkStructureType t) {
    auto *b = (const VkBaseInStructure *)p;
    while (b) {
        if (b->sType == t) return b;
        b = b->pNext;
    }
    return nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice dev, const VkImageCreateInfo *ci,
                                                  const VkAllocationCallbacks *a, VkImage *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->CreateImage(dev, ci, a, out);
    if (r != VK_SUCCESS) return r;
    auto *ext = (const VkExternalMemoryImageCreateInfo *)findPNext(ci->pNext,
                                                                   VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    auto *mod = findPNext(ci->pNext, VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    auto *modl = findPNext(ci->pNext, VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
    std::lock_guard<std::mutex> l(d->m);
    ImageInfo &ii = d->images[*out];
    ii.ci = *ci;
    ii.ci.pNext = nullptr;
    ii.exportable = ext != nullptr;
    ii.id = d->nextImageId++;
    if (wantEvent(d))
        LOG("CreateImage #%d %p fmt=%d %ux%ux%u layers=%u mips=%u samples=%d usage=0x%x flags=0x%x tiling=%d ext=0x%x "
            "drmmod=%s",
            ii.id, (void *)*out, ci->format, ci->extent.width, ci->extent.height, ci->extent.depth, ci->arrayLayers,
            ci->mipLevels, ci->samples, ci->usage, ci->flags, ci->tiling, ext ? ext->handleTypes : 0,
            mod ? "explicit" : modl ? "list" : "-");
    return r;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice dev, VkImage img, const VkAllocationCallbacks *a) {
    DeviceData *d = getDev(key(dev));
    {
        std::lock_guard<std::mutex> l(d->m);
        auto it = d->images.find(img);
        if (it != d->images.end()) {
            if (wantEvent(d)) LOG("DestroyImage #%d writes=%llu", it->second.id, (unsigned long long)it->second.writes);
            d->images.erase(it);
        }
        for (auto kt = d->keyTargets.begin(); kt != d->keyTargets.end();) {
            if (kt->second.image == img) {
                d->DestroyFramebuffer(dev, kt->second.fb, nullptr);
                d->DestroyImageView(dev, kt->second.view, nullptr);
                kt = d->keyTargets.erase(kt); // rp/pipeline/descriptor are leaked: tiny, rare
            } else ++kt;
        }
    }
    d->DestroyImage(dev, img, a);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice dev, const VkMemoryAllocateInfo *ai,
                                                     const VkAllocationCallbacks *a, VkDeviceMemory *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->AllocateMemory(dev, ai, a, out);
    if (r != VK_SUCCESS) return r;
    auto *exp = (const VkExportMemoryAllocateInfo *)findPNext(ai->pNext,
                                                              VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
    auto *imp = (const VkImportMemoryFdInfoKHR *)findPNext(ai->pNext, VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    auto *ded = (const VkMemoryDedicatedAllocateInfo *)findPNext(ai->pNext,
                                                                 VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    std::lock_guard<std::mutex> l(d->m);
    if (exp) d->exportMem.insert(*out);
    if (imp) d->importMem.insert(*out);
    if ((exp || imp) && wantEvent(d)) {
        int imgId = 0;
        if (ded && ded->image) {
            auto it = d->images.find(ded->image);
            if (it != d->images.end()) imgId = it->second.id;
        }
        LOG("AllocateMemory %p size=%llu %s handleType=0x%x dedicatedImg=#%d", (void *)*out,
            (unsigned long long)ai->allocationSize, exp ? "EXPORT" : "IMPORT",
            exp ? exp->handleTypes : imp->handleType, imgId);
    }
    return r;
}

static void noteBind(DeviceData *d, VkImage img, VkDeviceMemory mem) {
    auto it = d->images.find(img);
    if (it == d->images.end()) return;
    it->second.mem = mem;
    it->second.exported |= d->exportMem.count(mem) > 0;
    it->second.imported |= d->importMem.count(mem) > 0;
    d->memToImage[mem] = img;
}
static VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory(VkDevice dev, VkImage img, VkDeviceMemory mem, VkDeviceSize off) {
    DeviceData *d = getDev(key(dev));
    {
        std::lock_guard<std::mutex> l(d->m);
        noteBind(d, img, mem);
    }
    return d->BindImageMemory(dev, img, mem, off);
}
static VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory2(VkDevice dev, uint32_t n, const VkBindImageMemoryInfo *b) {
    DeviceData *d = getDev(key(dev));
    {
        std::lock_guard<std::mutex> l(d->m);
        for (uint32_t i = 0; i < n; i++) noteBind(d, b[i].image, b[i].memory);
    }
    return d->BindImageMemory2(dev, n, b);
}
static VKAPI_ATTR VkResult VKAPI_CALL GetMemoryFdKHR(VkDevice dev, const VkMemoryGetFdInfoKHR *gi, int *fd) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->GetMemoryFdKHR(dev, gi, fd);
    std::lock_guard<std::mutex> l(d->m);
    auto it = d->memToImage.find(gi->memory);
    int id = 0;
    if (it != d->memToImage.end()) {
        auto &ii = d->images[it->second];
        ii.exported = true;
        id = ii.id;
    }
    if (wantEvent(d)) LOG("GetMemoryFdKHR mem=%p -> image #%d handleType=0x%x", (void *)gi->memory, id, gi->handleType);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImageView(VkDevice dev, const VkImageViewCreateInfo *ci,
                                                      const VkAllocationCallbacks *a, VkImageView *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->CreateImageView(dev, ci, a, out);
    if (r != VK_SUCCESS) return r;
    std::lock_guard<std::mutex> l(d->m);
    ViewInfo &v = d->views[*out];
    v.image = ci->image;
    v.ci = *ci;
    v.ci.pNext = nullptr;
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice dev, const VkFramebufferCreateInfo *ci,
                                                        const VkAllocationCallbacks *a, VkFramebuffer *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->CreateFramebuffer(dev, ci, a, out);
    if (r != VK_SUCCESS) return r;
    std::lock_guard<std::mutex> l(d->m);
    FBInfo &f = d->fbs[*out];
    if (!(ci->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT))
        f.views.assign(ci->pAttachments, ci->pAttachments + ci->attachmentCount);
    f.w = ci->width;
    f.h = ci->height;
    f.layers = ci->layers;
    f.rp = ci->renderPass;
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice dev, const VkRenderPassCreateInfo *ci,
                                                       const VkAllocationCallbacks *a, VkRenderPass *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->CreateRenderPass(dev, ci, a, out);
    if (r != VK_SUCCESS) return r;
    std::lock_guard<std::mutex> l(d->m);
    auto &rp = d->rps[*out];
    rp.att.assign(ci->pAttachments, ci->pAttachments + ci->attachmentCount);
    if (wantEvent(d)) {
        for (uint32_t i = 0; i < ci->attachmentCount; i++)
            LOG("CreateRenderPass %p att%u fmt=%d load=%d store=%d init=%d final=%d", (void *)*out, i,
                ci->pAttachments[i].format, ci->pAttachments[i].loadOp, ci->pAttachments[i].storeOp,
                ci->pAttachments[i].initialLayout, ci->pAttachments[i].finalLayout);
    }
    return r;
}
static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass2(VkDevice dev, const VkRenderPassCreateInfo2 *ci,
                                                        const VkAllocationCallbacks *a, VkRenderPass *out) {
    DeviceData *d = getDev(key(dev));
    VkResult r = d->CreateRenderPass2(dev, ci, a, out);
    if (r != VK_SUCCESS) return r;
    std::lock_guard<std::mutex> l(d->m);
    auto &rp = d->rps[*out];
    for (uint32_t i = 0; i < ci->attachmentCount; i++) {
        auto &s = ci->pAttachments[i];
        VkAttachmentDescription ad{s.flags, s.format, s.samples, s.loadOp, s.storeOp, s.stencilLoadOp,
                                   s.stencilStoreOp, s.initialLayout, s.finalLayout};
        rp.att.push_back(ad);
        if (wantEvent(d))
            LOG("CreateRenderPass2 %p att%u fmt=%d load=%d store=%d init=%d final=%d", (void *)*out, i, s.format,
                s.loadOp, s.storeOp, s.initialLayout, s.finalLayout);
    }
    return r;
}

// ---------------------------------------------------------------- keying
// Fullscreen-triangle render pass that reads the target pixel as an input
// attachment and writes only the alpha channel (colorWriteMask = A).  On a
// tiler (Adreno) this is a single load/store of the tile with no extra copy.
struct PC {
    float key[4];       // key colour (rgb), mode in w (1=rect, 2=chroma)
    float params[4];    // threshold, softness, spill, invert
    float rect[4];      // x0 y0 x1 y1 normalized
    float size[4];      // width, height
};

static bool initKeyCommon(DeviceData *d) {
    if (d->keyInit) return !d->keyFailed;
    d->keyInit = true;
    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &b};
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC)};
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 64, 1, &ps};
    VkShaderModuleCreateInfo vsci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof(k_vs_spv), k_vs_spv};
    VkShaderModuleCreateInfo fsci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof(k_fs_spv), k_fs_spv};
    if (d->CreateDescriptorSetLayout(d->device, &dl, nullptr, &d->dsl) != VK_SUCCESS ||
        d->CreateDescriptorPool(d->device, &dp, nullptr, &d->dpool) != VK_SUCCESS ||
        d->CreateShaderModule(d->device, &vsci, nullptr, &d->vs) != VK_SUCCESS ||
        d->CreateShaderModule(d->device, &fsci, nullptr, &d->fs) != VK_SUCCESS) {
        LOG("key init failed (common)");
        d->keyFailed = true;
        return false;
    }
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &d->dsl, 1, &pcr};
    if (d->CreatePipelineLayout(d->device, &pl, nullptr, &d->pl) != VK_SUCCESS) {
        d->keyFailed = true;
        return false;
    }
    LOG("key init ok");
    return true;
}

static KeyTarget *getKeyTarget(DeviceData *d, VkImage img, const ImageInfo &ii, uint32_t layer, VkImageLayout layout) {
    uint64_t k = ((uint64_t)img) * 131 + layer * 7 + (uint64_t)layout;
    auto it = d->keyTargets.find(k);
    if (it != d->keyTargets.end()) return &it->second;
    if (!initKeyCommon(d)) return nullptr;
    KeyTarget t;
    t.image = img;
    t.layout = layout;
    t.w = ii.ci.extent.width;
    t.h = ii.ci.extent.height;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ii.ci.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
    if (d->CreateImageView(d->device, &vci, nullptr, &t.view) != VK_SUCCESS) return nullptr;
    // Render pass: the target is both input and colour attachment (GENERAL layout,
    // self-dependency).  Layout is restored to whatever the app left it in.
    VkAttachmentDescription ad{0, ii.ci.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                               VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                               VK_ATTACHMENT_STORE_OP_DONT_CARE, layout, layout};
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.inputAttachmentCount = 1;
    sp.pInputAttachments = &ref;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkSubpassDependency deps[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 0}};
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1, &ad, 1, &sp, 2, deps};
    if (d->CreateRenderPass(d->device, &rpci, nullptr, &t.rp) != VK_SUCCESS) return nullptr;
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, t.rp, 1, &t.view, t.w, t.h, 1};
    if (d->CreateFramebuffer(d->device, &fci, nullptr, &t.fb) != VK_SUCCESS) return nullptr;
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, d->dpool, 1, &d->dsl};
    if (d->AllocateDescriptorSets(d->device, &dai, &t.ds) != VK_SUCCESS) return nullptr;
    VkDescriptorImageInfo dii{VK_NULL_HANDLE, t.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = t.ds;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    w.pImageInfo = &dii;
    d->UpdateDescriptorSets(d->device, 1, &w, 0, nullptr);
    // Pipeline
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, d->vs, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, d->fs, "main"}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
                                              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp{0, 0, (float)t.w, (float)t.h, 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &vp, 1, &sc};
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    // RGBA: keyed pixels are written premultiplied (0,0,0,0) so the result is
    // correct whether the compositor blends straight or premultiplied alpha.
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = d->pl;
    gp.renderPass = t.rp;
    if (d->CreateGraphicsPipelines(d->device, VK_NULL_HANDLE, 1, &gp, nullptr, &t.pipe) != VK_SUCCESS) {
        LOG("pipeline create failed");
        return nullptr;
    }
    LOG("key target created: image #%d layer %u %ux%u fmt=%d layout=%d", ii.id, layer, t.w, t.h, ii.ci.format, layout);
    return &(d->keyTargets[k] = t);
}

// Record the key pass for (img, layer).  Must be called outside any render pass.
static void recordKey(DeviceData *d, VkCommandBuffer cb, VkImage img, const ImageInfo &ii, uint32_t layer,
                      VkImageLayout layout, const Config &c) {
    KeyTarget *t = getKeyTarget(d, img, ii, layer, layout);
    if (!t) return;
    VkClearValue cv{};
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, t->rp, t->fb, {{0, 0}, {t->w, t->h}}, 1, &cv};
    d->CmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, t->pipe);
    d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pl, 0, 1, &t->ds, 0, nullptr);
    PC pc{};
    pc.key[0] = c.key[0]; pc.key[1] = c.key[1]; pc.key[2] = c.key[2];
    pc.key[3] = c.mode == MODE_RECT ? 1.f : 2.f;
    pc.params[0] = c.threshold; pc.params[1] = c.softness; pc.params[2] = c.spill; pc.params[3] = c.invert ? 1.f : 0.f;
    memcpy(pc.rect, c.rect, sizeof pc.rect);
    pc.size[0] = (float)t->w; pc.size[1] = (float)t->h; pc.size[2] = (float)layer;
    VkFormat f = ii.ci.format;
    pc.size[3] = (f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_A8B8G8R8_SRGB_PACK32)
                     ? 1.f : 0.f;
    d->CmdPushConstants(cb, d->pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    d->CmdDraw(cb, 3, 1, 0, 0);
    d->CmdEndRenderPass(cb);
    d->keyedPasses++;
}

// Is this image a keying target under the current config?
static bool isTarget(DeviceData *d, const ImageInfo &ii, const Config &c) {
    if (c.mode != MODE_RECT && c.mode != MODE_CHROMA) return false;
    if ((int)ii.ci.extent.width < c.minWidth) return false;
    if (!ii.exported && !ii.exportable) return false;
    if (c.target >= 0 && ii.id != c.target) return false;
    return true;
}

// ---------------------------------------------------------------- command recording
static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo *bi,
                                                     VkSubpassContents sc) {
    DeviceData *d = getDev(key(cb));
    {
        std::lock_guard<std::mutex> l(d->m);
        d->cbActiveFb[cb] = bi->framebuffer;
        d->cbActiveRp[cb] = bi->renderPass;
        if (wantEvent(d)) {
            auto &fb = d->fbs[bi->framebuffer];
            std::string s;
            for (auto v : fb.views) {
                auto &vi = d->views[v];
                auto it = d->images.find(vi.image);
                char buf[96];
                snprintf(buf, sizeof buf, " #%d(fmt%d L%u%s)", it != d->images.end() ? it->second.id : 0, vi.ci.format,
                         vi.ci.subresourceRange.baseArrayLayer,
                         it != d->images.end() && it->second.exported ? ",EXP" : "");
                s += buf;
            }
            LOG("BeginRenderPass cb=%p rp=%p area=%ux%u atts:%s", (void *)cb, (void *)bi->renderPass,
                bi->renderArea.extent.width, bi->renderArea.extent.height, s.c_str());
        }
    }
    d->CmdBeginRenderPass(cb, bi, sc);
}

static void endPassCommon(DeviceData *d, VkCommandBuffer cb) {
    Config c = cfg();
    VkFramebuffer fbh;
    VkRenderPass rph;
    {
        std::lock_guard<std::mutex> l(d->m);
        fbh = d->cbActiveFb[cb];
        rph = d->cbActiveRp[cb];
        d->cbActiveFb.erase(cb);
        d->cbActiveRp.erase(cb);
    }
    if (g_role != ROLE_VRLINK) return;
    if (g_role != ROLE_VRLINK) return;
    if (c.mode != MODE_RECT && c.mode != MODE_CHROMA) return;
    // After the app's pass that wrote to an exported colour target, run the key pass.
    std::vector<std::tuple<VkImage, ImageInfo, uint32_t, VkImageLayout>> todo;
    {
        std::lock_guard<std::mutex> l(d->m);
        auto fit = d->fbs.find(fbh);
        auto rit = d->rps.find(rph);
        if (fit == d->fbs.end() || rit == d->rps.end()) return;
        for (size_t i = 0; i < fit->second.views.size() && i < rit->second.att.size(); i++) {
            auto vit = d->views.find(fit->second.views[i]);
            if (vit == d->views.end()) continue;
            auto iit = d->images.find(vit->second.image);
            if (iit == d->images.end()) continue;
            if (!(iit->second.ci.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) continue;
            if (!isTarget(d, iit->second, c)) continue;
            auto &sr = vit->second.ci.subresourceRange;
            uint32_t nl = sr.layerCount == VK_REMAINING_ARRAY_LAYERS ? iit->second.ci.arrayLayers - sr.baseArrayLayer
                                                                     : sr.layerCount;
            for (uint32_t L = 0; L < nl; L++)
                todo.emplace_back(iit->first, iit->second, sr.baseArrayLayer + L, rit->second.att[i].finalLayout);
        }
    }
    for (auto &t : todo) {
        std::lock_guard<std::mutex> l(d->m);
        recordKey(d, cb, std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t), c);
    }
}

static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cb) {
    DeviceData *d = getDev(key(cb));
    d->CmdEndRenderPass(cb);
    endPassCommon(d, cb);
}
static VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer cb, const VkRenderPassBeginInfo *bi,
                                                      const VkSubpassBeginInfo *sbi) {
    DeviceData *d = getDev(key(cb));
    {
        std::lock_guard<std::mutex> l(d->m);
        d->cbActiveFb[cb] = bi->framebuffer;
        d->cbActiveRp[cb] = bi->renderPass;
        if (wantEvent(d)) LOG("BeginRenderPass2 cb=%p rp=%p", (void *)cb, (void *)bi->renderPass);
    }
    d->CmdBeginRenderPass2(cb, bi, sbi);
}
static VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass2(VkCommandBuffer cb, const VkSubpassEndInfo *ei) {
    DeviceData *d = getDev(key(cb));
    d->CmdEndRenderPass2(cb, ei);
    endPassCommon(d, cb);
}
static VKAPI_ATTR void VKAPI_CALL CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *ri) {
    DeviceData *d = getDev(key(cb));
    {
        std::lock_guard<std::mutex> l(d->m);
        if (wantEvent(d)) {
            std::string s;
            for (uint32_t i = 0; i < ri->colorAttachmentCount; i++) {
                auto &vi = d->views[ri->pColorAttachments[i].imageView];
                auto it = d->images.find(vi.image);
                char buf[96];
                snprintf(buf, sizeof buf, " #%d(fmt%d L%u layout%d%s)", it != d->images.end() ? it->second.id : 0,
                         vi.ci.format, vi.ci.subresourceRange.baseArrayLayer, ri->pColorAttachments[i].imageLayout,
                         it != d->images.end() && it->second.exported ? ",EXP" : "");
                s += buf;
            }
            LOG("BeginRendering cb=%p area=%ux%u layers=%u atts:%s", (void *)cb, ri->renderArea.extent.width,
                ri->renderArea.extent.height, ri->layerCount, s.c_str());
        }
    }
    d->CmdBeginRendering(cb, ri);
}
static VKAPI_ATTR void VKAPI_CALL CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst,
                                               VkImageLayout dl, uint32_t n, const VkImageCopy *r) {
    DeviceData *d = getDev(key(cb));
    {
        std::lock_guard<std::mutex> l(d->m);
        if (wantEvent(d)) {
            auto s = d->images.find(src), t = d->images.find(dst);
            LOG("CmdCopyImage cb=%p #%d -> #%d%s regions=%u ext=%ux%u dstLayer=%u", (void *)cb,
                s != d->images.end() ? s->second.id : 0, t != d->images.end() ? t->second.id : 0,
                t != d->images.end() && t->second.exported ? "(EXP)" : "", n, r[0].extent.width, r[0].extent.height,
                r[0].dstSubresource.baseArrayLayer);
        }
    }
    d->CmdCopyImage(cb, src, sl, dst, dl, n, r);
}
static VKAPI_ATTR void VKAPI_CALL CmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst,
                                               VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) {
    DeviceData *d = getDev(key(cb));
    {
        std::lock_guard<std::mutex> l(d->m);
        if (wantEvent(d)) {
            auto s = d->images.find(src), t = d->images.find(dst);
            LOG("CmdBlitImage cb=%p #%d -> #%d%s regions=%u", (void *)cb, s != d->images.end() ? s->second.id : 0,
                t != d->images.end() ? t->second.id : 0, t != d->images.end() && t->second.exported ? "(EXP)" : "", n);
        }
    }
    d->CmdBlitImage(cb, src, sl, dst, dl, n, r, f);
}

static void summary(DeviceData *d) {
    double t = nowMs();
    if (t - d->lastSummary < 5000) return;
    if (d->lastSummary > 0) {
        Config c = cfg();
        LOG("summary: submits=%llu keyedPasses=%llu mode=%d", (unsigned long long)d->submits,
            (unsigned long long)d->keyedPasses, c.mode);
        if (g_role == ROLE_COMPOSITOR) compositorSummary(d);
    }
    d->lastSummary = t;
    loadConfig(false);
}

static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f) {
    DeviceData *d = getDev(key(q));
    {
        std::lock_guard<std::mutex> l(d->m);
        d->submits++;
        if (wantEvent(d)) {
            uint32_t cbs = 0, waits = 0, sigs = 0;
            for (uint32_t i = 0; i < n; i++) {
                cbs += s[i].commandBufferCount;
                waits += s[i].waitSemaphoreCount;
                sigs += s[i].signalSemaphoreCount;
            }
            std::string cl;
            for (uint32_t i = 0; i < n; i++)
                for (uint32_t j = 0; j < s[i].commandBufferCount; j++) {
                    char b[24];
                    snprintf(b, sizeof b, " %p", (void *)s[i].pCommandBuffers[j]);
                    cl += b;
                }
            LOG("QueueSubmit q=%p n=%u cbs=%u waits=%u sigs=%u fence=%p cb:%s", (void *)q, n, cbs, waits, sigs,
                (void *)f, cl.c_str());
        }
        summary(d);
    }
    return d->QueueSubmit(q, n, s, f);
}
static VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue q, uint32_t n, const VkSubmitInfo2 *s, VkFence f) {
    DeviceData *d = getDev(key(q));
    {
        std::lock_guard<std::mutex> l(d->m);
        d->submits++;
        if (wantEvent(d)) LOG("QueueSubmit2 q=%p n=%u", (void *)q, n);
        summary(d);
    }
    return d->QueueSubmit2(q, n, s, f);
}

#include "compositor_hooks.inc"

// ---------------------------------------------------------------- proc addr
#define HOOK(n) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)n;
static PFN_vkVoidFunction deviceHook(const char *name) {
    if (g_role == ROLE_COMPOSITOR) { // observe/override only; never touch the compositor's rendering
        HOOK(DestroyDevice) HOOK(CreateShaderModule) HOOK(CreateSampler) HOOK(CreateDescriptorSetLayout)
        HOOK(CreatePipelineLayout) HOOK(CreateGraphicsPipelines) HOOK(CreateComputePipelines) HOOK(CmdBindPipeline)
        HOOK(QueueSubmit) HOOK(QueueSubmit2) HOOK(CreateSamplerYcbcrConversion)
        if (!strcmp(name, "vkCreateSamplerYcbcrConversionKHR")) return (PFN_vkVoidFunction)CreateSamplerYcbcrConversion;
        if (!strcmp(name, "vkQueueSubmit2KHR")) return (PFN_vkVoidFunction)QueueSubmit2;
        return nullptr;
    }
    HOOK(DestroyDevice) HOOK(CreateImage) HOOK(DestroyImage) HOOK(AllocateMemory) HOOK(BindImageMemory)
    HOOK(BindImageMemory2) HOOK(GetMemoryFdKHR) HOOK(CreateImageView) HOOK(CreateFramebuffer) HOOK(CreateRenderPass)
    HOOK(CreateRenderPass2) HOOK(CmdBeginRenderPass) HOOK(CmdEndRenderPass) HOOK(CmdBeginRenderPass2)
    HOOK(CmdEndRenderPass2) HOOK(CmdBeginRendering) HOOK(CmdCopyImage) HOOK(CmdBlitImage) HOOK(QueueSubmit)
    HOOK(QueueSubmit2)
    if (!strcmp(name, "vkCreateRenderPass2KHR")) return (PFN_vkVoidFunction)CreateRenderPass2;
    if (!strcmp(name, "vkCmdBeginRenderPass2KHR")) return (PFN_vkVoidFunction)CmdBeginRenderPass2;
    if (!strcmp(name, "vkCmdEndRenderPass2KHR")) return (PFN_vkVoidFunction)CmdEndRenderPass2;
    if (!strcmp(name, "vkCmdBeginRenderingKHR")) return (PFN_vkVoidFunction)CmdBeginRendering;
    if (!strcmp(name, "vkQueueSubmit2KHR")) return (PFN_vkVoidFunction)QueueSubmit2;
    if (!strcmp(name, "vkBindImageMemory2KHR")) return (PFN_vkVoidFunction)BindImageMemory2;
    return nullptr;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mrchroma_GetDeviceProcAddr(VkDevice dev, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)mrchroma_GetDeviceProcAddr;
    DeviceData *d = getDev(key(dev));
    PFN_vkVoidFunction next = d->gdpa(dev, name);
    if (!next) return nullptr; // don't expose hooks for functions the driver lacks
    if (auto h = deviceHook(name)) return h;
    return next;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mrchroma_GetInstanceProcAddr(VkInstance inst, const char *name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)mrchroma_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)mrchroma_GetDeviceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)CreateInstance;
    if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)DestroyInstance;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)CreateDevice;
    if (inst) {
        InstanceData *id = getInst(key(inst));
        if (id) {
            PFN_vkVoidFunction next = id->gipa(inst, name);
            if (next)
                if (auto h = deviceHook(name)) return h;
            return next;
        }
    }
    return nullptr;
}

// Only load into allow-listed executables.  Failing negotiation makes the
// loader skip this layer, so every other process is completely untouched.
static std::string homePath(const char *rel) {
    const char *h = getenv("HOME");
    return std::string(h ? h : "/tmp") + rel;
}
// vrcompositor is opt-in (flag file) and protected by a crash guard: if it has
// started 3 times within 120 s with the layer, stop loading and leave a marker.
static bool compositorAllowed() {
    std::string flag = homePath("/.config/mrchroma/enable-compositor");
    std::string tripped = homePath("/.local/share/mrchroma/compositor-guard-tripped");
    std::string starts = homePath("/.local/share/mrchroma/compositor-starts");
    if (access(flag.c_str(), F_OK) != 0) return false;
    if (access(tripped.c_str(), F_OK) == 0) return false;
    time_t now = time(nullptr);
    std::vector<long> ts;
    if (FILE *f = fopen(starts.c_str(), "r")) {
        long t;
        while (fscanf(f, "%ld", &t) == 1)
            if (now - t < 120) ts.push_back(t);
        fclose(f);
    }
    ts.push_back(now);
    if (FILE *f = fopen(starts.c_str(), "w")) {
        for (long t : ts) fprintf(f, "%ld\n", t);
        fclose(f);
    }
    if (ts.size() >= 3) {
        if (FILE *f = fopen(tripped.c_str(), "w")) fclose(f);
        return false;
    }
    return true;
}
static bool processAllowed() {
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return false;
    exe[n] = 0;
    const char *base = strrchr(exe, '/');
    base = base ? base + 1 : exe;
    if (!strcmp(base, "vrcompositor")) {
        if (!compositorAllowed()) return false;
        g_role = ROLE_COMPOSITOR;
        return true;
    }
    const char *allow = getenv("MRCHROMA_PROCESSES");
    std::string list = allow ? allow : "vrlink";
    size_t p = 0;
    while (p <= list.size()) {
        size_t e = list.find(',', p);
        if (e == std::string::npos) e = list.size();
        if (list.substr(p, e - p) == base) {
            g_role = ROLE_VRLINK;
            return true;
        }
        p = e + 1;
    }
    return false;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL mrchroma_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (!processAllowed()) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = mrchroma_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = mrchroma_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
