#pragma once

#include "../import/import_main_preparation.h"
#include "../import/import_forwarding.h"
#include "../import/import_startup_forwarding.h"
#include "../import/import_startup_gate.h"

namespace frame_client_startup {

struct ClientSlot {
    // Exactly the prepared audit slot's public loader name, no basename/path
    // canonicalization. This is copied before any native startup callback.
    std::array<char,512> loader_name{};
    std::uintptr_t rx_rva_begin=0,rx_rva_end=0;
};
struct Setup {
    std::uint64_t process_generation=0;
    // Local startup verification bookkeeping, never an audit-open epoch,
    // connection generation, namespace identity or source/video frame.
    std::uint64_t local_verification_label=0;
    std::uintptr_t hidden_init_entry=0,hidden_shutdown_entry=0;
    ClientSlot client{};
    std::uintptr_t hidden_mutex_init_entry=0,hidden_mutex_destroy_entry=0;
};
enum class Refusal : unsigned {
    None,Unprepared,MainPreparation,ClientProfile,Setup,Providers,Subscriber,
    Audit,Stack,Gate,ClientName,ClientGeometry,RecordLoss,NotificationLoss,
    InitializationFailure,Shutdown,Unload,Finalized,MutexProvider,ClientBinding
};
enum class Kind : unsigned {
    Prepared,AuditAttached,InitBefore,ClientMapped,PipeBorn,InitAfter,
    InitAbandon,Shutdown,Retire,Refused,Loss,MutexBound
};
struct Record {
    Kind kind=Kind::Refused;
    std::uint64_t ordinal=0; // Diagnostic queue ordering ONLY.
    std::uint64_t local_verification_label=0;
    frame_import_native::Entry entry{};
    // module_lifetime_label remains local verification bookkeeping. Actual
    // audit open generations are recorded separately in audit_bindings/view.
    frame_import_startup::MappedClient module{};
    frame_import_pipe::ObjectToken object{};
    frame_import_startup_audit::BoundProvider audit_bindings{};
    frame_import_startup_audit::ModuleView retired_view{};
    std::array<char,512> verified_loader_name{};
    // Native token is available ONLY in InitAfter; native mutex result ONLY in
    // PipeBorn. Other kinds carry no synthetic native outcome.
    std::uint32_t actual_init_token=0;
    int actual_native_result=0,preserved_errno=0;
    frame_import_startup::Refusal gate_refusal=frame_import_startup::Refusal::None;
    Refusal refusal=Refusal::None;
};
struct Status {
    bool prepared=false,audit_attached=false,gate_constructed=false;
    bool startup_completed=false,retired=false,finalized=false;
    Refusal refusal=Refusal::Unprepared;
    std::uint64_t queued=0,drained=0;
};

// Explicit root constructor/preinit integration only; there is no constructor,
// profile-file I/O, deployment or automatic activation in this component.
// A real successful sealed main::Prepared capability is mandatory. Neither a
// fixture Image/Proof nor a mutable ready boolean can create that capability.
bool initialize_once(const frame_import_main::Prepared&,
                     const frame_import_module::ProfileBlob&,Setup) noexcept;
frame_import_startup_audit::BridgeResponse bridge(frame_import_startup_audit::BridgeRequest) noexcept;
Status status() noexcept;
// Background owner only. Copies bounded records; never leaves native borrowed
// pointers for later dereference. Records prove only their named local fact,
// not cross-process resource binding, source/UV/mask or GPU coherence.
bool pop(Record&) noexcept;
frame_import_pipe::Registry* registry() noexcept;
// Root's own finalizer calls this BEFORE releasing writer/other context state.
// Late audit la_objclose callbacks then return inert, even before final unmap.
void finalize() noexcept;

} // namespace frame_client_startup
