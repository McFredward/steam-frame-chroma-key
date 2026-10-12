#include "client_resource_sparse.h"

#include <array>

namespace frame_client_sparse {
namespace {
constexpr std::uintptr_t kMax = (std::uintptr_t{1} << 48) - 1;
struct Span { std::size_t offset, size; };
constexpr Span direct[]{{0,24},{136,8}};
constexpr Span pending[]{{0,24},{136,8},{224,4},{232,8}};
constexpr Span reference[]{{0,24},{104,8},{224,8}};
constexpr Span reads[]{{0,24},{96,16},{212,4},{288,24},{424,8}};
}
Result decode(frame_client_native::Boundary boundary, const frame_client_native::Entry& entry,
              std::uintptr_t client_bias, std::uintptr_t receiver_bias,
              const frame_client_native::Expected& expected, Read read, void* owner) noexcept {
    Result out;
    constexpr std::uintptr_t sites[]{0x3623c0,0x2456c8,0x245334,0x245460,0x3624c4,0x362d58};
    const auto which=static_cast<unsigned>(boundary);
    if(which>=6) {out.refusal=Refusal::Boundary;return out;}
    if(!read || !owner) {out.refusal=Refusal::Reader;return out;}
    if(client_bias<4096 || client_bias%4096 || client_bias>kMax-sites[which] ||
       receiver_bias<4096 || receiver_bias%4096 || receiver_bias>kMax-0x3462c ||
       entry.x[30]!=client_bias+sites[which] || entry.sp<4096 || entry.sp%16 ||
       entry.x[29]!=entry.sp || entry.sp>kMax-kScratchBytes) return out;

    std::array<Span,8> spans{};
    std::size_t count=0,extent=0;
    const auto assign=[&](const auto& values,std::size_t size) noexcept {
        for(const auto value:values)spans[count++]=value;
        extent=size;
    };
    using Boundary=frame_client_native::Boundary;
    if(boundary==Boundary::Begin)assign(direct,144);
    else if(boundary==Boundary::Pending)assign(pending,240);
    else if(boundary==Boundary::Reference)assign(reference,232);
    else if(boundary==Boundary::HeaderRead || boundary==Boundary::PayloadRead) {
        assign(reads,432);
        if(boundary==Boundary::PayloadRead)spans[count++]={252,4};
    } else {
        // Native total controls fixed arithmetic only, never pointer chasing.
        if(entry.x[20]<=12 || entry.x[20]>512 || entry.x[19]>=entry.x[20])return out;
        const auto rounded=(static_cast<std::size_t>(entry.x[20])+15)&~std::size_t{15};
        const auto send=80+rounded,method=send+80;
        spans={{{0,16},{32,4},{40,4},{send,16},{send+24,8},{send+64,8},{method,24},{method+136,8}}};
        count=spans.size();extent=method+144;
    }
    if(extent>kScratchBytes)return out;
    std::array<std::uint8_t,kScratchBytes> scratch{};
    for(std::size_t i=0;i<count;++i) {
        const auto span=spans[i];
        if(span.offset>extent || span.size>extent-span.offset ||
           !read(entry.sp+span.offset,scratch.data()+span.offset,span.size,owner)) {
            out.refusal=Refusal::Reader;return out;
        }
        out.bytes_copied+=span.size;
    }
    out.scalars=frame_client_native::decode(boundary,entry,{scratch.data(),entry.sp,extent},
                                           client_bias,receiver_bias,expected);
    out.refusal=out.scalars.valid()?Refusal::None:Refusal::Decode;
    return out;
}
} // namespace frame_client_sparse
