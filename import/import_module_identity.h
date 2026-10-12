#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace frame_import_module {
constexpr std::size_t kMaxSegments = 8;
constexpr std::size_t kMaxModuleBytes = 8 * 1024 * 1024;
constexpr std::size_t kProfileBytes = 1064;
using Digest = std::array<std::uint8_t, 32>;
using ProfileBlob = std::array<std::uint8_t, kProfileBytes>;

// Offline expected metadata, not native code bytes or permission to read a process.
struct ProgramHeader {
    std::uint64_t type, flags, file_offset, virtual_address, physical_address;
    std::uint64_t file_size, memory_size, alignment;
};
struct Profile {
    std::uint64_t schema, file_size, phoff, phnum, build_note_rva, build_note_size;
    std::uint64_t build_id_size, caller_rva, machine;
    Digest file_digest, header_digest, phdr_digest, build_note_digest, code_digest;
    std::array<std::uint8_t, 32> build_id;
    std::array<ProgramHeader, kMaxSegments> segments;
    std::array<Digest, kMaxSegments> rx_digests;
    Digest fingerprint;
};
static_assert(std::is_trivially_copyable<Profile>::value, "Profile must remain owned scalar metadata");
static_assert(sizeof(Profile) == kProfileBytes && offsetof(Profile, fingerprint) == 1032,
              "Prepared profile layout changed");

struct ReadableRange {
    std::uintptr_t begin = 0, end = 0; // Half-open actual loader-owned readable mapping.
};
// PRECONDITION: the caller holds a continuous, immutable loader lifetime lease for
// the real module reported by the original public dladdr call. These ranges and
// read implementation must derive from that lease, never arbitrary native pointers.
// The reader returns true only after copying exactly count bytes; failed or partial
// acquisition returns false. It must not dereference an unchecked or mutable mapping.
// A nonzero lifetime label is only a local lease identifier, not an ownership proof.
// The startup gate must call this outside graphics callbacks on its guarded startup
// thread. This verifier performs no mapping discovery, file I/O, dlsym or locking.
struct ModuleRead {
    std::uintptr_t image_base = 0; // Original dladdr.dli_fbase, not an inferred latest module.
    std::uintptr_t caller_pc = 0; // Original observed caller PC.
    std::uint64_t module_lifetime_label = 0;
    std::array<ReadableRange, kMaxSegments> ranges{};
    std::size_t range_count = 0;
    bool (*read)(void*, std::uintptr_t, void*, std::size_t) noexcept = nullptr;
    void* context = nullptr;
};
enum class Refusal : std::uint8_t {
    None, ProfileIdentity, UnsupportedProfile, MissingLease, InvalidRanges,
    AddressOverflow, CallerMismatch, Unreadable, ReadFailed,
    HeaderIdentity, HeaderLayout, ProgramHeadersIdentity, ProgramHeadersLayout,
    BuildIdentity, CodeIdentity
};
struct Result {
    Refusal refusal = Refusal::UnsupportedProfile;
    std::uintptr_t load_bias = 0, code_begin = 0, code_end = 0;
    std::uint64_t module_lifetime_label = 0;
    Digest code_identity{};
    bool valid() const noexcept { return refusal == Refusal::None; }
    // Success is mapped-code identity under the external lease precondition only;
    // it proves no native object, source frame, cross-process namespace, UV or GPU work.
};
bool decode_profile(const ProfileBlob&, Profile&) noexcept;
Result verify_startup(const Profile&, const ModuleRead&) noexcept;
} // namespace frame_import_module
