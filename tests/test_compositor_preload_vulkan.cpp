// Real loader/device smoke fixture. It supplies no compositor Ready seal,
// native import record, Frame provider or mask/GPU association proof.
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

unsigned checks=0;
void require(bool value,const char* name) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL stage=%s\n",name);std::fflush(stderr);std::exit(2);}
}
void success(VkResult r,const char* name) {
    ++checks;if(r!=VK_SUCCESS){std::fprintf(stderr,"FAIL stage=%s VkResult=%d\n",name,int(r));std::fflush(stderr);std::exit(3);}
}
template<class T>T function(PFN_vkVoidFunction value,const char* name) {require(value!=nullptr,name);return reinterpret_cast<T>(value);}
int main(int argc,char** argv) {
    require(argc==2,"arguments");
    const bool global=!std::strcmp(argv[1],"global");
    const bool direct=!std::strcmp(argv[1],"direct");
    const bool mixed=!std::strcmp(argv[1],"mixed");
    require(global || direct || mixed || !std::strcmp(argv[1],"loader"),"mode");
    std::setvbuf(stdout,nullptr,_IONBF,0);
    void* loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);require(loader,"loader-dlopen");
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(global?RTLD_DEFAULT:loader,"vkGetInstanceProcAddr"));
    require(gipa,"loader-gipa");Dl_info source{};require(dladdr(reinterpret_cast<void*>(gipa),&source),"gipa-source");
    std::printf("GIPA_SOURCE=%s\n",source.dli_fname);
    auto ci=function<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE,"vkCreateInstance"),"create-instance-proc");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Frame preload ABI fixture";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
    VkInstance instance{};success(direct?::vkCreateInstance(&info,nullptr,&instance):ci(&info,nullptr,&instance),"create-instance");
    auto enum_phys=function<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"),"enumerate-physical-proc");
    std::uint32_t n=0;success(enum_phys(instance,&n,nullptr),"physical-count");require(n,"physical-present");
    std::vector<VkPhysicalDevice> physical(n);success(enum_phys(instance,&n,physical.data()),"physical-enumerate");
    auto families=function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"),"queue-family-proc");
    families(physical[0],&n,nullptr);std::vector<VkQueueFamilyProperties> props(n);families(physical[0],&n,props.data());
    unsigned family=0;while(family<n && !(props[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;require(family<n,"graphics-family");
    float priority=1.0f;VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
    auto cd=function<PFN_vkCreateDevice>(gipa(instance,"vkCreateDevice"),"create-device-proc");
    VkDevice device{};success(direct?::vkCreateDevice(physical[0],&device_info,nullptr,&device):cd(physical[0],&device_info,nullptr,&device),"create-device");
    auto gdpa=mixed?reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(RTLD_DEFAULT,"vkGetDeviceProcAddr")):
        function<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"),"device-proc-interface");
    require(gdpa,"global-device-proc-interface");
    source={};require(dladdr(reinterpret_cast<void*>(gdpa),&source),"gdpa-source");std::printf("GDPA_SOURCE=%s\n",source.dli_fname);
    VkQueue queue{};auto get_queue=function<PFN_vkGetDeviceQueue>(gdpa(device,"vkGetDeviceQueue"),"get-device-queue-proc");get_queue(device,family,0,&queue);require(queue,"device-queue");
    VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query_info.queryType=VK_QUERY_TYPE_OCCLUSION;query_info.queryCount=2;
    VkQueryPool query{};auto query_create=function<PFN_vkCreateQueryPool>(gdpa(device,"vkCreateQueryPool"),"query-pool-proc");
    success((direct||mixed)?::vkCreateQueryPool(device,&query_info,nullptr,&query):query_create(device,&query_info,nullptr,&query),"create-query-pool");
    auto query_destroy=function<PFN_vkDestroyQueryPool>(gdpa(device,"vkDestroyQueryPool"),"query-destroy-proc");query_destroy(device,query,nullptr);
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;image_info.format=VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent={16,16,1};image_info.mipLevels=1;image_info.arrayLayers=1;image_info.samples=VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;image_info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    VkImage image{};auto image_create=function<PFN_vkCreateImage>(gdpa(device,"vkCreateImage"),"image-create-proc");success(image_create(device,&image_info,nullptr,&image),"create-image");
    VkMemoryRequirements requirements{};auto get_reqs=function<PFN_vkGetImageMemoryRequirements>(gdpa(device,"vkGetImageMemoryRequirements"),"image-memory-requirements-proc");get_reqs(device,image,&requirements);
    VkPhysicalDeviceMemoryProperties memory_props{};auto get_props=function<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"),"memory-properties-proc");get_props(physical[0],&memory_props);
    unsigned type=0;while(type<memory_props.memoryTypeCount && !(requirements.memoryTypeBits&(1U<<type)))++type;require(type<memory_props.memoryTypeCount,"image-memory-type");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    VkDeviceMemory memory{};auto allocate=function<PFN_vkAllocateMemory>(gdpa(device,"vkAllocateMemory"),"allocate-memory-proc");success(allocate(device,&allocation,nullptr,&memory),"allocate-image-memory");
    auto bind=function<PFN_vkBindImageMemory>(gdpa(device,"vkBindImageMemory"),"bind-image-proc");success(bind(device,image,memory,0),"bind-image-memory");
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view_info.image=image;view_info.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view_info.format=image_info.format;view_info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageView view{};auto cv=function<PFN_vkCreateImageView>(gdpa(device,"vkCreateImageView"),"create-view-proc");success(cv(device,&view_info,nullptr,&view),"create-image-view");
    function<PFN_vkDestroyImageView>(gdpa(device,"vkDestroyImageView"),"destroy-view-proc")(device,view,nullptr);
    function<PFN_vkDestroyImage>(gdpa(device,"vkDestroyImage"),"destroy-image-proc")(device,image,nullptr);
    function<PFN_vkFreeMemory>(gdpa(device,"vkFreeMemory"),"free-memory-proc")(device,memory,nullptr);
    success(function<PFN_vkDeviceWaitIdle>(gdpa(device,"vkDeviceWaitIdle"),"wait-idle-proc")(device),"device-wait-idle");
    function<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"),"destroy-device-proc")(device,nullptr);
    function<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"),"destroy-instance-proc")(instance,nullptr);
    require(!dlclose(loader),"loader-dlclose");
    std::printf("RESULT={\"checks\":%u,\"query_pool\":true,\"image_bind_view\":true,\"teardown\":true,\"native_frame\":false}\n",checks);
}
