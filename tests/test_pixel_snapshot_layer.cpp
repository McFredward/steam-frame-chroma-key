// Real Vulkan loader smoke for the production pixel-snapshot layer. No shader,
// PS_DISTORT draw, selected decoded resource, Frame or passthrough is supplied.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <dlfcn.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static unsigned checks = 0;
static void require(bool yes,const char* stage) {
    ++checks;
    if(!yes) { std::fprintf(stderr,"FAIL stage=%s\n",stage); std::fflush(stderr); std::exit(2); }
}
static void success(VkResult result,const char* stage) {
    ++checks;
    if(result!=VK_SUCCESS) { std::fprintf(stderr,"FAIL stage=%s VkResult=%d\n",stage,int(result)); std::fflush(stderr); std::exit(3); }
}
template<class T> static T function(PFN_vkVoidFunction p,const char* stage) {
    require(p!=nullptr,stage); return reinterpret_cast<T>(p);
}
static void waitForEvent(const char* path,const char* event) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(std::chrono::steady_clock::now()<end) {
        FILE* stream=std::fopen(path,"rb");
        if(stream) {
            std::array<char,4097> bytes{};
            const auto count=std::fread(bytes.data(),1,bytes.size()-1,stream);
            const bool bounded=!std::ferror(stream) && std::fgetc(stream)==EOF;
            std::fclose(stream);
            require(bounded,"bounded-background-status");
            bytes[count]=0;
            if(std::strstr(bytes.data(),event))return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // fixture only
    }
    require(false,"background-event-deadline");
}
static bool statusHas(const char* path,const char* token) {
    FILE* stream=std::fopen(path,"rb");
    if(!stream)return false;
    std::array<char,4097> bytes{};
    const auto count=std::fread(bytes.data(),1,bytes.size()-1,stream);
    const bool bounded=!std::ferror(stream) && std::fgetc(stream)==EOF;
    std::fclose(stream);require(bounded,"bounded-statistics-background-status");
    bytes[count]=0;return std::strstr(bytes.data(),token)!=nullptr;
}
int main(int argc,char** argv) {
    require(argc==3 || argc==5,"arguments: mode layer [status event]");
    const bool direct=!std::strcmp(argv[1],"direct");
    const bool mixed=!std::strcmp(argv[1],"mixed");
    const bool indexedStatistics=!std::strcmp(argv[1],"statistics-indexed");
    const bool statistics=indexedStatistics || !std::strcmp(argv[1],"statistics");
    require(direct || mixed || statistics || !std::strcmp(argv[1],"gpa"),"dispatch-mode");
    std::setvbuf(stdout,nullptr,_IONBF,0);
    // Probe the actual production export's null-dispatch guard separately from
    // loader global forwarding. It may legitimately return null for globals it
    // does not implement; the public loader below must retain native semantics.
    void* layer=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);require(layer,"production-library-open");
    auto exported=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(layer,"frame_pixel_vkGetInstanceProcAddr"));
    require(exported,"production-private-gipa-export");
    require(exported(VK_NULL_HANDLE,"vkGHVRUnknownDiagnosticFunction")==nullptr,"production-null-unknown-safe");
    require(exported(VK_NULL_HANDLE,"vkCreateInstance")!=nullptr,"production-create-global-provided");
    (void)exported(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties");
    require(true,"production-null-global-query-safe");
    auto negotiate=reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(dlsym(layer,"frame_pixel_vkNegotiateLoaderLayerInterfaceVersion"));
    require(negotiate,"production-negotiate-export");
    VkNegotiateLayerInterface negotiation{};negotiation.sType=LAYER_NEGOTIATE_INTERFACE_STRUCT;negotiation.loaderLayerInterfaceVersion=2;
    success(negotiate(&negotiation),"production-interface-negotiation");
    require(negotiation.loaderLayerInterfaceVersion==2 && negotiation.pfnGetInstanceProcAddr && negotiation.pfnGetDeviceProcAddr && negotiation.pfnGetPhysicalDeviceProcAddr,"production-negotiated-interfaces");
    void* loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);require(loader,"actual-loader-open");
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader,"vkGetInstanceProcAddr"));require(gipa,"public-loader-gipa");
    require(gipa(VK_NULL_HANDLE,"vkGHVRUnknownDiagnosticFunction")==nullptr,"loader-null-unknown-native");
    auto version=function<PFN_vkEnumerateInstanceVersion>(gipa(VK_NULL_HANDLE,"vkEnumerateInstanceVersion"),"loader-global-version");
    std::uint32_t api=0;success(version(&api),"loader-global-version-result");require(api>=VK_API_VERSION_1_1,"Vulkan-1.1-available");
    auto enumExt=function<PFN_vkEnumerateInstanceExtensionProperties>(gipa(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties"),"loader-global-extension-query");
    std::uint32_t extensionCount=0;success(enumExt(nullptr,&extensionCount,nullptr),"loader-global-extension-count");
    auto create=function<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE,"vkCreateInstance"),"loader-create-instance");
    VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.pApplicationName="Owned pixel snapshot layer smoke";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ci.pApplicationInfo=&app;
    VkInstance instance{};success(direct?::vkCreateInstance(&ci,nullptr,&instance):create(&ci,nullptr,&instance),"actual-instance-create");
    auto enumerate=function<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"),"enumerate-physical-proc");
    std::uint32_t n=0;success(enumerate(instance,&n,nullptr),"physical-count");require(n!=0,"physical-device-exists");
    std::vector<VkPhysicalDevice> physical(n);success(enumerate(instance,&n,physical.data()),"physical-device-enumeration");
    VkPhysicalDeviceProperties properties{};
    function<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"),"physical-properties-proc")(physical[0],&properties);
    VkPhysicalDeviceFeatures enabledFeatures{};
    bool indexedExtensionAvailable=false;
    unsigned enabledFeatureCount=0;
    if(statistics) {
        VkPhysicalDeviceFeatures supported{};
        function<PFN_vkGetPhysicalDeviceFeatures>(gipa(instance,"vkGetPhysicalDeviceFeatures"),"statistics-support-proc")(physical[0],&supported);
        if(indexedStatistics) {
            auto enumerateDeviceExtensions=function<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance,"vkEnumerateDeviceExtensionProperties"),"statistics-extension-enumeration-proc");
            std::uint32_t count=0;success(enumerateDeviceExtensions(physical[0],nullptr,&count,nullptr),"statistics-extension-count");
            std::vector<VkExtensionProperties> extensions(count);
            success(enumerateDeviceExtensions(physical[0],nullptr,&count,extensions.data()),"statistics-extension-list");
            for(const auto& extension:extensions)
                indexedExtensionAvailable=indexedExtensionAvailable || !std::strcmp(extension.extensionName,VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
        }
        if(supported.pipelineStatisticsQuery!=VK_TRUE || (indexedStatistics && !indexedExtensionAvailable)) {
            function<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"),"statistics-skip-instance-destroy")(instance,nullptr);
            require(!dlclose(loader) && !dlclose(layer),"statistics-skip-library-close");
            std::printf("RESULT={\"checks\":%u,\"statistics_skip\":\"%s\",\"statistics_feature_supported\":%s,\"statistics_feature_requested\":false,\"matched_capture\":false,\"native_frame\":false,\"mask_enabled\":false,\"teardown\":true}\n",
                checks,supported.pipelineStatisticsQuery!=VK_TRUE?"pipelineStatisticsQuery unsupported":"VK_EXT_transform_feedback unavailable",
                supported.pipelineStatisticsQuery==VK_TRUE?"true":"false");
            return 0;
        }
        // Actual vkCreateDevice input: one supported core feature, no Features2
        // or transform-feedback feature chain, and no sampling capture feature.
        enabledFeatures.pipelineStatisticsQuery=VK_TRUE;
        static_assert(sizeof(VkPhysicalDeviceFeatures)%sizeof(VkBool32)==0,"core feature words");
        std::array<VkBool32,sizeof(VkPhysicalDeviceFeatures)/sizeof(VkBool32)> featureWords{};
        std::memcpy(featureWords.data(),&enabledFeatures,sizeof(enabledFeatures));
        for(const auto feature:featureWords)enabledFeatureCount+=feature!=VK_FALSE;
        require(enabledFeatureCount==1,"statistics-only-actual-device-feature-input");
    }
    auto families=function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"),"queue-families-proc");
    families(physical[0],&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);families(physical[0],&n,queues.data());
    unsigned family=0;while(family<n && !(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;require(family<n,"graphics-family-exists");
    float priority=1;
    VkDeviceQueueCreateInfo q{};q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
    VkDeviceCreateInfo dc{};dc.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;
    const char* indexedExtension=VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
    if(statistics)dc.pEnabledFeatures=&enabledFeatures;
    if(indexedStatistics) {dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=&indexedExtension;}
    auto createDevice=function<PFN_vkCreateDevice>(gipa(instance,"vkCreateDevice"),"create-device-proc");
    VkDevice device{};success(direct?::vkCreateDevice(physical[0],&dc,nullptr,&device):createDevice(physical[0],&dc,nullptr,&device),"actual-device-create");
    auto gdpa=mixed?reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(loader,"vkGetDeviceProcAddr")):function<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"),"public-gdpa-interface");
    require(gdpa,"public-gdpa");
    require(gdpa(device,"vkGHVRUnknownDiagnosticFunction")==nullptr,"device-unknown-native");
    VkQueue queue{};function<PFN_vkGetDeviceQueue>(gdpa(device,"vkGetDeviceQueue"),"get-queue-proc")(device,family,0,&queue);require(queue,"actual-queue");
    if(argc==5)waitForEvent(argv[3],argv[4]);
    VkQueryPoolCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;qi.queryType=VK_QUERY_TYPE_OCCLUSION;qi.queryCount=2;
    auto queryCreate=function<PFN_vkCreateQueryPool>(gdpa(device,"vkCreateQueryPool"),"query-create-proc");
    VkQueryPool query{};success(direct||mixed?::vkCreateQueryPool(device,&qi,nullptr,&query):queryCreate(device,&qi,nullptr,&query),"actual-query-pool-create");
    VkQueryPool statisticsPool{};
    const std::uint32_t statisticsCount=indexedStatistics?2:1;
    if(statistics) {
        auto info=qi;info.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;info.queryCount=statisticsCount;
        info.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
        success(queryCreate(device,&info,nullptr,&statisticsPool),"statistics-pool-create");
    }
    VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};success(function<PFN_vkCreateCommandPool>(gdpa(device,"vkCreateCommandPool"),"create-command-pool-proc")(device,&pi,nullptr,&pool),"actual-command-pool-create");
    VkCommandBufferAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ai.commandPool=pool;ai.commandBufferCount=1;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer command{};success(function<PFN_vkAllocateCommandBuffers>(gdpa(device,"vkAllocateCommandBuffers"),"allocate-command-proc")(device,&ai,&command),"actual-command-allocate");
    VkCommandBufferBeginInfo bi{};bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; // zero flags, ordinary reusable native CB
    auto begin=function<PFN_vkBeginCommandBuffer>(gdpa(device,"vkBeginCommandBuffer"),"command-begin-proc");
    success(begin(command,&bi),"actual-command-begin");
    function<PFN_vkCmdResetQueryPool>(gdpa(device,"vkCmdResetQueryPool"),"query-reset-proc")(command,query,0,2);
    if(statistics) {
        function<PFN_vkCmdResetQueryPool>(gdpa(device,"vkCmdResetQueryPool"),"statistics-reset-proc")(command,statisticsPool,0,statisticsCount);
        function<PFN_vkCmdBeginQuery>(gdpa(device,"vkCmdBeginQuery"),"statistics-core-begin-proc")(command,statisticsPool,0,0);
        function<PFN_vkCmdEndQuery>(gdpa(device,"vkCmdEndQuery"),"statistics-core-end-proc")(command,statisticsPool,0);
        if(indexedStatistics) {
            // PIPELINE_STATISTICS is not a transform-feedback stream query:
            // index0 is required and needs no transformFeedback feature. The
            // graphics queue and reset/closed query scopes satisfy core rules.
            function<PFN_vkCmdBeginQueryIndexedEXT>(gdpa(device,"vkCmdBeginQueryIndexedEXT"),"statistics-indexed-begin-proc")(command,statisticsPool,1,0,0);
            function<PFN_vkCmdEndQueryIndexedEXT>(gdpa(device,"vkCmdEndQueryIndexedEXT"),"statistics-indexed-end-proc")(command,statisticsPool,1,0);
        }
    }
    success(function<PFN_vkEndCommandBuffer>(gdpa(device,"vkEndCommandBuffer"),"command-end-proc")(command),"actual-command-end");
    VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence{};success(function<PFN_vkCreateFence>(gdpa(device,"vkCreateFence"),"create-fence-proc")(device,&fi,nullptr,&fence),"actual-fence-create");
    VkSubmitInfo si{};si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;si.commandBufferCount=1;si.pCommandBuffers=&command;
    success(function<PFN_vkQueueSubmit>(gdpa(device,"vkQueueSubmit"),"queue-submit-proc")(queue,1,&si,fence),"actual-native-submit");
    success(function<PFN_vkWaitForFences>(gdpa(device,"vkWaitForFences"),"wait-native-fence-proc")(device,1,&fence,VK_TRUE,5000000000ULL),"actual-native-fence");
    std::array<std::uint64_t,4> values{~0ULL,~0ULL,~0ULL,~0ULL};
    auto getResults=function<PFN_vkGetQueryPoolResults>(gdpa(device,"vkGetQueryPoolResults"),"query-results-proc");
    const auto qr=getResults(device,query,0,2,sizeof(values),values.data(),2*sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    require(qr==VK_NOT_READY && values[1]==0 && values[3]==0,"exact-native-reset-query-not-ready-and-availability");
    if(statistics) {
        values.fill(~0ULL);
        success(getResults(device,statisticsPool,0,statisticsCount,sizeof(values),values.data(),2*sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),"statistics-native-results");
        for(std::uint32_t index=0;index<statisticsCount;++index)
            require(values[index*2]==0 && values[index*2+1]!=0,"statistics-native-empty-scope-zero-and-available");
        // The old layer may refuse this enabled feature cold. Finish its native
        // smoke so the Python negative control diagnoses complete=false instead
        // of failing merely because that older layer lacks this new event.
        if(argc==5 && statusHas(argv[3],"\"device_admission_complete\":true"))
            waitForEvent(argv[3],indexedStatistics?"\"query_routes_used\":3":"\"query_routes_used\":1");
    }
    success(function<PFN_vkResetCommandBuffer>(gdpa(device,"vkResetCommandBuffer"),"command-reset-proc")(command,0),"actual-command-reset");
    success(begin(command,&bi),"actual-command-rebegin");
    success(function<PFN_vkEndCommandBuffer>(gdpa(device,"vkEndCommandBuffer"),"command-end-again-proc")(command),"actual-command-end-again");
    function<PFN_vkFreeCommandBuffers>(gdpa(device,"vkFreeCommandBuffers"),"command-free-proc")(device,pool,1,&command);
    success(function<PFN_vkResetCommandPool>(gdpa(device,"vkResetCommandPool"),"command-pool-reset-proc")(device,pool,0),"actual-command-pool-reset");
    function<PFN_vkDestroyCommandPool>(gdpa(device,"vkDestroyCommandPool"),"command-pool-destroy-proc")(device,pool,nullptr);
    function<PFN_vkDestroyFence>(gdpa(device,"vkDestroyFence"),"fence-destroy-proc")(device,fence,nullptr);
    function<PFN_vkDestroyQueryPool>(gdpa(device,"vkDestroyQueryPool"),"query-destroy-proc")(device,query,nullptr);
    if(statisticsPool)function<PFN_vkDestroyQueryPool>(gdpa(device,"vkDestroyQueryPool"),"statistics-pool-destroy-proc")(device,statisticsPool,nullptr);
    VkImageCreateInfo ii{};ii.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent={16,16,1};ii.mipLevels=ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    VkImage image{};success(function<PFN_vkCreateImage>(gdpa(device,"vkCreateImage"),"image-create-proc")(device,&ii,nullptr,&image),"actual-image-create");
    VkMemoryRequirements req{};function<PFN_vkGetImageMemoryRequirements>(gdpa(device,"vkGetImageMemoryRequirements"),"image-requirements-proc")(device,image,&req);
    VkPhysicalDeviceMemoryProperties memoryProperties{};function<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"),"memory-properties-proc")(physical[0],&memoryProperties);
    unsigned type=0;while(type<memoryProperties.memoryTypeCount && !(req.memoryTypeBits&(1u<<type)))++type;require(type<memoryProperties.memoryTypeCount,"valid-image-memory-type");
    VkMemoryAllocateInfo ma{};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.allocationSize=req.size;ma.memoryTypeIndex=type;
    VkDeviceMemory memory{};success(function<PFN_vkAllocateMemory>(gdpa(device,"vkAllocateMemory"),"memory-allocate-proc")(device,&ma,nullptr,&memory),"actual-image-memory-allocate");
    success(function<PFN_vkBindImageMemory>(gdpa(device,"vkBindImageMemory"),"image-bind-proc")(device,image,memory,0),"actual-image-bind");
    VkImageViewCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;
    vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageView view{};success(function<PFN_vkCreateImageView>(gdpa(device,"vkCreateImageView"),"image-view-create-proc")(device,&vi,nullptr,&view),"actual-image-view-create");
    function<PFN_vkDestroyImageView>(gdpa(device,"vkDestroyImageView"),"image-view-destroy-proc")(device,view,nullptr);
    function<PFN_vkDestroyImage>(gdpa(device,"vkDestroyImage"),"image-destroy-proc")(device,image,nullptr);
    function<PFN_vkFreeMemory>(gdpa(device,"vkFreeMemory"),"memory-free-proc")(device,memory,nullptr);
    // The real Frame refused traceBindImageMemory merely because another
    // resource observer held the tracker mutex. Exercise the actual production
    // wrappers with independent native objects on simultaneous CPU threads.
    // All queue operations stay on the original thread; the workers never
    // share an image, memory allocation or view that needs external ownership.
    const auto createImage=function<PFN_vkCreateImage>(gdpa(device,"vkCreateImage"),"parallel-image-create-proc");
    const auto imageRequirements=function<PFN_vkGetImageMemoryRequirements>(gdpa(device,"vkGetImageMemoryRequirements"),"parallel-image-requirements-proc");
    const auto allocateMemory=function<PFN_vkAllocateMemory>(gdpa(device,"vkAllocateMemory"),"parallel-memory-allocate-proc");
    const auto bindImage=function<PFN_vkBindImageMemory>(gdpa(device,"vkBindImageMemory"),"parallel-image-bind-proc");
    const auto createView=function<PFN_vkCreateImageView>(gdpa(device,"vkCreateImageView"),"parallel-view-create-proc");
    const auto destroyView=function<PFN_vkDestroyImageView>(gdpa(device,"vkDestroyImageView"),"parallel-view-destroy-proc");
    const auto destroyImage=function<PFN_vkDestroyImage>(gdpa(device,"vkDestroyImage"),"parallel-image-destroy-proc");
    const auto freeMemory=function<PFN_vkFreeMemory>(gdpa(device,"vkFreeMemory"),"parallel-memory-free-proc");
    std::atomic<unsigned> ready{0},completed{0};
    std::atomic<bool> go{false},parallelOkay{true};
    std::array<std::thread,3> resourceThreads;
    for(auto& worker:resourceThreads)worker=std::thread([&] {
        ready.fetch_add(1,std::memory_order_release);
        while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned round=0;round<24;++round) {
            VkImage ownImage{};VkDeviceMemory ownMemory{};VkImageView ownView{};
            bool okay=createImage(device,&ii,nullptr,&ownImage)==VK_SUCCESS;
            if(okay) {
                VkMemoryRequirements ownRequirements{};
                imageRequirements(device,ownImage,&ownRequirements);
                unsigned ownType=0;
                while(ownType<memoryProperties.memoryTypeCount && !(ownRequirements.memoryTypeBits&(1u<<ownType)))++ownType;
                okay=ownType<memoryProperties.memoryTypeCount;
                if(okay) {
                    auto ownAllocation=ma;
                    ownAllocation.allocationSize=ownRequirements.size;ownAllocation.memoryTypeIndex=ownType;
                    okay=allocateMemory(device,&ownAllocation,nullptr,&ownMemory)==VK_SUCCESS;
                }
                if(okay)okay=bindImage(device,ownImage,ownMemory,0)==VK_SUCCESS;
                if(okay) {
                    auto ownViewInfo=vi;ownViewInfo.image=ownImage;
                    okay=createView(device,&ownViewInfo,nullptr,&ownView)==VK_SUCCESS;
                }
            }
            if(ownView)destroyView(device,ownView,nullptr);
            if(ownImage)destroyImage(device,ownImage,nullptr);
            if(ownMemory)freeMemory(device,ownMemory,nullptr);
            if(!okay){parallelOkay.store(false,std::memory_order_relaxed);break;}
            completed.fetch_add(1,std::memory_order_relaxed);
        }
    });
    while(ready.load(std::memory_order_acquire)!=resourceThreads.size())std::this_thread::yield();
    go.store(true,std::memory_order_release);
    for(auto& worker:resourceThreads)worker.join();
    require(parallelOkay.load() && completed.load()==72,"parallel-native-image-memory-view-lifecycles");
    // Fixture-only observation window for the existing 5-ms background writer;
    // do not tear it down before a sticky metadata refusal can be reported.
    if(argc==5)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    success(function<PFN_vkDeviceWaitIdle>(gdpa(device,"vkDeviceWaitIdle"),"device-idle-proc")(device),"actual-device-idle");
    function<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"),"device-destroy-proc")(device,nullptr);
    function<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"),"instance-destroy-proc")(instance,nullptr);
    require(!dlclose(loader) && !dlclose(layer),"native-library-close");
    if(statistics)
        std::printf("RESULT={\"checks\":%u,\"driver\":\"%s\",\"query_pool\":true,\"query_not_ready_native\":true,\"image_bind_view\":true,\"parallel_image_memory_view\":true,\"parallel_lifecycles\":72,\"command_submit_reset\":true,\"teardown\":true,\"matched_capture\":false,\"native_frame\":false,\"mask_enabled\":false,\"statistics_skip\":null,\"statistics_feature_supported\":true,\"statistics_feature_requested\":true,\"statistics_feature_enabled\":true,\"other_core_features_enabled\":%u,\"device_extension_count\":%u,\"statistics_core_query\":true,\"statistics_indexed_query\":%s,\"statistics_empty_counter_zero\":true,\"statistics_queries_available\":true}\n",
            checks,properties.deviceName,enabledFeatureCount-1,dc.enabledExtensionCount,indexedStatistics?"true":"false");
    else
        std::printf("RESULT={\"checks\":%u,\"driver\":\"%s\",\"query_pool\":true,\"query_not_ready_native\":true,\"image_bind_view\":true,\"parallel_image_memory_view\":true,\"parallel_lifecycles\":72,\"command_submit_reset\":true,\"teardown\":true,\"matched_capture\":false,\"native_frame\":false,\"mask_enabled\":false}\n",checks,properties.deviceName);
}
