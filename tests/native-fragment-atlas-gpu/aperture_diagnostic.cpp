// Bounded offscreen diagnostic for separately compiled FP module variants.
// A neutral-chroma, unit-G owned source and unit tint make the pinned YCbCr
// decoder's original RGB equal its aperture, without changing native code.
#define main frame_ssa_fixture_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main

int main(int argc,char** argv){
    try{
        require(argc==5,"usage: aperture-diagnostic VERTEX ORIGINAL PROBE ONE_ATLAS_CASE");
        std::ifstream file(argv[4],std::ios::binary|std::ios::ate);
        require(bool(file) && file.tellg()==200,"exact one-case fixture range");
        file.seekg(0);char magic[8]{};std::uint32_t count{};Case c{};
        static_assert(sizeof(Case)==88,"owned native-input struct layout");
        require(bool(file.read(magic,8)) && std::memcmp(magic,"ATLQRY1",8)==0,"owned case schema");
        require(bool(file.read(reinterpret_cast<char*>(&count),4)) && count==1,"one owned diagnostic case");
        require(bool(file.read(reinterpret_cast<char*>(&c),sizeof(c))),"complete own native input");
        require(((c.flags>>16)&4)==0,"neutral source diagnostic requires YCbCr branch");
        c.tint={1,1,1,1};Gpu gpu;gpu.init();require(gpu.validationEnabled,"active validation required");gpu.resources();
        auto* pixels=static_cast<float*>(gpu.buffers[gpu.upload].mapped);
        for(unsigned i=0;i<16*16;++i){pixels[i*4]=pixels[i*4+2]=.501960814f;pixels[i*4+1]=pixels[i*4+3]=1;}
        gpu.flush(gpu.upload);gpu.begin();
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=gpu.images[gpu.source].value;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};
        vkCmdCopyBufferToImage(gpu.command,gpu.buffers[gpu.upload].value,gpu.images[gpu.source].value,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);gpu.finish();
        gpu.pipelines(argv[1],argv[2],argv[3]);
        const auto a=gpu.draw(c,gpu.original),p=gpu.draw(c,gpu.probe),z=gpu.draw(c,gpu.original);
        require(std::equal(a.begin(),a.begin()+4,z.begin()),"stock original did not repeat exactly");
        require(validationErrors.load()==0,"neutral diagnostic validation errors");
        std::cout<<"{\"schema\":\"native-neutral-aperture-diagnostic-v1\",\"original0\":[";
        for(unsigned i=0;i<4;++i)std::cout<<(i?",":"")<<a[i];
        std::cout<<"],\"probe0\":[";for(unsigned i=0;i<4;++i)std::cout<<(i?",":"")<<p[i];
        std::cout<<"],\"original_again0\":[";for(unsigned i=0;i<4;++i)std::cout<<(i?",":"")<<z[i];
        std::cout<<"],\"probe_mrt3_words\":[";for(unsigned i=0;i<4;++i)std::cout<<(i?",":"")<<p[12+i];
        std::cout<<"],\"validation_enabled\":true,\"validation_errors\":0,\"passthrough_authorized\":false}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"aperture diagnostic failed: "<<e.what()<<'\n';return 1;}
}
