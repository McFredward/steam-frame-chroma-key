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
    static constexpr std::uint32_t Version = 2;
    static constexpr std::uint32_t DrawLimit = 32, ExecuteLimit = 16, SubmitLimit = 16, SnapshotLimit = 32, ProbeLimit = 4096;
    // Bit 0 reserves snapshots until import publication; bit 1 preserves the
    // lifecycle owner when optional trace output reaches its existing cap. Bit 2
    // requires exact imported image/view/memory generations at ordered replay;
    // the inspected descriptor subset remains set 0 / frame_bindings::TargetBindings.
    static constexpr std::uint32_t Flags = 7;
    Quota<ProbeLimit> probes;
    Quota<DrawLimit> draws;
    Quota<ExecuteLimit> executes;
    Quota<SubmitLimit> submits;
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Raw sampling must not introduce a render-thread lock");
} // namespace frame_compositor_sampling
