#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace frame_receiver {

constexpr std::size_t kSampleLimit = 32;
constexpr std::size_t kQueueCapacity = 32;
// Direct caller returns in the exact receiver pinned by receiver_observer.cpp.
// Selection and metadata moved by different amounts in SteamOS 0.4.5.
constexpr std::uint64_t kSelectedLock = 0x355f8;
constexpr std::uint64_t kSelectedUnlock = 0x3591c;
constexpr std::uint64_t kMetadataLock = 0x538dc;
constexpr std::uint64_t kMetadataUnlock = 0x5390c;

enum Reason : std::uint32_t {
    None = 0, InvalidPointer = 1, InvalidTuple = 2, NestedSelection = 4,
    UnmatchedUnlock = 8, NestedMetadata = 16, MissingMetadata = 32,
    InvalidMetadata = 64, KeyMismatch = 128, ChangedTuple = 256,
    LockFailure = 512, MultipleMetadata = 1024, IncompleteMetadata = 2048,
    UnexpectedMutex = 4096
};

struct Tuple {
    std::uint64_t token = 0;
    std::uint32_t key = 0;
    std::uint64_t content_generation = 0;
};

struct Record {
    std::uint64_t observation = 0;
    Tuple selected{};
    std::uint32_t metadata_word = 0;
    std::uint32_t reason = None;
    std::uint32_t metadata_pairs = 0;
    bool metadata_valid = false;
};

struct Scope {
    std::uintptr_t selected_mutex = 0;
    std::uintptr_t metadata_mutex = 0;
    Record record{};
    std::uint32_t selected_depth = 0;
    std::uint32_t metadata_depth = 0;
    bool active = false;
    bool excluded = false;
};

struct Counters {
    std::atomic<std::uint64_t> claimed{0}, completed{0}, valid{0}, invalid{0};
    std::atomic<std::uint64_t> active{0}, cap_skipped{0}, lock_failures{0};
    std::atomic<std::uint64_t> nested{0}, unmatched{0}, pointer_rejections{0};
    std::atomic<std::uint64_t> queue_loss{0};
};

// Only exact, prevalidated caller sites are passed to this engine. Pointer reads
// occur under the native mutex. Address checks are bounds checks, not a claim
// that arbitrary caller-supplied memory is safe to dereference.
class Observer {
public:
    std::atomic<bool> armed{false};
    Counters counters{};
    void after_lock(std::uint64_t site, std::uintptr_t mutex, int result,
                    Scope& scope) noexcept;
    void before_unlock(std::uint64_t site, std::uintptr_t mutex,
                       Scope& scope) noexcept;
    bool take(Record& record) noexcept;
    bool publish(const Record& record) noexcept;
    bool quiescent(const std::atomic<std::uint64_t>& callbacks) const noexcept;
    static bool selected_address(std::uintptr_t mutex) noexcept;
    static bool metadata_address(std::uintptr_t mutex) noexcept;
private:
    struct Slot {
        std::atomic<unsigned> state{0}; // free, writing, ready, reading
        Record value{};
    };
    std::array<Slot, kQueueCapacity> slots_{};
    static bool read_tuple(std::uintptr_t mutex, Tuple& tuple) noexcept;
};

} // namespace frame_receiver
