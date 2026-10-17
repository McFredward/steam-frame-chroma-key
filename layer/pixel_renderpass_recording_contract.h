#pragma once

// Synchronous public recording-shape observation only. This never constructs a
// replacement Begin/Next/End call, retains no caller pointers and performs no
// allocation, I/O or Vulkan call. A supported shape grants no object lifetime,
// submission, device, source-frame or eye authority. The original arguments must
// be forwarded verbatim; complete native pass/target tickets remain mandatory.
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_pixel_renderpass_recording {
constexpr std::size_t MaxChain=16,MaxAttachments=8,MaxLocationEntries=32;
// Public registry extension 606, offset 4. Older supported Vulkan headers do
// not name it yet. Header-only recognition refuses its memory-write/query
// semantics; no private declaration or payload interpretation is introduced.
constexpr std::uint32_t PerformanceCountersBeginType=1000605004;
// Valve's exact Mesa abc426bb62351045a9dcf7fa0a113e6dd810c303 declares
// vksubpass_begin_info_valve in tu_cmd_buffer.cc. This number alone grants no
// admission: its private LP64 payload requires an independently checked device
// and actual operation-specific NEXT entrypoint capability before any read.
constexpr std::uint32_t ValveSubpassSrgbType=1000109099;
enum class Reason : std::uint8_t {None,NullInfo,Type,Contents,UnknownExtension,UnsupportedExtension,
    WrongParent,DuplicateType,ChainCycle,ChainBounds,ArrayBounds,NullArray,DeviceMask,RenderArea,Identity,PrivateValue};
enum class Chain : std::uint8_t {RenderPassBegin,SubpassBegin,SubpassEnd,Count};
enum class Termination : std::uint8_t {Complete,Cycle,Bounds};
enum class Operation : std::uint8_t {Begin,Next,End};
enum class PrivateProfile : std::uint8_t {None,ValveSubpassSrgbZeroV1};
struct Refusal {Reason reason=Reason::None;std::uint32_t type=UINT32_MAX,index=UINT32_MAX;Chain chain=Chain::RenderPassBegin;};
struct Census {std::uint32_t rootType=UINT32_MAX;std::array<std::uint32_t,MaxChain> types{};std::uint32_t count=0;Termination termination=Termination::Complete;};
struct Shape {
    Operation operation=Operation::Begin;bool renderPass2=false,supported=false;
    Refusal refusal{};std::array<Census,static_cast<unsigned>(Chain::Count)> chains{};
    VkSubpassContents contents=VK_SUBPASS_CONTENTS_INLINE;
    VkRenderPass renderPass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
    VkRect2D renderArea{};std::uint32_t clearValueCount=0;
    bool attachmentBegin=false,deviceGroup=false,sampleLocations=false;
    std::uint32_t attachmentCount=0;std::array<VkImageView,MaxAttachments> attachments{};
    std::uint32_t deviceMask=0,deviceRenderAreaCount=0;VkRect2D deviceRenderArea{};
    std::uint32_t initialLocationCount=0,postLocationCount=0;
    std::array<std::uint32_t,MaxLocationEntries> initialAttachmentIndices{},postSubpassIndices{};
    PrivateProfile privateProfile=PrivateProfile::None;
    bool privatePayloadObserved=false;std::uint32_t privateSrgbMask=0;
};
inline const char* reasonName(Reason r) noexcept {
    switch(r){
#define N(e,s) case Reason::e:return s;
    N(None,"none") N(NullInfo,"null_info") N(Type,"type") N(Contents,"contents")
    N(UnknownExtension,"unknown_extension") N(UnsupportedExtension,"unsupported_extension")
    N(WrongParent,"wrong_parent") N(DuplicateType,"duplicate_type") N(ChainCycle,"chain_cycle")
    N(ChainBounds,"chain_bounds") N(ArrayBounds,"array_bounds") N(NullArray,"null_array")
    N(DeviceMask,"device_mask") N(RenderArea,"render_area") N(Identity,"identity") N(PrivateValue,"private_value")
#undef N
    }return "invalid_recording_reason";
}
inline const char* chainName(Chain c) noexcept {
    switch(c){case Chain::RenderPassBegin:return "render_pass_begin";case Chain::SubpassBegin:return "subpass_begin";
        case Chain::SubpassEnd:return "subpass_end";case Chain::Count:break;}return "invalid_chain";
}
inline bool refuse(Shape& out,Reason r,Chain chain,std::uint32_t index=UINT32_MAX,std::uint32_t type=UINT32_MAX) noexcept {
    if(out.refusal.reason==Reason::None)out.refusal={r,type,index,chain};
    return false;
}
inline bool knownBeginType(std::uint32_t type) noexcept {
    switch(type){
    case VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO:
    case VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO:
    case VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT:
    case VK_STRUCTURE_TYPE_RENDER_PASS_TRANSFORM_BEGIN_INFO_QCOM:
    case VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_RENDER_AREAS_RENDER_PASS_BEGIN_INFO_QCOM:
    case VK_STRUCTURE_TYPE_RENDER_PASS_STRIPE_BEGIN_INFO_ARM:
    case PerformanceCountersBeginType:return true;
    default:return false;}
}
inline bool knownEndType(std::uint32_t type) noexcept {
    // Current public headers use QCOM; newer registry versions also expose the
    // EXT name for the same fragment-density-offset semantics. Neither route is
    // admitted here: it changes density mapping, outside the ordinary contract.
    return type==VK_STRUCTURE_TYPE_SUBPASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_QCOM;
}
namespace detail {
struct Nodes {std::array<const VkBaseInStructure*,MaxChain> values{};};
inline Nodes census(const void* input,Chain chain,Shape& out) noexcept {
    Nodes nodes;auto& facts=out.chains[static_cast<unsigned>(chain)];
    auto* current=static_cast<const VkBaseInStructure*>(input);
    while(current){
        for(std::uint32_t i=0;i<facts.count;++i)if(nodes.values[i]==current){
            facts.termination=Termination::Cycle;refuse(out,Reason::ChainCycle,chain,facts.count,facts.types[i]);return nodes;}
        if(facts.count==MaxChain){facts.termination=Termination::Bounds;refuse(out,Reason::ChainBounds,chain,facts.count);return nodes;}
        const auto i=facts.count++;nodes.values[i]=current;facts.types[i]=static_cast<std::uint32_t>(current->sType);
        for(std::uint32_t j=0;j<i;++j)if(facts.types[j]==facts.types[i]){refuse(out,Reason::DuplicateType,chain,i,facts.types[i]);break;}
        current=current->pNext;
    }
    return nodes;
}
inline bool area(const VkRect2D& r) noexcept {
    return r.offset.x>=0&&r.offset.y>=0&&r.extent.width&&r.extent.height;
}
inline void chainPolicy(const Nodes& nodes,Chain chain,Shape& out) noexcept {
    const auto& facts=out.chains[static_cast<unsigned>(chain)];
    // A complete bounded census precedes decoding. Unknown headers are recorded
    // without interpreting their payload, and do not hide later chain headers.
    for(std::uint32_t i=0;i<facts.count;++i){const auto type=facts.types[i];
        if(chain!=Chain::RenderPassBegin){
            refuse(out,knownBeginType(type)||knownEndType(type)?
                (chain==Chain::SubpassEnd&&knownEndType(type)?Reason::UnsupportedExtension:Reason::WrongParent):Reason::UnknownExtension,chain,i,type);
            continue;
        }
        if(knownEndType(type)){refuse(out,Reason::WrongParent,chain,i,type);continue;}
        if(!knownBeginType(type)){refuse(out,Reason::UnknownExtension,chain,i,type);continue;}
        if(type!=VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO&&
           type!=VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO&&
           type!=VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT)
            refuse(out,Reason::UnsupportedExtension,chain,i,type);
    }
    // Validate the WHOLE chain before any payload. An unknown later header
    // cannot cause us to inspect an array governed by unsupported semantics.
    if(out.refusal.reason!=Reason::None)return;
    for(std::uint32_t i=0;i<facts.count;++i){const auto type=facts.types[i];
        if(out.refusal.reason!=Reason::None)break;
        switch(type){
        case VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO:{
            const auto& in=*reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(nodes.values[i]);
            out.attachmentBegin=true;out.attachmentCount=in.attachmentCount;
            if(in.attachmentCount>MaxAttachments){refuse(out,Reason::ArrayBounds,chain,i,type);break;}
            if(in.attachmentCount&&!in.pAttachments){refuse(out,Reason::NullArray,chain,i,type);break;}
            for(std::uint32_t j=0;j<in.attachmentCount;++j)out.attachments[j]=in.pAttachments[j];
            break;}
        case VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO:{
            const auto& in=*reinterpret_cast<const VkDeviceGroupRenderPassBeginInfo*>(nodes.values[i]);
            out.deviceGroup=true;out.deviceMask=in.deviceMask;out.deviceRenderAreaCount=in.deviceRenderAreaCount;
            // The owner only admits one original physical device. Do not expand
            // that authority from a recording header or a caller boolean.
            if(in.deviceMask!=1){refuse(out,Reason::DeviceMask,chain,i,type);break;}
            if(in.deviceRenderAreaCount>1){refuse(out,Reason::ArrayBounds,chain,i,type);break;}
            if(in.deviceRenderAreaCount){
                if(!in.pDeviceRenderAreas){refuse(out,Reason::NullArray,chain,i,type);break;}
                out.deviceRenderArea=in.pDeviceRenderAreas[0];
                if(!area(out.deviceRenderArea))refuse(out,Reason::RenderArea,chain,i,type);
            }break;}
        case VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT:{
            const auto& in=*reinterpret_cast<const VkRenderPassSampleLocationsBeginInfoEXT*>(nodes.values[i]);
            out.sampleLocations=true;out.initialLocationCount=in.attachmentInitialSampleLocationsCount;out.postLocationCount=in.postSubpassSampleLocationsCount;
            if(out.initialLocationCount>MaxLocationEntries||out.postLocationCount>MaxLocationEntries){refuse(out,Reason::ArrayBounds,chain,i,type);break;}
            if((out.initialLocationCount&&!in.pAttachmentInitialSampleLocations)||(out.postLocationCount&&!in.pPostSubpassSampleLocations)){
                refuse(out,Reason::NullArray,chain,i,type);break;}
            for(std::uint32_t j=0;j<out.initialLocationCount;++j)out.initialAttachmentIndices[j]=in.pAttachmentInitialSampleLocations[j].attachmentIndex;
            for(std::uint32_t j=0;j<out.postLocationCount;++j)out.postSubpassIndices[j]=in.pPostSubpassSampleLocations[j].subpassIndex;
            // https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentSampleLocationsEXT.html
            // https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassSampleLocationsEXT.html
            // Nested sampleLocationsInfo values can be ignored based on the
            // original image flags/variableSampleLocations. We have no such
            // image-property authority. Never read nested headers or arrays,
            // even for a nonempty outer list. The original Begin and transition
            // state are forwarded unchanged, not reconstructed from these facts.
            // Indices are owned diagnostics; actual pass-bound validity remains
            // an original-call obligation, not an invented global index limit.
            break;}
        default:refuse(out,Reason::UnsupportedExtension,chain,i,type);break;
        }
    }
}
inline void base(const VkRenderPassBeginInfo* info,Shape& out) noexcept {
    if(!info){refuse(out,Reason::NullInfo,Chain::RenderPassBegin);return;}
    out.chains[static_cast<unsigned>(Chain::RenderPassBegin)].rootType=static_cast<std::uint32_t>(info->sType);
    const auto nodes=census(info->pNext,Chain::RenderPassBegin,out);
    if(info->sType!=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO){refuse(out,Reason::Type,Chain::RenderPassBegin,UINT32_MAX,static_cast<std::uint32_t>(info->sType));return;}
    out.renderPass=info->renderPass;out.framebuffer=info->framebuffer;out.renderArea=info->renderArea;out.clearValueCount=info->clearValueCount;
    if(!info->renderPass||!info->framebuffer)refuse(out,Reason::Identity,Chain::RenderPassBegin);
    chainPolicy(nodes,Chain::RenderPassBegin,out);
    // Device-group render areas override the top-level area, including ignored
    // zero/negative top-level members. clear values are attachment/loadOp-
    // conditional, never consumed or copied by this observer.
    if(out.refusal.reason==Reason::None&&!(out.deviceGroup&&out.deviceRenderAreaCount)&&!area(out.renderArea))
        refuse(out,Reason::RenderArea,Chain::RenderPassBegin);
}
inline void subpassBegin(const VkSubpassBeginInfo* info,Shape& out) noexcept {
    if(!info){refuse(out,Reason::NullInfo,Chain::SubpassBegin);return;}
    out.chains[static_cast<unsigned>(Chain::SubpassBegin)].rootType=static_cast<std::uint32_t>(info->sType);
    const auto nodes=census(info->pNext,Chain::SubpassBegin,out);
    if(info->sType!=VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO)refuse(out,Reason::Type,Chain::SubpassBegin,UINT32_MAX,static_cast<std::uint32_t>(info->sType));
    out.contents=info->contents;chainPolicy(nodes,Chain::SubpassBegin,out);
}
inline void subpassEnd(const VkSubpassEndInfo* info,Shape& out) noexcept {
    if(!info){refuse(out,Reason::NullInfo,Chain::SubpassEnd);return;}
    out.chains[static_cast<unsigned>(Chain::SubpassEnd)].rootType=static_cast<std::uint32_t>(info->sType);
    const auto nodes=census(info->pNext,Chain::SubpassEnd,out);
    if(info->sType!=VK_STRUCTURE_TYPE_SUBPASS_END_INFO)refuse(out,Reason::Type,Chain::SubpassEnd,UINT32_MAX,static_cast<std::uint32_t>(info->sType));
    chainPolicy(nodes,Chain::SubpassEnd,out);
}
inline bool finish(Shape& out) noexcept {
    if(out.operation!=Operation::End&&out.contents!=VK_SUBPASS_CONTENTS_INLINE)refuse(out,Reason::Contents,out.renderPass2?Chain::SubpassBegin:Chain::RenderPassBegin);
    out.supported=out.refusal.reason==Reason::None;return out.supported;
}
 } // namespace detail
// Header-only evidence for an otherwise unsupported public route. This does
// not run parent policy or set supported=true. Supply a fresh Shape; this
// resets only the selected chain and owns no extension payload or pointer.
inline void headerCensus(std::uint32_t rootType,const void* pNext,Chain chain,Shape& out) noexcept {
    if(chain==Chain::Count)return;
    out.chains[static_cast<unsigned>(chain)]={};
    out.chains[static_cast<unsigned>(chain)].rootType=rootType;
    (void)detail::census(pNext,chain,out);
}
inline bool begin(const VkRenderPassBeginInfo* in,VkSubpassContents contents,Shape& out) noexcept {
    out={};out.contents=contents;detail::base(in,out);return detail::finish(out);
}
inline bool begin2(const VkRenderPassBeginInfo* in,const VkSubpassBeginInfo* subpass,Shape& out) noexcept {
    out={};out.renderPass2=true;detail::base(in,out);detail::subpassBegin(subpass,out);return detail::finish(out);
}
inline bool next(VkSubpassContents contents,Shape& out) noexcept {
    out={};out.operation=Operation::Next;out.contents=contents;return detail::finish(out);
}
inline bool next2(const VkSubpassBeginInfo* beginInfo,const VkSubpassEndInfo* endInfo,Shape& out) noexcept {
    out={};out.operation=Operation::Next;out.renderPass2=true;detail::subpassBegin(beginInfo,out);detail::subpassEnd(endInfo,out);return detail::finish(out);
}
inline bool end(Shape& out) noexcept {out={};out.operation=Operation::End;return detail::finish(out);}
inline bool end2(const VkSubpassEndInfo* endInfo,Shape& out) noexcept {
    out={};out.operation=Operation::End;out.renderPass2=true;detail::subpassEnd(endInfo,out);return detail::finish(out);
}
static_assert(sizeof(Shape)<1024,"recording shape stays finite and callback-local");
} // namespace frame_pixel_renderpass_recording
