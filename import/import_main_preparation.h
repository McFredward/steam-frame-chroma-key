#pragma once
#include "import_loader_reader.h"

namespace frame_import_main {
constexpr std::size_t kProfileBytes=2032,kModules=5,kHeaders=16,kNotes=4;
using Digest=frame_import_module::Digest;
using ProfileBlob=std::array<std::uint8_t,kProfileBytes>;
enum class Role : std::uint64_t { Receiver=1, Api=2, Libc=3, Loader=4, Observer=5, Fixture=6, Compositor=7 };
struct Note { std::uint64_t rva=0,size=0; Digest digest{}; };
struct Profile {
    std::uint64_t schema=0,role=0,machine=0,elf_type=0,file_size=0,phoff=0,phnum=0,note_count=0,build_id_size=0,mapped_span=0;
    Digest header_digest{},phdr_digest{},code_digest{},file_digest{};
    std::array<std::uint8_t,64> build_id{};
    std::array<frame_import_module::ProgramHeader,kHeaders> headers{};
    std::array<Digest,kHeaders> rx_digests{};
    std::array<Note,kNotes> notes{};
    Digest fingerprint{};
};
static_assert(sizeof(Profile)==kProfileBytes && offsetof(Profile,fingerprint)==2000,"Cold descriptor layout changed");
// Native roles have separately pinned complete descriptor hashes. Observer and
// fixture descriptors require an independently reviewed companion identity;
// their self-stored fingerprint is never sufficient. No native bytes are emitted.
bool decode_profile(const ProfileBlob&,const Digest& reviewed_identity,Profile&) noexcept;
Digest pinned_identity(Role) noexcept;
struct Profiles {
    std::array<Profile,kModules> modules{}; // Receiver, API, libc, loader, observer.
    Digest reviewed_observer_identity{}; // Reviewed own build descriptor, not a producer boolean.
};
enum class Refusal : unsigned {
    None, InvalidProfile, Lookup, LoaderLease, Metadata, MappedIdentity,
    UnsupportedPlatform, WrongExecutable, MissingApi, ProviderMismatch,
    ClientAlreadyPresent, DiscoveryIncomplete, SelfUnavailable
};
struct Image {
    std::uintptr_t bias=0,rx_begin=0,rx_end=0;
    Role role=Role::Fixture;
    Digest mapped_code_identity{},expected_file_identity{},profile_identity{};
    // This snapshot carries no la_objopen generation. Root must match actual
    // audit lifetime/geometry and retire it on close; never mint a generation
    // from ordinal/hash/address or retain this callback's loader lease.
};
struct Proof { Refusal refusal=Refusal::InvalidProfile; Image image{}; bool valid() const noexcept { return refusal==Refusal::None; } };
// Real public bootstrap functions, already linked in this MAIN namespace, are
// the initial trusted glibc ABI. They are never caller-injected fixture providers.
// This cold helper then authenticates mapped metadata/code under the real loader
// callback. Fixture proofs cannot populate Ready production preparation.
Proof verify_loaded(const Profile&,const Digest& reviewed_identity,std::uintptr_t actual_anchor) noexcept;
class Prepared final {
public:
    bool ready() const noexcept { return refusal_==Refusal::None; }
    Refusal refusal() const noexcept { return refusal_; }
    std::array<Image,kModules> images() const noexcept { return images_; }
    int (*lookup() const noexcept)(const void*,Dl_info*) { return lookup_; }
    frame_import_loader::Iterate iterate() const noexcept { return iterate_; }
    std::uintptr_t native_init() const noexcept { return native_init_; }
    std::uintptr_t native_shutdown() const noexcept { return native_shutdown_; }
    bool client_present() const noexcept { return client_present_; }
private:
    friend Prepared prepare(const Profiles&) noexcept;
    Prepared() noexcept = default;
    Refusal refusal_=Refusal::InvalidProfile;
    std::array<Image,kModules> images_{};
    int (*lookup_)(const void*,Dl_info*)=nullptr;
    frame_import_loader::Iterate iterate_=nullptr;
    std::uintptr_t native_init_=0,native_shutdown_=0;
    bool client_present_=false;
};
// Main observer constructor/preinit only, outside native/graphics callbacks and
// before selective audit redirects are enabled. Never calls dlopen, Init,
// Shutdown or a factory; never forces the API/client to load. Resolves already
// present public providers once and verifies their actual source RVAs/mappings.
// Missing API, wrong executable and unsupported platform remain harmlessly inert.
Prepared prepare(const Profiles&) noexcept;
} // namespace frame_import_main
