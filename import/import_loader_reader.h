#pragma once
#include "import_module_identity.h"
#include <dlfcn.h>
#include <link.h>

namespace frame_import_loader {
constexpr std::size_t kColdProgramHeaders = 16;
constexpr std::size_t kColdMappedBytes = 16 * 1024 * 1024;
using Iterate = int (*)(int (*)(dl_phdr_info*, std::size_t, void*), void*);
enum class Refusal : unsigned {
    None, Unprepared, WrongThread, Repeated, Recursion, InvalidTarget, NotFound,
    EnumerationLimit, InvalidMetadata, UnsupportedLayout, CallerOutsideCode,
    NativeEnumeration, ModuleIdentity
};
namespace detail { struct Context; }
// Borrowed only inside the public glibc iterator callback. Its ranges are copied
// from that actual loader-owned object. It exposes no unchecked pointer/reader.
class ScopedModule final {
public:
    ScopedModule(const ScopedModule&) = delete;
    ScopedModule& operator=(const ScopedModule&) = delete;
    std::uintptr_t base() const noexcept { return read_.image_base; }
    std::uintptr_t code_begin() const noexcept { return code_begin_; }
    std::uintptr_t code_end() const noexcept { return code_end_; }
    std::size_t program_header_count() const noexcept { return phnum_; }
    bool program_header(std::size_t index,frame_import_module::ProgramHeader&) const noexcept;
    bool copy(std::uintptr_t address, void* destination, std::size_t count) const noexcept;
private:
    friend struct detail::Context;
    frame_import_module::ModuleRead read_{};
    std::array<frame_import_module::ProgramHeader, kColdProgramHeaders> phdrs_{};
    std::size_t phnum_ = 0;
    std::uintptr_t code_begin_ = 0, code_end_ = 0;
    bool active_ = false;
    ScopedModule() noexcept = default;
    static bool read(void*,std::uintptr_t,void*,std::size_t) noexcept;
};
using Visitor = void (*)(const ScopedModule&,void*) noexcept;
struct Result {
    Refusal refusal = Refusal::Unprepared;
    frame_import_module::Result module{};
    unsigned visited = 0;
    bool valid() const noexcept { return refusal == Refusal::None && module.valid(); }
};
// Construct once on the guarded startup thread, with the real non-interposed
// public glibc dl_iterate_phdr provider resolved/identity-checked at preflight.
// Provider provenance is root bootstrap's existing public-forwarder obligation;
// an arbitrary injected function is not a loader lease. No hot-path resolution.
class Reader final {
public:
    Reader(const frame_import_module::Profile&, Iterate, std::uint64_t local_lifetime_label) noexcept;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Result verify(const Dl_info& original_dladdr, std::uintptr_t original_caller_pc) noexcept;
    // Startup-only scoped inspection uses the same range construction for loader
    // lifecycle tests. Never retain the borrowed module or call loader/mapping
    // APIs, user code, cancellation points, I/O, allocation or blocking work from
    // this visitor. Production verify uses only its fixed internal hash consumer.
    Refusal inspect(const Dl_info&,std::uintptr_t,Visitor,void*) noexcept;
    // Cold preflight only: public PHDR copies plus readonly file-backed bytes,
    // at most 16 headers/16 MiB virtual span/8 MiB readable file bytes. This
    // does not widen the strict client verifier's eight-header/RX-only scope.
    Refusal inspect_cold(const Dl_info&,std::uintptr_t,Visitor,void*) noexcept;
private:
    const frame_import_module::Profile profile_;
    const Iterate iterate_;
    const std::uint64_t label_;
    const std::uintptr_t owner_;
    bool consumed_ = false;
    Result run(const Dl_info&,std::uintptr_t,Visitor,void*,bool,bool = false) noexcept;
};
} // namespace frame_import_loader
