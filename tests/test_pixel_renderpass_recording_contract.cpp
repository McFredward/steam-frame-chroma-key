#include "layer/pixel_renderpass_recording_contract.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

namespace r=frame_pixel_renderpass_recording;
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
template<class T>T info(VkStructureType type){T v{};v.sType=type;return v;}
template<class T>T handle(std::uintptr_t value){T out{};static_assert(sizeof(out)<=sizeof(value));std::memcpy(&out,&value,sizeof(out));return out;}
template<class T>const T* poison(){return reinterpret_cast<const T*>(std::uintptr_t(1));}
VkRenderPassBeginInfo beginInfo(){auto v=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    v.renderPass=handle<VkRenderPass>(10);v.framebuffer=handle<VkFramebuffer>(20);v.renderArea={{2,3},{64,32}};return v;}
const r::Census& chain(const r::Shape& shape,r::Chain c){return shape.chains[static_cast<unsigned>(c)];}
void ordinary(){
    r::Shape shape;auto begin=beginInfo();auto sub=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);sub.contents=VK_SUBPASS_CONTENTS_INLINE;
    auto end=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    begin.clearValueCount=UINT32_MAX;begin.pClearValues=poison<VkClearValue>();
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape),"ordinary legacy Begin admitted without inspecting conditional clear-value array");
    require(shape.supported&&!shape.renderPass2&&shape.operation==r::Operation::Begin&&shape.renderPass==begin.renderPass&&shape.framebuffer==begin.framebuffer,"native handles and operation copied without authority");
    require(shape.clearValueCount==UINT32_MAX&&shape.renderArea.offset.x==2&&chain(shape,r::Chain::RenderPassBegin).rootType==VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,"safe scalar facts retained");
    require(r::next(VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.operation==r::Operation::Next&&!shape.renderPass,"legacy Next resets prior Begin metadata");
    require(r::end(shape)&&shape.operation==r::Operation::End,"ordinary legacy End admitted");
    for(unsigned alias=0;alias<2;++alias){
        // Core/KHR structures are ABI aliases; exact original function name is
        // separately retained by the exported hook, never selected here.
        require(r::begin2(&begin,&sub,shape)&&shape.renderPass2,"ordinary Core/KHR Begin2 shares public shape");
        require(chain(shape,r::Chain::SubpassBegin).rootType==VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,"Begin2 root type retained");
        require(r::next2(&sub,&end,shape)&&shape.operation==r::Operation::Next,"ordinary Core/KHR Next2 admitted");
        require(chain(shape,r::Chain::SubpassEnd).rootType==VK_STRUCTURE_TYPE_SUBPASS_END_INFO,"Next2 both roots observed");
        require(r::end2(&end,shape)&&shape.operation==r::Operation::End&&shape.renderPass2,"ordinary Core/KHR End2 admitted");
    }
    for(auto contents:{VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS,static_cast<VkSubpassContents>(2),static_cast<VkSubpassContents>(3)}){
        require(!r::begin(&begin,contents,shape)&&shape.refusal.reason==r::Reason::Contents,"non-inline legacy Begin refused");
        sub.contents=contents;require(!r::begin2(&begin,&sub,shape)&&shape.refusal.chain==r::Chain::SubpassBegin,"non-inline Begin2 refused in exact root");
        require(!r::next(contents,shape)&&shape.refusal.reason==r::Reason::Contents,"non-inline legacy Next refused");
        require(!r::next2(&sub,&end,shape)&&shape.refusal.reason==r::Reason::Contents,"non-inline Next2 refused");
    }
    require(!r::begin(nullptr,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::NullInfo,"null Begin safe refusal");
    require(!r::begin2(&begin,nullptr,shape)&&shape.refusal.chain==r::Chain::SubpassBegin,"null Begin2 subordinate safe refusal");
    require(!r::next2(nullptr,&end,shape)&&chain(shape,r::Chain::SubpassEnd).rootType==VK_STRUCTURE_TYPE_SUBPASS_END_INFO,"other root still observed after null next root");
    require(!r::end2(nullptr,shape)&&shape.refusal.chain==r::Chain::SubpassEnd,"null End2 safe refusal");
    begin=beginInfo();begin.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.type==VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,"wrong top-level type owned exactly");
    begin=beginInfo();sub.contents=VK_SUBPASS_CONTENTS_INLINE;sub.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    require(!r::begin2(&begin,&sub,shape)&&shape.refusal.reason==r::Reason::Type,"wrong Begin2 subordinate type refused");
    end.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;sub.sType=VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    require(!r::next2(&sub,&end,shape)&&shape.refusal.chain==r::Chain::SubpassEnd,"wrong Next2 end root type refused");
    require(!r::end2(&end,shape)&&shape.refusal.reason==r::Reason::Type,"wrong End2 type refused");
    begin=beginInfo();begin.framebuffer=VK_NULL_HANDLE;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::Identity,"null framebuffer not an ordinary native Begin");
    begin=beginInfo();begin.renderPass=VK_NULL_HANDLE;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::Identity,"null pass not ordinary Begin");
}
void attachments(){
    r::Shape shape;auto begin=beginInfo();auto a=info<VkRenderPassAttachmentBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);begin.pNext=&a;
    a.pAttachments=poison<VkImageView>();require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.attachmentBegin&&shape.attachmentCount==0,"zero imageless list ignores pointer");
    std::array<VkImageView,r::MaxAttachments> values{};for(unsigned i=0;i<values.size();++i)values[i]=handle<VkImageView>(100+i);
    a.attachmentCount=values.size();a.pAttachments=values.data();
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.attachments==values,"exact bound attachments copied");
    values[0]=VK_NULL_HANDLE;a.attachmentCount=1;
    require(shape.attachmentCount==8&&shape.attachments[0]==handle<VkImageView>(100),"attachment facts own values after caller mutation");
    a.attachmentCount=r::MaxAttachments+1;a.pAttachments=poison<VkImageView>();
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::ArrayBounds&&shape.refusal.type==a.sType,"oversize refused before array access");
    a.attachmentCount=1;a.pAttachments=nullptr;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::NullArray,"nonnull imageless count needs array");
}
void groups(){
    r::Shape shape;auto begin=beginInfo();auto group=info<VkDeviceGroupRenderPassBeginInfo>(VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO);begin.pNext=&group;
    group.deviceMask=1;group.pDeviceRenderAreas=poison<VkRect2D>();
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.deviceGroup&&shape.deviceMask==1,"single-device countzero ignores device array");
    for(auto mask:{0u,2u,3u,UINT32_MAX}){group.deviceMask=mask;require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::DeviceMask,"other physical-device execution masks refused");}
    group.deviceMask=1;group.deviceRenderAreaCount=2;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::ArrayBounds,"multi-area single-device route refuses before pointer");
    group.deviceRenderAreaCount=1;group.pDeviceRenderAreas=nullptr;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::NullArray,"actual device-area count needs array");
    VkRect2D area{{4,5},{12,13}};group.pDeviceRenderAreas=&area;begin.renderArea={{-20,-30},{0,0}};
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.deviceRenderArea.offset.x==4,"device render-area override preserves ignored top-level negative/empty values");
    area.offset.x=99;require(shape.deviceRenderArea.offset.x==4,"device area owned after source mutation");
    area.offset.y=-1;require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::RenderArea,"active device area must be nonnegative");
    area={{0,0},{0,2}};require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::RenderArea,"active device area cannot be empty");
    group.deviceRenderAreaCount=0;group.pDeviceRenderAreas=poison<VkRect2D>();
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::RenderArea,"without override top-level render area is active");
    begin=beginInfo();begin.renderArea.extent.height=0;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::RenderArea,"ordinary empty area rejected");
}
void samples(){
    r::Shape shape;auto begin=beginInfo();auto samples=info<VkRenderPassSampleLocationsBeginInfoEXT>(VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT);begin.pNext=&samples;
    samples.pAttachmentInitialSampleLocations=poison<VkAttachmentSampleLocationsEXT>();samples.pPostSubpassSampleLocations=poison<VkSubpassSampleLocationsEXT>();
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.sampleLocations,"empty sample lists ignore both pointers");
    std::array<VkAttachmentSampleLocationsEXT,r::MaxLocationEntries> initial{};std::array<VkSubpassSampleLocationsEXT,r::MaxLocationEntries> post{};
    for(unsigned i=0;i<initial.size();++i){
        initial[i].attachmentIndex=i%8;post[i].subpassIndex=i%8;
        for(auto* nested:{&initial[i].sampleLocationsInfo,&post[i].sampleLocationsInfo}){
            nested->sType=static_cast<VkStructureType>(-1);nested->pNext=poison<void>();nested->sampleLocationsCount=UINT32_MAX;nested->pSampleLocations=poison<VkSampleLocationEXT>();
        }
    }
    samples.attachmentInitialSampleLocationsCount=initial.size();samples.pAttachmentInitialSampleLocations=initial.data();
    samples.postSubpassSampleLocationsCount=post.size();samples.pPostSubpassSampleLocations=post.data();
    require(r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.initialLocationCount==32&&shape.postLocationCount==32,"nonempty outer sample lists are supported without reading image-conditional nested state");
    for(unsigned i=0;i<initial.size();++i)require(shape.initialAttachmentIndices[i]==i%8&&shape.postSubpassIndices[i]==i%8,"all bounded outer sample indices retained");
    initial[0].attachmentIndex=99;post[0].subpassIndex=88;
    require(shape.initialAttachmentIndices[0]==0&&shape.postSubpassIndices[0]==0,"sample indices owned without nested pointer capture");
    samples.attachmentInitialSampleLocationsCount=33;samples.pAttachmentInitialSampleLocations=poison<VkAttachmentSampleLocationsEXT>();
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::ArrayBounds,"sample outer bound before either array access");
    samples.attachmentInitialSampleLocationsCount=0;samples.postSubpassSampleLocationsCount=33;samples.pPostSubpassSampleLocations=poison<VkSubpassSampleLocationsEXT>();
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::ArrayBounds,"post-sample outer bound checked");
    samples.postSubpassSampleLocationsCount=1;samples.pPostSubpassSampleLocations=nullptr;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::NullArray,"nonempty post list needs valid outer array");
    samples.postSubpassSampleLocationsCount=0;samples.attachmentInitialSampleLocationsCount=1;samples.pAttachmentInitialSampleLocations=nullptr;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::NullArray,"nonempty initial list needs valid outer array");
}
void censusAndParents(){
    r::Shape shape;auto begin=beginInfo();auto sub=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);sub.contents=VK_SUBPASS_CONTENTS_INLINE;auto end=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    auto unknown=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));
    auto attach=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
    auto group=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO);
    unknown.pNext=&attach;attach.pNext=&group;begin.pNext=&unknown;
    require(!r::begin2(&begin,&sub,shape)&&shape.refusal.reason==r::Reason::UnknownExtension,"unknown extension never silently admitted");
    require(chain(shape,r::Chain::RenderPassBegin).count==3&&chain(shape,r::Chain::RenderPassBegin).types[2]==group.sType,"unknown payload not read and later full header chain retained");
    require(shape.refusal.index==0&&shape.refusal.type==static_cast<std::uint32_t>(unknown.sType),"unknown exact type/index retained");
    auto realAttach=info<VkRenderPassAttachmentBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
    realAttach.attachmentCount=1;realAttach.pAttachments=poison<VkImageView>();realAttach.pNext=&unknown;unknown.pNext=nullptr;begin.pNext=&realAttach;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::UnknownExtension&&shape.refusal.index==1,"later unknown header refuses before preceding governed array access");
    begin.pNext=&unknown;
    unknown.pNext=&unknown;require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::ChainCycle&&chain(shape,r::Chain::RenderPassBegin).termination==r::Termination::Cycle,"self-cycle finite refusal");
    unknown.pNext=&attach;attach.pNext=&unknown;require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&chain(shape,r::Chain::RenderPassBegin).count==2,"multi-node cycle finite");
    attach.pNext=&group;group.pNext=nullptr;group.sType=attach.sType;begin.pNext=&attach;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::DuplicateType&&shape.refusal.index==1,"duplicate type refused before payload interpretation");
    std::array<VkBaseInStructure,r::MaxChain> nodes{};
    for(unsigned i=0;i<nodes.size();++i){nodes[i].sType=static_cast<VkStructureType>(100+i);nodes[i].pNext=i+1<nodes.size()?&nodes[i+1]:nullptr;}
    shape={};r::headerCensus(VK_STRUCTURE_TYPE_RENDERING_INFO,nodes.data(),r::Chain::RenderPassBegin,shape);
    require(chain(shape,r::Chain::RenderPassBegin).count==16&&chain(shape,r::Chain::RenderPassBegin).termination==r::Termination::Complete&&shape.refusal.reason==r::Reason::None&&!shape.supported,"exact header bound is evidence, never permission");
    nodes.back().pNext=poison<VkBaseInStructure>();shape={};r::headerCensus(VK_STRUCTURE_TYPE_RENDERING_INFO,nodes.data(),r::Chain::RenderPassBegin,shape);
    require(shape.refusal.reason==r::Reason::ChainBounds&&shape.refusal.index==16&&chain(shape,r::Chain::RenderPassBegin).termination==r::Termination::Bounds,"overflow pointer not followed after exact header budget");
    require(chain(shape,r::Chain::RenderPassBegin).types[15]==115&&chain(shape,r::Chain::RenderPassBegin).rootType==VK_STRUCTURE_TYPE_RENDERING_INFO,"full bounded census and actual unsupported root retained");
    nodes.back().pNext=nullptr;shape={};r::headerCensus(1,nodes.data(),r::Chain::Count,shape);require(shape.refusal.reason==r::Reason::None,"invalid sentinel chain is inert");
    for(const auto type:{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO,VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT}){
        auto node=info<VkBaseInStructure>(type);begin.pNext=nullptr;sub.pNext=&node;
        require(!r::begin2(&begin,&sub,shape)&&shape.refusal.reason==r::Reason::WrongParent&&shape.refusal.chain==r::Chain::SubpassBegin,"known Begin extension cannot migrate to SubpassBegin");
        sub.pNext=nullptr;end.pNext=&node;
        require(!r::next2(&sub,&end,shape)&&shape.refusal.reason==r::Reason::WrongParent&&shape.refusal.chain==r::Chain::SubpassEnd,"known Begin extension cannot migrate to SubpassEnd");
    }
    for(const auto type:{VK_STRUCTURE_TYPE_RENDER_PASS_TRANSFORM_BEGIN_INFO_QCOM,VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_RENDER_AREAS_RENDER_PASS_BEGIN_INFO_QCOM,VK_STRUCTURE_TYPE_RENDER_PASS_STRIPE_BEGIN_INFO_ARM,static_cast<VkStructureType>(r::PerformanceCountersBeginType)}){
        auto node=info<VkBaseInStructure>(type);begin.pNext=&node;sub.pNext=nullptr;
        require(!r::begin2(&begin,&sub,shape)&&shape.refusal.reason==r::Reason::UnsupportedExtension&&shape.refusal.type==static_cast<std::uint32_t>(type),"known vendor Begin semantics explicitly refused without reading payload");
    }
    auto density=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_SUBPASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_QCOM);begin.pNext=&density;
    require(!r::begin(&begin,VK_SUBPASS_CONTENTS_INLINE,shape)&&shape.refusal.reason==r::Reason::WrongParent,"known End-only extension illegal at Begin");
    end.pNext=&density;require(!r::end2(&end,shape)&&shape.refusal.reason==r::Reason::UnsupportedExtension,"legal density End semantics identified but unsupported");
    auto other=info<VkBaseInStructure>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);unknown.pNext=&other;begin.pNext=&unknown;sub.pNext=&other;end.pNext=&unknown;
    require(!r::begin2(&begin,&sub,shape)&&chain(shape,r::Chain::RenderPassBegin).count==2&&chain(shape,r::Chain::SubpassBegin).count==1,"Begin2 all root chains retained despite first refusal");
    require(!r::next2(&sub,&end,shape)&&chain(shape,r::Chain::SubpassBegin).count==1&&chain(shape,r::Chain::SubpassEnd).count==2,"Next2 all root chains retained despite first refusal");
}
void fullPaths(){
    // Scoped adapter model: decode synchronously, then invoke the original with
    // EXACT caller pointers. Actual exported Vulkan hooks have separate tests.
    unsigned forwarded=0,admitted=0;auto begin=beginInfo();auto sub=info<VkSubpassBeginInfo>(VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO);sub.contents=VK_SUBPASS_CONTENTS_INLINE;auto end=info<VkSubpassEndInfo>(VK_STRUCTURE_TYPE_SUBPASS_END_INFO);
    auto attachment=info<VkRenderPassAttachmentBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
    VkImageView image=handle<VkImageView>(123);attachment.attachmentCount=1;attachment.pAttachments=&image;begin.pNext=&attachment;
    auto group=info<VkDeviceGroupRenderPassBeginInfo>(VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO);group.deviceMask=1;group.pDeviceRenderAreas=poison<VkRect2D>();attachment.pNext=&group;
    auto samples=info<VkRenderPassSampleLocationsBeginInfoEXT>(VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT);samples.pAttachmentInitialSampleLocations=poison<VkAttachmentSampleLocationsEXT>();group.pNext=&samples;
    const auto savedBegin=begin;const auto savedAttachment=attachment;const auto savedGroup=group;const auto savedSamples=samples;
    for(unsigned route=0;route<3;++route){r::Shape shape;
        const bool okay=route==0?r::begin(&begin,sub.contents,shape):r::begin2(&begin,&sub,shape);admitted+=okay;
        const auto original=[&](const VkRenderPassBeginInfo* actual,const VkSubpassBeginInfo* actualSub){++forwarded;require(actual==&begin&&(route==0?actualSub==nullptr:actualSub==&sub),"original Begin route receives exact pointers once");};
        original(&begin,route==0?nullptr:&sub);
        require(shape.attachmentCount==1&&shape.attachments[0]==image&&shape.deviceMask==1&&shape.sampleLocations,"combined portable Begin chain supported");
        admitted+=route==0?r::next(sub.contents,shape):r::next2(&sub,&end,shape);++forwarded;
        admitted+=route==0?r::end(shape):r::end2(&end,shape);++forwarded;
    }
    require(forwarded==9&&admitted==9,"legacy/Core2/KHR2 full-path ordinary operations all forward once");
    require(begin.pNext==savedBegin.pNext&&begin.pClearValues==savedBegin.pClearValues&&attachment.pNext==savedAttachment.pNext&&attachment.pAttachments==savedAttachment.pAttachments&&group.pNext==savedGroup.pNext&&group.pDeviceRenderAreas==savedGroup.pDeviceRenderAreas&&samples.pAttachmentInitialSampleLocations==savedSamples.pAttachmentInitialSampleLocations,"all original headers and array pointer arguments preserved");
    r::Shape shape;auto bad=info<VkBaseInStructure>(static_cast<VkStructureType>(0x6fffffff));begin.pNext=&bad;
    require(!r::begin2(&begin,&sub,shape),"unknown full-path refuses observer");++forwarded;
    require(forwarded==10&&shape.refusal.type==static_cast<std::uint32_t>(bad.sType),"refusal does not consume original call");
    require(std::strcmp(r::reasonName(r::Reason::WrongParent),"wrong_parent")==0&&std::strcmp(r::chainName(r::Chain::SubpassEnd),"subpass_end")==0,"stable reporter labels");
    require(std::strcmp(r::reasonName(static_cast<r::Reason>(255)),"invalid_recording_reason")==0&&std::strcmp(r::chainName(r::Chain::Count),"invalid_chain")==0,"finite fallback labels");
}
}
int main(){static_assert(std::is_trivially_copyable<r::Shape>::value,"owned scalar facts copy independently");ordinary();attachments();groups();samples();censusAndParents();fullPaths();std::cout<<"{\"scope\":\"renderpass-recording-contract\",\"checks\":"<<checks<<",\"shape_bytes\":"<<sizeof(r::Shape)<<",\"native_authority\":false}\n";}
