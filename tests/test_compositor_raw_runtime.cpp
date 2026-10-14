// Internal adapter integration fixtures include the production implementation
// to exercise its actual callbacks without exposing a deployment test bypass.
// They do NOT manufacture Ready Prepared or pretend to authenticate a native
// loaded executable. Production bootstrap refusal is tested separately below.
#ifndef FRAME_COMPOSITOR_RUNTIME_SOURCE
#define FRAME_COMPOSITOR_RUNTIME_SOURCE "../receiver/compositor_raw_runtime.cpp"
#endif
#include FRAME_COMPOSITOR_RUNTIME_SOURCE

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern "C" {
__attribute__((visibility("hidden"))) std::atomic<unsigned> frame_compositor_raw_ready{0};
}
namespace fixture_stubs {
std::atomic<bool> libc_loss{false},vulkan_loss{false},journal_loss{false};
std::atomic<unsigned> attributes_calls{0},journal_requests{0};
frame_import_forwarding::Callbacks libc_callbacks{};
frame_import_vulkan::Callbacks vulkan_callbacks{};
frame_compositor_journal::Callbacks journal_callbacks{};
bool registration=true,request_result=true,fail_attributes=false;
std::uint64_t requested_device=0,requested_tag=0,requested_cookie=0;
VkImage requested_image=VK_NULL_HANDLE;
VkImageView requested_view=VK_NULL_HANDLE;
}
// Explicit fixed stubs model publication registration/loss and receipt request
// transport only. No owning Journal generation, provider seal or GPU proof is
// inferred from their success. Separate production Journal tests cover replay.
namespace frame_import_forwarding {
bool register_callbacks_before_threads(Callbacks c,InitializationGate g) noexcept {
    if(!fixture_stubs::registration || !c.notify || g!=InitializationGate::OwnedPreparedContext)return false;
    fixture_stubs::libc_callbacks=c;return true;
}
LossStatus notification_loss() noexcept {return {fixture_stubs::libc_loss.load(),0,false};}
}
namespace frame_import_vulkan {
bool register_callbacks_before_threads(Callbacks c,bool owned) noexcept {
    if(!fixture_stubs::registration || !c.notify || !owned)return false;
    fixture_stubs::vulkan_callbacks=c;return true;
}
bool notification_loss() noexcept {return fixture_stubs::vulkan_loss.load();}
}
namespace frame_compositor_journal {
bool register_receipts_before_threads(Callbacks c) noexcept {
    if(!fixture_stubs::registration || !c.notify)return false;
    fixture_stubs::journal_callbacks=c;return true;
}
bool request(std::uint64_t device,VkImage image,VkImageView view,std::uint64_t tag,std::uint64_t cookie) noexcept {
    ++fixture_stubs::journal_requests;fixture_stubs::requested_device=device;
    fixture_stubs::requested_image=image;fixture_stubs::requested_view=view;
    fixture_stubs::requested_tag=tag;fixture_stubs::requested_cookie=cookie;
    return fixture_stubs::request_result;
}
bool notification_loss() noexcept {return fixture_stubs::journal_loss.load();}
}
namespace {
namespace rt=frame_compositor_runtime;
namespace raw=frame_compositor_raw;
namespace gate=frame_compositor_import_thread;
namespace audit=frame_compositor_audit;
using Boundary=raw::Boundary;
unsigned checks=0;
void check_at(bool v,unsigned line) {
    ++checks;if(!v){std::fprintf(stderr,"runtime fixture check %u failed at line %u\n",checks,line);std::abort();}
}
#define check(v) check_at((v),__LINE__)
constexpr std::uintptr_t bias=0x500000,libc_bias=0x900000,self_bias=0xb00000;
constexpr std::uint64_t device=71,native_device=0xabc0,token=0x8877665544332211ULL;
constexpr std::uint32_t sequence=0xffffffff;
const pthread_t fixture_cold_main=::pthread_self();
std::atomic<std::size_t> cancel_after_scalar{0},cancel_after_parent{0};
std::atomic<bool> parent_paused{false},parent_resume{false};
thread_local bool pause_parent=false;
unsigned char* marker_current(void*) noexcept {
    if(pause_parent && rt::runtime.parent_bytes.load()==16) {
        parent_paused.store(true,std::memory_order_release);
        while(!parent_resume.load(std::memory_order_acquire))std::this_thread::yield();
    }
    if((cancel_after_scalar.load() && rt::runtime.scalar_bytes.load()>=cancel_after_scalar.load()) ||
       (cancel_after_parent.load() && rt::runtime.parent_bytes.load()>=cancel_after_parent.load()))rt::retire(rt::Refusal::Thread);
    return &rt::marker;
}
int actual_attributes(pthread_t t,pthread_attr_t* a) {
    ++fixture_stubs::attributes_calls;
    return fixture_stubs::fail_attributes?EINVAL: ::pthread_getattr_np(t,a);
}
frame_import_startup::PreparedImage image() {
    frame_import_startup::PreparedImage p{bias,bias+0x10000,bias+0x400000,{}};
    constexpr char text[]="0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96";
    const auto nibble=[](char c){return unsigned(c>='a'?c-'a'+10:c-'0');};
    for(unsigned i=0;i<32;++i)p.file_identity[i]=std::uint8_t((nibble(text[i*2])<<4)|nibble(text[i*2+1]));
    return p;
}
audit::ModuleView module(std::uintptr_t b,std::uint64_t generation) {return {b,b+0x10000,b+(b==bias?0x400000:0x300000),generation,0,0};}
void model_cold_adapter_only(bool operator_arm=false,bool stream_image=false) {
    auto& r=rt::runtime;
    // Direct internal fixture state models already authenticated immutable
    // geometry. It cannot construct/seal Ready production Prepared and is not
    // compiled into the observer or a runtime interface.
    r.images[0]={bias,bias+0x10000,bias+0x400000,frame_import_main::Role::Compositor,{},image().file_identity,{}};
    r.images[1]={libc_bias,libc_bias+0x10000,libc_bias+0x300000,frame_import_main::Role::Libc,{},{},{}};
    r.images[3]={self_bias,self_bias+0x10000,self_bias+0x300000,frame_import_main::Role::Observer,{},{},{}};
    r.setup.lock_entry=self_bias+0x10100;r.setup.unlock_entry=self_bias+0x10200;r.setup.write_entry=self_bias+0x10300;
    r.setup.self=::pthread_self;r.setup.main_thread=fixture_cold_main;r.setup.attributes=actual_attributes;r.setup.get_stack=::pthread_attr_getstack;
    r.setup.destroy_attributes=::pthread_attr_destroy;r.setup.self_data_begin=self_bias+0x400000;r.setup.self_data_end=self_bias+0x500000;
    r.setup.require_operator_arm=operator_arm;r.arm_required.store(operator_arm);
    r.setup.require_stream_image=stream_image;
    if(!operator_arm)r.candidate.store(rt::Candidate::Armed);
    r.originals={libc_bias+0x87370,libc_bias+0x88f74,libc_bias+0xdfee0};
    r.gate=new(r.thread_storage.data())gate::Gate(image(),marker_current,&r);
    r.refusal.store(rt::Refusal::None);r.prepared.store(true);r.state.store(2);
}
audit::BoundProvider provider(audit::Function f=audit::Function::None) {
    audit::BoundProvider b{module(bias,1),module(self_bias,2),module(libc_bias,3),f,0};
    b.observer.data_begin=rt::runtime.setup.self_data_begin;b.observer.data_end=rt::runtime.setup.self_data_end;
    b.original=f==audit::Function::MutexLock?rt::runtime.originals[0]:f==audit::Function::MutexUnlock?rt::runtime.originals[1]:
        f==audit::Function::Write?rt::runtime.originals[2]:0;return b;
}
void attach(bool bindings=true,bool operator_arm=false,bool stream_image=false) {
    model_cold_adapter_only(operator_arm,stream_image);check(rt::bridge({audit::Stage::Prepare,provider(),false,{}}).ready);
    if(bindings)for(const auto f:{audit::Function::MutexLock,audit::Function::MutexUnlock,audit::Function::Write})
        check(rt::bridge({audit::Stage::Bind,provider(f),false,{}}).ready);
}
struct Fixture {
    alignas(16)std::array<std::uint8_t,1856> bytes{};
    std::uintptr_t base=reinterpret_cast<std::uintptr_t>(bytes.data()),dispatch=base+1232,pipe=0x700000;
    frame_import_native::CreationRoute selected=frame_import_native::CreationRoute::Vulkan;
    VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    std::array<std::uint8_t,21> packet{};
    explicit Fixture(frame_import_native::CreationRoute route=frame_import_native::CreationRoute::Vulkan):
        dispatch(base+frame_import_native::creation_dispatch_offset(route)),selected(route) {
        put(dispatch+108,sequence);link(dispatch,dispatch+320,0x24fee0);
        for(unsigned b=0;b<4;++b){packet[b]=0x235U>>(b*8);packet[b+4]=sequence>>(b*8);packet[b+8]=9U>>(b*8);}
        packet[12]=9;for(unsigned b=0;b<8;++b)packet[b+13]=token>>(b*8);
    }
    template<class T>void put(std::uintptr_t at,T value) {
        check(at>=base && at-base<=bytes.size()-sizeof(T));std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));
    }
    void link(std::uintptr_t at,std::uintptr_t parent,std::uintptr_t rva){put(at,std::uint64_t{parent});put(at+8,std::uint64_t{bias+rva});}
    raw::Entry entry(Boundary b,unsigned offset=0) {
        raw::Entry e{};constexpr std::uintptr_t sites[]{0x153fc0,0x1540b0,0x2c75ac,0x2c7684,0x2c7804,0x172ea0,0x172c00,0x172fbc};
        e.x[30]=bias+sites[static_cast<unsigned>(b)];
        if(b==Boundary::Begin || b==Boundary::EarlyUnlock) {
            e.sp=e.x[29]=dispatch;e.x[0]=e.x[19]=pipe;e.x[25]=1;put(dispatch+128,std::uint64_t{pipe});
        }else if(b==Boundary::Image || b==Boundary::Bind || b==Boundary::View) {
            e.sp=base;e.x[0]=native_device;
            if(selected==frame_import_native::CreationRoute::Facet) {
                constexpr std::uintptr_t pcs[]{0x3076a0,0x307874,0x30735c};
                e.x[30]=bias+pcs[static_cast<unsigned>(b)-2];e.x[29]=base;
                link(base,base+688,0x2ab100);link(base+688,base+1056,0x70bc8);
                link(base+1056,base+1216,0x13bbdc);link(base+1216,base+1312,0x13beb0);
                link(base+1312,dispatch,0x154194);
                put(base+744,std::uint32_t{0});put(base+1328,std::uint64_t{pipe});
                put(base+56,std::uint32_t{37});
            } else {
                e.x[29]=base+96;
                link(base+96,base+752,0x70bc8);link(base+752,base+912,0x13bbdc);
                link(base+912,base+1008,0x13beb0);link(base+1008,dispatch,0x154194);
                put(base+152,std::uint32_t{0});put(base+1024,std::uint64_t{pipe});
            }
            if(b==Boundary::Bind){e.x[1]=10;e.x[2]=20;e.x[3]=16;}
        }else {
            const auto send=dispatch-80;e.sp=e.x[29]=send;
            link(send,dispatch,0x1542b0);put(send+64,std::uint64_t{pipe});
            std::memcpy(reinterpret_cast<void*>(send-32),packet.data(),packet.size());
            if(b==Boundary::SendLock){e.x[0]=e.x[19]=pipe;e.x[20]=9;e.x[21]=0x235;e.x[22]=sequence;}
            else if(b==Boundary::Write){e.sp=e.x[29]=send-112;link(send-112,send,0x172f34);
                e.x[0]=7;e.x[19]=offset;e.x[20]=21;e.x[22]=pipe;e.x[23]=send-32;e.x[1]=send-32+offset;e.x[2]=21-offset;}
            else {e.sp=send-32;e.x[0]=pipe;e.x[20]=1;}
        }return e;
    }
    frame_import_forwarding::Notification note(Boundary b,raw::Phase phase,unsigned offset=0,std::int64_t result=0) {
        using O=frame_import_forwarding::Operation;
        const auto op=b==Boundary::Begin || b==Boundary::SendLock?O::Lock:b==Boundary::Write?O::Write:O::Unlock;
        return {op,phase,entry(b,offset),static_cast<std::uint64_t>(result),EBUSY};
    }
    void libc(Boundary b,raw::Phase phase,unsigned offset=0,std::int64_t result=0) {
        rt::libc_notification(note(b,phase,offset,result),&rt::runtime);
    }
    void libc_pair(Boundary b,unsigned offset=0,std::int64_t result=0) {
        auto n=note(b,raw::Phase::Before,offset);rt::libc_notification(n,&rt::runtime);
        n.phase=raw::Phase::After;n.result_bits=static_cast<std::uint64_t>(result);rt::libc_notification(n,&rt::runtime);
    }
    frame_import_vulkan::Notification vnote(Boundary b,frame_import_vulkan::Phase phase) {
        using O=frame_import_vulkan::Operation;
        frame_import_vulkan::Notification n{};n.operation=b==Boundary::Image?O::Image:b==Boundary::Bind?O::Bind:O::View;
        n.phase=phase;n.entry=entry(b);n.device=device;n.image=10;n.memory=20;n.view=30;n.memory_offset=16;
        n.format=format;n.extent={1152,4608,1};n.layers=1;n.result=VK_SUCCESS;
        n.invocation_tag=rt::kTag;n.preserved_errno=EBUSY;return n;
    }
    void vulkan_pair(Boundary b) {
        auto n=vnote(b,frame_import_vulkan::Phase::Before);
        check(rt::vulkan_notification(n,&rt::runtime)==rt::kTag);
        n.phase=frame_import_vulkan::Phase::After;check(rt::vulkan_notification(n,&rt::runtime)==rt::kTag);
    }
    void through(Boundary last) {
        libc_pair(Boundary::Begin);if(last==Boundary::Begin)return;
        libc_pair(Boundary::EarlyUnlock);if(last==Boundary::EarlyUnlock)return;
        for(auto b:{Boundary::Image,Boundary::Bind,Boundary::View}){vulkan_pair(b);if(last==b)return;}
        libc_pair(Boundary::SendLock);
    }
};
void no_proof(const raw::RawRecord& r) {
    check(r.raw_only && !r.request_header_observed && !r.continuous_connection_owned && !r.immutable_lifetime_lease);
    check(!r.native_cross_endpoint_associated && !r.native_namespace_associated && !r.source_frame_associated &&
        !r.uv_owned && !r.accepted_submit && !r.gpu_consumed);
}
unsigned drain(rt::Kind desired,rt::Record* result=nullptr) {
    rt::Record r;unsigned found=0;std::uint64_t ordinal=0;
    while(rt::pop(r)){check(r.ordinal==ordinal++);if(r.kind==desired){++found;if(result)*result=r;}}
    check(!rt::pop(r));return found;
}
void success(unsigned chunk,frame_import_native::CreationRoute route=frame_import_native::CreationRoute::Vulkan,bool operator_arm=false) {
    attach(true,operator_arm);
    if(operator_arm){check(rt::arm_candidate_once());check(!rt::arm_candidate_once());}
    Fixture f(route);f.through(Boundary::SendLock);
    unsigned writes=0;for(unsigned at=0;at<21;){const auto n=std::min(chunk,21-at);f.libc_pair(Boundary::Write,at,n);at+=n;++writes;}
    f.libc_pair(Boundary::Finish);const auto s=rt::status();
    const auto fixed=route==frame_import_native::CreationRoute::Facet?368U:320U;
    check(s.attached && !s.retired && s.started && s.terminal && s.parent_bytes==16 && s.scalar_bytes==fixed+44*writes && s.wire_bytes==21);
    check(s.candidate_arm_required==operator_arm && s.candidate_armed && s.candidate_claimed && !rt::arm_candidate_once());
    check(s.parent_bytes+s.scalar_bytes+s.wire_bytes<=(route==frame_import_native::CreationRoute::Facet?757U:709U) && fixture_stubs::attributes_calls==1);
    check(fixture_stubs::journal_requests==1 && fixture_stubs::requested_tag==rt::kTag && fixture_stubs::requested_cookie==rt::kCookie);
    check(fixture_stubs::requested_device==device && fixture_stubs::requested_image==(VkImage)(std::uintptr_t)10 && fixture_stubs::requested_view==(VkImageView)(std::uintptr_t)30);
    rt::Record r;check(drain(rt::Kind::RawMatched,&r)==1);no_proof(r.raw);
    check(r.raw.creation_route==route);
    const auto finish=f.entry(Boundary::Finish);
    check(r.entry.sp==finish.sp && r.entry.x==finish.x);
    check(r.raw.last_boundary==Boundary::Finish && r.raw.last_phase==raw::Phase::After &&
        r.raw.last_native_result_available && r.raw.last_native_result==0);
    check(r.raw.reply_token==token && r.raw.reply==f.packet && r.raw.writes==writes && r.raw.image.device==device && r.raw.image.native_device==native_device);
    const auto before=rt::status();f.libc_pair(Boundary::Begin);check(rt::status().queued==before.queued);
    rt::finalize();check(rt::status().retired && rt::quiescent() && !frame_compositor_raw_ready.load());
    check(!rt::arm_candidate_once() && rt::status().candidate_claimed);
}
void prearm_no_private_work() {
    attach(true,true);Fixture f;
    const auto before=f.note(Boundary::Begin,raw::Phase::Before),after=f.note(Boundary::Begin,raw::Phase::After);
    const auto queued=rt::status().queued;
    std::array<std::thread,8> workers;
    for(auto& worker:workers)worker=std::thread([&]{
        for(unsigned n=0;n<128;++n) {
            rt::libc_notification(before,&rt::runtime);
            rt::libc_notification(after,&rt::runtime);
        }
    });
    for(auto& worker:workers)worker.join();
    for(auto b:{Boundary::EarlyUnlock,Boundary::SendLock,Boundary::Write,Boundary::Finish})f.libc_pair(b);
    for(auto b:{Boundary::Image,Boundary::Bind,Boundary::View}) {
        auto note=f.vnote(b,frame_import_vulkan::Phase::Before);
        check(rt::vulkan_notification(note,&rt::runtime)==0);
        note.phase=frame_import_vulkan::Phase::After;check(rt::vulkan_notification(note,&rt::runtime)==0);
    }
    const auto s=rt::status();
    check(s.prepared && s.attached && !s.retired && s.candidate_arm_required && !s.candidate_armed && !s.candidate_claimed);
    check(!s.started && !s.terminal && !s.parent_bytes && !s.scalar_bytes && !s.wire_bytes && s.queued==queued);
    check(!fixture_stubs::attributes_calls && !fixture_stubs::journal_requests && rt::quiescent());
    check(!(rt::marker&gate::kMarkerBit) && !rt::runtime.candidate_stack.current());
    errno=ERANGE;check(rt::arm_candidate_once() && errno==ERANGE);
    check(!rt::arm_candidate_once() && rt::status().candidate_armed && !rt::status().candidate_claimed);
    // An unmatched post-arm After cannot claim or prepare a candidate.
    rt::libc_notification(after,&rt::runtime);
    check(!rt::status().retired && !rt::status().candidate_claimed && !fixture_stubs::attributes_calls);
    f.through(Boundary::SendLock);f.libc_pair(Boundary::Write,0,21);f.libc_pair(Boundary::Finish);
    check(rt::status().terminal && !rt::status().retired && rt::status().candidate_claimed);
    check(fixture_stubs::attributes_calls==1 && rt::status().parent_bytes==16 && rt::status().scalar_bytes==364 && rt::status().wire_bytes==21);
    check(drain(rt::Kind::RawMatched)==1 && !rt::arm_candidate_once());
}
void prearm_pair_crosses_signal() {
    attach(true,true);Fixture f;
    f.libc(Boundary::Begin,raw::Phase::Before);
    f.libc(Boundary::Begin,raw::Phase::Before);
    check(rt::skipped_begins==2 && !rt::status().started && !fixture_stubs::attributes_calls);
    check(rt::arm_candidate_once());
    f.libc(Boundary::Begin,raw::Phase::After);f.libc(Boundary::Begin,raw::Phase::After);
    check(!rt::skipped_begins && !rt::status().candidate_claimed && !rt::status().retired && !rt::status().parent_bytes);
    check(rt::status().begins_before_arm==2 && !rt::status().begins_after_arm && rt::status().skipped_prearm_afters==2);
    f.through(Boundary::SendLock);f.libc_pair(Boundary::Write,0,21);f.libc_pair(Boundary::Finish);
    check(rt::status().begins_after_arm==1 && rt::status().begins_before_arm==2 && !rt::status().begin_counts_overflow);
    check(rt::status().terminal && !rt::status().retired && fixture_stubs::attributes_calls==1);
    check(drain(rt::Kind::RawMatched)==1);
}
void foreign_prearm_after_claim() {
    attach(true,true);Fixture f;
    const auto before=f.note(Boundary::Begin,raw::Phase::Before),after=f.note(Boundary::Begin,raw::Phase::After);
    std::atomic<bool> pending{false},release{false};
    std::thread old([&]{
        rt::libc_notification(before,&rt::runtime);pending.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))std::this_thread::yield();
        rt::libc_notification(after,&rt::runtime);
    });
    while(!pending.load(std::memory_order_acquire))std::this_thread::yield();
    check(rt::arm_candidate_once());f.libc(Boundary::Begin,raw::Phase::Before);
    check(rt::status().candidate_claimed && rt::status().started && !rt::status().parent_bytes);
    release.store(true,std::memory_order_release);old.join();
    check(!rt::status().retired && !rt::status().parent_bytes && fixture_stubs::attributes_calls==1);
    f.libc(Boundary::Begin,raw::Phase::After);
    check(!rt::status().retired && rt::status().parent_bytes==16 && rt::status().scalar_bytes==12);
    rt::finalize();check(!rt::arm_candidate_once());
}
void arm_refusals(unsigned which) {
    if(which==0){check(!rt::arm_candidate_once() && !rt::status().candidate_armed);return;}
    model_cold_adapter_only(true);
    check(!rt::arm_candidate_once() && !rt::status().candidate_armed);
    if(which==1) {
        const auto b=provider(audit::Function::MutexLock);
        const auto response=rt::bridge({audit::Stage::ForwardOnly,b,false,{}});
        check(response.ready && response.hidden_entry==rt::runtime.setup.lock_entry);
        check(!rt::arm_candidate_once() && !rt::status().candidate_armed);return;
    }
    check(rt::bridge({audit::Stage::Prepare,provider(),false,{}}).ready);
    if(which==2)rt::finalize();
    else if(which==3)rt::runtime.parent_bytes.store(1);
    else if(which==4)rt::runtime.scalar_bytes.store(1);
    else if(which==5)rt::runtime.wire_bytes.store(1);
    else if(which==6)rt::runtime.started.store(true);
    else if(which==7)rt::runtime.terminal.store(true);
    else fixture_stubs::libc_loss.store(true);
    const auto before=rt::status();
    check(!rt::arm_candidate_once());const auto after=rt::status();
    check(!after.candidate_armed && !after.candidate_claimed && before.parent_bytes==after.parent_bytes &&
        before.scalar_bytes==after.scalar_bytes && before.wire_bytes==after.wire_bytes && before.retired==after.retired);
}
void armed_nested_refusal(bool repeated) {
    attach(true,true);Fixture f;check(rt::arm_candidate_once());
    f.libc(Boundary::Begin,raw::Phase::Before);
    if(repeated)f.libc(Boundary::Begin,raw::Phase::After);
    const auto before=rt::status();check(before.candidate_claimed && !rt::arm_candidate_once());
    f.libc(Boundary::Begin,raw::Phase::Before);const auto after=rt::status();
    check(after.retired && after.refusal==rt::Refusal::Thread && after.candidate_armed && after.candidate_claimed && !rt::arm_candidate_once());
    check(before.parent_bytes==after.parent_bytes && before.scalar_bytes==after.scalar_bytes && before.wire_bytes==after.wire_bytes && fixture_stubs::attributes_calls==1);
    check(drain(rt::Kind::Refused)==1);
}
void concurrent_operator_arm() {
    attach(true,true);std::array<std::thread,8> workers;
    std::atomic<bool> ready{false};std::atomic<unsigned> arms{0};
    for(auto& worker:workers)worker=std::thread([&]{
        while(!ready.load(std::memory_order_acquire))std::this_thread::yield();
        if(rt::arm_candidate_once())arms.fetch_add(1,std::memory_order_relaxed);
    });
    ready.store(true,std::memory_order_release);for(auto& worker:workers)worker.join();
    check(arms.load()==1 && rt::status().candidate_armed && !rt::status().candidate_claimed && !rt::status().started);
    check(!rt::status().parent_bytes && !rt::status().scalar_bytes && !rt::status().wire_bytes && !fixture_stubs::attributes_calls);
    check(!rt::arm_candidate_once());rt::finalize();check(!rt::arm_candidate_once());
}
void unqualified_begin_keeps_claim() {
    attach(true,true);Fixture f;check(rt::arm_candidate_once());
    auto note=f.note(Boundary::Begin,raw::Phase::Before);note.entry.x[30]+=4;
    rt::libc_notification(note,&rt::runtime);note.phase=raw::Phase::After;rt::libc_notification(note,&rt::runtime);
    check(!rt::status().candidate_claimed && !rt::status().started && !fixture_stubs::attributes_calls);
    check(!rt::status().parent_bytes && !rt::status().scalar_bytes && !rt::status().wire_bytes);
    check(!rt::status().begins_before_arm && !rt::status().begins_after_arm && !rt::status().skipped_prearm_afters);
    // Audit forwarding remains available while the capture is operator-armed.
    for(const auto fn:{audit::Function::MutexLock,audit::Function::MutexUnlock,audit::Function::Write}) {
        const auto response=rt::bridge({audit::Stage::ForwardOnly,provider(fn),false,{}});
        check(response.ready && response.hidden_entry==(fn==audit::Function::MutexLock?rt::runtime.setup.lock_entry:
            fn==audit::Function::MutexUnlock?rt::runtime.setup.unlock_entry:rt::runtime.setup.write_entry));
    }
    f.libc_pair(Boundary::Begin);check(rt::status().candidate_claimed && !rt::status().retired && fixture_stubs::attributes_calls==1);
    check(rt::status().parent_bytes==16 && rt::status().scalar_bytes==12);rt::finalize();
}
void mixed_renderer() {
    attach();Fixture f(frame_import_native::CreationRoute::Facet);f.through(Boundary::Image);
    auto n=f.vnote(Boundary::Bind,frame_import_vulkan::Phase::Before);
    n.entry.sp=f.dispatch-1232;n.entry.x[29]=n.entry.sp+96;n.entry.x[30]=bias+0x2c7684;
    const auto before=rt::status();check(rt::vulkan_notification(n,&rt::runtime)==0);
    const auto after=rt::status();check(after.retired && after.scalar_bytes==before.scalar_bytes &&
        before.scalar_bytes==120 && !fixture_stubs::journal_requests);
    rt::Record r;check(drain(rt::Kind::RawRefused,&r)==1 && r.raw.reason==raw::InvalidEntry);
}
void inert_production() {
    frame_compositor_main::Profiles p{};errno=ERANGE;const auto prepared=frame_compositor_main::prepare(p);
    check(!prepared.ready() && prepared.refusal()==frame_import_main::Refusal::InvalidProfile && errno==ERANGE);
    check(!rt::initialize(prepared,{}));check(!rt::status().prepared && rt::status().retired && rt::status().refusal==rt::Refusal::Preparation);
    check(!frame_compositor_raw_ready.load() && !fixture_stubs::libc_callbacks.notify && !fixture_stubs::vulkan_callbacks.notify && !fixture_stubs::journal_callbacks.notify);
    check(!rt::bridge({audit::Stage::Prepare,{},false,{}}).ready);
    Fixture f;f.libc_pair(Boundary::Begin);check(!rt::status().parent_bytes && !rt::status().scalar_bytes && !rt::status().queued);
    check(!rt::initialize(prepared,{}));
}
void forwarding_only() {
    model_cold_adapter_only();Fixture f;
    for(const auto fn:{audit::Function::MutexLock,audit::Function::MutexUnlock,audit::Function::Write}) {
        const auto b=provider(fn);const auto response=rt::bridge({audit::Stage::ForwardOnly,b,false,{}});
        check(response.ready && response.hidden_entry==(fn==audit::Function::MutexLock?rt::runtime.setup.lock_entry:fn==audit::Function::MutexUnlock?rt::runtime.setup.unlock_entry:rt::runtime.setup.write_entry));
    }
    f.libc_pair(Boundary::Begin);check(!fixture_stubs::attributes_calls && !rt::status().started && !rt::status().parent_bytes && !rt::status().scalar_bytes);
    rt::Record r;check(drain(rt::Kind::Bound,&r)==3 && r.forward_only);
}
void native_failure() {
    attach();Fixture f;f.libc_pair(Boundary::Begin,0,EINVAL);
    check(rt::status().retired && rt::status().refusal==rt::Refusal::Thread && !rt::status().parent_bytes && !rt::status().scalar_bytes);
    rt::Record r;check(drain(rt::Kind::Refused,&r)==1 && r.thread_reason==static_cast<unsigned>(gate::Reason::NativeFailure));
}
void incomplete_finalization() {
    attach();Fixture f;f.libc(Boundary::Begin,raw::Phase::Before);check(rt::status().started && !rt::status().terminal);
    rt::finalize();check(rt::status().retired && !rt::status().terminal && rt::quiescent());
    f.libc(Boundary::Begin,raw::Phase::After);check(!rt::status().parent_bytes && !rt::status().scalar_bytes);
    check(drain(rt::Kind::RawMatched)==0);
}
void parent_mismatch() {
    attach();Fixture f;f.put(f.dispatch+8,std::uint64_t{bias+0x24fee4});f.libc_pair(Boundary::Begin);
    check(rt::status().retired && rt::status().parent_bytes==16 && !rt::status().scalar_bytes);
    rt::Record r;check(drain(rt::Kind::Refused,&r)==1 && r.thread_reason==static_cast<unsigned>(gate::Reason::SourceLink));
}
void ownership_postcheck(unsigned which) {
    attach();Fixture f;
    if(which==0)cancel_after_parent=16;else cancel_after_scalar=4;
    f.libc_pair(Boundary::Begin);const auto s=rt::status();check(s.retired && s.parent_bytes==16 && s.scalar_bytes==(which==0?0U:4U));
    rt::Record r;if(which)check(drain(rt::Kind::RawRefused,&r)==1 && r.raw.scalar_bytes_copied==4);else check(drain(rt::Kind::Refused,&r)==1);
}
void queue_budget() {
    model_cold_adapter_only();for(unsigned i=0;i<rt::kRecordLimit;++i)check(rt::enqueue(rt::message(rt::Kind::Receipt)));
    check(!rt::enqueue(rt::message(rt::Kind::Receipt)) && rt::status().retired && rt::status().refusal==rt::Refusal::Queue);
    check(drain(rt::Kind::Receipt)==rt::kRecordLimit);
}
void foreign_begin() {
    attach();Fixture f;f.libc(Boundary::Begin,raw::Phase::Before);
    std::thread other([&]{f.libc(Boundary::Begin,raw::Phase::After);});other.join();
    check(rt::status().retired && !rt::status().parent_bytes && !rt::status().scalar_bytes && !rt::runtime.parent_read);
}
void callback_losses(unsigned which) {
    attach();Fixture f;
    if(which==0)fixture_stubs::libc_loss=true;else if(which==1)fixture_stubs::vulkan_loss=true;else fixture_stubs::journal_loss=true;
    f.libc_pair(Boundary::Begin);check(!rt::status().parent_bytes && !rt::status().scalar_bytes && !fixture_stubs::attributes_calls);
    check(drain(rt::Kind::RawMatched)==0);rt::finalize();check(rt::quiescent());
}
void mutated_binding(unsigned which) {
    attach();auto p=provider(audit::Function::Write);
    if(which==0)p.observer.data_begin+=16;else if(which==1)++p.libc.observed_open_generation;else ++p.original;
    check(!rt::bridge({audit::Stage::Bind,p,false,{}}).ready && rt::status().retired && rt::status().refusal==rt::Refusal::Audit);
}
void missing_binding() {
    attach(false);Fixture f;f.libc_pair(Boundary::Begin);
    check(rt::status().retired && rt::status().refusal==rt::Refusal::Audit && !fixture_stubs::attributes_calls && !rt::status().parent_bytes);
}
void request_failure() {
    attach();Fixture f;fixture_stubs::request_result=false;
    f.through(Boundary::Bind);auto n=f.vnote(Boundary::View,frame_import_vulkan::Phase::Before);
    check(rt::vulkan_notification(n,&rt::runtime)==rt::kTag);n.phase=frame_import_vulkan::Phase::After;
    const auto returned=rt::vulkan_notification(n,&rt::runtime);
    check(rt::status().retired && rt::status().refusal==rt::Refusal::Journal && fixture_stubs::journal_requests==1);
    check(returned==0);
}
void invalid_vulkan(unsigned which) {
    attach();Fixture f;f.through(Boundary::EarlyUnlock);
    auto n=f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
    const auto initial=rt::status().scalar_bytes;
    if(which==0)n.operation=static_cast<frame_import_vulkan::Operation>(99);
    else if(which==1)n.phase=static_cast<frame_import_vulkan::Phase>(99);
    else {check(rt::vulkan_notification(n,&rt::runtime)==rt::kTag);n.phase=frame_import_vulkan::Phase::After;n.invocation_tag^=1;}
    check(rt::vulkan_notification(n,&rt::runtime)==0 && rt::status().retired);
    check(rt::status().scalar_bytes==initial+(which==2?80:0) && !fixture_stubs::journal_requests);
}
void refusal_entry_evidence(bool vulkan) {
    attach();Fixture f;f.through(Boundary::EarlyUnlock);
    raw::Entry rejected{};
    const auto before=rt::status();
    if(vulkan) {
        auto n=f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
        n.entry.x[30]+=4;rejected=n.entry;
        check(rt::vulkan_notification(n,&rt::runtime)==0);
    } else {
        auto n=f.note(Boundary::SendLock,raw::Phase::Before);
        n.entry.x[20]=0;rejected=n.entry;
        rt::libc_notification(n,&rt::runtime);
    }
    check(rt::status().retired && rt::status().terminal && rt::status().refusal==rt::Refusal::Raw);
    check(rt::status().parent_bytes==before.parent_bytes && rt::status().scalar_bytes==before.scalar_bytes &&
        rt::status().wire_bytes==before.wire_bytes && before.parent_bytes==16 && before.scalar_bytes==24 && before.wire_bytes==0);
    rt::Record r;check(drain(rt::Kind::RawRefused,&r)==1);
    check(r.entry.sp==rejected.sp && r.entry.x==rejected.x);
    check(r.raw.reason==raw::InvalidEntry && r.raw.decoder_refusal==frame_import_native::Refusal::None &&
        r.raw.last_boundary==(vulkan?Boundary::Image:Boundary::SendLock) && r.raw.last_phase==raw::Phase::Before &&
        !r.raw.last_native_result_available && r.raw.last_native_result==0);
    no_proof(r.raw);
}
void attaching_not_published() {
    model_cold_adapter_only();rt::runtime.state.store(5);
    check(!rt::bridge({audit::Stage::Bind,provider(audit::Function::MutexLock),false,{}}).ready);
    check(!rt::status().attached && !rt::runtime.bound.load() && !rt::status().queued);
}
void foreign_after_during_parent_copy() {
    attach();raw::Entry entry{};
    std::thread owner([&] {
        Fixture f;entry=f.entry(Boundary::Begin);pause_parent=true;
        auto before=f.note(Boundary::Begin,raw::Phase::Before);rt::libc_notification(before,&rt::runtime);
        auto after=before;after.phase=raw::Phase::After;rt::libc_notification(after,&rt::runtime);
    });
    while(!parent_paused.load(std::memory_order_acquire))std::this_thread::yield();
    check(rt::runtime.parent_read && rt::runtime.parent_at==entry.sp && rt::status().parent_bytes==16);
    frame_import_forwarding::Notification n{frame_import_forwarding::Operation::Lock,raw::Phase::After,entry,0,0};
    rt::libc_notification(n,&rt::runtime);
    check(rt::runtime.parent_read && rt::runtime.parent_at==entry.sp && rt::status().parent_bytes==16 && !rt::status().scalar_bytes);
    parent_resume.store(true,std::memory_order_release);owner.join();
    check(rt::status().retired && !rt::runtime.parent_read && rt::quiescent());
}
void main_thread_rejected() {
    attach();rt::runtime.setup.main_thread=::pthread_self();Fixture f;
    f.libc_pair(Boundary::Begin);check(rt::status().retired && rt::status().refusal==rt::Refusal::Thread);
    check(!fixture_stubs::attributes_calls && !rt::status().parent_bytes && !rt::status().scalar_bytes);
}
void stream_staged_without_private() {
    const auto s=rt::status();
    check(s.stream_image_required && !s.candidate_claimed && !s.started && !s.terminal && !s.retired);
    check(!s.parent_bytes && !s.scalar_bytes && !s.wire_bytes && !rt::runtime.scope.active());
    check(s.queued==4); // Internal fixture models Attached and three initial provider binds.
}
void stream_complete(Fixture& f) {
    for(auto b:{Boundary::Image,Boundary::Bind,Boundary::View})f.vulkan_pair(b);
    f.libc_pair(Boundary::SendLock);f.libc_pair(Boundary::Write,0,21);f.libc_pair(Boundary::Finish);
    const auto s=rt::status();check(s.started && s.terminal && !s.retired && s.candidate_claimed);
    check(s.parent_bytes==16 && s.wire_bytes==21 && s.scalar_bytes+s.parent_bytes+s.wire_bytes<=757);
    rt::Record r;check(drain(rt::Kind::RawMatched,&r)==1);
    check(r.raw.selected_at_image && r.raw.private_acquisition_started_at_image);
    check(r.raw.selected_begin_result==0 && r.raw.selected_early_result==0 && r.raw.reply_token==token);
    check(r.raw.selected_image.format==static_cast<std::uint32_t>(f.format));
    no_proof(r.raw);
}
void stream_ui_then_video(unsigned ui_count,frame_import_native::CreationRoute route=frame_import_native::CreationRoute::Vulkan) {
    attach(true,true,true);check(rt::arm_candidate_once());
    for(unsigned i=0;i<ui_count;++i) {
        Fixture ui;ui.libc_pair(Boundary::Begin);ui.libc_pair(Boundary::EarlyUnlock);
        stream_staged_without_private();
        auto image=ui.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
        check(!rt::vulkan_notification(image,&rt::runtime));
        image.phase=frame_import_vulkan::Phase::After;check(!rt::vulkan_notification(image,&rt::runtime));
        stream_staged_without_private();ui.libc_pair(Boundary::Finish);
    }
    Fixture video(route);video.format=VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
    video.libc_pair(Boundary::Begin);video.libc_pair(Boundary::EarlyUnlock);
    stream_staged_without_private();
    // Source review found this saved pipe is overwritten by the time Image
    // runs. Deferred selection must never replay either old 12-byte decoder.
    video.put(video.dispatch+128,std::uint64_t{0xdeadbeef});
    const auto before=fixture_stubs::attributes_calls.load();stream_complete(video);
    check(fixture_stubs::attributes_calls==before && before==1);
    const auto s=rt::status();check(s.staged_begins==ui_count+1 && s.excluded_images==ui_count && s.eligible_images==1 && s.staging_threads==1);
}
void stream_reject(unsigned problem) {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    auto begin=f.note(Boundary::Begin,raw::Phase::Before);
    rt::libc_notification(begin,&rt::runtime);
    begin.phase=raw::Phase::After;
    if(problem==0)begin.result_bits=EAGAIN;
    if(problem==1)begin.entry.x[19]+=16;
    rt::libc_notification(begin,&rt::runtime);
    auto early=f.note(Boundary::EarlyUnlock,raw::Phase::Before);
    if(problem==2)early.entry.x[0]+=16;
    rt::libc_notification(early,&rt::runtime);early.phase=raw::Phase::After;
    if(problem==3)early.result_bits=EAGAIN;
    if(problem==4)early.entry.x[25]^=1;
    rt::libc_notification(early,&rt::runtime);
    if(problem==5)f.libc_pair(Boundary::Finish);
    if(problem==6)f.libc_pair(Boundary::Begin);
    auto image=f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
    if(problem==7)image.entry.sp+=16;
    if(problem==8)image.extent.width=1153;
    if(problem==9)image.extent.depth=2;
    if(problem==10)image.layers=2;
    if(problem==11)image.entry.x[30]+=4;
    if(problem==12)fixture_stubs::vulkan_loss.store(true);
    if(problem==13)rt::finalize();
    if(problem==14) {
        std::thread foreign([&]{check(!rt::vulkan_notification(image,&rt::runtime));});foreign.join();
    } else check(!rt::vulkan_notification(image,&rt::runtime));
    const auto s=rt::status();
    check(!s.candidate_claimed && !s.started && !s.parent_bytes && !s.scalar_bytes && !s.wire_bytes);
    check(s.queued==4);
}
void stream_parent_mismatch() {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);
    f.put(f.dispatch+8,std::uint64_t{bias+0x24fee4});
    auto n=f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
    check(!rt::vulkan_notification(n,&rt::runtime));
    const auto s=rt::status();check(s.candidate_claimed && s.retired && s.parent_bytes==16 && !s.scalar_bytes && !s.wire_bytes);
}
void stream_cached_stack_budget() {
    attach(true,true,true);check(rt::arm_candidate_once());
    for(unsigned i=0;i<rt::kStagingThreadLimit+4;++i) {
        std::thread owner([]{Fixture f;f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);});owner.join();
    }
    const auto s=rt::status();check(s.staging_threads==rt::kStagingThreadLimit && fixture_stubs::attributes_calls==rt::kStagingThreadLimit);
    check(s.staging_refusals>=4 && !s.candidate_claimed && !s.parent_bytes && !s.scalar_bytes && !s.wire_bytes && s.queued==4);
}
void stream_arm_crossing() {
    attach(true,true,true);Fixture f;
    auto n=f.note(Boundary::Begin,raw::Phase::Before);rt::libc_notification(n,&rt::runtime);
    check(rt::arm_candidate_once());n.phase=raw::Phase::After;rt::libc_notification(n,&rt::runtime);
    f.libc_pair(Boundary::EarlyUnlock);f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    check(!rt::vulkan_notification(f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before),&rt::runtime));
    check(!fixture_stubs::attributes_calls && rt::status().skipped_prearm_afters==1);
    stream_staged_without_private();f.libc_pair(Boundary::Finish);
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);stream_complete(f);
}
void stream_nested_finish_not_reused() {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);
    auto nested=f.note(Boundary::Begin,raw::Phase::Before);
    nested.entry.sp-=320;nested.entry.x[29]=nested.entry.sp;
    rt::libc_notification(nested,&rt::runtime);nested.phase=raw::Phase::After;rt::libc_notification(nested,&rt::runtime);
    f.libc_pair(Boundary::Finish); // The outer call is still in flight.
    f.libc_pair(Boundary::EarlyUnlock);
    check(!rt::vulkan_notification(f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before),&rt::runtime));
    stream_staged_without_private();f.libc_pair(Boundary::Finish);
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);stream_complete(f);
}
void stream_abandoned_import(unsigned failure) {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture ui;
    ui.libc_pair(Boundary::Begin);
    auto early=ui.note(Boundary::EarlyUnlock,raw::Phase::Before);
    if(failure==0)early.entry.x[25]=0; // Native payload read fails; no Finish.
    rt::libc_notification(early,&rt::runtime);early.phase=raw::Phase::After;
    rt::libc_notification(early,&rt::runtime);
    if(failure==1) { // Public Image fails; native method omits the send/Finish.
        auto image=ui.vnote(Boundary::Image,frame_import_vulkan::Phase::Before);
        check(!rt::vulkan_notification(image,&rt::runtime));image.phase=frame_import_vulkan::Phase::After;
        image.result=VK_ERROR_OUT_OF_HOST_MEMORY;check(!rt::vulkan_notification(image,&rt::runtime));
    }
    stream_staged_without_private();
    // Same source stack frame is reused only after that native import returns.
    ui.libc_pair(Boundary::Begin);ui.libc_pair(Boundary::EarlyUnlock);
    ui.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;stream_complete(ui);
    check(rt::status().abandoned_stages==1 && fixture_stubs::attributes_calls==1);
}
void stream_pending_begin_not_abandoned() {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    const auto before=f.note(Boundary::Begin,raw::Phase::Before);
    rt::libc_notification(before,&rt::runtime);
    auto repeated=before;rt::libc_notification(repeated,&rt::runtime);
    repeated.phase=raw::Phase::After;rt::libc_notification(repeated,&rt::runtime);
    f.libc_pair(Boundary::EarlyUnlock);f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    check(!rt::vulkan_notification(f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before),&rt::runtime));
    stream_staged_without_private();check(!rt::status().abandoned_stages);
}
void stream_retire_during_parent() {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);
    cancel_after_parent.store(16);
    check(!rt::vulkan_notification(f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before),&rt::runtime));
    const auto s=rt::status();check(s.candidate_claimed && s.retired && s.parent_bytes==16 && !s.scalar_bytes && !s.wire_bytes);
}
void stream_fork_retired() {
    attach(true,true,true);check(rt::arm_candidate_once());Fixture f;
    f.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    f.libc_pair(Boundary::Begin);f.libc_pair(Boundary::EarlyUnlock);
    const auto pid=::fork();check(pid>=0);
    if(!pid) {
        // The production atfork child route calls finalize; test that neither
        // inherited slot metadata nor cached stack can reopen its child state.
        rt::finalize();
        check(!rt::vulkan_notification(f.vnote(Boundary::Image,frame_import_vulkan::Phase::Before),&rt::runtime));
        check(!rt::arm_candidate_once() && !rt::status().candidate_claimed && !rt::status().parent_bytes);
        _exit(0);
    }
    int status=0;check(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
    stream_complete(f);
}
void run(void(*work)(),unsigned& passed) {
    const auto pid=::fork();check(pid>=0);
    if(!pid){checks=0;std::thread worker(work);worker.join();std::printf("runtime fixture controls: %u\n",checks);std::fflush(stdout);_exit(0);}
    int status=0;check(::waitpid(pid,&status,0)==pid);check(WIFEXITED(status)&&WEXITSTATUS(status)==0);++passed;
}
}
int main(int argc,char** argv) {
#if defined(__aarch64__)
    Dl_info provider{};check(::dladdr(reinterpret_cast<const void*>(&::pthread_self),&provider)!=0);
    const auto mapped_libc=reinterpret_cast<std::uintptr_t>(provider.dli_fbase);
    check(provider.dli_fname && std::strstr(provider.dli_fname,"/lib/libc.so.6"));
    check(reinterpret_cast<std::uintptr_t>(&::pthread_self)==mapped_libc+0x8b090);
    check(reinterpret_cast<std::uintptr_t>(&::pthread_getattr_np)==mapped_libc+0x85140);
    check(reinterpret_cast<std::uintptr_t>(&::pthread_attr_getstack)==mapped_libc+0x816d0);
    check(reinterpret_cast<std::uintptr_t>(&::pthread_attr_destroy)==mapped_libc+0x81420);
    std::printf("Actual retained ARM libc pthread helpers: %s\n",provider.dli_fname);
#endif
    constexpr std::array<void(*)(),76> cases{
        inert_production,forwarding_only,+[]{success(21);},+[]{success(3);},
        native_failure,incomplete_finalization,parent_mismatch,+[]{ownership_postcheck(0);},
        +[]{ownership_postcheck(1);},queue_budget,foreign_begin,
        +[]{callback_losses(0);},+[]{callback_losses(1);},+[]{callback_losses(2);},
        +[]{mutated_binding(0);},+[]{mutated_binding(1);},+[]{mutated_binding(2);},
        missing_binding,request_failure,+[]{invalid_vulkan(0);},+[]{invalid_vulkan(1);},
        +[]{invalid_vulkan(2);},attaching_not_published,foreign_after_during_parent_copy,main_thread_rejected,
        +[]{refusal_entry_evidence(false);},+[]{refusal_entry_evidence(true);},
        +[]{success(21,frame_import_native::CreationRoute::Facet);},
        +[]{success(3,frame_import_native::CreationRoute::Facet);},mixed_renderer,
        +[]{success(21,frame_import_native::CreationRoute::Vulkan,true);},
        +[]{success(21,frame_import_native::CreationRoute::Facet,true);},
        prearm_no_private_work,prearm_pair_crosses_signal,foreign_prearm_after_claim,
        +[]{arm_refusals(0);},+[]{arm_refusals(1);},+[]{arm_refusals(2);},
        +[]{arm_refusals(3);},+[]{arm_refusals(4);},+[]{arm_refusals(5);},
        +[]{arm_refusals(6);},+[]{arm_refusals(7);},+[]{arm_refusals(8);},
        +[]{armed_nested_refusal(false);},+[]{armed_nested_refusal(true);},
        concurrent_operator_arm,unqualified_begin_keeps_claim,+[]{
            attach(true,true);Fixture f;
            rt::runtime.begins_before_arm.store(std::numeric_limits<std::uint64_t>::max());
            f.libc_pair(Boundary::Begin);
            check(rt::status().begin_counts_overflow && !rt::status().candidate_claimed && !rt::status().started);
            check(!rt::status().parent_bytes && !rt::status().scalar_bytes && !rt::status().wire_bytes && !fixture_stubs::attributes_calls);
        },+[]{stream_ui_then_video(1);},+[]{stream_ui_then_video(2);},
        +[]{stream_ui_then_video(2,frame_import_native::CreationRoute::Facet);},
        +[]{stream_reject(0);},+[]{stream_reject(1);},+[]{stream_reject(2);},+[]{stream_reject(3);},
        +[]{stream_reject(4);},+[]{stream_reject(5);},+[]{stream_reject(6);},+[]{stream_reject(7);},
        +[]{stream_reject(8);},+[]{stream_reject(9);},+[]{stream_reject(10);},+[]{stream_reject(11);},
        +[]{stream_reject(12);},+[]{stream_reject(13);},+[]{stream_reject(14);},
        stream_parent_mismatch,stream_cached_stack_budget,stream_arm_crossing,
        stream_nested_finish_not_reused,stream_retire_during_parent,stream_fork_retired,
        +[]{stream_abandoned_import(0);},+[]{stream_abandoned_import(1);},stream_pending_begin_not_abandoned};
    unsigned passed=0;
    if(argc==2) {char* end=nullptr;const auto n=std::strtoul(argv[1],&end,10);check(end && !*end && n<cases.size());run(cases[n],passed);}
    else {check(argc==1);for(unsigned n=0;n<cases.size();++n){std::printf("runtime case %u\n",n);std::fflush(stdout);run(cases[n],passed);}}
    std::printf("compositor runtime: %u subprocess cases passed; internal adapter fixtures are not production cold admission or hardware proof\n",passed);
}
