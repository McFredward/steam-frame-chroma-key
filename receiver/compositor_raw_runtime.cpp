#include "compositor_raw_runtime.h"
#include "../import/compositor_import_thread.h"
#include "../import/compositor_resource_sparse.h"
#include "../import/import_vulkan_forwarding.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>

extern "C" __attribute__((visibility("hidden"))) std::atomic<unsigned> frame_compositor_raw_ready;

namespace frame_compositor_runtime {
namespace {
namespace raw=frame_compositor_raw;
namespace thread=frame_compositor_import_thread;
using Boundary=raw::Boundary;using Phase=raw::Phase;
thread_local unsigned char marker __attribute__((tls_model("initial-exec")))=0;
struct Slot {std::atomic<unsigned> state{0};Record record{};};
struct Runtime {
    std::atomic<unsigned> state{0}; // unprepared, preparing, prepared, attached, retired
    std::atomic<Refusal> refusal{Refusal::Unprepared};
    std::atomic<unsigned> callbacks{0},bound{0};
    std::atomic<std::uint64_t> queued{0},drained{0};
    std::atomic<bool> prepared{false},started{false},terminal{false};
    std::atomic<std::size_t> parent_bytes{0},scalar_bytes{0},wire_bytes{0};
    std::array<Slot,kRecordLimit> slots{};
    std::array<frame_compositor_main::Image,4> images{};
    Setup setup{};
    std::array<std::uintptr_t,3> originals{};
    frame_compositor_audit::BoundProvider bindings{};
    alignas(thread::Gate) std::array<unsigned char,sizeof(thread::Gate)> thread_storage{};
    thread::Gate* gate=nullptr;
    raw::Observer observer{};raw::Scope scope{};
    raw::Scalars saved{};
    frame_import_startup::OwnedThreadStack candidate_stack{};
    bool parent_read=false;
    std::uintptr_t parent_at=0;
    std::size_t event_copies=0;
} runtime;
static_assert(std::atomic<std::size_t>::is_always_lock_free);
unsigned char* current_marker(void*) noexcept {return &marker;}
bool alive() noexcept {
    return runtime.state.load(std::memory_order_acquire)==3 &&
        !frame_import_forwarding::notification_loss().loss &&
        !frame_import_vulkan::notification_loss() && !frame_compositor_journal::notification_loss();
}
void retire(Refusal why) noexcept {
    frame_compositor_raw_ready.store(0,std::memory_order_release);
    auto none=Refusal::None;runtime.refusal.compare_exchange_strong(none,why);
    runtime.state.store(4,std::memory_order_release);
    if(runtime.gate)runtime.gate->retire();
}
bool enqueue(Record record) noexcept {
    const auto ticket=runtime.queued.fetch_add(1,std::memory_order_relaxed);
    if(ticket>=kRecordLimit){retire(Refusal::Queue);return false;}
    auto& slot=runtime.slots[ticket];unsigned empty=0;
    if(!slot.state.compare_exchange_strong(empty,1)){retire(Refusal::Queue);return false;}
    record.ordinal=ticket;slot.record=record;slot.state.store(2,std::memory_order_release);return true;
}
Record message(Kind kind) noexcept {Record r{};r.kind=kind;r.refusal=runtime.refusal.load();return r;}
bool same(const frame_compositor_audit::ModuleView& a,const frame_compositor_audit::ModuleView& b) noexcept {
    return a.bias==b.bias && a.rx_begin==b.rx_begin && a.rx_end==b.rx_end &&
        a.observed_open_generation==b.observed_open_generation && a.observed_open_generation &&
        a.data_begin==b.data_begin && a.data_end==b.data_end;
}
bool same_bindings(const frame_compositor_audit::BoundProvider& b) noexcept {
    return same(b.compositor,runtime.bindings.compositor)&&same(b.observer,runtime.bindings.observer)&&same(b.libc,runtime.bindings.libc);
}
bool mapped(const frame_compositor_audit::ModuleView& view,const frame_compositor_main::Image& image) noexcept {
    return view.observed_open_generation && view.bias==image.bias && view.rx_begin==image.rx_begin && view.rx_end==image.rx_end;
}
struct Activity {Activity(){runtime.callbacks.fetch_add(1,std::memory_order_acq_rel);}
    ~Activity(){runtime.callbacks.fetch_sub(1,std::memory_order_release);}};
frame_import_startup::OwnedThreadStack prepare_stack(void* owner) noexcept {
    if(owner!=&runtime || !alive())return {};
    const auto current=runtime.setup.self();
    // The pinned libc's MAIN-thread fallback uses /proc and cancelling stdio.
    // Refuse before entering it; preparation is a single native IPC worker's
    // cold exception, never a MAIN/render callback or late graphics setup.
    if(current==runtime.setup.main_thread)return {};
    pthread_attr_t attributes;
    if(runtime.setup.attributes(current,&attributes))return {};
    void* base=nullptr;std::size_t size=0;
    const int got=runtime.setup.get_stack(&attributes,&base,&size);
    const int destroyed=runtime.setup.destroy_attributes(&attributes);
    const auto low=reinterpret_cast<std::uintptr_t>(base);
    if(got || destroyed || !alive() || size>std::numeric_limits<std::uintptr_t>::max()-low)return {};
    runtime.candidate_stack=frame_import_startup::OwnedThreadStack::prepare_current(low,low+size);
    return runtime.candidate_stack;
}
bool read_stack(std::uintptr_t at,void* output,std::size_t count,void* owner) noexcept {
    if(owner!=&runtime || !alive() || !count)return false;
    if(runtime.parent_read) {
        const auto region=runtime.candidate_stack.region();
        if(!runtime.candidate_stack.current() || at!=runtime.parent_at || count!=16 ||
           at<region.begin || at>region.end || count>region.end-at)return false;
    } else if(!runtime.gate->window_current(marker,at,count))return false;
    const auto& total=runtime.parent_read?runtime.parent_bytes:runtime.scalar_bytes;
    const auto limit=runtime.parent_read?std::size_t{16}:raw::kMaxScalarBytes;
    if(total.load(std::memory_order_relaxed)>limit || count>limit-total.load(std::memory_order_relaxed))return false;
    std::memcpy(output,reinterpret_cast<const void*>(at),count);
    runtime.event_copies+=count;
    (runtime.parent_read?runtime.parent_bytes:runtime.scalar_bytes).fetch_add(count,std::memory_order_relaxed);
    return alive() && (runtime.parent_read?runtime.candidate_stack.current():runtime.gate->window_current(marker,at,count));
}
void outcome(const raw::Outcome& out) noexcept {
    if(out.kind==raw::Kind::None)return;
    auto r=message(out.kind==raw::Kind::Matched?Kind::RawMatched:Kind::RawRefused);
    r.raw=out.record;runtime.terminal.store(true,std::memory_order_release);enqueue(r);
    if(out.kind==raw::Kind::Refused)retire(Refusal::Raw);
}
raw::Event event(Boundary b,Phase phase,const raw::Entry& entry,std::int64_t result,int error) noexcept {
    raw::Event e{};e.boundary=b;e.phase=phase;e.entry=entry;
    e.thread_identity=reinterpret_cast<std::uintptr_t>(&marker);e.load_bias=runtime.images[0].bias;
    e.current_thread_owned=runtime.gate->current(marker)||runtime.gate->pending_current(marker);
    e.native_module_current=alive();e.native_provider_current=e.native_module_current;
    e.source_current=e.native_module_current;e.native_result_available=phase==Phase::After;
    e.native_result=result;e.preserved_errno=error;return e;
}
void acquire(raw::Event& e) noexcept {
    if(!runtime.gate->current(marker) || !runtime.observer.can_acquire(e,runtime.scope))return;
    runtime.event_copies=0;
    const auto s=frame_compositor_sparse::decode(e.boundary,e.entry,e.load_bias,
        runtime.scope.expected(),read_stack,&runtime);
    e.scalars=s.scalars;e.decoded_available=s.valid();e.scalar_bytes_copied=runtime.event_copies;
    if(e.boundary==Boundary::Write && s.valid() && e.native_result>0 &&
       static_cast<std::uint64_t>(e.native_result)<=raw::kReplyBytes && alive() &&
       runtime.wire_bytes.load()<=raw::kMaxWireBytes && static_cast<std::size_t>(e.native_result)<=raw::kMaxWireBytes-runtime.wire_bytes.load() &&
       runtime.gate->window_current(marker,e.entry.x[1],static_cast<std::size_t>(e.native_result))) {
        e.prefix.count=static_cast<std::size_t>(e.native_result);
        std::memcpy(e.prefix.bytes.data(),reinterpret_cast<const void*>(e.entry.x[1]),e.prefix.count);
        runtime.wire_bytes.fetch_add(e.prefix.count,std::memory_order_relaxed);
        e.prefix.successful_prefix_owned=alive() && runtime.gate->window_current(marker,e.entry.x[1],e.prefix.count);
    }
    if(!alive() || !runtime.gate->current(marker)){e.current_thread_owned=false;e.native_module_current=false;}
}
void libc_notification(frame_import_forwarding::Notification note,void* owner) noexcept {
    const Activity activity;
    if(owner!=&runtime || !alive() || runtime.terminal.load(std::memory_order_acquire))return;
    const auto pc=note.entry.x[30],bias=runtime.images[0].bias;
    Boundary b;
    using Op=frame_import_forwarding::Operation;
    if(note.operation==Op::Lock && pc==bias+0x153fc0)b=Boundary::Begin;
    else if(note.operation==Op::Unlock && pc==bias+0x1540b0)b=Boundary::EarlyUnlock;
    else if(note.operation==Op::Lock && pc==bias+0x172ea0)b=Boundary::SendLock;
    else if(note.operation==Op::Write && pc==bias+0x172c00)b=Boundary::Write;
    else if(note.operation==Op::Unlock && pc==bias+0x172fbc)b=Boundary::Finish;
    else return;
    const unsigned required=note.operation==Op::Lock?1:note.operation==Op::Unlock?2:4;
    if(!(runtime.bound.load(std::memory_order_acquire)&required)){retire(Refusal::Audit);return;}
    std::int64_t result=0;
    if(note.phase==Phase::After) {
        if(note.operation==Op::Write)std::memcpy(&result,&note.result_bits,sizeof(result));
        else {const auto bits=static_cast<std::uint32_t>(note.result_bits);std::int32_t value;std::memcpy(&value,&bits,4);result=value;}
    }
    if(b==Boundary::Begin) {
        thread::Outcome t;
        if(note.phase==Phase::Before)t=runtime.gate->before_first_begin(note.entry,marker,prepare_stack,&runtime);
        else if(runtime.gate->pending_current(marker)) {
            runtime.parent_read=true;runtime.parent_at=note.entry.sp;runtime.event_copies=0;
            t=runtime.gate->after_first_begin(note.entry,static_cast<int>(result),marker,read_stack,&runtime);
            runtime.parent_read=false;
        } else t=runtime.gate->after_first_begin(note.entry,static_cast<int>(result),marker,read_stack,&runtime);
        if(t.kind==thread::Kind::None)return;
        if(t.kind==thread::Kind::Refused) {
            auto r=message(Kind::Refused);r.entry=note.entry;r.thread_reason=static_cast<unsigned>(t.reason);enqueue(r);
            retire(Refusal::Thread);return;
        }
        auto r=message(note.phase==Phase::Before?Kind::BeginStaged:Kind::ThreadAdmitted);r.entry=note.entry;enqueue(r);
        runtime.started.store(true,std::memory_order_release);
    } else if(!runtime.gate->current(marker))return;
    auto e=event(b,note.phase,note.entry,result,note.preserved_errno);
    if((b==Boundary::EarlyUnlock || b==Boundary::Finish) && note.phase==Phase::After) {
        e.scalars=runtime.saved;e.decoded_available=true;
    } else acquire(e);
    if((b==Boundary::EarlyUnlock || b==Boundary::Finish) && note.phase==Phase::Before && e.decoded_available)runtime.saved=e.scalars;
    outcome(runtime.observer.observe(e,runtime.scope));
}
std::uint64_t vulkan_notification(frame_import_vulkan::Notification note,void* owner) noexcept {
    const Activity activity;
    if(owner!=&runtime || !alive() || runtime.terminal.load() || !runtime.gate->current(marker) || !runtime.scope.active())return 0;
    using V=frame_import_vulkan::Operation;
    if((note.operation!=V::Image && note.operation!=V::Bind && note.operation!=V::View) ||
       (note.phase!=frame_import_vulkan::Phase::Before && note.phase!=frame_import_vulkan::Phase::After)) {
        retire(Refusal::NativeSource);return 0;
    }
    const auto b=note.operation==V::Image?Boundary::Image:note.operation==V::Bind?Boundary::Bind:Boundary::View;
    const auto phase=note.phase==frame_import_vulkan::Phase::Before?Phase::Before:Phase::After;
    auto e=event(b,phase,note.entry,static_cast<int>(note.result),note.preserved_errno);
    if(phase==Phase::Before){e.native_result_available=false;acquire(e);if(e.decoded_available)runtime.saved=e.scalars;}
    else {
        if(note.invocation_tag!=kTag){retire(Refusal::Journal);return 0;}
        e.scalars=runtime.saved;e.decoded_available=true;e.public_result_available=true;
        e.image={note.device,note.image,0,static_cast<std::uint32_t>(note.format),note.extent.width,note.extent.height,note.extent.depth,note.layers,
            static_cast<int>(note.result),false,note.entry.x[0]};
        e.bind={note.device,note.image,note.memory,note.memory_offset,static_cast<int>(note.result),note.entry.x[0]};
        e.view={note.device,note.image,note.view,static_cast<std::uint32_t>(note.format),static_cast<int>(note.result),note.entry.x[0]};
    }
    outcome(runtime.observer.observe(e,runtime.scope));
    if(!alive())return 0;
    if(phase==Phase::After && b==Boundary::View &&
       !frame_compositor_journal::request(note.device,(VkImage)(std::uintptr_t)note.image,(VkImageView)(std::uintptr_t)note.view,kTag,kCookie))retire(Refusal::Journal);
    return alive()?kTag:0;
}
void receipt_notification(const frame_compositor_journal::Receipt& receipt,void* owner) noexcept {
    const Activity activity;
    if(owner!=&runtime || runtime.state.load()<2 || runtime.state.load()==4)return;
    auto r=message(Kind::Receipt);r.receipt=receipt;enqueue(r);
}
}
bool initialize(const frame_compositor_main::Prepared& prepared,Setup setup) noexcept {
    unsigned empty=0;if(!runtime.state.compare_exchange_strong(empty,1))return false;
    runtime.refusal.store(Refusal::None,std::memory_order_release);
    if(!prepared.ready()){retire(Refusal::Preparation);return false;}
    runtime.images=prepared.images();runtime.setup=setup;
    runtime.originals={prepared.native_lock(),prepared.native_unlock(),prepared.native_write()};
    const auto& self=runtime.images[3];
    for(const auto entry:{setup.lock_entry,setup.unlock_entry,setup.write_entry})
        if(entry<self.rx_begin || entry>=self.rx_end){retire(Refusal::Setup);return false;}
    if(!setup.self || !setup.attributes || !setup.get_stack || !setup.destroy_attributes ||
       !setup.main_thread || setup.self()!=setup.main_thread){retire(Refusal::Setup);return false;}
    frame_import_startup::PreparedImage image{runtime.images[0].bias,runtime.images[0].rx_begin,runtime.images[0].rx_end,runtime.images[0].expected_file_identity};
    runtime.gate=new(runtime.thread_storage.data())thread::Gate(image,current_marker,&runtime);
    if(!frame_import_forwarding::register_callbacks_before_threads({libc_notification,&runtime},frame_import_forwarding::InitializationGate::OwnedPreparedContext) ||
       !frame_import_vulkan::register_callbacks_before_threads({vulkan_notification,&runtime},true) ||
       !frame_compositor_journal::register_receipts_before_threads({receipt_notification,&runtime})) {
        retire(Refusal::Registration);return false;
    }
    runtime.refusal.store(Refusal::None);runtime.prepared.store(true);runtime.state.store(2,std::memory_order_release);enqueue(message(Kind::Prepared));return true;
}
frame_compositor_audit::BridgeResponse bridge(frame_compositor_audit::BridgeRequest request) noexcept {
    const Activity activity;
    using Stage=frame_compositor_audit::Stage;using Function=frame_compositor_audit::Function;
    const auto state=runtime.state.load(std::memory_order_acquire);
    if(state!=2 && state!=3)return {};
    if(request.stage==Stage::Loss || request.stage==Stage::Retire || request.missed_before_preinit){retire(Refusal::Audit);return {};}
    const bool forward=request.stage==Stage::ForwardOnly;
    if(request.stage==Stage::Prepare || forward) {
        if(!mapped(request.provider.compositor,runtime.images[0]) || !mapped(request.provider.libc,runtime.images[1]) ||
           !mapped(request.provider.observer,runtime.images[3]) ||
           request.provider.observer.data_begin!=runtime.setup.self_data_begin || request.provider.observer.data_end!=runtime.setup.self_data_end) {
            retire(Refusal::Audit);return {};
        }
        if(!forward) {
        unsigned prepared=2;
        // Claim attachment before writing the immutable published views. The
        // audit normally runs serially, but a duplicated caller cannot race it.
        if(!runtime.state.compare_exchange_strong(prepared,5)){retire(Refusal::Audit);return {};}
        runtime.bindings=request.provider;
        unsigned attaching=5;
        if(!runtime.gate->arm_before_compositor_threads() || !runtime.state.compare_exchange_strong(attaching,3)){retire(Refusal::Audit);return {};}
        auto r=message(Kind::Attached);r.bindings=request.provider;enqueue(r);return {true,0};
        }
    }
    if((!forward && request.stage!=Stage::Bind) || (!forward && !same_bindings(request.provider))){retire(Refusal::Audit);return {};}
    std::uintptr_t entry=0,original=0;unsigned bit=0;
    // Real native provider identities were authenticated by cold preparation.
    switch(request.provider.function) {
    case Function::MutexLock:entry=runtime.setup.lock_entry;original=runtime.originals[0];bit=1;break;
    case Function::MutexUnlock:entry=runtime.setup.unlock_entry;original=runtime.originals[1];bit=2;break;
    case Function::Write:entry=runtime.setup.write_entry;original=runtime.originals[2];bit=4;break;
    default:retire(Refusal::Audit);return {};
    }
    if(request.provider.original!=original){retire(Refusal::Audit);return {};}
    runtime.bound.fetch_or(bit);auto r=message(Kind::Bound);r.bindings=request.provider;r.forward_only=forward;enqueue(r);return {true,entry};
}
bool pop(Record& result) noexcept {
    const auto ticket=runtime.drained.load();if(ticket>=kRecordLimit)return false;
    auto& slot=runtime.slots[ticket];if(slot.state.load(std::memory_order_acquire)!=2)return false;
    result=slot.record;slot.state.store(3);runtime.drained.store(ticket+1);return true;
}
Status status() noexcept {
    const auto s=runtime.state.load(std::memory_order_acquire);return {runtime.prepared.load(),s==3,s==4,runtime.started.load(),runtime.terminal.load(),runtime.refusal.load(),
        runtime.queued.load(),runtime.drained.load(),runtime.parent_bytes.load(),runtime.scalar_bytes.load(),runtime.wire_bytes.load()};
}
void finalize() noexcept {retire(Refusal::Finalized);}
bool quiescent() noexcept {return !runtime.callbacks.load(std::memory_order_acquire);}
bool association_current() noexcept {return alive();}
} // namespace frame_compositor_runtime
