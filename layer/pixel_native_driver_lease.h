#pragma once
// Narrow, cold-verified provider capability for the optional diagnostic adapter.
// This grants no frame/eye association, presenter, mask or passthrough authority.
#include <atomic>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace frame_pixel_native_driver {
enum class Route : std::uint8_t { Begin2, Begin2KHR, Next2, Next2KHR };
enum class State : std::uint8_t { Empty, Preparing, Ready, Refused, Revoked, Released };
enum class Refusal : std::uint8_t {
    None, Inputs, Reentry, Closed, Architecture, Resolver, ModulePath, ModulePin,
    FileOpen, FileIdentity, FileBounds, FileHash, Elf, Maps, MapIdentity,
    EntryPoint, Enumeration, LoadedIdentity, LoadedCode, Changed, EpochExhausted
};
struct Inputs {
    VkDevice device{};
    PFN_vkVoidFunction begin2{}, begin2KHR{}, next2{}, next2KHR{};
};
struct Status {
    State state{State::Empty}; Refusal refusal{Refusal::None};
    std::uint64_t epoch{}, device{}, moduleBase{}, inode{}, deviceMajor{}, deviceMinor{}, acquireMisses{};
    std::uint64_t rvas[4]{};
    std::uint32_t chip{}, routeMask{}, activePermits{}, observedRoute{UINT32_MAX};
    char providerPath[256]{};
};
namespace detail { struct Preparation; }
class Lease;
struct LeaseTestAccess; // Defined only by the owned test fixture, never a runtime issuer.
class Permit {
public:
    Permit() noexcept = default;
    Permit(const Permit&) = delete; Permit& operator=(const Permit&) = delete;
    Permit(Permit&&) noexcept; Permit& operator=(Permit&&) noexcept;
    ~Permit();
private:
    friend class Lease;
    friend bool authorizeValveSrgb(const Permit&, bool) noexcept;
    const Lease* lease_{}; std::uint64_t epoch_{}; Route route_{Route::Begin2};
    Permit(const Lease*,std::uint64_t,Route) noexcept;
    void reset() noexcept;
};
// nextSubpass=false authorizes Begin2 only; true authorizes Next2 only.
// Validation is scalar/atomic, with no resolver, allocation, lock, wait or I/O.
bool authorizeValveSrgb(const Permit&, bool nextSubpass) noexcept;
const char* refusalName(Refusal) noexcept;
const char* stateName(State) noexcept;
class Lease {
public:
    Lease() noexcept = default;
    Lease(const Lease&) = delete; Lease& operator=(const Lease&) = delete;
    // No implicit dlclose/FD release: the owner must close callback entry, revoke,
    // join its cold worker, wait for callback quiescence and call releaseCold().
    ~Lease() = default;
    bool prepareCold(const Inputs&) noexcept;
    Permit acquire(VkDevice actualDevice,Route actualRoute,PFN_vkVoidFunction actualSavedNext) const noexcept;
    void revoke() noexcept;
    bool releaseCold() noexcept;
    Status status() const noexcept;
private:
    friend class Permit;
    friend struct LeaseTestAccess;
    friend struct detail::Preparation;
    friend bool authorizeValveSrgb(const Permit&, bool) noexcept;
    std::atomic<State> state_{State::Empty};
    std::atomic<Refusal> refusal_{Refusal::None};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<bool> published_{false};
    mutable std::atomic<std::uint32_t> readers_{0};
    mutable std::atomic<std::uint64_t> acquireMisses_{0};
    std::atomic_flag cold_ = ATOMIC_FLAG_INIT;
    Inputs inputs_{}; std::uint64_t rvas_[4]{};
    std::uint64_t base_{}, inode_{}, devMajor_{}, devMinor_{};
    std::uint32_t chip_{}, routeMask_{}, observedRoute_{UINT32_MAX};
    char providerPath_[256]{};
    int fd_{-1}; void* module_{};
    bool fail(Refusal) noexcept;
};
} // namespace frame_pixel_native_driver
