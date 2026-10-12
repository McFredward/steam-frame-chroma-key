#include "native_client_route.h"
#include "client_carrier_core.h"
#include <cstring>
#include <limits>

namespace frame_client_native {
namespace {
constexpr std::uintptr_t kMax = (std::uintptr_t{1} << 48) - 1;
constexpr std::uint64_t kSites[] = {0x3623c0,0x2456c8,0x245334,0x245460,0x3624c4,0x362d58,0x244dbc};
bool add(std::uintptr_t a, std::size_t n, std::uintptr_t& out) noexcept {
    if (a > kMax || n > kMax - a) return false;
    out = a + n; return true;
}
bool address(std::uintptr_t v, std::size_t alignment = 16) noexcept { return v >= 4096 && v <= kMax && v % alignment == 0; }
bool pc(std::uint64_t v, std::uintptr_t bias, std::uint64_t rva) noexcept { return bias <= kMax && rva <= kMax - bias && v == bias + rva; }
template<class T> bool slot(const StackBytes& s, std::uintptr_t at, T& v) noexcept {
    if (!s.data || s.base > kMax || s.size > kMax - s.base || at < s.base || at - s.base > s.size || sizeof(T) > s.size - (at - s.base)) return false;
    std::memcpy(&v, s.data + at - s.base, sizeof(T)); return true;
}
template<class T> bool field(const StackBytes& s, std::uintptr_t base, std::size_t offset, T& v, Scalars& out) noexcept {
    std::uintptr_t at = 0;
    if (!add(base, offset, at) || !slot(s, at, v)) { out.refusal = Refusal::Window; return false; }
    return true;
}
bool link(const StackBytes& s, std::uintptr_t frame, std::uintptr_t parent, std::uintptr_t bias,
          std::uint64_t lr, Scalars& out) noexcept {
    std::uint64_t fp = 0, ret = 0;
    if (!field(s, frame, 0, fp, out) || !field(s, frame, 8, ret, out)) return false;
    if (fp != parent) { out.refusal = Refusal::Link; return false; }
    if (!pc(ret, bias, lr)) { out.refusal = Refusal::Return; return false; }
    return true;
}
bool import(const StackBytes& s, std::uintptr_t frame, std::uintptr_t receiver_bias, Scalars& out) noexcept {
    out.invocation.import_frame = frame;
    if (!add(frame, 352, out.invocation.receiver_frame) ||
        !link(s, frame, out.invocation.receiver_frame, receiver_bias, 0x34608, out) ||
        !field(s, frame, 136, out.invocation.pipe, out) ||
        !field(s, frame, 16, out.invocation.record, out)) return false;
    if (!address(out.invocation.pipe) || !address(out.invocation.record, 8)) { out.refusal = Refusal::Arguments; return false; }
    return true;
}
bool matches(Scalars& out, const Expected& e) noexcept {
    const auto& a = out.invocation; const auto& b = e.invocation;
    if (!b.import_frame || !b.receiver_frame || !b.pipe || !b.record || a.import_frame != b.import_frame ||
        a.receiver_frame != b.receiver_frame || a.pipe != b.pipe || a.record != b.record ||
        (out.sequence_available && e.sequence_available && out.native_sequence != e.sequence)) {
        out.refusal = Refusal::ChangedInvocation; return false;
    }
    return true;
}
bool output(std::uint64_t pointer, Scalars& out) noexcept {
    std::uintptr_t expected = 0;
    if (!add(out.invocation.record, 32, expected) || pointer != expected) { out.refusal = Refusal::Arguments; return false; }
    return true;
}
}
Scalars decode(Boundary boundary, const Entry& e, const StackBytes& stack,
               std::uintptr_t client_bias, std::uintptr_t receiver_bias, const Expected& expected) noexcept {
    Scalars out;
    const auto which = static_cast<unsigned>(boundary);
    if (which >= sizeof(kSites)/sizeof(kSites[0]) || !pc(e.x[30], client_bias, kSites[which])) { out.refusal = Refusal::Site; return out; }
    const auto fp = static_cast<std::uintptr_t>(e.x[29]);
    if (!address(fp) || fp != e.sp || !address(stack.base) || stack.size > 1552) return out;
    if (boundary == Boundary::AcceptedInit) {
        std::uintptr_t parent = 0, attributes = 0;
        if (!add(fp, 32, parent) || !link(stack, fp, parent, client_bias, 0x24500c, out)) return out;
        if (!address(e.x[0]) || !add(e.x[0], 48, attributes) || e.x[0] != e.x[19] || e.x[0] != e.x[23] ||
            e.x[1] != attributes || e.x[20] > std::numeric_limits<int>::max()) { out.refusal = Refusal::Arguments; return out; }
        out.invocation.pipe = e.x[0]; out.fd = static_cast<int>(e.x[20]); out.accepted_fd_pending = true;
    } else if (boundary == Boundary::Reference) {
        std::uintptr_t caller = 0;
        if (!add(fp, 240, caller) || !link(stack, fp, caller, receiver_bias, 0x3462c, out)) return out;
        // Only the copied expected import identity survives; the current frame
        // is a different native method after import returned to this same caller.
        out.invocation.import_frame = expected.invocation.import_frame;
        out.invocation.receiver_frame = caller;
        if (!field(stack, fp, 104, out.invocation.pipe, out) || !field(stack, fp, 16, out.invocation.record, out) ||
            !field(stack, fp, 224, out.token, out) || !matches(out, expected)) return out;
        if (!out.token || out.token != e.x[24] || e.x[0] != out.invocation.pipe || e.x[19] != out.invocation.pipe ||
            out.invocation.import_frame > kMax - 352 || out.invocation.import_frame + 352 != caller) { out.refusal = Refusal::Arguments; return out; }
        out.token_present = true;
    } else if (boundary == Boundary::Begin || boundary == Boundary::Pending) {
        if (!import(stack, fp, receiver_bias, out) || !output(e.x[20], out)) return out;
        if (e.x[0] != out.invocation.pipe || (boundary == Boundary::Begin && e.x[21] != out.invocation.pipe)) { out.refusal = Refusal::Arguments; return out; }
        if (boundary == Boundary::Pending) {
            if (!matches(out, expected)) return out;
            std::uint32_t present = 0;
            if (!field(stack, fp, 224, present, out) || !field(stack, fp, 232, out.token, out)) return out;
            if (e.x[24] > 1) { out.refusal = Refusal::Arguments; return out; }
            out.token_present = (present & 1) != 0; out.native_wait_success = e.x[24] == 1;
        }
        // x25 is unrelated to the request sequence at Begin, and may have been
        // clobbered by native deferred logging at Pending. Never infer it here.
    } else if (boundary == Boundary::Write) {
        if (e.x[20] <= 12 || e.x[20] > frame_client_carrier::kMaxRequestBytes || e.x[19] >= e.x[20]) { out.refusal = Refusal::Bound; return out; }
        std::uintptr_t packet = 0, send = 0, method = 0;
        const auto rounded = (static_cast<std::size_t>(e.x[20]) + 15) & ~std::size_t{15};
        if (!add(fp, 80, packet) || !add(packet, rounded, send) || !add(send, 80, method) ||
            !link(stack, fp, send, client_bias, 0x2459fc, out) ||
            !link(stack, send, method, client_bias, 0x362418, out) || !import(stack, method, receiver_bias, out)) return out;
        std::uint32_t opcode = 0; std::uint64_t saved_pipe = 0, saved_output = 0;
        if (!field(stack, fp, 32, opcode, out) || !field(stack, fp, 40, out.native_sequence, out) ||
            !field(stack, send, 64, saved_pipe, out) || !field(stack, send, 24, saved_output, out)) return out;
        out.sequence_available = true;
        if (!matches(out, expected) || !output(saved_output, out)) return out;
        if (opcode != 0x234 || e.x[22] != out.invocation.pipe || saved_pipe != out.invocation.pipe || e.x[23] != packet ||
            e.x[1] != packet + e.x[19] || e.x[2] != e.x[20] - e.x[19] || e.x[21] != e.x[2] || e.x[0] > std::numeric_limits<int>::max()) {
            out.refusal = Refusal::Arguments; return out;
        }
        out.buffer_base = packet; out.offset = static_cast<std::uint32_t>(e.x[19]);
        out.total = static_cast<std::uint32_t>(e.x[20]); out.remaining = static_cast<std::uint32_t>(e.x[2]); out.fd = static_cast<int>(e.x[0]);
    } else {
        std::uintptr_t waiter = 0, method = 0;
        if (!add(fp, 96, waiter) || !add(waiter, 192, method) ||
            !link(stack, fp, waiter, client_bias, boundary == Boundary::HeaderRead ? 0x364b58 : 0x364ba8, out) ||
            !link(stack, waiter, method, client_bias, 0x362440, out) || !import(stack, method, receiver_bias, out) ||
            !field(stack, waiter, 116, out.native_sequence, out)) return out;
        out.sequence_available = true;
        if (!expected.sequence_available || !matches(out, expected)) { out.refusal = Refusal::ChangedInvocation; return out; }
        std::uint64_t saved_pipe = 0;
        if (!field(stack, fp, 16, saved_pipe, out)) return out;
        if (saved_pipe != out.invocation.pipe || e.x[20] != out.invocation.pipe || e.x[0] > std::numeric_limits<int>::max()) { out.refusal = Refusal::Arguments; return out; }
        const auto offset = e.x[19];
        if (boundary == Boundary::HeaderRead) {
            std::uintptr_t buffer = 0;
            if (!add(fp, 80, buffer) || e.x[24] != 12 || offset >= 12 || e.x[25] != buffer || e.x[1] != buffer + offset || e.x[2] != 12 - offset) {
                out.refusal = Refusal::Arguments; return out;
            }
            out.buffer_base = buffer; out.total = 12;
        } else {
            std::uint32_t length = 0;
            if (!field(stack, waiter, 156, length, out)) return out;
            if (length != 9 || e.x[21] != 9 || e.x[23] != 9 || offset >= 9 || !address(e.x[22], 8) ||
                e.x[22] > kMax - 9 || e.x[1] != e.x[22] + offset || e.x[2] != 9 - offset) { out.refusal = Refusal::Arguments; return out; }
            out.buffer_base = e.x[22]; out.total = 9;
        }
        out.offset = static_cast<std::uint32_t>(offset); out.remaining = static_cast<std::uint32_t>(e.x[2]); out.fd = static_cast<int>(e.x[0]);
    }
    out.refusal = Refusal::None; return out;
}
} // namespace frame_client_native
