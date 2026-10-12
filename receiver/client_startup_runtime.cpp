#include "client_startup_runtime.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <pthread.h>

namespace frame_client_startup {
namespace {
using Gate=frame_import_startup::Gate;
using Reader=frame_import_loader::Reader;
using Registry=frame_import_pipe::Registry;
namespace audit=frame_import_startup_audit;
namespace forwarding=frame_import_startup_forwarding;
constexpr unsigned kQueueCapacity=32;
enum State : unsigned { Empty,Preparing,Prepared,Attached,Starting,Observing,Completed,Refused };
struct Slot { std::atomic<unsigned> state{0};Record value{}; };
struct Runtime {
    std::atomic<unsigned> state{Empty},retired{0},finalized{0};
    std::atomic<Refusal> refusal{Refusal::Unprepared};
    std::atomic<std::uint64_t> queued{0},drained{0};
    std::array<Slot,kQueueCapacity> queue{};
    frame_import_module::Profile profile{};
    Setup setup{};
    std::array<frame_import_main::Image,frame_import_main::kModules> images{};
    int (*lookup)(const void*,Dl_info*)=nullptr;
    frame_import_loader::Iterate iterate=nullptr;
    std::uintptr_t native_init=0,native_shutdown=0;
    alignas(Registry)std::array<unsigned char,sizeof(Registry)> registry_storage{};
    alignas(Reader)std::array<unsigned char,sizeof(Reader)> reader_storage{};
    alignas(Gate)std::array<unsigned char,sizeof(Gate)> gate_storage{};
    std::atomic<Registry*> registry{nullptr};
    std::atomic<Gate*> gate{nullptr};
    Reader* reader=nullptr; // Written/read solely on the startup owner thread.
};
Runtime runtime;
static_assert(std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free &&
              std::atomic<Gate*>::is_always_lock_free,"Native callbacks require lock-free publication");

void retire(Refusal why) noexcept {
    runtime.retired.store(1,std::memory_order_release);
    auto expected=Refusal::None;
    runtime.refusal.compare_exchange_strong(expected,why,std::memory_order_acq_rel);
    forwarding::retire_evidence();
    if(auto* gate=runtime.gate.load(std::memory_order_acquire))gate->observer_loss();
    if(auto* registry=runtime.registry.load(std::memory_order_acquire))registry->observer_loss();
}
bool enqueue(Record value) noexcept {
    if(runtime.finalized.load(std::memory_order_acquire))return false;
    auto next=runtime.queued.load(std::memory_order_relaxed);
    const auto drained=runtime.drained.load(std::memory_order_acquire);
    if(next==std::numeric_limits<std::uint64_t>::max() || next<drained || next-drained>=kQueueCapacity ||
       !runtime.queued.compare_exchange_strong(next,next+1,std::memory_order_acq_rel)) {
        retire(Refusal::RecordLoss);return false; // Never wait on another producer.
    }
    auto& slot=runtime.queue[next%kQueueCapacity];unsigned empty=0;
    if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acq_rel)) {
        retire(Refusal::RecordLoss);return false;
    }
    value.ordinal=next;value.local_verification_label=runtime.setup.local_verification_label;
    slot.value=value;slot.state.store(2,std::memory_order_release);return true;
}
Record record(Kind kind) noexcept {
    Record value{};value.kind=kind;value.refusal=runtime.refusal.load(std::memory_order_acquire);return value;
}
bool losses() noexcept {
    if(frame_import_forwarding::notification_loss().loss || forwarding::loss_status().lost) {
        const bool first=!runtime.retired.load(std::memory_order_acquire);
        retire(Refusal::NotificationLoss);
        if(first)enqueue(record(Kind::Loss));
        return true;
    }return false;
}
bool within(const frame_import_main::Image& image,std::uintptr_t address) noexcept {
    return address>=image.rx_begin && address<image.rx_end;
}
bool add(std::uintptr_t base,std::uintptr_t offset,std::uintptr_t& out) noexcept {
    if(offset>std::numeric_limits<std::uintptr_t>::max()-base)return false;
    out=base+offset;return true;
}
bool terminated(const std::array<char,512>& name) noexcept {
    if(!name[0])return false;
    for(const auto c:name)if(!c)return true;
    return false;
}
bool same_name(const char* actual,const std::array<char,512>& expected) noexcept {
    if(!actual)return false;
    for(unsigned i=0;i<expected.size();++i) {
        if(actual[i]!=expected[i])return false;
        if(!actual[i])return true;
    }return false;
}
frame_import_startup::MappedClient validate(const Dl_info& info,const frame_import_native::Entry& entry,
                                          void* owner) noexcept {
    if(owner!=&runtime || runtime.retired.load(std::memory_order_acquire) || losses() || !runtime.reader)return {};
    const auto verified=runtime.reader->verify(info,entry.x[30]);
    if(!verified.valid() || runtime.retired.load(std::memory_order_acquire) || losses())return {};
    // This exact native caller is authenticated through the actual public
    // loader callback before inspecting its public name or five birth words.
    if(!same_name(info.dli_fname,runtime.setup.client.loader_name)) {
        retire(Refusal::ClientName);return {};
    }
    const auto& m=verified.module;std::uintptr_t begin=0,end=0;
    if(!add(m.load_bias,runtime.setup.client.rx_rva_begin,begin) ||
       !add(m.load_bias,runtime.setup.client.rx_rva_end,end) || m.code_begin!=begin || m.code_end!=end) {
        retire(Refusal::ClientGeometry);return {};
    }
    frame_import_startup::MappedClient copied{m.load_bias,m.code_begin,m.code_end,
                                            m.module_lifetime_label,m.code_identity};
    auto value=record(Kind::ClientMapped);value.entry=entry;value.module=copied;
    value.verified_loader_name=runtime.setup.client.loader_name;
    if(!enqueue(value))return {};
    return copied;
}
bool stack_helpers(const frame_import_main::Prepared& prepared) noexcept {
    // Cold main-namespace preflight, never symbol resolution inside a native
    // notification. These ordinary linked public APIs must belong to its
    // independently authenticated libc image, not another preload provider.
    const std::uintptr_t functions[]{reinterpret_cast<std::uintptr_t>(&::pthread_getattr_np),
        reinterpret_cast<std::uintptr_t>(&::pthread_self),reinterpret_cast<std::uintptr_t>(&::pthread_attr_getstack),
        reinterpret_cast<std::uintptr_t>(&::pthread_attr_destroy)};
    const auto libc=prepared.images()[2];
    for(const auto address:functions) {
        Dl_info info{};
        if(!within(libc,address) || !prepared.lookup()(reinterpret_cast<const void*>(address),&info) ||
           reinterpret_cast<std::uintptr_t>(info.dli_fbase)!=libc.bias)return false;
    }return true;
}
bool start(const frame_import_native::Entry& entry) noexcept {
    const auto& receiver=runtime.images[0];std::uintptr_t site=0;
    if(!add(receiver.bias,0x2ae94,site) || entry.x[30]!=site || static_cast<std::uint32_t>(entry.x[1])!=1)return false;
    unsigned attached=Attached;
    if(!runtime.state.compare_exchange_strong(attached,Starting,std::memory_order_acq_rel)) {
        retire(Refusal::Gate);return false;
    }
    // The only explicitly cold callback work: cache the ACTUAL invoking
    // pthread's usable stack before the unchanged native VR_Init provider.
    pthread_attr_t attributes;
    if(::pthread_getattr_np(::pthread_self(),&attributes)!=0){retire(Refusal::Stack);return false;}
    void* base=nullptr;std::size_t size=0;
    const auto got=::pthread_attr_getstack(&attributes,&base,&size);
    const auto destroyed=::pthread_attr_destroy(&attributes);
    const auto low=reinterpret_cast<std::uintptr_t>(base);
    if(got || destroyed || size>std::numeric_limits<std::uintptr_t>::max()-low) {
        retire(Refusal::Stack);return false;
    }
    const auto stack=frame_import_startup::OwnedThreadStack::prepare_current(low,low+size);
    if(!stack.current() || runtime.retired.load(std::memory_order_acquire) || losses()) {
        retire(Refusal::Stack);return false;
    }
    runtime.reader=new(runtime.reader_storage.data())Reader(runtime.profile,runtime.iterate,
                                                          runtime.setup.local_verification_label);
    frame_import_startup::Prepared prepared{};
    prepared.receiver={receiver.bias,receiver.rx_begin,receiver.rx_end,receiver.expected_file_identity};
    const auto& api=runtime.images[1];prepared.api={api.bias,api.rx_begin,api.rx_end,api.expected_file_identity};
    prepared.native_init_provider=runtime.native_init;prepared.native_shutdown_provider=runtime.native_shutdown;
    prepared.client_code_identity=runtime.profile.code_digest;prepared.stack=stack;
    prepared.client=frame_import_startup::InitialClient::NotLoadedAtPreflight;
    prepared.process_generation=runtime.setup.process_generation;prepared.lookup=runtime.lookup;
    prepared.validate=validate;prepared.validator_owner=&runtime;
    auto* gate=new(runtime.gate_storage.data())Gate(prepared,*runtime.registry.load(std::memory_order_acquire));
    runtime.gate.store(gate,std::memory_order_release);
    if(runtime.retired.load(std::memory_order_acquire) || losses() || !gate->before_vr_init(entry)) {
        retire(Refusal::Gate);return false;
    }
    runtime.state.store(Observing,std::memory_order_release);
    auto value=record(Kind::InitBefore);value.entry=entry;return enqueue(value);
}
void startup(forwarding::Notification n,void* owner) noexcept {
    if(owner!=&runtime || runtime.finalized.load(std::memory_order_acquire) ||
       runtime.retired.load(std::memory_order_acquire) || losses())return;
    if(n.operation==forwarding::Operation::Shutdown && n.phase==forwarding::Phase::Before) {
        if(auto* gate=runtime.gate.load(std::memory_order_acquire))gate->before_shutdown();
        retire(Refusal::Shutdown);auto value=record(Kind::Shutdown);value.entry=n.entry;
        value.preserved_errno=n.preserved_errno;enqueue(value);return;
    }
    if(n.operation!=forwarding::Operation::Init)return;
    if(n.phase==forwarding::Phase::Before){
        if(!start(n.entry)) {
            if(!runtime.retired.load(std::memory_order_acquire))retire(Refusal::Gate);
            auto value=record(Kind::Refused);value.entry=n.entry;value.preserved_errno=n.preserved_errno;
            if(auto* gate=runtime.gate.load(std::memory_order_acquire))value.gate_refusal=gate->refusal();
            enqueue(value);
        }return;
    }
    auto* gate=runtime.gate.load(std::memory_order_acquire);if(!gate)return;
    auto value=record(n.phase==forwarding::Phase::After?Kind::InitAfter:Kind::InitAbandon);
    value.entry=n.entry;value.actual_init_token=n.result_token;value.preserved_errno=n.preserved_errno;
    if(n.phase==forwarding::Phase::Abandon){gate->abandon_initialization();retire(Refusal::InitializationFailure);}
    else if(!gate->after_vr_init(n.entry,n.result_token)){retire(Refusal::InitializationFailure);}
    else runtime.state.store(Completed,std::memory_order_release);
    value.gate_refusal=gate->refusal();value.refusal=runtime.refusal.load(std::memory_order_acquire);enqueue(value);
}
void libc(frame_import_forwarding::Notification n,void* owner) noexcept {
    if(owner!=&runtime || runtime.finalized.load(std::memory_order_acquire) ||
       runtime.retired.load(std::memory_order_acquire) || n.operation!=frame_import_forwarding::Operation::Init || losses())return;
    auto* gate=runtime.gate.load(std::memory_order_acquire);if(!gate)return;
    if(n.phase==frame_import_forwarding::Phase::Before) {
        gate->native_init_before(n.entry);
        if(gate->refusal()!=frame_import_startup::Refusal::None) {
            retire(Refusal::Gate);auto value=record(Kind::Refused);value.entry=n.entry;
            value.gate_refusal=gate->refusal();enqueue(value);
        }
    }else {
        const auto word=static_cast<std::uint32_t>(n.result_bits);int actual=0;
        static_assert(sizeof(actual)==sizeof(word),"Native mutex result ABI changed");std::memcpy(&actual,&word,sizeof(actual));
        const auto object=gate->native_init_after(n.entry,actual);
        if(object.valid()) {
            auto value=record(Kind::PipeBorn);value.entry=n.entry;value.object=object;
            value.actual_native_result=actual;value.preserved_errno=n.preserved_errno;gate->mapping(value.module);enqueue(value);
        }else if(gate->refusal()!=frame_import_startup::Refusal::None) {
            retire(Refusal::Gate);auto value=record(Kind::Refused);value.entry=n.entry;
            value.gate_refusal=gate->refusal();enqueue(value);
        }
    }
}
bool deny(Refusal why) noexcept {
    runtime.refusal.store(why,std::memory_order_release);runtime.state.store(Refused,std::memory_order_release);
    auto value=record(Kind::Refused);value.refusal=why;enqueue(value);return false;
}
} // namespace

