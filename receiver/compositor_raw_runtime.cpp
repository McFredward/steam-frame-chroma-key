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
// Pair native Begin calls skipped before the operator signal. Their matching
// After must remain inert even if the writer arms while the native call runs.
thread_local unsigned skipped_begins __attribute__((tls_model("initial-exec")))=0;
thread_local unsigned staging_slot __attribute__((tls_model("initial-exec")))=kStagingThreadLimit;
enum class Candidate : unsigned { Waiting,Armed,Claimed };
enum class StagingState : unsigned { Empty,BeginBefore,Begun,EarlyBefore,Ready,Invalid };
struct Staging {
    std::atomic<unsigned> reserved{0};
    const unsigned char* owner=nullptr;
    frame_import_startup::OwnedThreadStack stack{};
    bool stack_attempted=false;
    StagingState state=StagingState::Empty;
    unsigned nesting=0;
    raw::Entry begin{},early{},finish{};
    bool finish_pending=false;
    int begin_result=0,early_result=0;
    bool payload_ready=false;
    int begin_before_errno=0,begin_after_errno=0,early_before_errno=0,early_after_errno=0;
};
struct Slot {std::atomic<unsigned> state{0};Record record{};};
struct Runtime {
    std::atomic<unsigned> state{0}; // unprepared, preparing, prepared, attached, retired
    std::atomic<Refusal> refusal{Refusal::Unprepared};
    std::atomic<unsigned> callbacks{0},bound{0};
    std::atomic<std::uint64_t> queued{0},drained{0};
    std::atomic<std::uint64_t> begins_before_arm{0},begins_after_arm{0},skipped_prearm_afters{0};
    std::atomic<bool> begin_counts_overflow{false};
    std::atomic<std::uint64_t> staged_begins{0},excluded_images{0},eligible_images{0},staging_refusals{0};
    std::atomic<std::uint64_t> abandoned_stages{0};
    std::atomic<unsigned> staging_threads{0};
    std::atomic<bool> prepared{false},started{false},terminal{false};
    std::atomic<bool> arm_required{false};
    std::atomic<Candidate> candidate{Candidate::Waiting};
    std::atomic<std::size_t> parent_bytes{0},scalar_bytes{0},wire_bytes{0};
    std::array<Slot,kRecordLimit> slots{};
    std::array<Staging,kStagingThreadLimit> staging{};
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
static_assert(std::atomic<Candidate>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

void count_begin(std::atomic<std::uint64_t>& counter) noexcept {
    // Only already supplied public notifications; no private acquisition or
    // additional event publication. Overflow is explicitly not complete data.
    if(counter.fetch_add(1,std::memory_order_relaxed)==std::numeric_limits<std::uint64_t>::max())
        runtime.begin_counts_overflow.store(true,std::memory_order_relaxed);
}
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
frame_import_startup::OwnedThreadStack prepare_current_stack(void* owner) noexcept {
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
    return frame_import_startup::OwnedThreadStack::prepare_current(low,low+size);
}
frame_import_startup::OwnedThreadStack prepare_stack(void* owner) noexcept {
    runtime.candidate_stack=prepare_current_stack(owner);
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
void outcome(const raw::Outcome& out,const raw::Entry& entry) noexcept {
    if(out.kind==raw::Kind::None)return;
    auto r=message(out.kind==raw::Kind::Matched?Kind::RawMatched:Kind::RawRefused);
    // Preserve only the already supplied public-entry snapshot. This adds no
    // native read or candidate and never changes admission/refusal behavior.
    r.raw=out.record;r.entry=entry;runtime.terminal.store(true,std::memory_order_release);enqueue(r);
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
bool same_entry(const raw::Entry& a,const raw::Entry& b) noexcept {return a.sp==b.sp && a.x==b.x;}
bool stack_contains(const frame_import_startup::OwnedThreadStack& stack,std::uintptr_t at,std::size_t count) noexcept {
    const auto region=stack.region();
    return stack.current() && region.begin<region.end && at>=region.begin && at<=region.end && count<=region.end-at;
}
Staging* current_staging(bool reserve=false) noexcept {
    if(staging_slot<kStagingThreadLimit) {
        auto& slot=runtime.staging[staging_slot];
        return slot.reserved.load(std::memory_order_acquire) && slot.owner==&marker?&slot:nullptr;
    }
    if(!reserve)return nullptr;
    // Each slot belongs to one original thread for the entire capture; it is
    // never recycled after a thread exits or a call is invalidated.
    for(unsigned index=0;index<kStagingThreadLimit;++index) {
        auto& slot=runtime.staging[index];unsigned empty=0;
        if(!slot.reserved.compare_exchange_strong(empty,1,std::memory_order_acq_rel))continue;
        slot.owner=&marker;staging_slot=index;
        runtime.staging_threads.fetch_add(1,std::memory_order_relaxed);
        return &slot;
    }
    count_begin(runtime.staging_refusals);return nullptr;
}
void invalidate_staging(Staging& slot) noexcept {
    slot.state=StagingState::Invalid;count_begin(runtime.staging_refusals);
}
bool stage_libc(Boundary b,const frame_import_forwarding::Notification& note,int result) noexcept {
    if(!runtime.setup.require_stream_image)return false;
    const auto candidate=runtime.candidate.load(std::memory_order_acquire);
    if(candidate==Candidate::Claimed) {
        // Other native import workers remain independent of the selected one.
        // A repeated Begin on its actual owner is still a terminal nesting.
        return !runtime.gate->current(marker);
    }
    if(b==Boundary::Begin && note.phase==Phase::After && skipped_begins) {
        --skipped_begins;count_begin(runtime.skipped_prearm_afters);return true;
    }
    if(b==Boundary::Begin && note.phase==Phase::Before && (candidate!=Candidate::Armed || skipped_begins)) {
        if(skipped_begins==std::numeric_limits<unsigned>::max())retire(Refusal::Thread);
        else ++skipped_begins;
        return true;
    }
    if(candidate!=Candidate::Armed)return true;
    auto* slot=current_staging(b==Boundary::Begin && note.phase==Phase::Before);
    if(!slot)return true;
    if(b==Boundary::Finish) {
        // An actual paired Finish consumes one native nesting level. Neither
        // its Before alone nor an inner Finish can revive an outer old call.
        if(note.phase==Phase::Before) {
            if(slot->finish_pending){slot->finish_pending=false;invalidate_staging(*slot);return true;}
            if(!slot->nesting)return true;
            slot->finish=note.entry;slot->finish_pending=true;slot->state=StagingState::Invalid;
        } else if(note.phase==Phase::After) {
            if(!slot->finish_pending || !same_entry(slot->finish,note.entry) || result!=0) {
                slot->finish_pending=false;invalidate_staging(*slot);return true;
            }
            slot->finish_pending=false;if(slot->nesting)--slot->nesting;
            slot->state=slot->nesting?StagingState::Invalid:StagingState::Empty;
        } else invalidate_staging(*slot);
        return true;
    }
    if(b==Boundary::Begin && note.phase==Phase::Before) {
        const auto& e=note.entry;
        constexpr auto max=std::uintptr_t{0x0000ffffffffffffULL};
        if(e.sp<4096 || e.sp%16 || e.sp>max-thread::kBeginFrameBytes || e.x[29]!=e.sp ||
           e.x[0]<4096 || e.x[0]>max || e.x[0]%16 || e.x[0]!=e.x[19]) {
            invalidate_staging(*slot);return true;
        }
        if(slot->nesting) {
            // Native payload/method failures can return without Finish. The
            // pinned function restores its 320-byte down-growing frame; a new
            // same/higher-SP Begin is a fresh call, whereas real recursion has
            // a strictly lower SP. Recovery is metadata-only, before any claim.
            const bool fresh=!slot->finish_pending && slot->state!=StagingState::BeginBefore &&
                slot->state!=StagingState::EarlyBefore && e.sp>=slot->begin.sp &&
                !runtime.parent_bytes.load() && !runtime.scalar_bytes.load() && !runtime.wire_bytes.load() &&
                stack_contains(slot->stack,e.sp,thread::kBeginFrameBytes);
            if(fresh) {
                slot->nesting=0;slot->state=StagingState::Empty;
                count_begin(runtime.abandoned_stages);
            } else {
                if(slot->nesting==std::numeric_limits<unsigned>::max())retire(Refusal::Thread);
                else ++slot->nesting;
                invalidate_staging(*slot);return true;
            }
        }
        slot->nesting=1;
        slot->finish_pending=false;
        if(!slot->stack_attempted) {
            slot->stack_attempted=true;
            // This bounded cold exception runs before the real native lock,
            // never for the first time at the later Vulkan Image callback.
            slot->stack=prepare_current_stack(&runtime);
        }
        if(!alive() || !stack_contains(slot->stack,e.sp,thread::kBeginFrameBytes)) {
            invalidate_staging(*slot);return true;
        }
        slot->begin=e;slot->begin_before_errno=note.preserved_errno;
        slot->state=StagingState::BeginBefore;count_begin(runtime.staged_begins);return true;
    }
    if(b==Boundary::Begin && note.phase==Phase::After) {
        if(slot->state!=StagingState::BeginBefore || slot->nesting!=1 ||
           !same_entry(slot->begin,note.entry) || !slot->stack.current() || result!=0) {
            invalidate_staging(*slot);return true;
        }
        slot->begin_result=result;slot->begin_after_errno=note.preserved_errno;
        slot->state=StagingState::Begun;return true;
    }
    if(b==Boundary::EarlyUnlock && note.phase==Phase::Before) {
        if(slot->state!=StagingState::Begun || slot->nesting!=1 || !slot->stack.current() ||
           note.entry.sp!=slot->begin.sp || note.entry.x[29]!=note.entry.sp ||
           note.entry.x[0]!=slot->begin.x[0]) {
            invalidate_staging(*slot);return true;
        }
        slot->early=note.entry;slot->early_before_errno=note.preserved_errno;
        slot->payload_ready=bool(note.entry.x[25]&0xffU);
        slot->state=StagingState::EarlyBefore;return true;
    }
    if(b==Boundary::EarlyUnlock && note.phase==Phase::After) {
        if(slot->state!=StagingState::EarlyBefore || slot->nesting!=1 ||
           !same_entry(slot->early,note.entry) || !slot->stack.current() || result!=0) {
            invalidate_staging(*slot);return true;
        }
        slot->early_result=result;slot->early_after_errno=note.preserved_errno;
        if(!slot->payload_ready) {
            slot->state=StagingState::Empty;slot->nesting=0;
            count_begin(runtime.abandoned_stages);return true;
        }
        slot->state=StagingState::Ready;return true;
    }
    return true;
}
void acquire(raw::Event& e) noexcept {
    if(!runtime.gate->current(marker) || !runtime.observer.can_acquire(e,runtime.scope))return;
    runtime.event_copies=0;
    const auto s=runtime.setup.require_stream_image && e.boundary==Boundary::Image?
        frame_compositor_sparse::decode_selected_image(e.entry,e.load_bias,runtime.scope.expected(),read_stack,&runtime):
        frame_compositor_sparse::decode(e.boundary,e.entry,e.load_bias,runtime.scope.expected(),read_stack,&runtime);
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
    if(b==Boundary::Begin && note.phase==Phase::Before)
        count_begin(runtime.candidate.load(std::memory_order_acquire)==Candidate::Waiting?
            runtime.begins_before_arm:runtime.begins_after_arm);
    const unsigned required=note.operation==Op::Lock?1:note.operation==Op::Unlock?2:4;
    if(runtime.setup.require_stream_image && runtime.candidate.load(std::memory_order_acquire)!=Candidate::Waiting &&
       !(runtime.bound.load(std::memory_order_acquire)&required)){retire(Refusal::Audit);return;}
    std::int64_t result=0;
    if(note.phase==Phase::After) {
        if(note.operation==Op::Write)std::memcpy(&result,&note.result_bits,sizeof(result));
        else {const auto bits=static_cast<std::uint32_t>(note.result_bits);std::int32_t value;std::memcpy(&value,&bits,4);result=value;}
    }
    if(stage_libc(b,note,static_cast<int>(result)))return;
    if(b==Boundary::Begin && runtime.arm_required.load(std::memory_order_acquire)) {
        const auto candidate=runtime.candidate.load(std::memory_order_acquire);
        if(note.phase==Phase::Before && (candidate==Candidate::Waiting || skipped_begins)) {
            // This is existing TLS metadata only: no stack preparation, native
            // memory read, candidate claim, allocation or provider discovery.
            if(skipped_begins==std::numeric_limits<unsigned>::max())retire(Refusal::Thread);
            else ++skipped_begins;
            return;
        }
        if(note.phase==Phase::After && skipped_begins){count_begin(runtime.skipped_prearm_afters);--skipped_begins;return;}
        if(candidate==Candidate::Waiting)return;
        if(note.phase==Phase::Before) {
            auto armed=Candidate::Armed;
            runtime.candidate.compare_exchange_strong(armed,Candidate::Claimed,std::memory_order_acq_rel);
            // A previously claimed Begin still reaches the original Gate,
            // which rejects nested/repeated/foreign candidates fail-closed.
        } else if(candidate==Candidate::Armed)return;
    } else if(b==Boundary::Begin && note.phase==Phase::Before) {
        auto armed=Candidate::Armed;
        runtime.candidate.compare_exchange_strong(armed,Candidate::Claimed,std::memory_order_acq_rel);
    }
    if(!(runtime.bound.load(std::memory_order_acquire)&required)){retire(Refusal::Audit);return;}
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
    outcome(runtime.observer.observe(e,runtime.scope),e.entry);
}
std::uint64_t vulkan_notification(frame_import_vulkan::Notification note,void* owner) noexcept {
    const Activity activity;
    if(owner!=&runtime || !alive() || runtime.terminal.load())return 0;
    using V=frame_import_vulkan::Operation;
    if(runtime.setup.require_stream_image && runtime.candidate.load(std::memory_order_acquire)==Candidate::Armed &&
       note.operation==V::Image && note.phase==frame_import_vulkan::Phase::Before) {
        auto* slot=current_staging();
        if(!slot || slot->state!=StagingState::Ready || slot->nesting!=1 || !slot->stack.current())return 0;
        const auto route=frame_import_native::creation_route(Boundary::Image,note.entry,runtime.images[0].bias);
        const auto offset=frame_import_native::creation_dispatch_offset(route);
        if(!offset || note.entry.sp>std::numeric_limits<std::uintptr_t>::max()-offset ||
           note.entry.sp+offset!=slot->begin.sp)return 0;
        const raw::ImageSelection selection{static_cast<std::uint32_t>(note.format),note.extent.width,
            note.extent.height,note.extent.depth,note.layers};
        auto selected=event(Boundary::Image,Phase::Before,note.entry,0,note.preserved_errno);
        if(!raw::selected_image_eligible(selected,selection)) {
            count_begin(runtime.excluded_images);return 0;
        }
        count_begin(runtime.eligible_images);
        auto armed=Candidate::Armed;
        if(!runtime.candidate.compare_exchange_strong(armed,Candidate::Claimed,std::memory_order_acq_rel))return 0;
        runtime.candidate_stack=slot->stack;
        runtime.parent_read=true;runtime.parent_at=slot->begin.sp;runtime.event_copies=0;
        const auto admitted=runtime.gate->admit_staged_image(slot->begin,note.entry,slot->stack,marker,read_stack,&runtime);
        runtime.parent_read=false;
        if(admitted.kind!=thread::Kind::BeginAdmitted) {
            auto r=message(Kind::Refused);r.entry=note.entry;r.thread_reason=static_cast<unsigned>(admitted.reason);enqueue(r);
            retire(Refusal::Thread);return 0;
        }
        raw::SourcePrelude prelude{
            event(Boundary::Begin,Phase::Before,slot->begin,0,slot->begin_before_errno),
            event(Boundary::Begin,Phase::After,slot->begin,slot->begin_result,slot->begin_after_errno),
            event(Boundary::EarlyUnlock,Phase::Before,slot->early,0,slot->early_before_errno),
            event(Boundary::EarlyUnlock,Phase::After,slot->early,slot->early_result,slot->early_after_errno)};
        selected=event(Boundary::Image,Phase::Before,note.entry,0,note.preserved_errno);
        outcome(runtime.observer.begin_selected_image(selected,prelude,selection,runtime.scope),note.entry);
        if(!alive() || !runtime.scope.active())return 0;
        auto staged=message(Kind::BeginStaged);staged.entry=slot->begin;enqueue(staged);
        auto ready=message(Kind::ThreadAdmitted);ready.entry=note.entry;enqueue(ready);
        runtime.started.store(true,std::memory_order_release);
    }
    if(!runtime.gate->current(marker) || !runtime.scope.active())return 0;
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
    outcome(runtime.observer.observe(e,runtime.scope),e.entry);
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
    runtime.arm_required.store(setup.require_operator_arm,std::memory_order_relaxed);
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
    if(!setup.require_operator_arm)runtime.candidate.store(Candidate::Armed,std::memory_order_relaxed);
    runtime.refusal.store(Refusal::None);runtime.prepared.store(true);runtime.state.store(2,std::memory_order_release);enqueue(message(Kind::Prepared));return true;
}
bool arm_candidate_once() noexcept {
    if(!runtime.arm_required.load(std::memory_order_acquire) || !alive() ||
       !runtime.prepared.load(std::memory_order_acquire) || runtime.started.load(std::memory_order_acquire) ||
       runtime.terminal.load(std::memory_order_acquire) || runtime.parent_bytes.load() ||
       runtime.scalar_bytes.load() || runtime.wire_bytes.load())return false;
    auto waiting=Candidate::Waiting;
    if(!runtime.candidate.compare_exchange_strong(waiting,Candidate::Armed,std::memory_order_acq_rel))return false;
    // Concurrent retirement cannot reopen a capture. Publication has no side
    // effects on the native Gate or ownership/budget state.
    return alive();
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
    const auto s=runtime.state.load(std::memory_order_acquire),candidate=static_cast<unsigned>(runtime.candidate.load(std::memory_order_acquire));
    return {runtime.prepared.load(),s==3,s==4,runtime.started.load(),runtime.terminal.load(),runtime.refusal.load(),
        runtime.queued.load(),runtime.drained.load(),runtime.parent_bytes.load(),runtime.scalar_bytes.load(),runtime.wire_bytes.load(),
        runtime.arm_required.load(std::memory_order_acquire),candidate>=static_cast<unsigned>(Candidate::Armed),candidate==static_cast<unsigned>(Candidate::Claimed),
        runtime.begins_before_arm.load(),runtime.begins_after_arm.load(),runtime.skipped_prearm_afters.load(),runtime.begin_counts_overflow.load(),
        runtime.setup.require_stream_image,runtime.staged_begins.load(),runtime.excluded_images.load(),runtime.eligible_images.load(),
        runtime.staging_refusals.load(),runtime.staging_threads.load(),runtime.abandoned_stages.load()};
}
void finalize() noexcept {retire(Refusal::Finalized);}
bool quiescent() noexcept {return !runtime.callbacks.load(std::memory_order_acquire);}
bool association_current() noexcept {return alive();}
} // namespace frame_compositor_runtime
