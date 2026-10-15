#pragma once
#include "pixel_capture_tracker.h"

namespace frame_pixel_selection {
// An API handle may name a different recording by the time submission occurs.
// Only the immutable pre-call identity of the recording that actually contains
// our GPU copy can complete it. Never substitute a later tracker candidate.
inline bool matchesSubmit(const frame_pixel_capture::Candidate& chosen,
                          const frame_pixel_capture::SubmitTicket& before) noexcept {
    return before.eligible && chosen.device && chosen.command && chosen.commandGeneration && chosen.serial &&
        before.device == chosen.device && before.command == chosen.command &&
        before.commandGeneration == chosen.commandGeneration && before.serial == chosen.serial &&
        before.queueFamily == chosen.queueFamily;
}
inline bool matchesRetirement(const frame_pixel_capture::Candidate& chosen,
                              const frame_pixel_capture::RetirementTicket& before) noexcept {
    return before.eligible && chosen.device && chosen.command && chosen.commandGeneration && chosen.pool && chosen.poolGeneration &&
        before.device == chosen.device && before.command == chosen.command &&
        before.commandGeneration == chosen.commandGeneration && before.pool == chosen.pool &&
        before.poolGeneration == chosen.poolGeneration;
}
} // namespace frame_pixel_selection