bool initialize_once(const frame_import_main::Prepared& prepared,
                     const frame_import_module::ProfileBlob& blob,Setup setup) noexcept {
    unsigned empty=Empty;
    if(runtime.finalized.load(std::memory_order_acquire) ||
       !runtime.state.compare_exchange_strong(empty,Preparing,std::memory_order_acq_rel))return false;
    if(!prepared.ready() || prepared.client_present() || !prepared.lookup() || !prepared.iterate())return deny(Refusal::MainPreparation);
    if(!frame_import_module::decode_profile(blob,runtime.profile))return deny(Refusal::ClientProfile);
    const auto images=prepared.images();
    for(std::size_t i=0;i<images.size();++i)
        if(images[i].role!=static_cast<frame_import_main::Role>(i+1) || !images[i].bias ||
           (i<4 && images[i].profile_identity!=frame_import_main::pinned_identity(images[i].role)))return deny(Refusal::MainPreparation);
    if(!setup.process_generation || !setup.local_verification_label || !terminated(setup.client.loader_name) ||
       setup.client.rx_rva_begin>=setup.client.rx_rva_end || !within(images[4],setup.hidden_init_entry) ||
       !within(images[4],setup.hidden_shutdown_entry) || setup.hidden_init_entry==setup.hidden_shutdown_entry)return deny(Refusal::Setup);
    bool profile_rx=false;
    for(const auto& segment:runtime.profile.segments)
        if(segment.type==PT_LOAD && (segment.flags&PF_X) &&
           runtime.profile.caller_rva>=segment.virtual_address &&
           runtime.profile.caller_rva-segment.virtual_address<segment.file_size)
            profile_rx=setup.client.rx_rva_begin==segment.virtual_address &&
                       segment.file_size<=std::numeric_limits<std::uintptr_t>::max()-segment.virtual_address &&
                       setup.client.rx_rva_end==segment.virtual_address+segment.file_size;
    if(!profile_rx || !stack_helpers(prepared))return deny(Refusal::Setup);
    runtime.setup=setup;runtime.images=images;runtime.lookup=prepared.lookup();runtime.iterate=prepared.iterate();
    runtime.native_init=prepared.native_init();runtime.native_shutdown=prepared.native_shutdown();
    runtime.refusal.store(Refusal::None,std::memory_order_release);
    auto* registry=new(runtime.registry_storage.data())Registry(setup.process_generation);
    runtime.registry.store(registry,std::memory_order_release);
    auto pinned=[](const frame_import_main::Image& i) noexcept {
        return forwarding::PinnedModule{i.bias,i.rx_begin,i.rx_end,i.expected_file_identity};
    };
    if(!forwarding::prepare_once({reinterpret_cast<forwarding::Init>(prepared.native_init()),
                                reinterpret_cast<forwarding::Shutdown>(prepared.native_shutdown())},
                                {startup,&runtime},{pinned(images[0]),pinned(images[1]),pinned(images[4])}))return deny(Refusal::Providers);
    if(!frame_import_forwarding::register_callbacks_before_threads({libc,&runtime},
       frame_import_forwarding::InitializationGate::OwnedPreparedContext)) {
        retire(Refusal::Subscriber);return deny(Refusal::Subscriber);
    }
    if(runtime.finalized.load(std::memory_order_acquire) || runtime.retired.load(std::memory_order_acquire) || losses())return false;
    if(!enqueue(record(Kind::Prepared)))return false;
    runtime.state.store(Prepared,std::memory_order_release);
    return !runtime.retired.load(std::memory_order_acquire) && !runtime.finalized.load(std::memory_order_acquire);
}
audit::BridgeResponse bridge(audit::BridgeRequest request) noexcept {
    if(runtime.finalized.load(std::memory_order_acquire))return {};
    if(request.stage==audit::Stage::Retire) {
        if(runtime.state.load(std::memory_order_acquire)>=Prepared && runtime.state.load()!=Refused) {
            retire(Refusal::Unload);auto value=record(Kind::Retire);
            value.retired_view=request.retired_module;enqueue(value);
        }return {};
    }
    if(runtime.retired.load(std::memory_order_acquire) || runtime.state.load(std::memory_order_acquire)<Prepared ||
       runtime.state.load()==Refused)return {};
    if(request.missed_before_preinit){retire(Refusal::Audit);enqueue(record(Kind::Loss));return {};}
    if(request.stage==audit::Stage::Prepare) {
        unsigned expected=Prepared;
        if(!forwarding::attach_views(request.provider) ||
           !runtime.state.compare_exchange_strong(expected,Attached,std::memory_order_acq_rel)) {
            retire(Refusal::Audit);enqueue(record(Kind::Loss));return {};
        }
        auto value=record(Kind::AuditAttached);value.audit_bindings=request.provider;
        if(!enqueue(value))return {};
        return {true,0};
    }
    if(request.stage!=audit::Stage::Bind || runtime.state.load(std::memory_order_acquire)<Attached ||
       !forwarding::provider_matches(request.provider))return {};
    const auto entry=request.provider.function==audit::Function::Init?runtime.setup.hidden_init_entry:
                     runtime.setup.hidden_shutdown_entry;
    return {true,entry};
}
Status status() noexcept {
    const auto s=runtime.state.load(std::memory_order_acquire);
    return {s>=Prepared && s!=Refused,s>=Attached && s!=Refused,
        runtime.gate.load(std::memory_order_acquire)!=nullptr,s==Completed,
        runtime.retired.load(std::memory_order_acquire)!=0,runtime.finalized.load(std::memory_order_acquire)!=0,
        runtime.refusal.load(std::memory_order_acquire),runtime.queued.load(),runtime.drained.load()};
}
bool pop(Record& out) noexcept {
    const auto next=runtime.drained.load(std::memory_order_relaxed);
    if(next>=runtime.queued.load(std::memory_order_acquire))return false;
    auto& slot=runtime.queue[next%kQueueCapacity];unsigned ready=2;
    if(!slot.state.compare_exchange_strong(ready,3,std::memory_order_acq_rel))return false;
    out=slot.value;slot.state.store(0,std::memory_order_release);
    runtime.drained.store(next+1,std::memory_order_release);return true;
}
Registry* registry() noexcept {return runtime.registry.load(std::memory_order_acquire);}
void finalize() noexcept {
    if(runtime.finalized.exchange(1,std::memory_order_acq_rel))return;
    retire(Refusal::Finalized);
}
} // namespace frame_client_startup
