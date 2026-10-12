#include "client_resource_thread.h"

#include <cerrno>

namespace frame_client_resource_thread {
namespace {
constexpr std::array<std::uint8_t,32> digest(const char* text) noexcept {
    std::array<std::uint8_t,32> out{};
    const auto nibble=[](char c) constexpr { return static_cast<unsigned>(c>='a'?c-'a'+10:c-'0'); };
    for (unsigned i=0;i<32;++i)
        out[i]=static_cast<std::uint8_t>((nibble(text[2*i])<<4)|nibble(text[2*i+1]));
    return out;
}
constexpr auto receiver_identity=digest("31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153");
constexpr std::uintptr_t max_address=0x0000ffffffffffffULL;
std::uintptr_t return_point(const frame_import_startup::PreparedImage& receiver) noexcept {
    return receiver.bias>=4096 && receiver.bias<=max_address-kFirstWaitReturn
        ? receiver.bias+kFirstWaitReturn : 0;
}
bool valid_image(const frame_import_startup::PreparedImage& receiver,std::uintptr_t at) noexcept {
    return at && receiver.bias%4096==0 && receiver.file_identity==receiver_identity &&
        receiver.code_begin>=4096 && receiver.code_begin<receiver.code_end && receiver.code_end<=max_address &&
        receiver.code_begin<=receiver.bias+0x32878 &&
        at<receiver.code_end;
}
bool geometry(const Entry& e) noexcept {
    return e.sp>=4096 && e.sp<=max_address-kWorkerFrameBytes && e.sp%16==0 && e.x[29]==e.sp &&
        e.x[1]==e.sp+120 && e.x[23]==e.x[1] && e.x[19]>=4096 && e.x[19]<=max_address && e.x[19]%8==0 &&
        static_cast<std::uint32_t>(e.x[0])<=0x7fffffffU &&
        static_cast<std::uint32_t>(e.x[2])==1 && static_cast<std::uint32_t>(e.x[3])==0xffffffffU;
}
bool contains(const frame_native_client_birth::StackRegion& region,std::uintptr_t at) noexcept {
    return region.begin<region.end && at>=region.begin && at<=region.end &&
        kWorkerFrameBytes<=region.end-at;
}
Outcome refused(const Entry& e,Reason reason) noexcept { return {Kind::Refused,reason,e}; }
} // namespace

Gate::Gate(frame_import_startup::PreparedImage receiver) noexcept
    : receiver_(receiver),wait_return_(return_point(receiver)) {
    if (valid_image(receiver_,wait_return_)) {
        reason_.store(Reason::None,std::memory_order_relaxed);
        state_.store(State::Dormant,std::memory_order_relaxed);
    }
}
void Gate::refuse(Reason why) noexcept {
    auto reason=Reason::None;
    reason_.compare_exchange_strong(reason,why,std::memory_order_acq_rel);
    // Both terminal states deny ownership/rearming. A concurrent retirement
    // may become Refused here, but its first reason remains Retired. No retry
    // or waiting loop runs on a native entry callback.
    state_.exchange(State::Refused,std::memory_order_acq_rel);
}
bool Gate::arm_before_receiver_threads() noexcept {
    auto expected=State::Dormant;
    if (state_.compare_exchange_strong(expected,State::Active,std::memory_order_acq_rel)) return true;
    if (expected!=State::Unprepared && expected!=State::Refused && expected!=State::Retired)
        refuse(Reason::LateArm);
    return false;
}
Outcome Gate::before_first_wait(const Entry& e,unsigned char& marker,Prepare prepare,void* context) noexcept {
    if (!wait_return_ || e.x[30]!=wait_return_) return {};
    const auto observed=state_.load(std::memory_order_acquire);
    if (observed==State::Prepared) {
        if (!current(marker)) { refuse(Reason::WrongThread);return refused(e,reason()); }
        if (!geometry(e) || e.sp!=frame_ || e.x[19]!=decoder_) {
            refuse(Reason::Entry);return refused(e,reason());
        }
        return {};
    }
    if (observed==State::Refused || observed==State::Retired || observed==State::Unprepared)
        return refused(e,reason());
    if (observed==State::Dormant) { refuse(Reason::NotArmed);return refused(e,reason()); }
    if (observed==State::Preparing) { refuse(Reason::Preparing);return refused(e,reason()); }
    auto expected=State::Active;
    if (!state_.compare_exchange_strong(expected,State::Preparing,std::memory_order_acq_rel)) {
        refuse(Reason::Preparing);return refused(e,reason());
    }
    const Entry first=e;
    if (!geometry(e)) { refuse(Reason::Entry);return refused(e,reason()); }
    if (marker&kMarkerBit) { refuse(Reason::MarkerAlreadyUsed);return refused(e,reason()); }
    if (!prepare) { refuse(Reason::MissingPrepare);return refused(e,reason()); }
    if (state_.load(std::memory_order_acquire)!=State::Preparing) return refused(e,reason());
    const int saved_errno=errno;
    const auto stack=prepare(context);
    errno=saved_errno;
    if (state_.load(std::memory_order_acquire)!=State::Preparing) return refused(first,reason());
    if (e.sp!=first.sp || e.x!=first.x) {
        refuse(Reason::Entry);return refused(first,reason());
    }
    if (!stack.current() || !contains(stack.region(),first.sp)) {
        refuse(Reason::Stack);return refused(first,reason());
    }
    // No stack byte or decoder pointer is dereferenced. Bounds come solely
    // from the root's independently validated cold current-pthread callback.
    stack_=stack;marker_address_=&marker;frame_=first.sp;decoder_=first.x[19];
    marker=static_cast<unsigned char>(marker|kMarkerBit);
    expected=State::Preparing;
    if (!state_.compare_exchange_strong(expected,State::Prepared,std::memory_order_release))
        return refused(first,reason());
    return {Kind::Prepared,Reason::None,first};
}
bool Gate::current(const unsigned char& marker) const noexcept {
    if (state_.load(std::memory_order_acquire)!=State::Prepared || marker_address_!=&marker ||
        !stack_.current()) return false;
    return (marker&kMarkerBit) && state_.load(std::memory_order_acquire)==State::Prepared;
}
frame_native_client_birth::StackRegion Gate::region_current(const unsigned char& marker) const noexcept {
    if (!current(marker)) return {};
    const auto region=stack_.region();
    return current(marker)?region:frame_native_client_birth::StackRegion{};
}
void Gate::retire() noexcept {
    auto expected=Reason::None;
    reason_.compare_exchange_strong(expected,Reason::Retired,std::memory_order_acq_rel);
    state_.store(State::Retired,std::memory_order_release);
}
} // namespace frame_client_resource_thread
