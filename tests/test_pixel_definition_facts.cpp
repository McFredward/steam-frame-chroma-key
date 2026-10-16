#include "layer/pixel_definition_facts.h"
#include <cstdlib>
#include <iostream>

namespace facts=frame_pixel_definition_facts;
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T value{};value.sType=type;return value;}
void deviceChain(){
    facts::Facts out;
    auto ci=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto features=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    auto timeline=info<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    auto other=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));
    features.pNext=&timeline;timeline.pNext=&other;ci.pNext=&features;
    facts::device(out,ci,VK_ERROR_INITIALIZATION_FAILED);
    require(!out.deviceObserved && out.chainCount==0,"failed create contributes no observations");
    facts::device(out,ci,VK_SUCCESS);
    require(out.deviceObserved && out.chainEnd==facts::ChainEnd::Complete && out.chainCount==3,"complete public chain survives unrelated unknown feature payload");
    require(out.chainTypes[0]==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && out.chainTypes[1]==1000207000 && out.chainTypes[2]==0x6fffffff,"all headers retained in exact native order");
    timeline.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;other.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    require(out.chainTypes[1]==1000207000 && out.chainTypes[2]==0x6fffffff,"facts own values after native inputs change");
    ci.pNext=nullptr;facts::device(out,ci,VK_SUCCESS);
    require(out.chainCount==3,"later device callback cannot replace original input");
    facts::Facts empty;facts::device(empty,ci,VK_SUCCESS);
    require(empty.chainCount==0 && empty.chainEnd==facts::ChainEnd::Complete,"empty successful chain distinguished from absent observation");
    facts::Facts cycle;other.pNext=&other;ci.pNext=&other;facts::device(cycle,ci,VK_SUCCESS);
    require(cycle.chainEnd==facts::ChainEnd::Cycle && cycle.chainCount==1,"cycle is finite and specifically incomplete");
    std::array<VkBaseInStructure,33> nodes{};
    for(std::size_t i=0;i<nodes.size();++i){nodes[i].sType=static_cast<VkStructureType>(i+1);nodes[i].pNext=i+1<nodes.size()?&nodes[i+1]:nullptr;}
    facts::Facts bound;ci.pNext=nodes.data();facts::device(bound,ci,VK_SUCCESS);
    require(bound.chainEnd==facts::ChainEnd::Bounds && bound.chainCount==32,"fixed header budget rejects excess without following it");
    nodes[31].pNext=nullptr;facts::Facts exact;facts::device(exact,ci,VK_SUCCESS);
    require(exact.chainEnd==facts::ChainEnd::Complete && exact.chainCount==32,"exact final header completes without artificial truncation");
}
void setShapes(){
    facts::Facts out;
    auto device=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    auto ci=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_ALL,nullptr};
    ci.bindingCount=1;ci.pBindings=&binding;
    facts::set(out,ci,VK_SUCCESS);require(out.setCount==0,"no successful device means no independent set observations");
    facts::device(out,device,VK_SUCCESS);facts::set(out,ci,VK_ERROR_OUT_OF_HOST_MEMORY);
    require(out.setCount==0,"failed set create never supplies a successful native shape");
    facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==1 && out.sets[0].bindings[0].count==1,"actual Frame count1 is retained without pretending it matches clone array3");
    facts::set(out,ci,VK_SUCCESS);require(out.setCount==1,"identical hot recreations cannot grow output");
    binding.descriptorCount=3;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==2 && out.sets[0].bindings[0].count==1 && out.sets[1].bindings[0].count==3,"distinct layouts never overwrite prior fact");
    binding.pImmutableSamplers=reinterpret_cast<const VkSampler*>(std::uintptr_t(1));facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==3 && out.sets[2].bindings[0].immutableSampler,"sampler pointer presence copied without following invalid pointer");
    binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==4 && out.sets[3].bindings[0].type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,"unexpected descriptor remains an observation only");
    binding.descriptorCount=8;facts::set(out,ci,VK_SUCCESS);
    require(out.setCount==4 && (out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Capacity)),"exhaustion cannot evict earlier shape");
    ci.pBindings=reinterpret_cast<const VkDescriptorSetLayoutBinding*>(std::uintptr_t(1));ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    facts::set(out,ci,VK_SUCCESS);require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Flags),"unknown array semantics refuse before ignored-array dereference");
    ci.flags=0;ci.pNext=reinterpret_cast<const void*>(std::uintptr_t(1));facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Extension),"unknown extension never followed or used to read arrays");
    ci.pNext=nullptr;ci.bindingCount=33;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Bounds),"oversized array refused before access");
    ci.bindingCount=1;ci.pBindings=nullptr;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::NullArray),"missing array has distinct bounded reason");
    ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;facts::set(out,ci,VK_SUCCESS);
    require(out.setSkipReasons&static_cast<unsigned>(facts::SetSkip::Shape),"unexpected create shape does not leak payload");
    facts::Facts absent;facts::device(absent,device,VK_SUCCESS);ci=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    binding.binding=8;ci.bindingCount=1;ci.pBindings=&binding;facts::set(absent,ci,VK_SUCCESS);
    require(absent.setCount==0 && absent.setSkipReasons==0,"unrelated layouts do not consume target-shape budget");
}
}
int main(){deviceChain();setShapes();std::cout<<"{\"checks\":"<<checks<<",\"facts_bytes\":"<<sizeof(facts::Facts)<<",\"runtime_authority\":false}\n";}
