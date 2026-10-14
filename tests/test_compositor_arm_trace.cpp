// Exercise the actual raw-mode layer's producer gate and existing background
// writer. Public scalar fixtures below are not native import or GPU evidence.
#define FRAME_MASK_COMPOSITOR_RAW 1
#define FRAME_MASK_PRIVATE_VULKAN_EXPORTS 1
#include "../layer/passive_pipeline_trace.cpp"
#include <fstream>

namespace {
unsigned checks=0;
void require(bool ok) {++checks;if(!ok){std::fprintf(stderr,"arm trace check %u failed\n",checks);std::abort();}}
std::string read_file(const std::string& path) {
    std::ifstream input(path);return std::string(std::istreambuf_iterator<char>(input),{});
}
}
int main(int argc,char**) {
    const bool armed_run=argc==1;
    char pattern[]="/tmp/frame-raw-arm-trace-XXXXXX";const auto directory=mkdtemp(pattern);require(directory);
    const std::string root(directory),journal=root+"/journal.jsonl",marker=root+"/capture-arm.marker";
    auto& c=capture();c.bindingsEnabled=true;c.core.enable(true);c.importProcessAdmitted=true;
    require(c.importReceipts.initialize_before_threads(true));
    std::thread writer([&]{c.write(journal,marker);});
    for(unsigned i=0;i<1000 && !c.importWriterReady;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(c.importWriterReady==1 && !c.bindingsArmed && !c.drawsReady());
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={4,4,1};
    image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    observeBindings([&](auto& bindings){bindings.createImage(1,reinterpret_cast<VkImage>(2),image);});
    // More than the complete event/snapshot capacity before the operator signal
    // must still reserve every draw and resource-snapshot slot for the stream.
    for(unsigned i=0;i<2400;++i)recordedDraw(1,reinterpret_cast<VkCommandBuffer>(3),1);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    require(!c.bindingsArmed && c.bindingSnapshots==0 && c.events.emitted()==0 && !c.events.dropped());
    if(armed_run) {
        const int fd=open(marker.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);require(fd>=0);
        require(write(fd,frame_compositor_arm::Contents,sizeof(frame_compositor_arm::Contents)-1)==sizeof(frame_compositor_arm::Contents)-1);
        require(!close(fd));
        for(unsigned i=0;i<1000 && !c.bindingsArmed;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!c.bindingsArmed || !c.drawsReady() || c.events.emitted()!=1)
            std::fprintf(stderr,"armed=%d ready=%d enabled=%d emitted=%llu dropped=%llu\n",int(c.bindingsArmed.load()),int(c.drawsReady()),int(c.core.enabled()),
                static_cast<unsigned long long>(c.events.emitted()),static_cast<unsigned long long>(c.events.dropped()));
        require(c.bindingsArmed && c.drawsReady() && c.events.emitted()==1);
        // A deliberately unknown command is reported as incomplete, never as
        // native frame ownership. It still proves that arming opens this gate.
        recordedDraw(1,reinterpret_cast<VkCommandBuffer>(3),1);
        require(c.bindingSnapshots==0 && c.events.emitted()==2);
    }
    c.stop=true;writer.join();const auto text=read_file(journal);
    require(text.size()<1048576 && text.find("\"binding_arm_required\":true")!=std::string::npos);
    require(text.find("\"binding_ready_at_header\":false")!=std::string::npos);
    require(text.find("\"published\":1,\"replayed\":1")!=std::string::npos);
    require(text.find("\"byte_cap_reached\":false")!=std::string::npos);
    require((text.find("\"event\":\"bindings_armed\"")!=std::string::npos)==armed_run);
    require((text.find("\"event\":\"draw_recorded\"")!=std::string::npos)==armed_run);
    require(!unlink(journal.c_str()));if(armed_run)require(!unlink(marker.c_str()));require(!rmdir(root.c_str()));
    std::printf("PASS: %u production raw-mode writer controls; no native admission or GPU proof\n",checks);
}
