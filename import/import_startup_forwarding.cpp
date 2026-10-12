#include "import_startup_forwarding.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>

#if !defined(__EXCEPTIONS)
#error "Startup provider forwarding requires unwind cleanup for its Abandon callback"
#endif

namespace frame_import_startup_forwarding {
namespace {
static_assert(sizeof(std::uintptr_t)==8 && sizeof(std::uint32_t)==4,
              "Startup forwarding ABI changed");
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "Startup forwarding publication must remain lock-free");
// 0 untouched, 1 publishing, 2 immutable. Only copied owned metadata is stored.
std::atomic<unsigned> prepared{0}, attached{0}, lost{0}, suppressed{0};
constexpr unsigned kSuppressedLimit=1024;
Providers providers{};
Callbacks callbacks{};
PreparedModules modules{};
frame_import_startup_audit::BoundProvider views{};
thread_local bool notifying __attribute__((tls_model("initial-exec")))=false;

bool module(const PinnedModule& m) noexcept {
    bool identity=false;for(const auto byte:m.file_identity)identity|=byte!=0;
    return m.bias && !(m.bias%4096) && m.rx_begin>=m.bias && m.rx_begin<m.rx_end && identity;
}
bool contains(const PinnedModule& m,std::uintptr_t address) noexcept {
    return address>=m.rx_begin && address<m.rx_end;
}
bool geometry(const PinnedModule& m,const frame_import_startup_audit::ModuleView& v) noexcept {
    return m.bias==v.bias && m.rx_begin==v.rx_begin && m.rx_end==v.rx_end && v.observed_open_generation;
}
bool same_view(const frame_import_startup_audit::ModuleView& a,
               const frame_import_startup_audit::ModuleView& b) noexcept {
    return a.bias==b.bias && a.rx_begin==b.rx_begin && a.rx_end==b.rx_end &&
        a.observed_open_generation==b.observed_open_generation;
}
void loss() noexcept {
    lost.store(1,std::memory_order_release);
    auto count=suppressed.load(std::memory_order_relaxed);
    if(count<kSuppressedLimit)
        suppressed.compare_exchange_strong(count,count+1,std::memory_order_relaxed,
                                            std::memory_order_relaxed);
}
void notify(Operation operation,Phase phase,const frame_import_native::Entry& entry,
            std::uint32_t result,int preserved_errno) noexcept {
    if(notifying){loss();return;}
    const auto state=prepared.load(std::memory_order_acquire);
    if(state!=2){if(state==1)loss();return;}
    Notification value{operation,phase,entry,result,preserved_errno};
    notifying=true;
    callbacks.notify(value,callbacks.context);
    notifying=false;
    errno=preserved_errno;
}
class Invocation {
    Operation operation_;
    frame_import_native::Entry entry_;
    bool complete_=false;
public:
    Invocation(Operation operation,const frame_import_native::Entry& entry) noexcept
        :operation_(operation),entry_(entry) {
        const int saved=errno;notify(operation_,Phase::Before,entry_,0,saved);errno=saved;
    }
    ~Invocation() noexcept {
        if(!complete_){const int saved=errno;notify(operation_,Phase::Abandon,entry_,0,saved);errno=saved;}
    }
    void finish(std::uint32_t result) noexcept {
        const int saved=errno;complete_=true;notify(operation_,Phase::After,entry_,result,saved);errno=saved;
    }
};
void require_dispatch() noexcept {
    // Root's selective Bind gate makes this unreachable for native callers.
    // A direct internal ABI misuse must not fabricate a successful token or
    // invoke a different provider to conceal a broken bootstrap.
    if(prepared.load(std::memory_order_acquire)!=2 || attached.load(std::memory_order_acquire)!=2)
        std::abort();
}
} // namespace

bool prepare_once(Providers p,Callbacks c,PreparedModules m) noexcept {
    if(!p.init || !p.shutdown || !c.notify || !c.context ||
       !module(m.receiver) || !module(m.api) || !module(m.observer) ||
       !contains(m.api,reinterpret_cast<std::uintptr_t>(p.init)) ||
       !contains(m.api,reinterpret_cast<std::uintptr_t>(p.shutdown)) ||
       reinterpret_cast<std::uintptr_t>(p.init)==reinterpret_cast<std::uintptr_t>(p.shutdown))return false;
    unsigned expected=0;
    if(!prepared.compare_exchange_strong(expected,1,std::memory_order_acq_rel))return false;
    providers=p;callbacks=c;modules=m;
    prepared.store(2,std::memory_order_release);return true;
}
bool providers_prepared() noexcept {return prepared.load(std::memory_order_acquire)==2;}
bool attach_views(const frame_import_startup_audit::BoundProvider& v) noexcept {
    if(prepared.load(std::memory_order_acquire)!=2 || lost.load(std::memory_order_acquire) ||
       v.function!=frame_import_startup_audit::Function::None || v.original ||
       !geometry(modules.receiver,v.receiver) || !geometry(modules.api,v.api) ||
       !geometry(modules.observer,v.observer))return false;
    unsigned expected=0;
    if(!attached.compare_exchange_strong(expected,1,std::memory_order_acq_rel))return false;
    views=v;attached.store(2,std::memory_order_release);
    return !lost.load(std::memory_order_acquire);
}
bool provider_matches(const frame_import_startup_audit::BoundProvider& v) noexcept {
    if(prepared.load(std::memory_order_acquire)!=2 || attached.load(std::memory_order_acquire)!=2 ||
       lost.load(std::memory_order_acquire) || !same_view(views.receiver,v.receiver) ||
       !same_view(views.api,v.api) || !same_view(views.observer,v.observer))return false;
    bool matches=false;
    if(v.function==frame_import_startup_audit::Function::Init)
        matches=v.original==reinterpret_cast<std::uintptr_t>(providers.init);
    if(v.function==frame_import_startup_audit::Function::Shutdown)
        matches=v.original==reinterpret_cast<std::uintptr_t>(providers.shutdown);
    return matches && !lost.load(std::memory_order_acquire);
}
void retire_evidence() noexcept {lost.store(1,std::memory_order_release);}
LossStatus loss_status() noexcept {
    const auto count=suppressed.load(std::memory_order_relaxed);
    return {lost.load(std::memory_order_acquire)!=0,count,count==kSuppressedLimit};
}
} // namespace frame_import_startup_forwarding

extern "C" std::uint64_t frame_import_vr_init_dispatch(const frame_import_native::Entry* original) {
    frame_import_startup_forwarding::require_dispatch();
    const auto entry=*original;
    frame_import_startup_forwarding::Invocation call(frame_import_startup_forwarding::Operation::Init,entry);
    const auto token=frame_import_startup_forwarding::providers.init(
        reinterpret_cast<std::uint32_t*>(entry.x[0]),static_cast<std::uint32_t>(entry.x[1]),
        reinterpret_cast<const char*>(entry.x[2]));
    call.finish(token);return token;
}
extern "C" void frame_import_vr_shutdown_dispatch(const frame_import_native::Entry* original) {
    frame_import_startup_forwarding::require_dispatch();
    const auto entry=*original;
    frame_import_startup_forwarding::Invocation call(frame_import_startup_forwarding::Operation::Shutdown,entry);
    frame_import_startup_forwarding::providers.shutdown();call.finish(0);
}
