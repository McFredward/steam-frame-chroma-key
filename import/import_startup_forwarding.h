#pragma once

#include "native_import_route.h"
#include "import_startup_audit.h"

#include <array>
#include <cstdint>

// Main-namespace forwarding only. This component neither exports VR_* symbols,
// resolves providers, validates mapped ELF bytes, nor activates an observer.
namespace frame_import_startup_forwarding {

using Init = std::uint32_t (*)(std::uint32_t*,std::uint32_t,const char*);
using Shutdown = void (*)();
struct Providers { Init init = nullptr; Shutdown shutdown = nullptr; };
struct PinnedModule {
    std::uintptr_t bias = 0, rx_begin = 0, rx_end = 0;
    // Independently verified outside callbacks. A nonzero copied digest is not
    // itself verification or permission to read native memory.
    std::array<std::uint8_t,32> file_identity{};
};
struct PreparedModules { PinnedModule receiver{},api{},observer{}; };

enum class Operation : std::uint8_t { Init, Shutdown };
enum class Phase : std::uint8_t { Before, After, Abandon };
struct Notification {
    Operation operation{};
    Phase phase{};
    frame_import_native::Entry entry{}; // Original true-entry value copy.
    std::uint32_t result_token = 0; // Only Init/After has a native token.
    int preserved_errno = 0;
};
using Notify = void (*)(Notification,void*) noexcept;
struct Callbacks { Notify notify = nullptr; void* context = nullptr; };

// Outside all audit/native callbacks, copy real, already pinned providers and
// module geometries once. Providers/context must remain alive until process
// exit. The callback must not throw, mutate native arguments or release its
// owner; its cold Init/Before may explicitly prepare the startup thread gate.
bool prepare_once(Providers,Callbacks,PreparedModules) noexcept;
bool providers_prepared() noexcept;

// Stage::Prepare copies actual la_objopen labels, never constructor-invented
// labels. Geometry must match the independently prepared main namespace. Root
// must retire evidence on missed early binding, audit object retirement or any
// lost loader event; these copied views do not discover lifetime continuity.
bool attach_views(const frame_import_startup_audit::BoundProvider&) noexcept;
bool provider_matches(const frame_import_startup_audit::BoundProvider&) noexcept;
void retire_evidence() noexcept;
struct LossStatus {
    bool lost = false;
    unsigned suppressed_lower_bound = 0; // Bounded, single-CAS increments.
    bool saturated = false;
};
LossStatus loss_status() noexcept;

} // namespace frame_import_startup_forwarding

// Root exposes hidden entry addresses ONLY after provider_matches. Direct use
// without completed preparation/attachment violates this internal ABI: there
// is no synthetic native failure/token and no hot-path provider fallback.
// These functions are deliberately not noexcept. Native C++/forced unwind
// crosses their normal CFI and reports Abandon via a bounded RAII callback.
extern "C" std::uint64_t frame_import_vr_init_dispatch(const frame_import_native::Entry*);
extern "C" void frame_import_vr_shutdown_dispatch(const frame_import_native::Entry*);
