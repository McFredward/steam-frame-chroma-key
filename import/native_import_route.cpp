#include "native_import_route.h"

#include <cstring>
#include <limits>

namespace frame_import_native {
namespace {
constexpr std::uintptr_t kMaxUser = (std::uintptr_t{1} << 48) - 1;
constexpr std::uint64_t kSites[] = {
    0x153fc0, 0x1540b0, 0x2c75ac, 0x2c7684,
    0x2c7804, 0x172ea0, 0x172c00, 0x172fbc
};
bool add(std::uintptr_t a, std::size_t b, std::uintptr_t& out) noexcept {
    if (a > kMaxUser || b > kMaxUser - a) return false;
    out = a + b;
    return true;
}
bool address(std::uintptr_t value) noexcept {
    return value >= 4096 && value <= kMaxUser && !(value & 15);
}
template<class T> bool slot(const StackBytes& stack, std::uintptr_t at, T& out) noexcept {
    if (!stack.data || at < stack.base || stack.size > kMaxUser - stack.base ||
        at - stack.base > stack.size || sizeof(T) > stack.size - (at - stack.base)) return false;
    std::memcpy(&out, stack.data + (at - stack.base), sizeof(T));
    return true;
}
bool pc(std::uint64_t value, std::uintptr_t bias, std::uint64_t rva) noexcept {
    return bias <= kMaxUser && rva <= kMaxUser - bias && value == bias + rva;
}
bool link(const StackBytes& stack, std::uintptr_t frame, std::uintptr_t parent,
          std::uintptr_t bias, std::uint64_t rva, Refusal& refusal,
          std::uint64_t alternate = 0) noexcept {
    std::uint64_t saved_fp = 0, saved_lr = 0;
    std::uintptr_t lr_at = 0;
    if (!add(frame, 8, lr_at) || !slot(stack, frame, saved_fp) || !slot(stack, lr_at, saved_lr)) {
        refusal = Refusal::OutsideWindow; return false;
    }
    if (saved_fp != parent) { refusal = Refusal::FrameLink; return false; }
    if (!pc(saved_lr, bias, rva) && (!alternate || !pc(saved_lr, bias, alternate))) {
        refusal = Refusal::ParentReturn; return false;
    }
    return true;
}
bool sequence(const StackBytes& stack, Scalars& result) noexcept {
    std::uintptr_t at = 0;
    if (!add(result.invocation.dispatch_frame, 108, at) || !slot(stack, at, result.invocation.sequence)) {
        result.refusal = Refusal::OutsideWindow; return false;
    }
    return true;
}
bool matches(Scalars& result, const Expected& expected) noexcept {
    if (!expected.dispatch_frame || !expected.pipe ||
        result.invocation.dispatch_frame != expected.dispatch_frame ||
        result.invocation.pipe != expected.pipe || result.invocation.sequence != expected.sequence) {
        result.refusal = Refusal::ChangedInvocation; return false;
    }
    return true;
}
}

Scalars decode(Boundary boundary, const Entry& entry, const StackBytes& stack,
               std::uintptr_t bias, const Expected& expected) noexcept {
    Scalars out{};
    const auto index = static_cast<unsigned>(boundary);
    if (index >= sizeof(kSites)/sizeof(kSites[0]) || !pc(entry.x[30], bias, kSites[index])) {
        out.refusal = Refusal::UnknownSite; return out;
    }
    const auto fp = static_cast<std::uintptr_t>(entry.x[29]);
    if (!address(fp) || !address(entry.sp) || stack.size > 1552 || !address(stack.base)) return out;
    if (boundary == Boundary::Begin || boundary == Boundary::EarlyUnlock) {
        if (fp != entry.sp) return out;
        out.invocation.dispatch_frame = fp;
        out.invocation.pipe = entry.x[0];
        std::uint64_t saved_pipe = 0;
        std::uintptr_t at = 0;
        if (!add(fp, 128, at) || !slot(stack, at, saved_pipe)) {
            out.refusal = Refusal::OutsideWindow; return out;
        }
        if (!out.invocation.pipe || saved_pipe != out.invocation.pipe ||
            (boundary == Boundary::Begin && entry.x[19] != out.invocation.pipe)) {
            out.refusal = Refusal::Arguments; return out;
        }
        if (!sequence(stack, out)) return out;
        // x25 is only assigned by the native payload-read route after Begin.
        if (boundary == Boundary::EarlyUnlock)
            out.native_payload_read = (entry.x[25] & 255) != 0;
        if (boundary == Boundary::EarlyUnlock && !matches(out, expected)) return out;
    } else if (boundary == Boundary::Image || boundary == Boundary::Bind || boundary == Boundary::View) {
        std::uintptr_t entry_fp = 0, wrapper = 0, helper = 0, method = 0, dispatch = 0;
        if (!add(entry.sp, 96, entry_fp) || entry_fp != fp ||
            !add(fp, 656, wrapper) || !add(fp, 816, helper) ||
            !add(fp, 912, method) || !add(fp, 1136, dispatch)) return out;
        if (!link(stack, fp, wrapper, bias, 0x70bc8, out.refusal) ||
            !link(stack, wrapper, helper, bias, 0x13bbdc, out.refusal, 0x13bc8c) ||
            !link(stack, helper, method, bias, 0x13beb0, out.refusal) ||
            !link(stack, method, dispatch, bias, 0x154194, out.refusal)) return out;
        out.invocation.dispatch_frame = dispatch;
        std::uintptr_t client_at = 0, pipe_at = 0;
        // Method saves the dispatch caller's x19, which is still the pipe.
        if (!add(fp, 56, client_at) || !slot(stack, client_at, out.client) ||
            !add(method, 16, pipe_at) || !slot(stack, pipe_at, out.invocation.pipe)) {
            out.refusal = Refusal::OutsideWindow; return out;
        }
        out.client_available = true;
        if (!sequence(stack, out) || !matches(out, expected)) return out;
    } else {
        std::uintptr_t send = fp, dispatch = 0;
        if (boundary == Boundary::Write) {
            if (fp != entry.sp || !add(fp, 112, send)) return out;
            if (!link(stack, fp, send, bias, 0x172f34, out.refusal)) return out;
        } else if (boundary == Boundary::SendLock) {
            if (fp != entry.sp) return out;
        } else {
            std::uintptr_t plus32 = 0;
            if (!add(entry.sp, 32, plus32) || plus32 != fp) return out;
        }
        if (!add(send, 80, dispatch) || !link(stack, send, dispatch, bias, 0x1542b0, out.refusal)) return out;
        out.invocation.dispatch_frame = dispatch;
        std::uint64_t saved_pipe = 0;
        std::uintptr_t at = 0;
        if (!add(send, 64, at) || !slot(stack, at, saved_pipe)) {
            out.refusal = Refusal::OutsideWindow; return out;
        }
        out.invocation.pipe = saved_pipe;
        if (!sequence(stack, out) || !matches(out, expected)) return out;
        if (boundary == Boundary::SendLock) {
            if (entry.x[0] != saved_pipe || entry.x[19] != saved_pipe ||
                entry.x[20] != 9 || entry.x[21] != 0x235 || entry.x[22] != out.invocation.sequence) {
                out.refusal = Refusal::ResponseShape; return out;
            }
        } else if (boundary == Boundary::Finish) {
            if (entry.x[0] != saved_pipe || entry.x[20] > 1) {
                out.refusal = Refusal::Arguments; return out;
            }
            out.native_send_success = entry.x[20] == 1;
        } else {
            if (send < 32) return out;
            out.packet_base = send - 32;
            if (entry.x[19] >= 21 || entry.x[20] != 21 || entry.x[22] != saved_pipe ||
                entry.x[23] != out.packet_base || entry.x[1] != out.packet_base + entry.x[19] ||
                entry.x[2] != 21 - entry.x[19] || entry.x[0] > std::numeric_limits<int>::max()) {
                out.refusal = Refusal::Arguments; return out;
            }
            out.packet_offset = static_cast<std::uint32_t>(entry.x[19]);
            out.remaining = static_cast<std::uint32_t>(entry.x[2]);
            out.fd = static_cast<int>(entry.x[0]);
        }
    }
    out.refusal = Refusal::None;
    return out;
}
} // namespace frame_import_native
