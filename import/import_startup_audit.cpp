#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "import_startup_audit.h"
#include <atomic>
#include <link.h>
#include <limits>

// Build this DSO with hidden visibility, no exceptions/RTTI/builtins and (on
// AArch64) -mno-outline-atomics. It needs no libc, TLS, resolver or I/O provider.
// Only the public loader-audit callbacks below have default visibility.
namespace frame_import_startup_audit {
__attribute__((weak, visibility("hidden")))
const Preparation* preparation() noexcept { return nullptr; }
}
namespace {
using namespace frame_import_startup_audit;
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free,
              "The audit bridge cannot introduce an out-of-line atomic lock");
enum : unsigned { Empty, Filling, Alive, Retired };
struct Slot {
    std::atomic<unsigned> state{Empty};
    std::atomic<std::uintptr_t> bias{0}, begin{0}, end{0};
    std::atomic<std::uint64_t> generation{0};
};
Slot slots[5];
std::atomic<std::uint64_t> next_generation{1};
std::atomic<bool> preinit_seen{false}, ready{false}, lost{false}, missed{false};
std::atomic<bool> bridge_admitted{false};
std::atomic<unsigned> binding_state[10];
std::atomic<std::uintptr_t> entry_cache[10], original_cache[10];

bool same_name(const char* a, const char* b) noexcept {
    if (!a || !b) return false;
    for (unsigned i = 0; i < 512; ++i) {
        const auto x = static_cast<const volatile char*>(a)[i];
        const auto y = static_cast<const volatile char*>(b)[i];
        if (x != y) return false;
        if (!x) return true;
    }
    return false;
}
bool add(std::uintptr_t a, std::uintptr_t b, std::uintptr_t& result) noexcept {
    if (b > std::numeric_limits<std::uintptr_t>::max() - a) return false;
    result = a + b; return true;
}
bool in_range(std::uintptr_t rva, const ModulePreparation& module) noexcept {
    return module.rx_rva_begin < module.rx_rva_end &&
           rva >= module.rx_rva_begin && rva < module.rx_rva_end;
}
bool mutex_scope(const Preparation& p) noexcept {
    return p.libc.loader_name || p.native_mutex_init_rva || p.native_mutex_destroy_rva;
}
bool connection_scope(const Preparation& p) noexcept {
    return p.native_mutex_lock_rva || p.native_mutex_unlock_rva || p.native_connect_rva;
}
bool resource_thread_scope(const Preparation& p) noexcept { return p.native_epoll_wait_rva!=0; }
bool raw_scope(const Preparation& p) noexcept { return p.native_read_rva || p.native_write_rva; }
const Preparation* prepared() noexcept {
    const auto* p = preparation();
    if (!p || !p->owned_prepared_files || !p->receiver.loader_name || !p->api.loader_name ||
        !p->observer.loader_name || !in_range(p->bridge_rva, p->observer) ||
        !in_range(p->init_rva, p->api) || !in_range(p->shutdown_rva, p->api) ||
        p->init_rva == p->shutdown_rva || p->receiver.rx_rva_begin >= p->receiver.rx_rva_end ||
        same_name(p->api.loader_name, p->observer.loader_name)) return nullptr;
    if (mutex_scope(*p)) {
        if (!p->client.loader_name || !p->libc.loader_name ||
            p->client.rx_rva_begin >= p->client.rx_rva_end ||
            !in_range(p->native_mutex_init_rva,p->libc) ||
            !in_range(p->native_mutex_destroy_rva,p->libc) ||
            p->native_mutex_init_rva==p->native_mutex_destroy_rva) return nullptr;
        const ModulePreparation* modules[]{&p->receiver,&p->api,&p->observer,&p->client,&p->libc};
        for (unsigned i=0;i<5;++i)
            for (unsigned j=i+1;j<5;++j)
                if (same_name(modules[i]->loader_name,modules[j]->loader_name)) return nullptr;
    }
    if (connection_scope(*p)) {
        if (!mutex_scope(*p)) return nullptr;
        const std::uintptr_t providers[]{p->native_mutex_init_rva,p->native_mutex_destroy_rva,
            p->native_mutex_lock_rva,p->native_mutex_unlock_rva,p->native_connect_rva};
        for (unsigned i=0;i<5;++i) {
            if (!providers[i] || !in_range(providers[i],p->libc)) return nullptr;
            for (unsigned j=i+1;j<5;++j)
                if (providers[i]==providers[j]) return nullptr;
        }
    }
    if(resource_thread_scope(*p)) {
        if(!connection_scope(*p) || !in_range(p->native_epoll_wait_rva,p->libc))return nullptr;
        const std::uintptr_t other[]{p->native_mutex_init_rva,p->native_mutex_destroy_rva,
            p->native_mutex_lock_rva,p->native_mutex_unlock_rva,p->native_connect_rva};
        for(const auto rva:other)if(rva==p->native_epoll_wait_rva)return nullptr;
    }
    if(raw_scope(*p)) {
        if(!resource_thread_scope(*p))return nullptr;
        const std::uintptr_t providers[]{p->native_mutex_init_rva,p->native_mutex_destroy_rva,
            p->native_mutex_lock_rva,p->native_mutex_unlock_rva,p->native_connect_rva,
            p->native_epoll_wait_rva,p->native_read_rva,p->native_write_rva};
        for(unsigned i=0;i<8;++i) {
            if(!providers[i] || !in_range(providers[i],p->libc))return nullptr;
            for(unsigned j=i+1;j<8;++j)if(providers[i]==providers[j])return nullptr;
        }
    }
    return p;
}
bool view(unsigned index, ModuleView& output) noexcept {
    auto& slot = slots[index];
    if (slot.state.load(std::memory_order_acquire) != Alive) return false;
    output.bias = slot.bias.load(); output.rx_begin = slot.begin.load(); output.rx_end = slot.end.load();
    output.observed_open_generation = slot.generation.load();
    return slot.state.load(std::memory_order_acquire) == Alive && output.observed_open_generation;
}
bool views(BoundProvider& output) noexcept {
    const auto* p=prepared();
    if (!p || !view(0,output.receiver) || !view(1,output.api) || !view(2,output.observer) ||
        (mutex_scope(*p) && !view(4,output.libc)) || lost.load()) return false;
    (void)view(3,output.client); // Client is naturally absent at initial preinit.
    return true;
}
std::uintptr_t bridge_address(const Preparation& p, const ModuleView& observer) noexcept {
    std::uintptr_t address{};
    return add(observer.bias, p.bridge_rva, address) && address >= observer.rx_begin &&
           address < observer.rx_end ? address : 0;
}
void loss() noexcept {
    const bool first=!lost.exchange(true,std::memory_order_acq_rel);
    ready.store(false,std::memory_order_release);
    if (!first || !preinit_seen.load() || !bridge_admitted.load()) return;
    const auto* p=prepared();
    BridgeRequest request{};request.stage=Stage::Loss;
    request.missed_before_preinit=missed.load();
    // Earlier eager relocations may already point to owned veneers. They must
    // keep forwarding native behavior, but MAIN retires their evidence now.
    // Do not call views(): the newly sticky loss deliberately rejects it.
    if (!p || !view(2,request.provider.observer)) return;
    (void)view(0,request.provider.receiver);(void)view(1,request.provider.api);
    (void)view(3,request.provider.client);(void)view(4,request.provider.libc);
    const auto address=bridge_address(*p,request.provider.observer);
    if (address) (void)reinterpret_cast<Bridge>(address)(request);
}
} // namespace

