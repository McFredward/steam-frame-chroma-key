#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "compositor_startup_audit.h"

#include <atomic>
#include <link.h>
#include <limits>

// Compile hidden, without exceptions/RTTI/builtins and with ARM outline atomics
// disabled. This audit needs no TLS, libc I/O, resolver or OpenVR provider.
namespace frame_compositor_audit {
__attribute__((weak, visibility("hidden")))
const Preparation* preparation() noexcept { return nullptr; }
}
namespace {
using namespace frame_compositor_audit;
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
enum : unsigned { Empty, Filling, Alive, Retired };
struct Slot {
    std::atomic<unsigned> state{Empty};
    std::atomic<std::uintptr_t> bias{0}, begin{0}, end{0};
    std::atomic<std::uint64_t> generation{0};
};
Slot slots[3];
std::atomic<std::uint64_t> next_generation{1};
std::atomic<bool> preinit_seen{false}, ready{false}, lost{false}, missed{false}, bridge_admitted{false};
std::atomic<unsigned> binding_state[3];
std::atomic<std::uintptr_t> entry_cache[3], original_cache[3];
bool same_name(const char* a, const char* b) noexcept {
    if(!a || !b)return false;
    for(unsigned i = 0; i < 512; ++i) {
        const auto x = static_cast<const volatile char*>(a)[i], y = static_cast<const volatile char*>(b)[i];
        if(x != y)return false;
        if(!x)return true;
    }
    return false;
}
bool add(std::uintptr_t a, std::uintptr_t b, std::uintptr_t& out) noexcept {
    if(b > std::numeric_limits<std::uintptr_t>::max() - a)return false;
    out = a + b; return true;
}
bool in_range(std::uintptr_t rva, const ModulePreparation& p) noexcept {
    return p.rx_rva_begin < p.rx_rva_end && rva >= p.rx_rva_begin && rva < p.rx_rva_end;
}
const Preparation* prepared() noexcept {
    const auto* p = preparation();
    if(!p || !p->owned_prepared_files || !p->compositor.loader_name || !p->observer.loader_name || !p->libc.loader_name ||
       !in_range(p->bridge_rva,p->observer) || p->compositor.rx_rva_begin >= p->compositor.rx_rva_end ||
       !in_range(p->native_mutex_lock_rva,p->libc) || !in_range(p->native_mutex_unlock_rva,p->libc) || !in_range(p->native_write_rva,p->libc) ||
       p->native_mutex_lock_rva == p->native_mutex_unlock_rva || p->native_mutex_lock_rva == p->native_write_rva ||
       p->native_mutex_unlock_rva == p->native_write_rva || same_name(p->compositor.loader_name,p->observer.loader_name) ||
       same_name(p->compositor.loader_name,p->libc.loader_name) || same_name(p->observer.loader_name,p->libc.loader_name))return nullptr;
    return p;
}
bool view(unsigned i, ModuleView& out) noexcept {
    auto& s = slots[i];
    if(s.state.load(std::memory_order_acquire) != Alive)return false;
    out = {s.bias.load(),s.begin.load(),s.end.load(),s.generation.load()};
    return s.state.load(std::memory_order_acquire) == Alive && out.observed_open_generation;
}
bool views(BoundProvider& p) noexcept {
    return !lost.load() && view(0,p.compositor) && view(1,p.observer) && view(2,p.libc);
}
std::uintptr_t bridge_address(const Preparation& p, const ModuleView& observer) noexcept {
    std::uintptr_t at = 0;
    return add(observer.bias,p.bridge_rva,at) && at >= observer.rx_begin && at < observer.rx_end ? at : 0;
}
void loss() noexcept {
    const bool first = !lost.exchange(true,std::memory_order_acq_rel);
    ready.store(false,std::memory_order_release);
    if(!first || !preinit_seen.load() || !bridge_admitted.load())return;
    const auto* p = prepared(); BridgeRequest r{}; r.stage = Stage::Loss; r.missed_before_preinit = missed.load();
    if(!p || !view(1,r.provider.observer))return;
    (void)view(0,r.provider.compositor); (void)view(2,r.provider.libc);
    const auto at = bridge_address(*p,r.provider.observer);
    if(at)(void)reinterpret_cast<Bridge>(at)(r);
}
bool target(const char* name, unsigned& index, Function& function) noexcept {
    if(same_name(name,"pthread_mutex_lock")) { index = 0; function = Function::MutexLock; return true; }
    if(same_name(name,"pthread_mutex_unlock")) { index = 1; function = Function::MutexUnlock; return true; }
    if(same_name(name,"write")) { index = 2; function = Function::Write; return true; }
    return false;
}
} // namespace
#define AUDIT_EXPORT extern "C" __attribute__((visibility("default")))
AUDIT_EXPORT unsigned int la_version(unsigned int version) { return version >= LAV_CURRENT ? LAV_CURRENT : 0; }
AUDIT_EXPORT unsigned int la_objopen(link_map* map, Lmid_t lmid, std::uintptr_t* cookie) {
    if(!cookie)return 0;
    *cookie = 0;
    const auto* p = prepared();
    if(!p || !map)return 0;
    const ModulePreparation* modules[]{&p->compositor,&p->observer,&p->libc};
    for(unsigned i = 0; i < 3; ++i) {
        const auto& module = *modules[i];
        if(!same_name(map->l_name,module.loader_name))continue;
        if(lmid != LM_ID_BASE) { loss(); return 0; }
        unsigned empty = Empty;
        auto& s = slots[i];
        if(!s.state.compare_exchange_strong(empty,Filling)) { loss(); return 0; }
        std::uintptr_t begin = 0, end = 0;
        const auto generation = next_generation.fetch_add(1);
        if(!generation || generation == std::numeric_limits<std::uint64_t>::max() || !add(map->l_addr,module.rx_rva_begin,begin) ||
           !add(map->l_addr,module.rx_rva_end,end)) { s.state.store(Retired); loss(); return 0; }
        s.bias.store(map->l_addr); s.begin.store(begin); s.end.store(end); s.generation.store(generation);
        s.state.store(Alive,std::memory_order_release); *cookie = i + 1;
        return i == 0 ? LA_FLG_BINDFROM : i == 2 ? LA_FLG_BINDTO : 0;
    }
    return 0;
}
AUDIT_EXPORT void la_preinit(std::uintptr_t*) {
    if(preinit_seen.exchange(true)) { loss(); return; }
    const auto* p = prepared(); BridgeRequest r{};
    if(!p || !views(r.provider))return;
    r.missed_before_preinit = missed.load();
    const auto at = bridge_address(*p,r.provider.observer);
    if(!at) { loss(); return; }
    const auto response = reinterpret_cast<Bridge>(at)(r);
    const bool accepted = response.ready && !missed.load() && !lost.load();
    bridge_admitted.store(accepted); ready.store(accepted);
}
AUDIT_EXPORT std::uintptr_t la_symbind64(Elf64_Sym* symbol, unsigned int,
                                      std::uintptr_t* reference, std::uintptr_t* definition,
                                      unsigned int* flags, const char* name) {
    if(!symbol)return 0;
    const auto original = symbol->st_value;
    const auto* p = prepared(); unsigned index = 0; Function function = Function::None;
    if(!p || !reference || !definition || !flags || *reference != 1 || *definition != 3 || !target(name,index,function))return original;
    if(*flags & (LA_SYMB_DLSYM | LA_SYMB_ALTVALUE)) { loss(); return original; }
    BoundProvider provider{};
    if(!views(provider))return original;
    const std::uintptr_t rvas[]{p->native_mutex_lock_rva,p->native_mutex_unlock_rva,p->native_write_rva};
    std::uintptr_t expected = 0;
    if(!add(provider.libc.bias,rvas[index],expected) || original != expected || original < provider.libc.rx_begin || original >= provider.libc.rx_end) {
        loss(); return original;
    }
    if(!preinit_seen.load()) { missed.store(true); return original; }
    if(!ready.load())return original;
    const auto state = binding_state[index].load(std::memory_order_acquire);
    if(state == Alive) {
        if(original_cache[index].load() != original) { loss(); return original; }
        *flags |= LA_SYMB_NOPLTENTER | LA_SYMB_NOPLTEXIT; return entry_cache[index].load();
    }
    unsigned empty = Empty;
    if(!binding_state[index].compare_exchange_strong(empty,Filling)) { loss(); return original; }
    provider.function = function; provider.original = original;
    const auto at = bridge_address(*p,provider.observer);
    if(!at) { loss(); return original; }
    const auto response = reinterpret_cast<Bridge>(at)(BridgeRequest{Stage::Bind,provider,false,{}});
    if(!response.ready || !response.hidden_entry || response.hidden_entry < provider.observer.rx_begin ||
       response.hidden_entry >= provider.observer.rx_end || lost.load()) {
        binding_state[index].store(Retired); loss(); return original;
    }
    original_cache[index].store(original); entry_cache[index].store(response.hidden_entry);
    binding_state[index].store(Alive,std::memory_order_release);
    *flags |= LA_SYMB_NOPLTENTER | LA_SYMB_NOPLTEXIT; return response.hidden_entry;
}
AUDIT_EXPORT unsigned int la_objclose(std::uintptr_t* cookie) {
    if(cookie && *cookie >= 1 && *cookie <= 3) {
        const auto* p = prepared(); BridgeRequest r{}; r.stage = Stage::Retire; r.missed_before_preinit = missed.load();
        if(p && preinit_seen.load() && bridge_admitted.load() && view(*cookie - 1,r.retired_module) && view(1,r.provider.observer)) {
            (void)view(0,r.provider.compositor); (void)view(2,r.provider.libc);
            const auto at = bridge_address(*p,r.provider.observer);
            if(at)(void)reinterpret_cast<Bridge>(at)(r);
        }
        slots[*cookie - 1].state.store(Retired); loss();
    }
    return 0;
}
