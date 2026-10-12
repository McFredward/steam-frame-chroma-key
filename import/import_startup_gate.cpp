#include "import_startup_gate.h"

#include <cerrno>
#include <cstring>
#include <limits>

namespace frame_import_startup {
namespace {
thread_local char stack_owner_anchor __attribute__((tls_model("initial-exec"))) = 0;
std::uintptr_t current_owner() noexcept { return reinterpret_cast<std::uintptr_t>(&stack_owner_anchor); }
constexpr std::array<std::uint8_t,32> digest(const char* hex) {
    std::array<std::uint8_t,32> out{};
    for (unsigned i=0;i<32;++i) {
        const auto nibble=[](char c)constexpr{return static_cast<unsigned>(c>='a'?c-'a'+10:c-'0');};
        out[i]=static_cast<std::uint8_t>((nibble(hex[2*i])<<4)|nibble(hex[2*i+1]));
    }
    return out;
}
constexpr auto receiver_identity=digest("31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153");
constexpr auto api_identity=digest("4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4");
bool point(const PreparedImage& image,std::uintptr_t offset) noexcept {
    return image.bias && image.bias%4096==0 && offset<=std::numeric_limits<std::uintptr_t>::max()-image.bias &&
        image.code_begin<image.code_end && image.bias+offset>=image.code_begin && image.bias+offset<image.code_end;
}
bool nonzero(const std::array<std::uint8_t,32>& bytes) noexcept {
    for (const auto b:bytes) if (b) return true;
    return false;
}
bool equal_entry(const frame_import_native::Entry& a,const frame_import_native::Entry& b) noexcept {
    return a.sp==b.sp && a.x==b.x;
}
bool region(const frame_native_client_birth::StackRegion& b,std::uintptr_t at,std::size_t size) noexcept {
    return b.begin<b.end && at>=b.begin && at<=b.end && size<=b.end-at;
}
}
OwnedThreadStack OwnedThreadStack::prepare_current(std::uintptr_t begin,std::uintptr_t end) noexcept {
    OwnedThreadStack out;
    const char marker=0; const auto here=reinterpret_cast<std::uintptr_t>(&marker);
    if (!begin || begin%16 || end%16 || begin>=end || end-begin>256ULL*1024*1024 || here<begin || here>=end) return out;
    out.region_={begin,end}; out.owner_=current_owner(); return out;
}
bool OwnedThreadStack::current() const noexcept { return owner_ && owner_==current_owner(); }
Gate::Gate(Prepared p,frame_import_pipe::Registry& registry) noexcept : prepared_(p),registry_(registry) {
    if (p.stack.current() && p.client==InitialClient::NotLoadedAtPreflight && p.process_generation &&
        p.receiver.file_identity==receiver_identity && p.api.file_identity==api_identity &&
        point(p.receiver,0x2ae94) && point(p.api,0x5768) && point(p.api,0x5330) &&
        p.native_init_provider==p.api.bias+0x5768 && p.native_shutdown_provider==p.api.bias+0x5330 &&
        nonzero(p.client_code_identity) && p.lookup && p.validate && p.validator_owner && registry.healthy()) {
        reason_.store(Refusal::None); state_.store(State::Prepared);
    }
}
void Gate::retire(Refusal why) noexcept {
    auto expected=Refusal::None;
    reason_.compare_exchange_strong(expected,why,std::memory_order_acq_rel);
    state_.store(State::Retired,std::memory_order_release);
    registry_.observer_loss();
}
void Gate::cancel_on_owner() noexcept {
    if (!prepared_.stack.current() || !pending_owner_) return;
    registry_.cancel_birth(reservation_); pending_owner_=false;
}
bool Gate::before_vr_init(const frame_import_native::Entry& e) noexcept {
    if (!prepared_.stack.current()) {
        if (state_.load(std::memory_order_acquire)==State::Prepared &&
            e.x[30]==prepared_.receiver.bias+0x2ae94 && static_cast<std::uint32_t>(e.x[1])==1)
            retire(Refusal::WrongThread); // A known first initialization was missed.
        return false;
    }
    const auto state=state_.load(std::memory_order_acquire);
    if (state==State::Unprepared || state==State::Retired) return false;
    if (e.x[30]!=prepared_.receiver.bias+0x2ae94 || static_cast<std::uint32_t>(e.x[1])!=1 ||
        e.sp%16 || e.x[29]!=e.sp || !region(prepared_.stack.region(),e.sp,4448) ||
        !region(prepared_.stack.region(),e.x[0],sizeof(std::uint32_t))) return false;
    if (state!=State::Prepared) { retire(state==State::Active || state==State::Discovering ? Refusal::Recursion : Refusal::RepeatedInitialization); return false; }
    init_entry_=e;
    auto expected=State::Prepared;
    return state_.compare_exchange_strong(expected,State::Active,std::memory_order_release);
}
bool Gate::native_init_before(const frame_import_native::Entry& e) noexcept {
    if (!prepared_.stack.current()) return false;
    const auto state=state_.load(std::memory_order_acquire);
    if (!frame_native_client_birth::Reader::candidate(e)) return false;
    if (state==State::Discovering || state==State::NativePending) { retire(Refusal::Recursion); return false; }
    if (state!=State::Active ||
        !region(prepared_.stack.region(),e.sp,frame_native_client_birth::kKnownFrameBytes)) return false;
    auto expected=State::Active;
    if (!state_.compare_exchange_strong(expected,State::Discovering,std::memory_order_acq_rel)) return false;
    if (lookups_.fetch_add(1)!=0) { retire(Refusal::Recursion); return false; }
    const int saved_errno=errno;
    Dl_info info{};
    const int found=prepared_.lookup(reinterpret_cast<const void*>(e.x[30]),&info);
    errno=saved_errno;
    if (state_.load(std::memory_order_acquire)!=State::Discovering) return false;
    if (!found) { retire(Refusal::LookupFailed); return false; }
    const auto mapped=prepared_.validate(info,e,prepared_.validator_owner);
    errno=saved_errno;
    if (state_.load(std::memory_order_acquire)!=State::Discovering) return false;
    if (!mapped.load_bias || reinterpret_cast<std::uintptr_t>(info.dli_fbase)!=mapped.load_bias ||
        !mapped.module_lifetime_label || mapped.code_identity!=prepared_.client_code_identity) { retire(Refusal::Mapping); return false; }
    const auto facts=frame_native_client_birth::Reader::acquire(e,prepared_.stack.region(),
        {mapped.load_bias,mapped.code_begin,mapped.code_end});
    if (!facts.valid()) { retire(Refusal::NativeWindow); return false; }
    if (state_.load(std::memory_order_acquire)!=State::Discovering) return false;
    frame_import_pipe::BirthProof proof{};
    proof.process_generation=prepared_.process_generation; proof.pipe=proof.mutex_argument=facts.pipe;
    proof.attributes_argument=e.x[1]; proof.entry_return=frame_native_client_birth::kInitReturn;
    proof.constructor_parent_return=frame_native_client_birth::kHmdReturn;
    proof.domain=frame_import_pipe::Domain::Client045; proof.kind=frame_import_pipe::BirthKind::GenericClient;
    proof.pinned_schema_verified=proof.active_constructor_verified=proof.constructor_initializes_peer_pid_zero=true;
    reservation_=registry_.reserve_birth(proof); pending_owner_=reservation_.valid();
    if (!pending_owner_) { retire(Refusal::Registry); return false; }
    mapping_=mapped; native_entry_=e;
    expected=State::Discovering;
    if (!state_.compare_exchange_strong(expected,State::NativePending,std::memory_order_release)) { cancel_on_owner(); return false; }
    return true;
}
frame_import_pipe::ObjectToken Gate::native_init_after(const frame_import_native::Entry& e,int result) noexcept {
    if (!prepared_.stack.current()) return {};
    if (state_.load(std::memory_order_acquire)!=State::NativePending) { cancel_on_owner(); return {}; }
    if (e.x[30]!=native_entry_.x[30]) return {};
    if (!equal_entry(e,native_entry_)) { retire(Refusal::Recursion); cancel_on_owner(); return {}; }
    const auto object=registry_.created(reservation_,result); pending_owner_=false;
    if (result!=0 || !object.valid()) { retire(result ? Refusal::NativeInitFailed : Refusal::Registry); return {}; }
    auto expected=State::NativePending;
    if (!state_.compare_exchange_strong(expected,State::Seen,std::memory_order_release)) return {};
    return object;
}
bool Gate::after_vr_init(const frame_import_native::Entry& e,std::uint32_t token) noexcept {
    if (!prepared_.stack.current()) return false;
    const auto state=state_.load(std::memory_order_acquire);
    if (state==State::Prepared || state==State::Ready || state==State::Unprepared) return false;
    if (e.x[30]!=init_entry_.x[30]) return false;
    if (!equal_entry(e,init_entry_)) { retire(Refusal::Unmatched); cancel_on_owner(); return false; }
    if (state==State::Retired || state==State::Unprepared) { cancel_on_owner(); return false; }
    if (!token || state!=State::Seen) {
        retire(!token ? Refusal::NativeInitializationFailed : Refusal::MissingConstructor); cancel_on_owner(); return false;
    }
    auto expected=State::Seen;
    return state_.compare_exchange_strong(expected,State::Ready,std::memory_order_release);
}
void Gate::abandon_initialization() noexcept { retire(Refusal::NativeInitializationFailed); cancel_on_owner(); }
void Gate::before_shutdown() noexcept { retire(Refusal::Shutdown); cancel_on_owner(); }
void Gate::before_unload() noexcept { retire(Refusal::Unload); cancel_on_owner(); }
void Gate::observer_loss() noexcept { retire(Refusal::ObserverLoss); cancel_on_owner(); }
bool Gate::mapping(MappedClient& out) const noexcept {
    out={}; const auto state=state_.load(std::memory_order_acquire);
    if (state!=State::NativePending && state!=State::Seen && state!=State::Ready) return false;
    const auto copy=mapping_;
    if (state_.load(std::memory_order_acquire)!=state || !registry_.healthy()) return false;
    out=copy; return true;
}
} // namespace frame_import_startup
