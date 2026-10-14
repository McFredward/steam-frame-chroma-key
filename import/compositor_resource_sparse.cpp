#include "compositor_resource_sparse.h"

#include <array>

namespace frame_compositor_sparse {
namespace {
constexpr std::uintptr_t kMax = (std::uintptr_t{1} << 48) - 1;
struct Span { std::size_t offset, size; };
constexpr Span direct[]{{108,4},{128,8}};
constexpr Span creation[]{{96,16},{752,16},{912,16},{1008,16},{152,4},{1024,8},{1340,4}};
constexpr Span facet_creation[]{{0,16},{688,16},{1056,16},{1216,16},{1312,16},
                                {744,4},{1328,8},{1644,4}};
constexpr Span send[]{{0,16},{64,8},{188,4}};
constexpr Span write[]{{0,16},{112,16},{176,8},{300,4}};
constexpr Span finish[]{{32,16},{96,8},{220,4}};
constexpr std::uintptr_t sites[]{0x153fc0,0x1540b0,0x2c75ac,0x2c7684,
                                0x2c7804,0x172ea0,0x172c00,0x172fbc};
}

std::size_t scalar_budget(frame_import_native::Boundary boundary,
                         frame_import_native::CreationRoute route) noexcept {
    using B=frame_import_native::Boundary;
    switch(boundary) {
    case B::Begin: case B::EarlyUnlock:return 12;
    case B::Image: case B::Bind: case B::View:
        return route == frame_import_native::CreationRoute::Facet ? 96 :
               route == frame_import_native::CreationRoute::Vulkan ? 80 : 0;
    case B::SendLock: case B::Finish:return 28;
    case B::Write:return 44;
    default:return 0;
    }
}

Result decode(frame_import_native::Boundary boundary, const frame_import_native::Entry& entry,
              std::uintptr_t bias, const frame_import_native::Expected& expected,
              Read reader, void* owner) noexcept {
    Result out{};
    const auto which=static_cast<unsigned>(boundary);
    if(which>=8) {out.refusal=Refusal::Boundary;return out;}
    if(!reader || !owner) {out.refusal=Refusal::Reader;return out;}
    using B=frame_import_native::Boundary;
    const bool creating = boundary==B::Image || boundary==B::Bind || boundary==B::View;
    const auto selected=frame_import_native::creation_route(boundary,entry,bias);
    const auto fp_offset=boundary==B::Finish?32U:0U;
    if(bias<4096 || bias%4096 || entry.sp<4096 || entry.sp%16 || entry.sp>kMax-kScratchBytes ||
       (creating ? selected==frame_import_native::CreationRoute::None :
        bias>kMax-sites[which] || entry.x[30]!=bias+sites[which] || entry.x[29]!=entry.sp+fp_offset)) return out;

    std::array<Span,8> spans{};
    std::size_t count=0,extent=0;
    const auto assign=[&](const auto& values,std::size_t size) noexcept {
        for(const auto value:values)spans[count++]=value;
        extent=size;
    };
    if(boundary==B::Begin || boundary==B::EarlyUnlock)assign(direct,136);
    else if(creating && selected==frame_import_native::CreationRoute::Facet)assign(facet_creation,1648);
    else if(creating)assign(creation,1344);
    else if(boundary==B::SendLock)assign(send,192);
    else if(boundary==B::Write)assign(write,304);
    else assign(finish,224);

    std::array<std::uint8_t,kScratchBytes> scratch{};
    for(std::size_t i=0;i<count;++i) {
        const auto span=spans[i];
        out.bytes_requested+=span.size;
        if(!reader(entry.sp+span.offset,scratch.data()+span.offset,span.size,owner)) {
            out.refusal=Refusal::Reader;return out;
        }
        out.bytes_confirmed+=span.size;
    }
    out.scalars=frame_import_native::decode(boundary,entry,{scratch.data(),entry.sp,extent},bias,expected);
    out.refusal=out.scalars.valid()?Refusal::None:Refusal::Decode;
    return out;
}
} // namespace frame_compositor_sparse
