// Reuse the approved owned Vulkan harness, including its array[3] descriptors
// and failure cleanup. No Valve module or native source is embedded here.
#define main frame_ssa_fixture_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main

namespace {
struct AtlasCase {
    std::uint32_t eye, flags;
    Vec u, r0, r1, patch, tint;
    std::array<float,12> expected; // MRT1, MRT2, MRT3, in that order.
    Vec colorU, maskU;
    std::array<float,5> oracle; // panel apertures; inverse-decoder raw-G weights.
};
static_assert(sizeof(AtlasCase)==188,"fixed private fixture record layout");
std::vector<AtlasCase> readCases(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"owned case input unavailable");
    auto bytes=file.tellg();require(bytes>=12 && bytes<=12+64*188,"bounded owned case file");
    file.seekg(0);char magic[8]{};std::uint32_t count{};
    require(bool(file.read(magic,8)) && std::memcmp(magic,"ATLQRY1",8)==0,"owned fixture schema");
    require(bool(file.read(reinterpret_cast<char*>(&count),4)) && count>0 && count<=64,"bounded case count");
    require(bytes==std::streamoff(12+count*188),"complete fixed case range");
    std::vector<AtlasCase> result(count);require(bool(file.read(reinterpret_cast<char*>(result.data()),count*188)),"complete owned cases");
    for(const auto& c:result){
        require(c.eye<2 && (c.expected[11]==0 || c.expected[11]==1),"valid owned eye/enable flag");
        for(const auto* v:{&c.u,&c.r0,&c.r1,&c.patch,&c.tint,&c.colorU,&c.maskU})
            for(float f:*v)require(std::isfinite(f),"finite owned coordinate/metadata");
        for(float f:c.expected)require(std::isfinite(f),"finite owned expected value");
        for(float f:c.oracle)require(std::isfinite(f),"finite owned oracle coefficient");
    }
    return result;
}
float floating(std::uint32_t v){float f;std::memcpy(&f,&v,4);return f;}
std::uint32_t ordered(std::uint32_t v){return (v&0x80000000u)?~v:(v|0x80000000u);}
unsigned unorm8(float f){return static_cast<unsigned>(std::floor(std::max(0.f,std::min(1.f,f))*255.f+.5f));}
void near(float actual,float expected,const char* name){
    require(std::isfinite(actual) && std::abs(actual-expected)<=.000025f,std::string("atlas ")+name+" differs");
}
Case nativeCase(const AtlasCase& c){return {c.eye,c.flags,c.u,c.r0,c.r1,c.patch,c.tint};}
}
int main(int argc,char** argv){
    try{
        require(argc==5,"usage: atlas-fixture VERTEX ORIGINAL ATLAS CASES");
        const auto input=readCases(argv[4]);Gpu gpu;gpu.init();
        require(gpu.validationEnabled,"Khronos validation must be active from initialization");
        gpu.resources();gpu.pipelines(argv[1],argv[2],argv[3]);
        unsigned complete=0,colorGpuChecks=0,maskGpuChecks=0,unequalWeights=0,fractionalApertures=0;
        unsigned bitIdenticalCases=0,unorm8CodeChanges=0;float maximumAbsolute=0;std::uint64_t maximumUlp=0;
        for(const auto& c:input){
            const auto n=nativeCase(c);
            const auto old=gpu.draw(n,gpu.original),observed=gpu.draw(n,gpu.probe);
            // Separate module compilation can alter permitted native FP
            // contraction/CSE. The retained neutral-source experiment isolated
            // a1.49e-6 aperture difference. This INERT host diagnostic uses the
            // root-approved2e-6 absolute bound; it is not live override admission.
            bitIdenticalCases+=std::equal(old.begin(),old.begin()+4,observed.begin());
            for(unsigned i=0;i<4;++i){
                const float a=floating(old[i]),b=floating(observed[i]);
                require(std::isfinite(a) && std::isfinite(b),"finite original outputs");
                const float error=std::abs(a-b);maximumAbsolute=std::max(maximumAbsolute,error);
                const auto x=ordered(old[i]),y=ordered(observed[i]);
                maximumUlp=std::max(maximumUlp,static_cast<std::uint64_t>(x>y?x-y:y-x));
                unorm8CodeChanges+=unorm8(a)!=unorm8(b);
                require(error<=.000002f,"atlas original output0 exceeds explicit2e-6 FP bound");
            }
            require(observed[3]==old[3],"original output0 alpha differs");
            require(old[3]==bits(1.f),"native original rasterized");
            if(c.expected[11]==0){
                require(std::equal(observed.begin(),observed.begin()+4,observed.begin()+4),"disabled output1 differs bitwise from same-module original output0");
            }else{
                for(unsigned i=0;i<4;++i)near(floating(observed[4+i]),c.expected[i],i==3?"raw component-G":"retargeted color");
                Case color=n;color.u=c.colorU;
                Case mask=n;mask.u=c.maskU;mask.tint={1,1,1,1};
                // Independent comparison instances of the UNCHANGED original PS,
                // not the added clone, consume the retargeted raster inputs.
                const auto nativeColor=gpu.draw(color,gpu.original),nativeMask=gpu.draw(mask,gpu.original);
                if(c.oracle[0]>.0001f){
                    for(unsigned i=0;i<3;++i)near(floating(observed[4+i]),floating(nativeColor[i])/c.oracle[0]*c.expected[10],"original-PS retargeted color oracle");
                    ++colorGpuChecks;
                }
                if(c.oracle[1]>.0001f){
                    // Both pinned decoders are linear in sampled components.
                    // The supplied inverse coefficients remove decoder chroma;
                    // unit tint and division remove ONLY this panel's aperture.
                    float g=0;for(unsigned i=0;i<3;++i)g+=floating(nativeMask[i])*c.oracle[2+i];
                    near(floating(observed[7]),g/c.oracle[1],"original-PS inverse-decoder raw-G oracle");++maskGpuChecks;
                }
                unequalWeights+=std::abs(c.expected[8]-c.expected[9])>.01f;
                fractionalApertures+=c.expected[10]>.01f && c.expected[10]<.99f;
            }
            for(unsigned i=0;i<8;++i)near(floating(observed[8+i]),c.expected[4+i],i<4?"mapped coordinates":"independent weights/aperture/enable");
            for(unsigned i=0;i<3;++i)require(observed[16+i]==bits(c.tint[i]),"atlas tint bits changed");
            require(observed[19]==c.flags,"atlas flags bits changed");++complete;
        }
        const bool enabled=input.front().expected[11]==1;
        require(!enabled || (colorGpuChecks>=16 && maskGpuChecks>=16),"enough independent original-PS GPU oracle cases");
        require(!enabled || unequalWeights>=4,"independent color/mask weights exercised");
        require(!enabled || fractionalApertures>=4,"original-q partial aperture exercised");
        require(validationErrors.load()==0,"atlas Vulkan validation errors");
        std::cout<<"{\"schema\":\"frame-native-fragment-atlas-gpu-controls-v1\",\"cases\":"<<complete<<",\"checks\":"<<checks
            <<",\"validation_enabled\":true,\"validation_errors\":"<<validationErrors.load()<<",\"device_vendor\":"<<gpu.properties.vendorID
            <<",\"device_name\":\""<<gpu.properties.deviceName<<"\",\"original_color_gpu_oracles\":"<<colorGpuChecks
            <<",\"original_mask_gpu_oracles\":"<<maskGpuChecks<<",\"unequal_weight_cases\":"<<unequalWeights<<",\"partial_aperture_cases\":"<<fractionalApertures
            <<",\"enabled\":"<<(enabled?"true":"false")<<",\"output0_bit_identical\":"<<(bitIdenticalCases==complete?"true":"false")
            <<",\"output0_bit_identical_cases\":"<<bitIdenticalCases<<",\"output0_max_absolute_error\":"<<maximumAbsolute
            <<",\"output0_max_ulp_distance\":"<<maximumUlp<<",\"output0_clamped_unorm8_code_changes\":"<<unorm8CodeChanges
            <<",\"output0_absolute_tolerance\":0.000002,\"native_spatial_mapping_proven\":false,\"passthrough_authorized\":false}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"atlas fixture failed: "<<e.what()<<"; checks="<<checks<<'\n';return 1;}
}
