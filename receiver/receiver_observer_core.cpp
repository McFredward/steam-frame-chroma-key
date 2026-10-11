#include "receiver_observer_core.h"

#include <cstring>

namespace frame_receiver {
namespace {
constexpr std::uintptr_t kMaxUserPointer = (std::uintptr_t{1} << 48) - 1;

template <typename T>
T scalar(std::uintptr_t address) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

bool equal(const Tuple& a, const Tuple& b) noexcept {
    return a.token == b.token && a.key == b.key &&
           a.content_generation == b.content_generation;
}
} // namespace

bool Observer::selected_address(std::uintptr_t mutex) noexcept {
    return mutex >= 0x18 && mutex <= kMaxUserPointer - 7 && mutex % 8 == 0;
}

bool Observer::metadata_address(std::uintptr_t mutex) noexcept {
    return mutex >= 0x4240 && mutex <= kMaxUserPointer - 0x33 && mutex % 8 == 0;
}

bool Observer::read_tuple(std::uintptr_t mutex, Tuple& tuple) noexcept {
    if (!selected_address(mutex)) return false;
    tuple.token = scalar<std::uint64_t>(mutex - 0x18);
    tuple.key = scalar<std::uint32_t>(mutex - 0x10);
    tuple.content_generation = scalar<std::uint64_t>(mutex - 0x08);
    return true;
}

void Observer::after_lock(std::uint64_t site, std::uintptr_t mutex, int result,
                          Scope& scope) noexcept {
    if (!armed.load(std::memory_order_seq_cst) && !scope.active) return;
    if (site != kSelectedLock && site != kMetadataLock) return;
    if (site == kMetadataLock && !scope.active) return;
    if (result != 0) {
        counters.lock_failures.fetch_add(1, std::memory_order_relaxed);
        if (scope.active) scope.record.reason |= LockFailure;
        return;
    }
    if (site == kSelectedLock) {
        if (scope.active) {
            ++scope.selected_depth;
            scope.record.reason |= NestedSelection;
            counters.nested.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!armed.load(std::memory_order_seq_cst)) return;
        const auto ticket = counters.claimed.fetch_add(1, std::memory_order_relaxed);
        if (ticket >= kSampleLimit) {
            counters.cap_skipped.fetch_add(1, std::memory_order_relaxed);
            scope.excluded = true;
            return;
        }
        scope = Scope{};
        scope.active = true;
        scope.selected_depth = 1;
        scope.selected_mutex = mutex;
        scope.record.observation = ticket + 1;
        counters.active.fetch_add(1, std::memory_order_seq_cst);
        if (!read_tuple(mutex, scope.record.selected)) {
            scope.record.reason |= InvalidPointer;
            counters.pointer_rejections.fetch_add(1, std::memory_order_relaxed);
        } else if (!scope.record.selected.token || !scope.record.selected.content_generation ||
                   scope.record.selected.key > 0xffff) {
            scope.record.reason |= InvalidTuple;
        }
        return;
    }
    if (!scope.active) return; // An ordinary lookup is not selected evidence.
    if (scope.metadata_depth) {
        ++scope.metadata_depth;
        scope.record.reason |= NestedMetadata;
        counters.nested.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    scope.metadata_depth = 1;
    scope.metadata_mutex = mutex;
    if (!metadata_address(mutex)) {
        scope.record.reason |= InvalidPointer;
        counters.pointer_rejections.fetch_add(1, std::memory_order_relaxed);
    }
}

void Observer::before_unlock(std::uint64_t site, std::uintptr_t mutex,
                             Scope& scope) noexcept {
    if (scope.excluded) {
        if (site == kSelectedUnlock) scope = Scope{};
        return;
    }
    if (!armed.load(std::memory_order_seq_cst) && !scope.active) return;
    if (site != kSelectedUnlock && site != kMetadataUnlock) return;
    if (!scope.active) {
        if (site == kMetadataUnlock) return;
        counters.unmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (site == kMetadataUnlock) {
        if (!scope.metadata_depth) {
            scope.record.reason |= UnmatchedUnlock;
            counters.unmatched.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (--scope.metadata_depth) return;
        ++scope.record.metadata_pairs;
        if (mutex != scope.metadata_mutex) {
            scope.record.reason |= UnexpectedMutex;
        } else if (metadata_address(mutex) && scope.record.reason == None) {
            const auto valid = scalar<std::uint8_t>(mutex - 0x423c);
            const auto key = scalar<std::uint32_t>(mutex - 0x4240);
            if (!valid) scope.record.reason |= InvalidMetadata;
            else if (key != scope.record.selected.key) scope.record.reason |= KeyMismatch;
            else {
                scope.record.metadata_word = scalar<std::uint32_t>(mutex + 0x30);
                scope.record.metadata_valid = true;
            }
        }
        scope.metadata_mutex = 0;
        if (scope.record.metadata_pairs > 1) scope.record.reason |= MultipleMetadata;
        return;
    }
    if (--scope.selected_depth) return;
    if (mutex != scope.selected_mutex) scope.record.reason |= UnexpectedMutex;
    if (scope.metadata_depth) scope.record.reason |= IncompleteMetadata;
    if (!scope.record.metadata_pairs) scope.record.reason |= MissingMetadata;
    if (scope.record.metadata_pairs != 1 || !scope.record.metadata_valid)
        scope.record.metadata_valid = false;
    Tuple end{};
    if (mutex == scope.selected_mutex && read_tuple(mutex, end) &&
        !equal(scope.record.selected, end)) scope.record.reason |= ChangedTuple;
    if (scope.record.reason != None) scope.record.metadata_valid = false;
    publish(scope.record);
    counters.completed.fetch_add(1, std::memory_order_release);
    (scope.record.reason == None ? counters.valid : counters.invalid)
        .fetch_add(1, std::memory_order_relaxed);
    scope = Scope{};
    counters.active.fetch_sub(1, std::memory_order_seq_cst);
}

bool Observer::quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept {
    // Admissions have already been closed with armed.store(false, seq_cst).
    // Existing scopes can start a closing callback between the two reads. The
    // second read excludes that callback unless it already finished forwarding.
    const auto before=callbacks.load(std::memory_order_seq_cst);
    const auto active=counters.active.load(std::memory_order_seq_cst);
    const auto after=callbacks.load(std::memory_order_seq_cst);
    return !before && !active && !after;
}

bool Observer::publish(const Record& record) noexcept {
    // Bounded attempts, no waiting for another producer or for the recorder.
    for (auto& slot : slots_) {
        unsigned free = 0;
        if (!slot.state.compare_exchange_strong(free, 1, std::memory_order_acquire,
                                                std::memory_order_relaxed)) continue;
        slot.value = record;
        slot.state.store(2, std::memory_order_release);
        return true;
    }
    counters.queue_loss.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool Observer::take(Record& record) noexcept {
    for (auto& slot : slots_) {
        unsigned ready = 2;
        if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire,
                                                std::memory_order_relaxed)) continue;
        record = slot.value;
        slot.state.store(0, std::memory_order_release);
        return true;
    }
    return false;
}

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Receiver callbacks require lock-free counters");
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "Receiver callbacks require lock-free publication");
} // namespace frame_receiver
