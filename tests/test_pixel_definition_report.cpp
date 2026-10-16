#include "layer/pixel_definition_report.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace report=frame_pixel_definition_report;
namespace facts=frame_pixel_definition_facts;
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
}
int main(){
    facts::GraphicsObservation item;
    item.sequence=item.pipeline=item.layout=item.renderPass=std::numeric_limits<std::uint64_t>::max();
    item.subpass=item.scope.colorCount=std::numeric_limits<std::uint32_t>::max();
    item.scope.known=true;item.scope.hasDepth=true;
    item.dynamicRead=facts::GraphicsRead::UnsupportedPrefix;
    item.dynamicCount=facts::MaxGraphicsDynamics;
    item.dynamics.fill(std::numeric_limits<std::uint32_t>::max());
    for(auto& state:item.states){
        state.present=true;state.type=state.flags=std::numeric_limits<std::uint32_t>::max();
        state.counts.fill(std::numeric_limits<std::uint32_t>::max());state.read=facts::GraphicsRead::UnsupportedPrefix;
        state.chainEnd=facts::ChainEnd::NotObserved;state.chainCount=facts::MaxChain;
        state.chainTypes.fill(std::numeric_limits<std::uint32_t>::max());
    }
    std::string text;
    require(report::graphics(item,7,text),"full finite target observation fits its report budget");
    const auto largest=text.size();
    require(largest<report::MaxGraphicsReportBytes,"largest integer and longest diagnostic spellings fit bounded line");
    require(text.find("\"pipeline\":18446744073709551615")!=std::string::npos,"exact 64-bit target handle is not truncated");
    require(text.find("\"dynamic_read\":\"unsupported_prefix\"")!=std::string::npos,"public dynamic read status survives formatter");
    require(text.find("\"state_index\":11")!=std::string::npos,"last child state cannot disappear behind grouped top-level shape");
    require(text.find("\"native_pipeline_association_proven\":false")!=std::string::npos,"observation never promotes entry names to shader identity authority");
    // Previous worst-case grouped progress plus all new target lines must fit
    // the separate 512-KiB reader. Ordinary configuration JSON stays 16 KiB.
    require(245416+facts::MaxGraphics*report::MaxGraphicsReportBytes<512*1024,"entire finite receipt bank fits the explicit progress envelope");
    item.dynamicCount=65;require(!report::graphics(item,0,text)&&text.empty(),"bad array count refuses without indexing beyond bank");
    item.dynamicCount=64;item.states[11].chainCount=33;
    require(!report::graphics(item,0,text)&&text.empty(),"bad last chain refuses before following it");
    item.states[11].chainCount=32;
    require(!report::graphics(item,8,text)&&text.empty(),"invalid exact-target observation index refuses");
    require(report::graphics(item,0,text),"failed formatting cannot poison the next valid observation");
    std::cout<<"{\"checks\":"<<checks<<",\"largest_graphics_report_bytes\":"<<largest
             <<",\"native_frame_association_proven\":false}\n";
}