#define AUDIT_EXPORT extern "C" __attribute__((visibility("default")))
AUDIT_EXPORT unsigned int la_version(unsigned int version) {
    return version >= LAV_CURRENT ? LAV_CURRENT : 0;
}

AUDIT_EXPORT unsigned int la_objopen(link_map* map, Lmid_t lmid, std::uintptr_t* cookie) {
    if (!cookie) return 0;
    *cookie = 0;
    const auto* p = prepared();
    if (!p || !map || lmid != LM_ID_BASE) return 0;
    const ModulePreparation* modules[] = {&p->receiver, &p->api, &p->observer, &p->client, &p->libc};
    for (unsigned index = 0; index < 5; ++index) {
        const auto& module = *modules[index];
        if (!module.loader_name || module.rx_rva_begin >= module.rx_rva_end) continue;
        if (!same_name(map->l_name, module.loader_name)) continue;
        unsigned expected = Empty;
        auto& slot = slots[index];
        if (!slot.state.compare_exchange_strong(expected, Filling)) { loss(); return 0; }
        std::uintptr_t begin{}, end{};
        const auto generation = next_generation.fetch_add(1);
        if (!generation || generation == std::numeric_limits<std::uint64_t>::max() ||
            !add(map->l_addr, module.rx_rva_begin, begin) || !add(map->l_addr, module.rx_rva_end, end)) {
            slot.state.store(Retired); loss(); return 0;
        }
        slot.bias.store(map->l_addr); slot.begin.store(begin); slot.end.store(end); slot.generation.store(generation);
        slot.state.store(Alive, std::memory_order_release);
        *cookie = index + 1;
        return index == 0 || (index == 3 && mutex_scope(*p)) ? LA_FLG_BINDFROM :
               index == 1 || (index == 4 && mutex_scope(*p)) ? LA_FLG_BINDTO : 0;
    }
    return 0;
}

