#include "native_client_birth.h"

#include <cstring>
#include <limits>

namespace frame_native_client_birth {
namespace {
bool address(std::uint64_t value) noexcept { return value && value < (1ULL << 48); }
bool add(std::uintptr_t a, std::uintptr_t b, std::uintptr_t& out) noexcept {
    if (b > std::numeric_limits<std::uintptr_t>::max() - a) return false;
    out = a + b; return true;
}
bool code_at(const CodeRegion& code, std::uintptr_t offset) noexcept {
    std::uintptr_t at = 0;
    return add(code.bias, offset, at) && at >= code.begin && at < code.end;
}
std::uint64_t word(const std::array<std::uint8_t,kPrefixBytes>& bytes, std::size_t offset) noexcept {
    std::uint64_t result = 0;
    std::memcpy(&result,bytes.data()+offset,sizeof(result)); return result;
}
Facts shape(const frame_import_native::Entry& e,const CodeRegion& code) noexcept {
    Facts f{};
    if (!code.bias || code.bias % 4096 || code.begin >= code.end ||
        !code_at(code,kInitReturn) || !code_at(code,kHmdReturn) || !code_at(code,kGlobalReturn) ||
        !address(e.x[30]) || e.x[30] < code.bias || e.x[30]-code.bias != kInitReturn) return f;
    f.refusal = Refusal::Arguments;
    if (!Reader::candidate(e) || !address(e.sp) || e.sp != e.x[29]) return f;
    std::uintptr_t pipe = 0;
    if (!add(code.bias,kPipeObject,pipe) || pipe != e.x[0]) { f.refusal=Refusal::Singleton; return f; }
    f.pipe=pipe; f.frame=e.sp;
    if (!add(e.sp,32,f.hmd_frame) || !add(e.sp,416,f.global_frame)) { f.refusal=Refusal::FrameBounds; return f; }
    f.refusal=Refusal::None; return f;
}
}
bool Reader::candidate(const frame_import_native::Entry& e) noexcept {
    return address(e.x[30]) && (e.x[30] & 4095) == (kInitReturn & 4095) &&
        address(e.x[0]) && e.x[0] % 16 == 0 && e.x[0] == e.x[19] &&
        e.x[0] <= std::numeric_limits<std::uintptr_t>::max()-48 && e.x[1] == e.x[0]+48 &&
        address(e.sp) && e.sp % 16 == 0 && e.x[29] == e.sp;
}
Facts Reader::decode(const frame_import_native::Entry& e,
                     const std::array<std::uint8_t,kPrefixBytes>& bytes,
                     const CodeRegion& code) noexcept {
    auto f=shape(e,code); if (!f.valid()) return f;
    if (word(bytes,0)!=f.hmd_frame || word(bytes,32)!=f.global_frame) { f.refusal=Refusal::FrameLink; return f; }
    if (word(bytes,8)!=code.bias+kHmdReturn || word(bytes,40)!=code.bias+kGlobalReturn) {
        f.refusal=Refusal::ParentReturn; return f;
    }
    if (word(bytes,16)!=code.bias+kHmdObject) f.refusal=Refusal::Singleton;
    return f;
}
Facts Reader::acquire(const frame_import_native::Entry& e,const StackRegion& stack,
                      const CodeRegion& code) noexcept {
    auto f=shape(e,code); if (!f.valid()) return f;
    std::uintptr_t end=0;
    if (stack.begin>=stack.end || e.sp<stack.begin || !add(e.sp,kKnownFrameBytes,end) || end>stack.end) {
        f.refusal=Refusal::FrameBounds; return f;
    }
    std::array<std::uint8_t,kPrefixBytes> bytes{};
    for (const auto offset : {0U,8U,16U,32U,40U})
        std::memcpy(bytes.data()+offset,reinterpret_cast<const void*>(e.sp+offset),sizeof(std::uint64_t));
    return decode(e,bytes,code);
}
} // namespace frame_native_client_birth
