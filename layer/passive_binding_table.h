#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace frame_bindings {

// Fixed storage and a separate half-full index keep lookups away from large
// descriptor/command payloads. A single owner supplies synchronization. Entries
// never move: generation-bearing references cannot be revived by compaction.
// Erasure uses backward shifting rather than tombstones, so lifetime churn does
// not turn successful or missing-key lookups into accumulated linear scans.
template<class T, std::size_t N> class ObjectTable {
    static_assert(N != 0 && (N & (N - 1)) == 0, "Object capacities must be powers of two");
    static_assert(N < std::numeric_limits<std::uint16_t>::max(), "Slot index exceeds its fixed representation");
    static constexpr std::size_t IndexCapacity = N * 2;
    static constexpr std::size_t Mask = IndexCapacity - 1;
public:
    ObjectTable() noexcept {
        for (std::size_t i = 0; i < N; ++i) free_[i] = static_cast<std::uint16_t>(N - 1 - i);
    }
    static std::size_t bucketFor(std::uint64_t device, std::uint64_t value) noexcept {
        auto key = value ^ (device * UINT64_C(0x9e3779b97f4a7c15));
        key ^= key >> 30; key *= UINT64_C(0xbf58476d1ce4e5b9);
        key ^= key >> 27; key *= UINT64_C(0x94d049bb133111eb);
        key ^= key >> 31;
        return static_cast<std::size_t>(key) & Mask;
    }
    T* find(std::uint64_t device, std::uint64_t value) noexcept {
        if (!value) return nullptr;
        const auto bucket = locate(device, value);
        if (bucket == IndexCapacity) return nullptr;
        auto& entry = entries_[index_[bucket] - 1];
        return entry.id.generation ? &entry : nullptr;
    }
    bool hasRoom() const noexcept { return available_ != 0; }
    // The caller rejects duplicate/zero keys and generation exhaustion before
    // insertion. Repeat these checks here so the index cannot be corrupted by a
    // mistaken direct caller; bounded exhaustion leaves all live entries intact.
    T* insert(std::uint64_t device, std::uint64_t value, std::uint64_t generation) noexcept {
        if (!value || !generation || !available_ || locate(device, value) != IndexCapacity) return nullptr;
        auto bucket = bucketFor(device, value);
        for (std::size_t steps = 0; steps < IndexCapacity && index_[bucket]; ++steps) bucket = (bucket + 1) & Mask;
        if (index_[bucket]) return nullptr;
        const auto slot = free_[--available_];
        auto& entry = entries_[slot]; entry = {};
        entry.device = device; entry.id = {value, generation};
        index_[bucket] = static_cast<std::uint16_t>(slot + 1);
        return &entry;
    }
    bool erase(std::uint64_t device, std::uint64_t value) noexcept {
        if (!value) return false;
        const auto bucket = locate(device, value);
        if (bucket == IndexCapacity) return false;
        eraseBucket(bucket);
        return true;
    }
    void eraseEntry(T& entry) noexcept { erase(entry.device, entry.id.handle); }
    template<class Predicate> void eraseIf(Predicate predicate) noexcept {
        for (auto& entry : entries_) if (entry.id.handle && predicate(entry)) eraseEntry(entry);
    }
    auto begin() noexcept { return entries_.begin(); }
    auto end() noexcept { return entries_.end(); }
private:
    std::size_t locate(std::uint64_t device, std::uint64_t value) const noexcept {
        auto bucket = bucketFor(device, value);
        // At most N occupied entries in 2N buckets guarantees an empty bucket.
        for (std::size_t steps = 0; steps < IndexCapacity; ++steps) {
            const auto encoded = index_[bucket];
            if (!encoded) return IndexCapacity;
            const auto& entry = entries_[encoded - 1];
            if (entry.device == device && entry.id.handle == value) return bucket;
            bucket = (bucket + 1) & Mask;
        }
        return IndexCapacity;
    }
    void eraseBucket(std::size_t gap) noexcept {
        const auto slot = static_cast<std::uint16_t>(index_[gap] - 1);
        auto next = (gap + 1) & Mask;
        for (std::size_t steps = 0; steps < IndexCapacity && index_[next]; ++steps) {
            const auto& entry = entries_[index_[next] - 1];
            const auto home = bucketFor(entry.device, entry.id.handle);
            if (((next - home) & Mask) >= ((gap - home) & Mask)) {
                index_[gap] = index_[next]; gap = next;
            }
            next = (next + 1) & Mask;
        }
        index_[gap] = 0;
        entries_[slot] = {};
        free_[available_++] = slot;
    }
    std::array<T, N> entries_{};
    std::array<std::uint16_t, IndexCapacity> index_{};
    std::array<std::uint16_t, N> free_{};
    std::size_t available_ = N;
};

} // namespace frame_bindings
