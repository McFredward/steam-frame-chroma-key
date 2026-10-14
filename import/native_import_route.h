#pragma once

// Offline-tested decoder of the exact SteamOS 0.4.5 active-frame contract.
// This is NOT an interposer, memory-mapping validator, runtime authorization or
// resource/session association. A future entry adapter must establish the
// pinned native executable, true public entry context and live stack ownership
// before supplying bytes. No generic unwinding or saved-pointer chasing occurs.
#include <array>
#include <cstddef>
#include <cstdint>

namespace frame_import_native {

struct Entry {
    std::array<std::uint64_t, 31> x{};
    std::uint64_t sp = 0;
};
static_assert(sizeof(Entry) == 256, "AArch64 entry storage contract changed");
static_assert(offsetof(Entry, sp) == 248, "AArch64 SP slot changed");

enum class Boundary {
    Begin, EarlyUnlock, Image, Bind, View, SendLock, Write, Finish, AcceptedInit
};
enum class CreationRoute : unsigned { None, Vulkan, Facet };
// Zero-read classification of the exact public entry shape only. A non-None
// result still requires every source-specific caller link and scalar below;
// neither the return PC nor this label proves a complete import invocation.
CreationRoute creation_route(Boundary, const Entry&, std::uintptr_t load_bias) noexcept;
std::size_t creation_dispatch_offset(CreationRoute) noexcept;
enum class Refusal {
    None, UnknownSite, InvalidEntry, OutsideWindow, FrameLink,
    ParentReturn, ChangedInvocation, Arguments, ResponseShape
};

// Offline fixtures supply owned bytes. A future native adapter may supply only
// its independently proven contiguous active windows; bounds alone never make
// an arbitrary pointer safe. The decoder reads scalar slots, not whole frames.
struct StackBytes {
    const std::uint8_t* data = nullptr;
    std::uintptr_t base = 0;
    std::size_t size = 0;
};
struct Expected {
    std::uintptr_t dispatch_frame = 0;
    std::uintptr_t pipe = 0;
    std::uint32_t sequence = 0;
};
struct Scalars {
    Expected invocation{};
    CreationRoute creation_route = CreationRoute::None;
    std::uint32_t client = 0;
    bool client_available = false;
    bool native_payload_read = false;
    bool native_send_success = false;
    std::uintptr_t packet_base = 0;
    std::uint32_t packet_offset = 0, remaining = 0;
    int fd = -1;
    // These are scalar call-route facts, never native namespace or frame proof.
    Refusal refusal = Refusal::InvalidEntry;
    bool valid() const noexcept { return refusal == Refusal::None; }
};

Scalars decode(Boundary, const Entry&, const StackBytes&, std::uintptr_t load_bias,
               const Expected& expected = {}) noexcept;

// A separate Image-only entry point establishes the sequence from the current
// authenticated creation chain. expected.sequence must be zero because no
// earlier private Begin/EarlyUnlock scalar copy was made. The dispatch and pipe
// still have to match the actual retained public prelude; every later decode()
// retains its complete, including sequence, invocation comparison.
Scalars decode_selected_image(const Entry&, const StackBytes&, std::uintptr_t load_bias,
                              const Expected& public_prelude) noexcept;

} // namespace frame_import_native
