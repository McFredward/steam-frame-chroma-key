#pragma once

#include "import_main_preparation.h"

// Cold MAIN-namespace preparation for the exact native compositor. This is a
// different bootstrap from the receiver: it never loads/initializes OpenVR or
// requires a receiver/client API. Loaded identity is a snapshot, not a lifetime
// lease; the selective audit must authenticate and retire its actual lifetimes.
namespace frame_compositor_main {
using Digest = frame_import_main::Digest;
using Profile = frame_import_main::Profile;
using Image = frame_import_main::Image;
using Refusal = frame_import_main::Refusal;
constexpr std::size_t kModules = 4;
struct Profiles {
    std::array<Profile, kModules> modules{}; // Compositor 7, libc 3, loader 4, observer 5.
    Digest reviewed_observer_identity{};
};
class Prepared final {
public:
    bool ready() const noexcept { return refusal_ == Refusal::None; }
    Refusal refusal() const noexcept { return refusal_; }
    std::array<Image, kModules> images() const noexcept { return images_; }
    int (*lookup() const noexcept)(const void*, Dl_info*) { return lookup_; }
    frame_import_loader::Iterate iterate() const noexcept { return iterate_; }
    std::uintptr_t native_lock() const noexcept { return lock_; }
    std::uintptr_t native_unlock() const noexcept { return unlock_; }
    std::uintptr_t native_write() const noexcept { return write_; }
    // Independently versioned distinct provider names, authenticated once.
    // They currently share their public counterpart's exact native address.
    std::uintptr_t original_lock() const noexcept { return original_lock_; }
    std::uintptr_t original_unlock() const noexcept { return original_unlock_; }
    std::uintptr_t original_write() const noexcept { return original_write_; }
private:
    friend Prepared prepare(const Profiles&) noexcept;
    Prepared() noexcept = default;
    Refusal refusal_ = Refusal::InvalidProfile;
    std::array<Image, kModules> images_{};
    int (*lookup_)(const void*, Dl_info*) = nullptr;
    frame_import_loader::Iterate iterate_ = nullptr;
    std::uintptr_t lock_ = 0, unlock_ = 0, write_ = 0;
    std::uintptr_t original_lock_ = 0, original_unlock_ = 0, original_write_ = 0;
};
// Constructor/preinit only, before selective redirection and native worker
// sampling. No forced load, factory call, environment input or hot resolution.
Prepared prepare(const Profiles&) noexcept;
} // namespace frame_compositor_main
