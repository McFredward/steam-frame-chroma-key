#include "compositor_import_thread.h"

#include <cerrno>

namespace frame_compositor_import_thread {
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
    constexpr std::uintptr_t sites[]{0x153f70,kBeginReturn,0x24fb70,0x24fedc,kDispatchReturn};
    if(p.file_identity!=identity() || p.bias<4096 || p.bias%4096 ||
       p.code_begin<4096 || p.code_begin>=p.code_end || p.code_end>kMax)return false;
    for(const auto site:sites)
        if(p.bias>kMax-site || p.bias+site<p.code_begin || p.bias+site>=p.code_end)return false;
    return true;
}
bool begin_shape(const Entry& e) noexcept {
    return e.sp>=4096 && e.sp%16==0 && e.sp<=kMax-kBeginFrameBytes && e.x[29]==e.sp &&
        e.x[0]>=4096 && e.x[0]<=kMax && e.x[0]%16==0 && e.x[0]==e.x[19];
}
bool contains(Region r,std::uintptr_t at,std::size_t count) noexcept {
    return r.begin<r.end && at>=r.begin && at<=r.end && count<=r.end-at;
}
bool same(const Entry& a,const Entry& b) noexcept {return a.sp==b.sp && a.x==b.x;}
struct ErrnoGuard {int saved=errno;~ErrnoGuard(){errno=saved;}};
}
Gate::Gate(frame_import_startup::PreparedImage p,CurrentMarker marker,void* owner) noexcept
    : compositor_(p),current_marker_(marker),marker_owner_(owner) {
    if(!image(compositor_))return;
    if(!marker || !owner) {reason_.store(Reason::MissingCurrentMarker,std::memory_order_relaxed);return;}
    reason_.store(Reason::None,std::memory_order_relaxed);
    state_.store(State::Dormant,std::memory_order_relaxed);
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
    const ErrnoGuard saved_errno;
    auto expected=State::Dormant;
    if(state_.compare_exchange_strong(expected,State::Armed,std::memory_order_acq_rel))return true;
    if(expected!=State::Unprepared && expected!=State::Refused && expected!=State::Retired)
        refuse(Reason::LateArm);
    return false;
}
bool Gate::marker_owned(const unsigned char& marker) const noexcept {
    // Call the externally authenticated current-owner function before touching
    // a supplied marker. Passing the original owner's address on a foreign
    // thread must never accidentally confer metadata or private-read ownership.
    return current_marker_ && current_marker_(marker_owner_)==&marker &&
        marker_address_==&marker && (marker&kMarkerBit);
}
bool Gate::stack_owned(const unsigned char& marker) const noexcept {
    return marker_owned(marker) && stack_.current();
}
Outcome Gate::before_first_begin(const Entry& e,unsigned char& marker,
                                Prepare prepare,void* prepare_owner) noexcept {
    const ErrnoGuard saved_errno;
    if(compositor_.bias>kMax-kBeginReturn || e.x[30]!=compositor_.bias+kBeginReturn)return {};
    const Entry original=e;
    const auto observed=state_.load(std::memory_order_acquire);
    if(observed==State::Unprepared || observed==State::Refused || observed==State::Retired)
        return {Kind::Refused,reason(),original};
    if(observed==State::Dormant)return failure(original,Reason::NotArmed);
    if(observed==State::Ready)return failure(original,Reason::RepeatedBegin);
    if(observed!=State::Armed)return failure(original,Reason::Preparing);
    auto expected=State::Armed;
    if(!state_.compare_exchange_strong(expected,State::Staging,std::memory_order_acq_rel))
        return failure(original,Reason::Preparing);
    // Claim only the import-specific return, before any preparer or native copy.
    if(!begin_shape(original))return failure(original,Reason::Entry);
    if(current_marker_(marker_owner_)!=&marker)return failure(original,Reason::WrongThread);
    if(marker&kMarkerBit)return failure(original,Reason::MarkerAlreadyUsed);
    if(!same(e,original))return failure(original,Reason::ChangedEntry);
    if(!prepare || !prepare_owner)return failure(original,Reason::MissingPrepare);
    if(state_.load(std::memory_order_acquire)!=State::Staging)return {Kind::Refused,reason(),original};
    begin_entry_=original;marker_address_=&marker;
    marker=static_cast<unsigned char>(marker|kMarkerBit);
    expected=State::Staging;
    if(!state_.compare_exchange_strong(expected,State::Preparing,std::memory_order_release))
        return {Kind::Refused,reason(),original};
    // Cold preparation precedes the original native pipe lock. It cannot run
    // for the first time inside a graphics callback or under that native lock.
    // The root independently authenticates the helper and source candidate.
    const auto stack=prepare(prepare_owner);
    if(state_.load(std::memory_order_acquire)!=State::Preparing)return {Kind::Refused,reason(),original};
    if(!marker_owned(marker) || !same(e,original))return failure(original,Reason::ChangedEntry);
    if(!stack.current() || !contains(stack.region(),original.sp,kBeginFrameBytes))
        return failure(original,Reason::Stack);
    stack_=stack;
    expected=State::Preparing;
    if(!state_.compare_exchange_strong(expected,State::PendingBegin,std::memory_order_release))
        return {Kind::Refused,reason(),original};
    return {Kind::BeginStaged,Reason::None,original};
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
    if(observed==State::Staging || observed==State::Preparing || observed==State::CheckingBegin)
        return failure(original,Reason::Preparing);
    if(observed!=State::PendingBegin)return failure(original,Reason::MissingBefore);
    if(!stack_owned(marker))return failure(original,Reason::WrongThread);
    if(!same(original,begin_entry_) || !begin_shape(original))return failure(original,Reason::ChangedEntry);
    if(result!=0)return failure(original,Reason::NativeFailure);
    if(!reader || !reader_owner)return failure(original,Reason::MissingReader);
    auto expected=State::PendingBegin;
    if(!state_.compare_exchange_strong(expected,State::CheckingBegin,std::memory_order_acq_rel))
        return failure(original,Reason::Preparing);
    if(!stack_owned(marker) || !same(e,original))return failure(original,Reason::ChangedEntry);
    if(state_.load(std::memory_order_acquire)!=State::CheckingBegin)return {Kind::Refused,reason(),original};
    std::array<std::uint64_t,2> parent{};
    requested_.fetch_add(kParentCopyBytes,std::memory_order_relaxed);
    const bool copied=reader(original.sp,parent.data(),kParentCopyBytes,reader_owner);
    const std::size_t confirmed=copied?kParentCopyBytes:0;
    if(copied)confirmed_.fetch_add(kParentCopyBytes,std::memory_order_relaxed);
    if(state_.load(std::memory_order_acquire)!=State::CheckingBegin)
        return {Kind::Refused,reason(),original,kParentCopyBytes,confirmed};
    if(!copied)return failure(original,Reason::Reader,kParentCopyBytes,confirmed);
    if(!stack_owned(marker) || !same(e,original))
        return failure(original,Reason::ChangedEntry,kParentCopyBytes,confirmed);
    if(parent[0]!=original.sp+kBeginFrameBytes || parent[1]!=compositor_.bias+kDispatchReturn)
        return failure(original,Reason::SourceLink,kParentCopyBytes,confirmed);
    expected=State::CheckingBegin;
    if(!state_.compare_exchange_strong(expected,State::Ready,std::memory_order_release))
        return {Kind::Refused,reason(),original,kParentCopyBytes,confirmed};
    return {Kind::BeginAdmitted,Reason::None,original,kParentCopyBytes,confirmed};
}
bool Gate::pending_current(const unsigned char& marker) const noexcept {
    const ErrnoGuard saved_errno;
    return state_.load(std::memory_order_acquire)==State::PendingBegin && stack_owned(marker) &&
        state_.load(std::memory_order_acquire)==State::PendingBegin;
}
bool Gate::current(const unsigned char& marker) const noexcept {
    const ErrnoGuard saved_errno;
    return state_.load(std::memory_order_acquire)==State::Ready && stack_owned(marker) &&
        state_.load(std::memory_order_acquire)==State::Ready;
}
Region Gate::region_current(const unsigned char& marker) const noexcept {
    if(!current(marker))return {};
    const auto r=stack_.region();return current(marker)?r:Region{};
}
bool Gate::window_current(const unsigned char& marker,std::uintptr_t at,std::size_t count) const noexcept {
    const auto r=region_current(marker);return contains(r,at,count) && current(marker);
}
std::uintptr_t Gate::begin_frame_current(const unsigned char& marker) const noexcept {
    return current(marker)?begin_entry_.sp:0;
}
void Gate::cancel() noexcept {refuse(Reason::Cancelled);}
void Gate::retire() noexcept {
    auto expected=Reason::None;
    reason_.compare_exchange_strong(expected,Reason::Retired,std::memory_order_acq_rel);
    state_.store(State::Retired,std::memory_order_release);
}
} // namespace frame_compositor_import_thread
