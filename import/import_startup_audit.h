#pragma once

#include <cstdint>

// The audit DSO never exports OpenVR functions or forwards through its namespace.
// Requests are copied values. The main observer verifies its already prepared
// actual mappings/providers; this bridge performs no discovery or native reads.
namespace frame_import_startup_audit {
enum class Function : std::uint8_t {
    None, Init, Shutdown, MutexInit, MutexDestroy, MutexLock, MutexUnlock, Connect
};
enum class Stage : std::uint8_t { Prepare, Bind, Retire, Loss };
struct ModuleView {
    std::uintptr_t bias = 0, rx_begin = 0, rx_end = 0;
    // An observed la_objopen lifetime, not a video frame or trusted file hash.
    std::uint64_t observed_open_generation = 0;
};
struct BoundProvider {
    ModuleView receiver{}, api{}, observer{};
    Function function = Function::None;
    std::uintptr_t original = 0;
    // Append-only mutex scope: real public la_objopen lifetimes. The main
    // bridge must authenticate these copied views, never infer a code identity
    // from loader names or a binding ordinal.
    ModuleView libc{}, client{};
};
struct BridgeRequest {
    Stage stage = Stage::Prepare;
    BoundProvider provider{};
    bool missed_before_preinit = false;
    ModuleView retired_module{};
};
struct BridgeResponse {
    bool ready = false;
    std::uintptr_t hidden_entry = 0;
};
using Bridge = BridgeResponse (*)(BridgeRequest) noexcept;

// Immutable own-source preparation, supplied when building the audit DSO. File
// identities/freshness must be checked outside callbacks. Loader names alone
// never prove native ownership: Stage::Prepare must validate the main observer's
// cached mapping proofs. The executable's public loader name is normally empty.
struct ModulePreparation {
    const char* loader_name = nullptr;
    std::uintptr_t rx_rva_begin = 0, rx_rva_end = 0;
};
struct Preparation {
    ModulePreparation receiver{}, api{}, observer{};
    std::uintptr_t bridge_rva = 0, init_rva = 0, shutdown_rva = 0;
    bool owned_prepared_files = false;
    // Optional source-prepared client. Production capture requires its actual
    // loader name/geometry to match, or unsupported unloads are not observed.
    ModulePreparation client{};
    // All defaults disable mutex binding observation and preserve the original
    // API-only fixtures. A partial opt-in is refused. Native client flags,
    // including RTLD_NOW|RTLD_DEEPBIND, remain unchanged.
    ModulePreparation libc{};
    std::uintptr_t native_mutex_init_rva = 0, native_mutex_destroy_rva = 0;
    // Optional register-only connect observation. These append-only fields
    // require the complete mutex scope above and all three distinct providers.
    // They do not authorize native memory reads or prove FD ownership/lifetime.
    std::uintptr_t native_mutex_lock_rva = 0, native_mutex_unlock_rva = 0,
                   native_connect_rva = 0;
};

// A hidden weak default returns nullptr (inactive). A separate prepared own-
// source TU may replace it. No public API, runtime environment/file parsing,
// dlsym, dlopen, execve or audit-local libc provider discovery is required.
const Preparation* preparation() noexcept;
} // namespace frame_import_startup_audit
