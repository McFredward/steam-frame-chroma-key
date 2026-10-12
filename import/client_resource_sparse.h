#pragma once

#include "native_client_route.h"

// Copies only fixed source-proven scalar slots through an independently owned
// reader. This helper grants no native memory permission. The root must supply
// current-thread/module/lock ownership at the real original call boundary.
namespace frame_client_sparse {
using Read = bool (*)(std::uintptr_t, void*, std::size_t, void*) noexcept;
constexpr std::size_t kScratchBytes = 816;
enum class Refusal { None, Boundary, Entry, Reader, Decode };
struct Result {
    frame_client_native::Scalars scalars{};
    std::size_t bytes_copied = 0;
    Refusal refusal = Refusal::Entry;
    bool valid() const noexcept { return refusal == Refusal::None; }
};
Result decode(frame_client_native::Boundary, const frame_client_native::Entry&,
              std::uintptr_t client_bias, std::uintptr_t receiver_bias,
              const frame_client_native::Expected&, Read, void*) noexcept;
} // namespace frame_client_sparse
