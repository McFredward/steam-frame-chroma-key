#pragma once

// Formatting runs only on the diagnostic background writer. The first-failure
// snapshot owns fixed counters and static API-site labels; no native strings,
// handles, pixels or per-frame event stream are included in this report.
#include "pixel_capture_tracker.h"
#include <cstddef>
#include <cstdio>

namespace frame_pixel_status {

constexpr std::size_t MaximumRefusalBytes = 2048;

inline const char* failureName(frame_pixel_capture::Failure failure) noexcept {
    using frame_pixel_capture::Failure;
    switch (failure) {
    case Failure::None: return "none";
    case Failure::Contention: return "contention";
    case Failure::Capacity: return "capacity";
    case Failure::CoreLoss: return "core_loss";
    case Failure::Unknown: return "unknown";
    case Failure::GenerationWrap: return "generation_wrap";
    }
    return "invalid";
}

// Only bounded, compiler-generated API labels may enter JSON. Reject an invalid
// label wholesale rather than attempting to escape arbitrary process strings.
inline const char* siteName(const char* site) noexcept {
    if (!site || !*site) return "unknown_site";
    for (std::size_t i = 0; i < 80; ++i) {
        const char c = site[i];
        if (!c) return site;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return "invalid_site";
    }
    return "invalid_site";
}

namespace detail {
struct Writer {
    char* output;
    std::size_t capacity, used = 0;
    bool valid = true;
    template<class... Args> void append(const char* format, Args... args) noexcept {
        if (!valid) return;
        int count;
        if constexpr (sizeof...(Args) == 0)
            count = std::snprintf(output + used, capacity - used, "%s", format);
        else
            count = std::snprintf(output + used, capacity - used, format, args...);
        if (count < 0 || static_cast<std::size_t>(count) >= capacity - used) {
            valid = false;
            return;
        }
        used += static_cast<std::size_t>(count);
    }
};
} // namespace detail

// Return zero until the frozen snapshot is published, or if the caller cannot
// hold the complete bounded record. Never emit a partial JSON line as evidence.
inline std::size_t trackingRefusal(char* output, std::size_t capacity,
                                  const frame_pixel_capture::FailureSnapshot& snapshot,
                                  bool selected, bool armed) noexcept {
    if (!output || !capacity || capacity > MaximumRefusalBytes || !snapshot.complete ||
        snapshot.failure == frame_pixel_capture::Failure::None) return 0;
    static constexpr const char* reasons[]{"none", "capacity", "contention", "unknown",
        "unsupported", "stale", "bounds", "layout_mismatch"};
    static constexpr const char* tables[]{"set_layout", "pipeline_layout", "pool", "set",
        "buffer", "image", "view", "sampler", "memory", "command", "conversion"};
    static_assert(std::size(reasons) == frame_pixel_capture::CoreReasonCount);
    static_assert(std::size(tables) == frame_pixel_capture::CoreTableCount);
    detail::Writer writer{output, capacity};
    writer.append("{\"event\":\"tracking_refused\",\"failure_code\":%u,\"failure\":\"%s\",\"site\":\"%s\","
                  "\"core_lost_state\":%llu,\"core_reason_counts\":{",
                  static_cast<unsigned>(snapshot.failure), failureName(snapshot.failure), siteName(snapshot.site),
                  static_cast<unsigned long long>(snapshot.coreLostState));
    for (std::size_t i = 0; i < std::size(reasons); ++i)
        writer.append("%s\"%s\":%llu", i ? "," : "", reasons[i],
                      static_cast<unsigned long long>(snapshot.coreReasonCounts[i]));
    writer.append("},\"core_capacity_counts\":{");
    for (std::size_t i = 0; i < std::size(tables); ++i)
        writer.append("%s\"%s\":%llu", i ? "," : "", tables[i],
                      static_cast<unsigned long long>(snapshot.coreCapacityCounts[i]));
    writer.append("},\"armed\":%s,\"selected\":%s,"
                  "\"mask_enabled\":false,\"native_frame_association_proven\":false}\n",
                  armed ? "true" : "false", selected ? "true" : "false");
    if (!writer.valid) { output[0] = 0; return 0; }
    return writer.used;
}

} // namespace frame_pixel_status
