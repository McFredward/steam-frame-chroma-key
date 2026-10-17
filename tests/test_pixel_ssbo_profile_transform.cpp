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
void mirror(const char* vertexPath,const char* fragmentPath,const char* artifactPath){
    const auto vs=shader(vertexPath),ps=shader(fragmentPath),expected=shader(artifactPath);
    clone::ShaderDefinition vertex,fragment;clone::Refusal why;profile::Artifact output;
    VkShaderModuleCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;ci.codeSize=vs.size()*4;ci.pCode=vs.data();
    require(clone::captureShader(ci,clone::ShaderRole::Vertex,vertex,why),"actual pinned original vertex required");
    ci.codeSize=ps.size()*4;ci.pCode=ps.data();require(clone::captureShader(ci,clone::ShaderRole::Fragment,fragment,why),"actual pinned original fragment required");
    require(profile::transformMirror(vertex,fragment,output,why),"checked both-stage mirror transform succeeds");
    require(output.bytes==expected.size()*4&&std::equal(expected.begin(),expected.end(),output.words.begin()),"mirror C++ bytes equal independent Python canonical artifact");
    const auto checked=output.profile;require(checked.compatibleSet0Mirror()&&checked.sparseGrid32()&&checked.specializedCount1()&&checked.recordSet()==0&&checked.recordBinding()==8,"new profile explicitly identifies one-set mirror interface");
    require(checked.matches(expected.data(),expected.size()*4),"actual mirror bytes match own checked interface");
    clone::ProbeInterface rejected;require(!clone::ProbeInterface::count1(expected.data(),expected.size()*4,rejected,why)&&!rejected.compatibleSet0Mirror(),"artifact alone cannot mint mirror authority through old API");
    require(clone::ProbeInterface::set0Mirror(vs.data(),vs.size()*4,ps.data(),ps.size()*4,expected.data(),expected.size()*4,rejected,why)&&rejected.sameProfile(checked),"actual both-stage pins independently attest artifact");
    profile::Artifact old;require(profile::transform(fragment,profile::Election::Sparse32,old,why)&&!old.profile.compatibleSet0Mirror(),"legacy sparse Set1 remains distinct");
    require(!checked.matches(old.words.data(),old.bytes)&&!old.profile.matches(expected.data(),expected.size()*4),"mirror and Set1 artifacts never substitute for one another");
    for(unsigned mutation=0;mutation<3;++mutation){auto a=vs,b=ps,c=expected;(mutation==0?a:mutation==1?b:c)[7]^=1;
        require(!clone::ProbeInterface::set0Mirror(a.data(),a.size()*4,b.data(),b.size()*4,c.data(),c.size()*4,rejected,why)&&!rejected.compatibleSet0Mirror(),"changed stage/artifact bytes refuse and clear profile");}
    auto colliding=vs;bool patched=false;for(std::size_t at=5;at<colliding.size();at+=colliding[at]>>16)if((colliding[at]&65535u)==71&&(colliding[at]>>16)==4&&colliding[at+2]==33){colliding[at+3]=8;patched=true;break;}
    require(patched&&!clone::ProbeInterface::set0Mirror(colliding.data(),colliding.size()*4,ps.data(),ps.size()*4,expected.data(),expected.size()*4,rejected,why),"binding8 collision cannot pass original pin");
    vertex.words[7]^=1;require(!profile::transformMirror(vertex,fragment,output,why)&&!output.bytes,"changed original vertex refuses runtime transform and clears output");vertex.words[7]^=1;
    fragment.words[7]^=1;require(!profile::transformMirror(vertex,fragment,output,why)&&!output.bytes,"changed original fragment refuses runtime transform");fragment.words[7]^=1;
    std::array<VkDescriptorSetLayoutBinding,29> bindings{};for(unsigned i=0;i<bindings.size();++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_ALL,nullptr};
    bindings[8].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[9].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;bindings[25].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;
    VkDescriptorSetLayoutCreateInfo set{};set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set.bindingCount=bindings.size();set.pBindings=bindings.data();clone::Set0Definition definition;
    require(clone::captureSet0(set,definition,why,checked)&&definition.count==29,"complete original 29binding layout retained with checked unused8");
    for(unsigned i=0;i<bindings.size();++i)require(definition.bindings[i].binding==i&&definition.bindings[i].descriptorType==bindings[i].descriptorType,"all native layout definitions kept exactly, not only copied slots");
    for(unsigned i=0;i<5;++i){const auto number=clone::ProbeInterface::MirrorBindings[i];bindings[number].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        require(clone::captureSet0(set,definition,why,checked),"statically uniform original slot permits equivalent dynamic descriptor type");bindings[number].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
    for(const auto number:clone::ProbeInterface::MirrorBindings){bindings[number].descriptorCount=2;require(!clone::captureSet0(set,definition,why,checked),"each copied binding must be exact count1");bindings[number].descriptorCount=1;}
    bindings[8].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;require(!clone::captureSet0(set,definition,why,checked),"record binding must already be compatible native storagebuffer");bindings[8].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[8].descriptorCount=2;require(!clone::captureSet0(set,definition,why,checked),"record binding count mismatch refused");bindings[8].descriptorCount=1;
    bindings[8].stageFlags=VK_SHADER_STAGE_VERTEX_BIT;require(!clone::captureSet0(set,definition,why,checked),"record binding must permit fragment writes");bindings[8].stageFlags=VK_SHADER_STAGE_ALL;
    for(const auto number:clone::ProbeInterface::MirrorBindings){const auto saved=bindings[number].descriptorType;bindings[number].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        require(!clone::captureSet0(set,definition,why,checked),"incompatible original copied type refused");bindings[number].descriptorType=saved;}
    require(clone::captureSet0(set,definition,why,checked),"recovered exact full mirror layout");
    clone::Recipe recipe;recipe.definitionComplete=true;recipe.set0=definition;clone::LayoutView layout;
    const auto set0=reinterpret_cast<VkDescriptorSetLayout>(std::uintptr_t(0x123));
    require(recipe.layoutView(set0,VK_NULL_HANDLE,layout)&&layout.info.setLayoutCount==1&&layout.sets[0]==set0&&!layout.sets[1],"checked mirror pipeline layout stays exactly one set");
    recipe.set0.probeInterface=old.profile;require(!recipe.layoutView(set0,VK_NULL_HANDLE,layout),"legacy profile still requires added Set1");
    require(recipe.layoutView(set0,set0,layout)&&layout.info.setLayoutCount==2,"legacy layout API behavior preserved");
}

}
int main(int argc,char** argv){
    if(argc==5&&!std::strcmp(argv[1],"--mirror")){mirror(argv[2],argv[3],argv[4]);std::cout<<"{\"scope\":\"checked-set0-mirror-profile\",\"checks\":"<<checks<<"}\n";return 0;}
    require(argc==1 || argc==4 || argc==5,"optional private originalPS pixelSSBO firstSSBO [sparseSSBO]");
    clone::ShaderDefinition source;profile::Artifact output;clone::Refusal why;
    require(!profile::transform(source,false,output,why) && !output.bytes,"absent source cannot generate artifact");
    source.complete=true;source.bytes=6620;source.words[0]=0x07230203u;
    require(!profile::transform(source,false,output,why) && !output.bytes,"plausible header without source pin cannot generate artifact");
    if(argc>=4){
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
        if(argc==5){const auto expected=shader(argv[4]);
            require(profile::transform(source,profile::Election::Sparse32,output,why),"sparse first-eligible grid32 cold transformer");
            require(output.bytes==expected.size()*4 && std::equal(expected.begin(),expected.end(),output.words.begin()),"sparse C++ bytes exactly match independent Python canonical artifact");
            require(output.profile.sparseGrid32() && !output.profile.firstCoveredInvocation(),"sparse profile does not claim all-fragment first coverage");
            require(!output.profile.matches(pixel.data(),pixel.size()*4) && !output.profile.matches(first.data(),first.size()*4),"other canonical modes cannot substitute for sparse");
            require(!profile::transform(source,static_cast<profile::Election>(255),output,why) && !output.bytes,"unknown election refuses rather than widening default");
        }
        source.words[7]^=1;require(!profile::transform(source,true,output,why) && !output.bytes,"changed original bytes refuse and clear any previous output");
    }
    std::cout<<"{\"schema\":\"cold-count1-ssbo-transform-v1\",\"checks\":"<<checks<<",\"private_source_tested\":"<<(argc>=4?"true":"false")<<",\"runtime_activation_authorized\":false}\n";
}
