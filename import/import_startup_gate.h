#pragma once

#include "native_client_birth.h"
#include "import_pipe_epoch.h"

#include <atomic>
#include <dlfcn.h>

namespace frame_import_startup {

// Construct on the actual initialization thread, outside callbacks, using its
// independently cached usable stack interval. The marker/owner checks prevent
// accidental heap or another-thread ownership; they do not discover mappings.
class OwnedThreadStack {
public:
    static OwnedThreadStack prepare_current(std::uintptr_t begin, std::uintptr_t end) noexcept;
    bool current() const noexcept;
    frame_native_client_birth::StackRegion region() const noexcept { return region_; }
private:
    frame_native_client_birth::StackRegion region_{};
    std::uintptr_t owner_ = 0;
};
struct PreparedImage {
    std::uintptr_t bias = 0, code_begin = 0, code_end = 0;
    std::array<std::uint8_t,32> file_identity{};
};
struct MappedClient {
    std::uintptr_t load_bias = 0, code_begin = 0, code_end = 0;
    std::uint64_t module_lifetime_label = 0;
    std::array<std::uint8_t,32> code_identity{};
};
using Lookup = int (*)(const void*,Dl_info*);
// Root binds its prepared ELF/phdr/build-ID/RX-hash verifier to the actual
// public dladdr result and current loader-owned lease/reader. It returns these
// copied fields only after verification; no producer boolean grants permission.
using Validate = MappedClient (*)(const Dl_info&,const frame_import_native::Entry&,void*) noexcept;
enum class InitialClient { Unknown, NotLoadedAtPreflight };
struct Prepared {
    PreparedImage receiver{}, api{};
    std::uintptr_t native_init_provider = 0, native_shutdown_provider = 0;
    std::array<std::uint8_t,32> client_code_identity{};
    OwnedThreadStack stack{};
    InitialClient client = InitialClient::Unknown;
    std::uint64_t process_generation = 0;
    Lookup lookup = nullptr; // Actual non-interposed public dladdr, resolved once.
    Validate validate = nullptr;
    void* validator_owner = nullptr; // Immutable process-lifetime prepared owner.
};
enum class Refusal : unsigned {
    None, Unprepared, WrongThread, Unmatched, RepeatedInitialization, Recursion,
    LookupFailed, Mapping, NativeWindow, Registry, NativeInitFailed,
    MissingConstructor, NativeInitializationFailed, Shutdown, Unload, ObserverLoss
};

// The root owns the true-entry/provider wrappers. This class never invokes
// VR_Init/Shutdown or loads a library. Native results/arguments remain unchanged.
// Startup discovery is the sole explicit exception to hot-path no-lookup work.
class Gate {
public:
    Gate(Prepared,frame_import_pipe::Registry&) noexcept;
    bool before_vr_init(const frame_import_native::Entry&) noexcept;
    bool native_init_before(const frame_import_native::Entry&) noexcept;
    frame_import_pipe::ObjectToken native_init_after(const frame_import_native::Entry&,int original_result) noexcept;
    bool after_vr_init(const frame_import_native::Entry&,std::uint32_t original_token) noexcept;
    // Call from the wrapper's unwind cleanup when native initialization exits
    // without After. These methods never throw across a provider/cancellation.
    void abandon_initialization() noexcept;
    void before_shutdown() noexcept;
    void before_unload() noexcept;
    void observer_loss() noexcept;
    // Copied immutable provisional mapping supports connection callbacks during
    // native Init; publication is still contingent on its later successful exit.
    bool mapping(MappedClient&) const noexcept;
    Refusal refusal() const noexcept { return reason_.load(std::memory_order_acquire); }
    unsigned lookups() const noexcept { return lookups_.load(std::memory_order_relaxed); }
private:
    enum class State : unsigned { Unprepared, Prepared, Active, Discovering,
                                  NativePending, Seen, Ready, Retired };
    const Prepared prepared_;
    frame_import_pipe::Registry& registry_;
    std::atomic<State> state_{State::Unprepared};
    std::atomic<Refusal> reason_{Refusal::Unprepared};
    std::atomic<unsigned> lookups_{0};
    // Written once by the startup owner before release publication. Retirement
    // changes only atomics/Registry, never these copied immutable values.
    MappedClient mapping_{};
    frame_import_native::Entry init_entry_{}, native_entry_{};
    frame_import_pipe::Reservation reservation_{};
    bool pending_owner_ = false;
    void retire(Refusal) noexcept;
    void cancel_on_owner() noexcept;
};

} // namespace frame_import_startup
