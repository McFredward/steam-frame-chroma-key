#pragma once

#include <cstdint>

// A separate, inactive-by-default selective audit for the compositor. It
// forwards copied public loader facts to the prepared MAIN observer. A proved
// own cold-ready marker permits forwarding-only constructor bindings; capture
// remains unarmed until actual la_preinit. Names/ordinals never authenticate
// mapped native bytes or establish a private-read capability.
namespace frame_compositor_audit {
enum class Function : std::uint8_t { None, MutexLock, MutexUnlock, Write };
enum class Stage : std::uint8_t { Prepare, Bind, Retire, Loss, ForwardOnly };
struct ModuleView {
    std::uintptr_t bias = 0, rx_begin = 0, rx_end = 0;
    std::uint64_t observed_open_generation = 0;
    // Only the own observer supplies this independently prepared, nonexecuting
    // writable range. Root matches it to its authenticated actual PHDR proof.
    std::uintptr_t data_begin = 0, data_end = 0;
};
struct BoundProvider {
    ModuleView compositor{}, observer{}, libc{};
    Function function = Function::None;
    std::uintptr_t original = 0;
};
struct BridgeRequest {
    Stage stage = Stage::Prepare;
    BoundProvider provider{};
    bool missed_before_preinit = false;
    ModuleView retired_module{};
};
struct BridgeResponse { bool ready = false; std::uintptr_t hidden_entry = 0; };
using Bridge = BridgeResponse (*)(BridgeRequest) noexcept;
struct ModulePreparation {
    const char* loader_name = nullptr;
    std::uintptr_t rx_rva_begin = 0, rx_rva_end = 0;
    std::uintptr_t data_rva_begin = 0, data_rva_end = 0;
};
struct Preparation {
    ModulePreparation compositor{}, observer{}, libc{};
    std::uintptr_t bridge_rva = 0;
    std::uintptr_t native_mutex_lock_rva = 0, native_mutex_unlock_rva = 0, native_write_rva = 0;
    bool owned_prepared_files = false;
    // Hidden own atomic<unsigned>, initialized zero. Root release-stores one
    // only after genuine cold preparation. It never authorizes native reads;
    // preinit veneers remain forwarding-only until actual la_preinit admission.
    std::uintptr_t owned_ready_rva = 0;
};
// An immutable source TU may replace this hidden weak null default. No runtime
// file/environment parsing or audit-namespace provider discovery is permitted.
const Preparation* preparation() noexcept;
} // namespace frame_compositor_audit
