#pragma once

// Cold bounded JSON formatting. Never call this on a compositor callback. These
// copied public facts explain admission; neither shader entry names nor a handle
// in this report grants a native lifetime, source-frame or replacement authority.
#include "pixel_definition_facts.h"
#include <locale>
#include <sstream>
#include <string>

namespace frame_pixel_definition_report {
constexpr std::size_t MaxGraphicsReportBytes=16*1024;
inline bool graphics(const frame_pixel_definition_facts::GraphicsObservation& item,
                     unsigned index,std::string& text) {
    namespace facts=frame_pixel_definition_facts;
    text.clear();
    if(index>=facts::MaxGraphics || item.dynamicCount>facts::MaxGraphicsDynamics)return false;
    for(const auto& state:item.states)if(state.chainCount>facts::MaxChain)return false;
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"{\"event\":\"ssbo_definition_graphics_state\",\"graphics_index\":"<<index
       <<",\"sequence\":"<<item.sequence<<",\"pipeline\":"<<item.pipeline
       <<",\"layout\":"<<item.layout<<",\"render_pass\":"<<item.renderPass
       <<",\"subpass\":"<<item.subpass<<",\"scope_known\":"<<(item.scope.known?"true":"false")
       <<",\"color_count\":"<<item.scope.colorCount<<",\"depth_present\":"<<(item.scope.hasDepth?"true":"false")
       <<",\"dynamic_read\":\""<<facts::graphicsReadName(item.dynamicRead)<<"\",\"dynamics\":[";
    for(unsigned i=0;i<item.dynamicCount;++i){if(i)out<<',';out<<item.dynamics[i];}
    out<<"],\"states\":[";
    for(unsigned i=0;i<item.states.size();++i){if(i)out<<',';const auto& state=item.states[i];
        out<<"{\"state_index\":"<<i<<",\"present\":"<<(state.present?"true":"false")
           <<",\"type\":"<<state.type<<",\"flags\":"<<state.flags
           <<",\"counts\":["<<state.counts[0]<<','<<state.counts[1]<<"],\"read\":\""
           <<facts::graphicsReadName(state.read)<<"\",\"chain_end\":\""
           <<facts::chainEndName(state.chainEnd)<<"\",\"chain_types\":[";
        for(unsigned n=0;n<state.chainCount;++n){if(n)out<<',';out<<state.chainTypes[n];}
        out<<"]}";
    }
    out<<"],\"native_pipeline_association_proven\":false,\"native_lifetime_lease\":false,"
          "\"native_frame_association_proven\":false,\"mask_enabled\":false}\n";
    text=out.str();
    if(text.size()>MaxGraphicsReportBytes){text.clear();return false;}
    return true;
}
}
