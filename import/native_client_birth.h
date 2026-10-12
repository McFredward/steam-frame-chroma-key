#pragma once

#include "native_import_route.h"

#include <array>

namespace frame_import_startup { class Gate; }
namespace frame_native_client_birth {

constexpr std::size_t kPrefixBytes = 48, kKnownFrameBytes = 544;
constexpr std::uintptr_t kInitReturn = 0x244dbc, kHmdReturn = 0x1b957c,
    kGlobalReturn = 0xa9250, kHmdObject = 0x647d78, kPipeObject = 0x649ff0;
struct CodeRegion {
    std::uintptr_t bias = 0, begin = 0, end = 0;
};
struct StackRegion { std::uintptr_t begin = 0, end = 0; };
enum class Refusal { None, Site, Arguments, FrameBounds, FrameLink, ParentReturn, Singleton };
struct Facts {
    std::uintptr_t pipe = 0, frame = 0, hmd_frame = 0, global_frame = 0;
    Refusal refusal = Refusal::Site;
    bool valid() const noexcept { return refusal == Refusal::None; }
};

class Reader {
public:
    // Pure offline decoder: supplied bytes are caller-owned, not native reads.
    static Facts decode(const frame_import_native::Entry&,
                        const std::array<std::uint8_t, kPrefixBytes>&,
                        const CodeRegion&) noexcept;
    // Register-only candidate filter. It neither validates a mapped module nor
    // permits a read. Unrelated mutex/graphics entries do not invoke lookup.
    static bool candidate(const frame_import_native::Entry&) noexcept;
private:
    friend class frame_import_startup::Gate;
    // Only the startup gate calls acquisition, after actual code identity and
    // current-thread stack ownership are established. Five fixed scalar copies
    // read no padding, native global, FD, peer field or deferred pointer.
    static Facts acquire(const frame_import_native::Entry&, const StackRegion&,
                         const CodeRegion&) noexcept;
};
} // namespace frame_native_client_birth
