#pragma once

#include <atomic>
#include <cstdint>

// Raw compositor diagnosis only. Repeated command observations are deliberately
// sampled; omitted calls are not queue loss and cannot certify a complete draw
// history. The immutable lifecycle journal is never subject to these quotas.
namespace frame_compositor_sampling {
template<std::uint32_t Limit> class Quota {
public:
    bool take() noexcept {
        return attempts_.fetch_add(1, std::memory_order_relaxed) < Limit;
    }
    std::uint64_t sampled() const noexcept {
        const auto count = attempts_.load(std::memory_order_relaxed);
        return count < Limit ? count : Limit;
    }
    std::uint64_t omitted() const noexcept {
        const auto count = attempts_.load(std::memory_order_relaxed);
        return count > Limit ? count - Limit : 0;
    }
private:
    std::atomic<std::uint64_t> attempts_{0};
};
struct Budget {
    static constexpr std::uint32_t Version = 1;
    static constexpr std::uint32_t DrawLimit = 32, ExecuteLimit = 16, SubmitLimit = 16, SnapshotLimit = 32;
    // Bit 0 reserves snapshots until import publication; bit 1 preserves the
    // lifecycle owner when optional trace output reaches its existing cap.
    static constexpr std::uint32_t Flags = 3;
    Quota<DrawLimit> draws;
    Quota<ExecuteLimit> executes;
    Quota<SubmitLimit> submits;
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Raw sampling must not introduce a render-thread lock");
} // namespace frame_compositor_sampling