AUDIT_EXPORT void la_preinit(std::uintptr_t*) {
    if (preinit_seen.exchange(true)) { loss(); return; }
    const auto* p = prepared();
    BridgeRequest request{};
    if (!p || !views(request.provider)) return;
    request.missed_before_preinit = missed.load();
    const auto address = bridge_address(*p, request.provider.observer);
    if (!address) { loss(); return; }
    // Actual la_preinit, not a guessed constructor priority, establishes that
    // the main observer's relocations and initializers have completed.
    const auto response = reinterpret_cast<Bridge>(address)(request);
    const bool accepted = response.ready && !missed.load() && !lost.load();
    bridge_admitted.store(accepted); ready.store(accepted);
}

AUDIT_EXPORT std::uintptr_t la_symbind64(Elf64_Sym* symbol, unsigned int,
                                      std::uintptr_t* reference, std::uintptr_t* definition,
                                      unsigned int* flags, const char* name) {
    if (!symbol) return 0; // A real loader invocation always supplies this pointer.
    const auto original = symbol->st_value;
    const auto* p = prepared();
    if (!p || !reference || !definition || !flags) return original;
    if(*flags & (LA_SYMB_DLSYM | LA_SYMB_ALTVALUE)) {
        // A skipped first decoder wait cannot later become a cold entry.
        // Legacy scopes retain their previous unchanged binding behavior.
        if(resource_thread_scope(*p) && *reference==1 && *definition==5 &&
           same_name(name,"epoll_wait"))loss();
        if(raw_scope(*p) && *reference==4 && *definition==5 &&
           (same_name(name,"read") || same_name(name,"write")))loss();
        return original;
    }
    Function function;
    unsigned index;
    bool mutex=false,epoll=false;
    if (*reference==1 && *definition==2 && same_name(name,"VR_InitInternal2")) { function=Function::Init;index=0; }
    else if (*reference==1 && *definition==2 && same_name(name,"VR_ShutdownInternal")) { function=Function::Shutdown;index=1; }
    else if (mutex_scope(*p) && *reference==4 && *definition==5 && same_name(name,"pthread_mutex_init")) {
        function=Function::MutexInit;index=2;mutex=true;
    } else if (mutex_scope(*p) && *reference==4 && *definition==5 && same_name(name,"pthread_mutex_destroy")) {
        function=Function::MutexDestroy;index=3;mutex=true;
    } else if (connection_scope(*p) && *reference==4 && *definition==5 && same_name(name,"pthread_mutex_lock")) {
        function=Function::MutexLock;index=4;mutex=true;
    } else if (connection_scope(*p) && *reference==4 && *definition==5 && same_name(name,"pthread_mutex_unlock")) {
        function=Function::MutexUnlock;index=5;mutex=true;
    } else if (connection_scope(*p) && *reference==4 && *definition==5 && same_name(name,"connect")) {
        function=Function::Connect;index=6;mutex=true;
    } else if(resource_thread_scope(*p) && *reference==1 && *definition==5 && same_name(name,"epoll_wait")) {
        function=Function::EpollWait;index=7;epoll=true;
    } else if(raw_scope(*p) && *reference==4 && *definition==5 && same_name(name,"read")) {
        function=Function::Read;index=8;mutex=true;
    } else if(raw_scope(*p) && *reference==4 && *definition==5 && same_name(name,"write")) {
        function=Function::Write;index=9;mutex=true;
    } else return original;
    BoundProvider provider{};
    if (!views(provider)) return original;
    std::uintptr_t expected_address{};
    if (mutex && !view(3,provider.client)) return original;
    const auto& definition_view=mutex||epoll?provider.libc:provider.api;
    const std::uintptr_t native_rvas[]{p->init_rva,p->shutdown_rva,p->native_mutex_init_rva,
        p->native_mutex_destroy_rva,p->native_mutex_lock_rva,p->native_mutex_unlock_rva,p->native_connect_rva,
        p->native_epoll_wait_rva,p->native_read_rva,p->native_write_rva};
    const auto native_rva=native_rvas[index];
    if (!add(definition_view.bias,native_rva,expected_address) ||
        original!=expected_address || original<definition_view.rx_begin || original>=definition_view.rx_end) {
        loss(); return original;
    }
    if (!preinit_seen.load()) { missed.store(true); return original; }
    if (!ready.load()) return original;
    unsigned state = binding_state[index].load(std::memory_order_acquire);
    if (state == Alive) {
        if (original_cache[index].load() != original) { loss(); return original; }
        *flags |= LA_SYMB_NOPLTENTER | LA_SYMB_NOPLTEXIT;
        return entry_cache[index].load();
    }
    unsigned empty = Empty;
    if (!binding_state[index].compare_exchange_strong(empty, Filling)) { loss(); return original; }
    provider.function = function; provider.original = original;
    BridgeRequest request{Stage::Bind, provider, false, {}};
    const auto address = bridge_address(*p, provider.observer);
    if (!address) { loss(); return original; }
    const auto response = reinterpret_cast<Bridge>(address)(request);
    if (!response.ready || !response.hidden_entry || response.hidden_entry < provider.observer.rx_begin ||
        response.hidden_entry >= provider.observer.rx_end || lost.load()) {
        binding_state[index].store(Retired); loss(); return original;
    }
    original_cache[index].store(original); entry_cache[index].store(response.hidden_entry);
    binding_state[index].store(Alive, std::memory_order_release);
    *flags |= LA_SYMB_NOPLTENTER | LA_SYMB_NOPLTEXIT;
    return response.hidden_entry;
}

AUDIT_EXPORT unsigned int la_objclose(std::uintptr_t* cookie) {
    if (cookie && *cookie >= 1 && *cookie <= 5) {
        BridgeRequest request{};
        request.stage = Stage::Retire;
        request.missed_before_preinit = missed.load();
        const auto* p = prepared();
        if (p && preinit_seen.load() && bridge_admitted.load() && view(*cookie - 1, request.retired_module) &&
            view(2, request.provider.observer)) {
            // Finalizers already ran, but the closing object's mapping and the
            // owned main-observer bridge are still live until callback return.
            (void)view(0, request.provider.receiver);
            (void)view(1, request.provider.api);
            (void)view(3, request.provider.client);
            (void)view(4, request.provider.libc);
            const auto address = bridge_address(*p, request.provider.observer);
            if (address) (void)reinterpret_cast<Bridge>(address)(request);
        }
        slots[*cookie - 1].state.store(Retired); loss();
    }
    return 0;
}
