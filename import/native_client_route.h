#pragma once

// Fixed-window decoding of owned entry/stack copies for the exact retained
// SteamOS 0.4.5 binaries. No native acquisition, generic unwinding, activation,
// lifetime certification or permission to read any process memory is provided.
#include "native_import_route.h"

namespace frame_client_native {
using Entry = frame_import_native::Entry;
using StackBytes = frame_import_native::StackBytes;
enum class Boundary { Begin, Write, HeaderRead, PayloadRead, Pending, Reference, AcceptedInit };
enum class Refusal { None, Site, Entry, Window, Link, Return, ChangedInvocation, Arguments, Bound };
struct Invocation {
    std::uintptr_t import_frame = 0, receiver_frame = 0, pipe = 0, record = 0;
};
struct Expected {
    Invocation invocation{};
    std::uint32_t sequence = 0;
    bool sequence_available = false;
};
struct Scalars {
    Invocation invocation{};
    std::uint32_t native_sequence = 0;
    bool sequence_available = false;
    std::uintptr_t buffer_base = 0;
    std::uint32_t offset = 0, total = 0, remaining = 0;
    int fd = -1;
    std::uint64_t token = 0;
    bool token_present = false, native_wait_success = false;
    bool accepted_fd_pending = false;
    Refusal refusal = Refusal::Entry;
    bool valid() const noexcept { return refusal == Refusal::None; }
};
Scalars decode(Boundary, const Entry&, const StackBytes&, std::uintptr_t client_bias,
               std::uintptr_t receiver_bias, const Expected& expected = {}) noexcept;
} // namespace frame_client_native
