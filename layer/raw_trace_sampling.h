#pragma once

#include <atomic>
#include <cstdint>

// Raw compositor diagnosis only. Repeated command observations are deliberately
// sampled; omitted calls are not queue loss and cannot certify a complete draw
// history. The immutable lifecycle journal is never subject to these quotas.
namespace frame_compositor_sampling {
// Only the background owner issues time-labelled permits. A draw callback
// attempts one atomic consumption: no clock, allocation, wait or catch-up burst.
template<unsigned Credits> class PeriodicCredits {
    static_assert(Credits > 0 && Credits <= 32);
public:
    static constexpr std::uint64_t PeriodNs = 60000000, DurationNs = 120000000000;
    bool issue(std::uint64_t elapsed_ns) noexcept {
        if (elapsed_ns >= DurationNs) { close(); return false; }
        const auto period = elapsed_ns / PeriodNs + 1;
        auto state = state_.load(std::memory_order_acquire);
        while (state != Closed && period > (state >> 6)) {
            if (state_.compare_exchange_weak(state, (period << 6) | Credits,
                    std::memory_order_acq_rel, std::memory_order_acquire)) return true;
        }
        return false;
    }
    bool take(std::uint64_t& period) noexcept {
        auto state = state_.load(std::memory_order_acquire);
        if (state == Closed || !(state & 63)) return false;
        const auto issued = state;
        if (!state_.compare_exchange_strong(state, state - 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) return false;
        period = (issued >> 6) - 1;
        return true;
    }
    bool available() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
        return state != Closed && (state & 63);
    }
    void close() noexcept { state_.store(Closed, std::memory_order_release); }
private:
    static constexpr std::uint64_t Closed = UINT64_MAX;
    std::atomic<std::uint64_t> state_{0};
};
using ProbeSchedule = PeriodicCredits<1>;
using ProbeClassificationSchedule = PeriodicCredits<32>;
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
    static constexpr std::uint32_t Version = 3;
    static constexpr std::uint32_t DrawLimit = 32, ExecuteLimit = 16, SubmitLimit = 16, SnapshotLimit = 32, ProbeLimit = 4096;
    // Bit 0 reserves snapshots until import publication; bit 1 preserves the
    // lifecycle owner when optional trace output reaches its existing cap. Bit 2
    // requires exact imported image/view/memory generations at ordered replay;
    // the inspected descriptor subset remains set 0 / frame_bindings::TargetBindings.
    // Bits 3/4/5 authenticate typed public denials, time-spread permits and a
    // separate permit for the observed PS_DISTORT pipeline entry. Entry names
    // only select observations; they never prove source video or eye identity.
    static constexpr std::uint32_t Flags = 63, DenialSampleLimit = 16, DiagnosticsVersion = 1;
    ProbeSchedule general, distortion;
    ProbeClassificationSchedule classifications;
    Quota<ProbeLimit> probes;
    Quota<DrawLimit> draws;
    Quota<ExecuteLimit> executes;
    Quota<SubmitLimit> submits;
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Raw sampling must not introduce a render-thread lock");
} // namespace frame_compositor_sampling
