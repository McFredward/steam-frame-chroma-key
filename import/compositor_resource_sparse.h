#pragma once

#include "native_import_route.h"

// Fixed source-specific scalar copies for the raw compositor diagnostic. This
// helper does not acquire a live thread/module/lock capability or a namespace.
// The root must admit the paired original call BEFORE invoking it, and account
// for actual reads (including copies whose ownership postcheck fails).
namespace frame_compositor_sparse {
using Read = bool (*)(std::uintptr_t, void*, std::size_t, void*) noexcept;
constexpr std::size_t kScratchBytes = 1648;
enum class Refusal { None, Boundary, Entry, Reader, Decode };
struct Result {
    frame_import_native::Scalars scalars{};
    // Requested bytes are a conservative admission charge, even on failure.
    // Confirmed bytes count successful reader calls only; they cannot replace
    // the root's actual-copy ledger if its reader fails after a partial copy.
    std::size_t bytes_requested = 0, bytes_confirmed = 0;
    Refusal refusal = Refusal::Entry;
    bool valid() const noexcept { return refusal == Refusal::None; }
};
std::size_t scalar_budget(frame_import_native::Boundary,
                         frame_import_native::CreationRoute = frame_import_native::CreationRoute::Vulkan) noexcept;
Result decode(frame_import_native::Boundary, const frame_import_native::Entry&,
              std::uintptr_t compositor_bias, const frame_import_native::Expected&,
              Read, void*) noexcept;
} // namespace frame_compositor_sparse
