#include "compositor_main_preparation.h"

#include <cerrno>
#include <cstring>
#include <limits>

namespace frame_compositor_main {
namespace {
bool add(std::uintptr_t base, std::uintptr_t rva, std::uintptr_t& out) noexcept {
    if(rva > std::numeric_limits<std::uintptr_t>::max() - base)return false;
    out = base + rva; return true;
}
struct Discovery {
    std::uintptr_t main = 0;
    unsigned visits = 0;
    bool found = false, complete = true;
    static int visit(dl_phdr_info* info, std::size_t size, void* pointer) noexcept {
        auto& d = *static_cast<Discovery*>(pointer);
        if(++d.visits > 256 || !info || size < offsetof(dl_phdr_info,dlpi_phnum) + sizeof(info->dlpi_phnum) ||
           !info->dlpi_name || !info->dlpi_phdr || !info->dlpi_phnum || info->dlpi_phnum > 16) {
            d.complete = false; return 1;
        }
        if(!info->dlpi_name[0]) {
            if(d.found) { d.complete = false; return 1; }
            d.main = info->dlpi_addr; d.found = true;
        }
        return 0;
    }
};
bool canonical(const Profile& p, const Digest& review) noexcept {
    // Reuse the sole canonical fixed-layout decoder instead of a second hash
    // interpretation. Production cannot substitute a host fixture descriptor.
    frame_import_main::ProfileBlob blob{};
    std::memcpy(blob.data(), &p, blob.size());
    Profile decoded{};
    return frame_import_main::decode_profile(blob, review, decoded);
}
} // namespace
Prepared prepare(const Profiles& profiles) noexcept {
    Prepared out; const int saved = errno;
    auto finish = [&](Refusal why) noexcept {
        out.refusal_ = why;
        if(why != Refusal::None) {
            out.lookup_ = nullptr; out.iterate_ = nullptr;
            out.lock_ = out.unlock_ = out.write_ = 0;
            out.original_lock_ = out.original_unlock_ = out.original_write_ = 0;
        }
        errno = saved; return out;
    };
    constexpr std::uint64_t roles[]{7, 3, 4, 5};
    for(std::size_t i = 0; i < kModules; ++i)
        if(profiles.modules[i].role != roles[i] || !canonical(profiles.modules[i], i == 3 ? profiles.reviewed_observer_identity : Digest{}))
            return finish(Refusal::InvalidProfile);
    if(sizeof(void*) != 8)return finish(Refusal::UnsupportedPlatform);
    Discovery discovery;
    if(::dl_iterate_phdr(Discovery::visit, &discovery) != 0 || !discovery.complete || !discovery.found)
        return finish(Refusal::DiscoveryIncomplete);
    std::uintptr_t anchor = 0;
    if(!add(discovery.main, 0x153fc0, anchor))return finish(Refusal::WrongExecutable);
    const auto compositor = frame_import_main::verify_loaded(profiles.modules[0], {}, anchor);
    if(!compositor.valid())return finish(Refusal::WrongExecutable);
    out.images_[0] = compositor.image;
    const auto libc = frame_import_main::verify_loaded(profiles.modules[1], {}, reinterpret_cast<std::uintptr_t>(&::dl_iterate_phdr));
    if(!libc.valid())return finish(Refusal::ProviderMismatch);
    out.images_[1] = libc.image;
    auto provider = [&](const char* name, const char* version, std::uintptr_t rva) noexcept {
        const auto value = reinterpret_cast<std::uintptr_t>(::dlvsym(RTLD_DEFAULT, name, version));
        std::uintptr_t expected = 0;
        return add(libc.image.bias, rva, expected) && value == expected && value >= libc.image.rx_begin && value < libc.image.rx_end ? value : 0;
    };
    const auto iterate = provider("dl_iterate_phdr", "GLIBC_2.17", 0x134b20);
    const auto lookup = provider("dladdr", "GLIBC_2.34", 0x7f320);
    if(iterate != reinterpret_cast<std::uintptr_t>(&::dl_iterate_phdr) || lookup != reinterpret_cast<std::uintptr_t>(&::dladdr))
        return finish(Refusal::ProviderMismatch);
    out.lookup_ = reinterpret_cast<int (*)(const void*, Dl_info*)>(lookup);
    out.iterate_ = reinterpret_cast<frame_import_loader::Iterate>(iterate);
    out.lock_ = provider("pthread_mutex_lock", "GLIBC_2.17", 0x87370);
    out.unlock_ = provider("pthread_mutex_unlock", "GLIBC_2.17", 0x88f74);
    out.write_ = provider("write", "GLIBC_2.17", 0xdfee0);
    out.original_lock_ = provider("__pthread_mutex_lock", "GLIBC_2.17", 0x87370);
    out.original_unlock_ = provider("__pthread_mutex_unlock", "GLIBC_2.17", 0x88f74);
    out.original_write_ = provider("__write", "GLIBC_2.17", 0xdfee0);
    if(!out.lock_ || !out.unlock_ || !out.write_ || out.lock_ != out.original_lock_ ||
       out.unlock_ != out.original_unlock_ || out.write_ != out.original_write_)
        return finish(Refusal::ProviderMismatch);
    const auto tls = reinterpret_cast<std::uintptr_t>(::dlvsym(RTLD_DEFAULT, "__tls_get_addr", "GLIBC_2.17"));
    const auto loader = frame_import_main::verify_loaded(profiles.modules[2], {}, tls);
    const auto self = frame_import_main::verify_loaded(profiles.modules[3], profiles.reviewed_observer_identity,
                                                      reinterpret_cast<std::uintptr_t>(&prepare));
    std::uintptr_t expected_tls = 0;
    if(!loader.valid() || !add(loader.image.bias, 0x10180, expected_tls) || tls != expected_tls)
        return finish(Refusal::ProviderMismatch);
    if(!self.valid())return finish(Refusal::SelfUnavailable);
    out.images_[2] = loader.image; out.images_[3] = self.image;
    return finish(Refusal::None);
}
} // namespace frame_compositor_main
