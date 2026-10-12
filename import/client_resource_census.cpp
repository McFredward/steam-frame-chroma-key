#include "client_resource_census.h"

#include <cstring>
#include <limits>

namespace frame_client_resource {
namespace {
using Operation=frame_import_forwarding::Operation;
using Phase=frame_import_forwarding::Phase;
using Entry=frame_import_native::Entry;
constexpr std::uintptr_t kMax=(std::uintptr_t{1}<<48)-1;
constexpr std::uintptr_t kPipe=0x649ff0;
constexpr std::uintptr_t kSites[]{0x3623c0,0x3624c4,0x362d58};
bool address(std::uint64_t value,unsigned alignment=16) noexcept {
    return value>=4096 && value<=kMax && value%alignment==0;
}
bool target_valid(const frame_client_connect::Target& t) noexcept {
    const auto& m=t.module;const auto& o=t.object;
    if(!address(m.load_bias,4096) || m.load_bias>kMax-kPipe ||
       !m.module_lifetime_label || !o.valid() || !o.registry_identity ||
       !o.process_generation || !o.object_generation ||
       o.domain!=frame_import_pipe::Domain::Client045 || o.pipe!=m.load_bias+kPipe ||
       m.code_begin<m.load_bias || m.code_begin>m.load_bias+kSites[0] ||
       m.code_end<=m.load_bias+kSites[2] || m.code_end>kMax)return false;
    for(const auto byte:m.code_identity)if(byte)return true;
    return false;
}
bool native_integer(std::uint64_t bits,int& result) noexcept {
    if(bits>std::numeric_limits<std::uint32_t>::max())return false;
    const auto word=static_cast<std::uint32_t>(bits);
    static_assert(sizeof(word)==sizeof(result),"Native integer ABI changed");
    std::memcpy(&result,&word,sizeof(result));return true;
}
bool same(const Entry& a,const Entry& b) noexcept {return a.sp==b.sp && a.x==b.x;}
} // namespace
Scope Scope::prepare_current(bool owned) noexcept {
    Scope s;s.thread_=frame_client_connect::Scope::prepare_current();
    s.startup_thread_stack_owned_=owned;return s;
}
bool Scope::current() const noexcept {return thread_.current();}
bool Scope::complete() const noexcept {return current() && state_==State::Complete;}
bool Scope::expired() const noexcept {return current() && state_==State::Expired;}
Gate::Gate(frame_client_connect::Target t) noexcept:target_(t),valid_(target_valid(t)){}
void Gate::expire(Scope& s) const noexcept {
    if(!s.current())return;
    s.state_=Scope::State::Expired;s.pending_={};s.import_frame_=0;s.output_=0;
}
Outcome Gate::observe(frame_import_forwarding::Notification n,Scope& s) const noexcept {
    using State=Scope::State;
    Outcome out;out.entry=n.entry;out.phase=n.phase;out.preserved_errno=n.preserved_errno;
    out.startup_thread_stack_owned=s.startup_thread_stack_owned_;
    const auto reject=[&](Reason why) noexcept {
        expire(s);out.kind=Kind::Rejected;out.reason=why;return out;
    };
    if(!s.current())return reject(Reason::Thread);
    if(s.state_==State::Complete || s.state_==State::Expired)return {};
    if(!valid_ || (s.gate_owner_ && s.gate_owner_!=this))return reject(Reason::Target);
    s.gate_owner_=this;
    unsigned site=3;
    for(unsigned i=0;i<3;++i)if(n.entry.x[30]==target_.module.load_bias+kSites[i])site=i;
    if(site==3) {
        if(s.state_==State::BeginPending || s.state_==State::PendingPending ||
           s.state_==State::ReferencePending)return reject(Reason::Invocation);
        return {};
    }
    out.boundary=static_cast<Boundary>(site);
    const auto expected=site==1?Operation::Unlock:Operation::Lock;
    if(n.operation!=expected || (n.phase!=Phase::Before && n.phase!=Phase::After))return reject(Reason::Order);
    const auto& e=n.entry;
    if(!address(e.sp) || e.x[29]!=e.sp || e.x[0]!=target_.object.pipe)return reject(Reason::Entry);
    if(n.phase==Phase::Before) {
        if(site==0) {
            if(s.state_!=State::Idle)return reject(Reason::Order);
            if(e.x[21]!=target_.object.pipe || !address(e.x[20],8) ||
               e.sp>kMax-352)return reject(Reason::Arguments);
            s.import_frame_=e.sp;s.output_=e.x[20];s.state_=State::BeginPending;
        } else if(site==1) {
            if(s.state_!=State::AwaitPending)return reject(Reason::Order);
            if(e.sp!=s.import_frame_ || e.x[20]!=s.output_ || e.x[24]>1)return reject(Reason::Invocation);
            // x21 is not stable at Pending; native deferred logging may alter it.
            s.state_=State::PendingPending;
        } else {
            if(s.state_!=State::AwaitReference)return reject(Reason::Order);
            if(e.sp!=s.import_frame_+112 || e.x[19]!=target_.object.pipe ||
               e.x[20]!=target_.module.load_bias+0x649f78 || !e.x[24])return reject(Reason::Invocation);
            s.state_=State::ReferencePending;
        }
        s.pending_=e;out.kind=Kind::Boundary;return out;
    }
    const State pending[]{State::BeginPending,State::PendingPending,State::ReferencePending};
    if(s.state_!=pending[site] || !same(e,s.pending_))return reject(Reason::Invocation);
    int result=0;
    if(!native_integer(n.result_bits,result))return reject(Reason::NativeResult);
    out.native_result=result;out.native_result_available=true;
    if(result!=0)return reject(Reason::NativeResult);
    s.pending_={};
    s.state_=site==0?State::AwaitPending:site==1?State::AwaitReference:State::Complete;
    out.kind=Kind::Boundary;return out;
}
} // namespace frame_client_resource
