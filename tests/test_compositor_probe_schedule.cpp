#include "layer/raw_trace_sampling.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using frame_compositor_sampling::ProbeSchedule;
unsigned checks = 0;
void require(bool value) { ++checks; if (!value) std::abort(); }
int main() {
    ProbeSchedule schedule;
    std::uint64_t period = UINT64_MAX;
    require(!schedule.take(period) && period == UINT64_MAX);
    require(schedule.issue(0) && schedule.take(period) && period == 0);
    for (unsigned i = 0; i < 10000; ++i) {
        require(!schedule.issue(ProbeSchedule::PeriodNs - 1));
        require(!schedule.take(period));
    }
    require(schedule.issue(ProbeSchedule::PeriodNs));
    require(!schedule.issue(0)); // A delayed clock sample cannot refill an old period.
    std::atomic<unsigned> winners{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 16; ++i) threads.emplace_back([&] {
        std::uint64_t observed = UINT64_MAX;
        if (schedule.take(observed)) {
            if (observed != 1) std::abort();
            ++winners;
        }
    });
    for (auto& thread : threads) thread.join();
    require(winners == 1);
    require(schedule.issue(100 * ProbeSchedule::PeriodNs));
    require(schedule.take(period) && period == 100);
    require(!schedule.take(period)); // Skipped periods do not produce a catch-up burst.
    schedule.close();
    require(!schedule.issue(101 * ProbeSchedule::PeriodNs) && !schedule.take(period));

    ProbeSchedule general, distortion;
    unsigned issued = 0;
    for (std::uint64_t now = 0; now < ProbeSchedule::DurationNs; now += ProbeSchedule::PeriodNs) {
        require(general.issue(now) && distortion.issue(now));
        require(general.take(period)); ++issued;
        require(distortion.take(period)); ++issued;
        require(!general.take(period) && !distortion.take(period));
    }
    require(issued == 4000 && issued < frame_compositor_sampling::Budget::ProbeLimit);
    require(!general.issue(ProbeSchedule::DurationNs) && !general.take(period));
    require(!general.issue(0)); // Expiration is permanent, not another window.
    std::cout << "bounded compositor probe schedule: " << checks << " checks passed\n";
}
