#include "compositor_resource_thread.h"

#include <cerrno>
#include <cstring>

namespace frame_compositor_resource_thread {
namespace {
constexpr std::uintptr_t kMax = 0x0000ffffffffffffULL;
constexpr std::array<std::uint8_t,32> identity() noexcept {
    constexpr char text[] = "0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96";
    std::array<std::uint8_t,32> out{};
    const auto nibble=[](char c) constexpr { return unsigned(c>='a'?c-'a'+10:c-'0'); };
    for(unsigned i=0;i<32;++i)out[i]=std::uint8_t((nibble(text[2*i])<<4)|nibble(text[2*i+1]));
    return out;
}
bool image(const frame_import_startup::PreparedImage& p) noexcept {
    constexpr std::uintptr_t sites[]{0x153f70,kBeginReturn,0x172348,kPollReturn,
                                    0x1727a4,0x24fc54,0x24fee0};
    if(p.file_identity!=identity() || p.bias<4096 || p.bias%4096 ||
       p.code_begin<4096 || p.code_begin>=p.code_end || p.code_end>kMax)return false;
    for(const auto site:sites)
        if(p.bias>kMax-site || p.bias+site<p.code_begin || p.bias+site>=p.code_end)return false;
    return true;
}
bool frame(const Entry& e,std::size_t size) noexcept {
    return e.sp>=4096 && e.sp%16==0 && e.sp<=kMax-size && e.x[29]==e.sp;
}
bool poll_shape(const Entry& e) noexcept {
    return frame(e,160) && e.x[0]==e.sp+56 && e.x[1]==1 && e.x[2]==200 && e.x[20]==200;
}
bool contains(Region region,std::uintptr_t at,std::size_t count) noexcept {
    return region.begin<region.end && at>=region.begin && at<=region.end && count<=region.end-at;
}
bool same(const Entry& a,const Entry& b) noexcept {return a.sp==b.sp && a.x==b.x;}
struct ErrnoGuard { int saved=errno; ~ErrnoGuard(){errno=saved;} };
}

Gate::Gate(frame_import_startup::PreparedImage compositor) noexcept : compositor_(compositor) {
    if(image(compositor_)) {
        reason_.store(Reason::None,std::memory_order_relaxed);
        state_.store(State::Dormant,std::memory_order_relaxed);
    }
}
void Gate::refuse(Reason why) noexcept {
    auto expected=Reason::None;
    reason_.compare_exchange_strong(expected,why,std::memory_order_acq_rel);
    state_.exchange(State::Refused,std::memory_order_acq_rel);
}
Outcome Gate::failure(const Entry& e,Reason why,std::size_t requested,std::size_t confirmed) noexcept {
    refuse(why);return {Kind::Refused,reason(),e,requested,confirmed};
}
bool Gate::arm_before_compositor_threads() noexcept {
    auto expected=State::Dormant;
    if(state_.compare_exchange_strong(expected,State::Armed,std::memory_order_acq_rel))return true;
    if(expected!=State::Unprepared && expected!=State::Refused && expected!=State::Retired)
        refuse(Reason::LateArm);
    return false;
}
bool Gate::owned(const unsigned char& marker) const noexcept {
    // Check the actual current stack owner before touching its TLS byte, even
    // if another thread supplies the original owner's marker reference.
    return marker_address_==&marker && stack_.current() && (marker&kMarkerBit);
}
Outcome Gate::before_first_poll(const Entry& e,unsigned char& marker,Prepare prepare,void* prepare_owner,
                                Read reader,void* reader_owner) noexcept {
    const ErrnoGuard saved_errno;
    if(compositor_.bias>kMax-kPollReturn || e.x[30]!=compositor_.bias+kPollReturn)return {};
    const Entry original=e;
    const auto observed=state_.load(std::memory_order_acquire);
    if(observed==State::ColdPrepared || observed==State::Ready) {
        if(!owned(marker))return failure(original,Reason::WrongThread);
        if(!poll_shape(e) || e.sp!=poll_frame_)return failure(original,Reason::Entry);
        return {};
    }
    if(observed==State::Unprepared || observed==State::Refused || observed==State::Retired)
        return {Kind::Refused,reason(),original};
    if(observed==State::Dormant)return failure(original,Reason::NotArmed);
    if(observed==State::Preparing || observed==State::CheckingBegin)return failure(original,Reason::Preparing);
    auto expected=State::Armed;
    if(!state_.compare_exchange_strong(expected,State::Preparing,std::memory_order_acq_rel))
        return failure(original,Reason::Preparing);
    // Single process-wide claim precedes preparation and every native byte.
    if(!poll_shape(e))return failure(original,Reason::Entry);
    if(marker&kMarkerBit)return failure(original,Reason::MarkerAlreadyUsed);
    if(!prepare || !prepare_owner)return failure(original,Reason::MissingPrepare);
    if(!reader || !reader_owner)return failure(original,Reason::MissingReader);
    if(state_.load(std::memory_order_acquire)!=State::Preparing)
        return {Kind::Refused,reason(),original};
    const auto stack=prepare(prepare_owner);
    if(state_.load(std::memory_order_acquire)!=State::Preparing)return {Kind::Refused,reason(),original};
    if(!same(e,original))return failure(original,Reason::ChangedEntry);
    if(!stack.current() || !contains(stack.region(),original.sp,160))return failure(original,Reason::Stack);
    std::array<std::uint64_t,4> links{};
    std::size_t requested=0,confirmed=0;
    for(unsigned index=0;index<2;++index) {
        if(state_.load(std::memory_order_acquire)!=State::Preparing || !stack.current() ||
           !same(e,original) || (marker&kMarkerBit))
            return failure(original,Reason::ChangedEntry,requested,confirmed);
        requested+=16;requested_.fetch_add(16,std::memory_order_relaxed);
        const bool copied=reader(original.sp+64*index,links.data()+2*index,16,reader_owner);
        if(copied) {confirmed+=16;confirmed_.fetch_add(16,std::memory_order_relaxed);}
        if(state_.load(std::memory_order_acquire)!=State::Preparing)return {Kind::Refused,reason(),original,requested,confirmed};
        if(!copied)return failure(original,Reason::Reader,requested,confirmed);
        if(!same(e,original) || !stack.current() || (marker&kMarkerBit))
            return failure(original,Reason::ChangedEntry,requested,confirmed);
    }
    if(links[0]!=original.sp+64 || links[1]!=compositor_.bias+0x1727a4 ||
       links[2]!=original.sp+160 || links[3]!=compositor_.bias+0x24fc54)
        return failure(original,Reason::SourceLink,requested,confirmed);
    stack_=stack;marker_address_=&marker;poll_frame_=original.sp;dispatch_frame_=original.sp+160;
    marker=static_cast<unsigned char>(marker|kMarkerBit);
    expected=State::Preparing;
    if(!state_.compare_exchange_strong(expected,State::ColdPrepared,std::memory_order_release))
        return {Kind::Refused,reason(),original,requested,confirmed};
    return {Kind::ColdPrepared,Reason::None,original,requested,confirmed};
}
Outcome Gate::after_first_begin(const Entry& e,int result,const unsigned char& marker,
                                Read reader,void* reader_owner) noexcept {
    const ErrnoGuard saved_errno;
    if(compositor_.bias>kMax-kBeginReturn || e.x[30]!=compositor_.bias+kBeginReturn)return {};
    const Entry original=e;
    const auto observed=state_.load(std::memory_order_acquire);
    if(observed==State::Unprepared || observed==State::Refused || observed==State::Retired)
        return {Kind::Refused,reason(),original};
    if(observed==State::Ready)return failure(original,Reason::RepeatedBegin);
    if(observed==State::Preparing || observed==State::CheckingBegin)return failure(original,Reason::Preparing);
    if(observed!=State::ColdPrepared)return failure(original,Reason::MissingColdPreparation);
    if(!owned(marker))return failure(original,Reason::WrongThread);
    if(result!=0)return failure(original,Reason::NativeFailure);
    if(!frame(e,320) || e.sp+320!=dispatch_frame_)return failure(original,Reason::Entry);
    if(!reader || !reader_owner)return failure(original,Reason::MissingReader);
    if(!contains(stack_.region(),original.sp,320))return failure(original,Reason::Stack);
    auto expected=State::ColdPrepared;
    if(!state_.compare_exchange_strong(expected,State::CheckingBegin,std::memory_order_acq_rel))
        return failure(original,Reason::Preparing);
    if(state_.load(std::memory_order_acquire)!=State::CheckingBegin || !owned(marker) || !same(e,original))
        return failure(original,Reason::ChangedEntry);
    std::array<std::uint64_t,2> link{};
    requested_.fetch_add(16,std::memory_order_relaxed);
    const bool copied=reader(original.sp,link.data(),16,reader_owner);
    const std::size_t confirmed=copied?16:0;
    if(copied)confirmed_.fetch_add(16,std::memory_order_relaxed);
    if(state_.load(std::memory_order_acquire)!=State::CheckingBegin)
        return {Kind::Refused,reason(),original,16,confirmed};
    if(!copied)return failure(original,Reason::Reader,16,confirmed);
    if(!owned(marker) || !same(e,original))return failure(original,Reason::ChangedEntry,16,confirmed);
    if(link[0]!=dispatch_frame_ || link[1]!=compositor_.bias+0x24fee0)
        return failure(original,Reason::SourceLink,16,confirmed);
    begin_frame_=original.sp;
    expected=State::CheckingBegin;
    if(!state_.compare_exchange_strong(expected,State::Ready,std::memory_order_release))
        return {Kind::Refused,reason(),original,16,confirmed};
    return {Kind::BeginAdmitted,Reason::None,original,16,confirmed};
}
bool Gate::current(const unsigned char& marker) const noexcept {
    return state_.load(std::memory_order_acquire)==State::Ready && owned(marker) &&
        state_.load(std::memory_order_acquire)==State::Ready;
}
Region Gate::region_current(const unsigned char& marker) const noexcept {
    if(!current(marker))return {};
    const auto region=stack_.region();return current(marker)?region:Region{};
}
bool Gate::window_current(const unsigned char& marker,std::uintptr_t at,std::size_t count) const noexcept {
    const auto region=region_current(marker);return contains(region,at,count) && current(marker);
}
std::uintptr_t Gate::begin_frame_current(const unsigned char& marker) const noexcept {
    return current(marker)?begin_frame_:0;
}
void Gate::cancel() noexcept {refuse(Reason::Cancelled);}
void Gate::retire() noexcept {
    auto expected=Reason::None;
    reason_.compare_exchange_strong(expected,Reason::Retired,std::memory_order_acq_rel);
    state_.store(State::Retired,std::memory_order_release);
}
} // namespace frame_compositor_resource_thread
