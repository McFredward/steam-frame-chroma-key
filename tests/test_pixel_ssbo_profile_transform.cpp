#include "layer/pixel_ssbo_profile_transform.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace profile=frame_pixel_ssbo_profile;
namespace {
namespace clone=frame_pixel_ssbo_clone;
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<message<<'\n';std::exit(1);}}
std::vector<std::uint32_t> shader(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"explicit private file readable");const auto bytes=file.tellg();
    require(bytes>=20 && bytes<=32768 && bytes%4==0,"bounded explicit module");std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4);
    file.seekg(0);require(bool(file.read(reinterpret_cast<char*>(words.data()),bytes)),"complete module read");return words;
}
}
int main(int argc,char** argv){
    require(argc==1 || argc==4,"optional private originalPS pixelSSBO firstSSBO");
    clone::ShaderDefinition source;profile::Artifact output;clone::Refusal why;
    require(!profile::transform(source,false,output,why) && !output.bytes,"absent source cannot generate artifact");
    source.complete=true;source.bytes=6620;source.words[0]=0x07230203u;
    require(!profile::transform(source,false,output,why) && !output.bytes,"plausible header without source pin cannot generate artifact");
    if(argc==4){
        const auto original=shader(argv[1]),pixel=shader(argv[2]),first=shader(argv[3]);
        VkShaderModuleCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;info.codeSize=original.size()*4;info.pCode=original.data();
        require(clone::captureShader(info,clone::ShaderRole::Fragment,source,why),"successful callback's actual source exact pin");
        for(bool firstCovered:{false,true}){
            const auto& expected=firstCovered?first:pixel;
            require(profile::transform(source,firstCovered,output,why),"cold bounded C++ runtime transformer succeeds");
            require(output.bytes==expected.size()*4 && std::equal(expected.begin(),expected.end(),output.words.begin()),"C++ output byte-for-byte equals independent Python canonical transform");
            require(output.profile.specializedCount1() && output.profile.firstCoveredInvocation()==firstCovered,"actual output bytes attest requested distinct profile");
            require(output.profile.matches(output.words.data(),output.bytes),"canonical artifact profile matches actual generated output");
            const auto& other=firstCovered?pixel:first;
            require(!output.profile.matches(other.data(),other.size()*4),"pixel and first-covered profile cannot substitute for each other");
        }
        source.words[7]^=1;require(!profile::transform(source,true,output,why) && !output.bytes,"changed original bytes refuse and clear any previous output");
    }
    std::cout<<"{\"schema\":\"cold-count1-ssbo-transform-v1\",\"checks\":"<<checks<<",\"private_source_tested\":"<<(argc==4?"true":"false")<<",\"runtime_activation_authorized\":false}\n";
}
