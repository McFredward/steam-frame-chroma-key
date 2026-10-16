#pragma once
#include <atomic>

namespace frame_pixel_observation {
// Contain optional cold-observer failures at Vulkan's C ABI. Once any callback
// is missed, the entire history is ineligible. This is not a synchronization or
// native-object lease; callbacks already in flight may still complete.
class Boundary {
public:
    template<class F> void observe(F&& action) noexcept {
        if(failed())return;
        try { action(); }
        catch(...) { failed_.store(true,std::memory_order_release); }
    }
    bool failed() const noexcept{return failed_.load(std::memory_order_acquire);}
private:
    std::atomic<bool> failed_{false};
};
} // namespace frame_pixel_observation
