#pragma once

#include "../layer/passive_binding_journal.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

// CPU-only import receipts from the passive layer's existing owning Journal.
// Invocation labels, cookies and replay order are not native generations,
// accepted submissions, video-frame identities or GPU-completion evidence.
namespace frame_compositor_journal {
struct Receipt {
    frame_bindings::ImportResourceReceipt resource{};
    bool current_after_drain = false;
};
using Notify = void (*)(const Receipt&, void*) noexcept;
struct Callbacks { Notify notify = nullptr; void* context = nullptr; };
using SnapshotNotify = void (*)(std::uint32_t, const frame_bindings::Snapshot&, void*) noexcept;
struct Counters {
    std::uint64_t requested = 0, delivered = 0, invalid_after_drain = 0, refused = 0;
};

// The layer supplies exactly its existing Journal. This class creates no
// resource table, resource generation or second journal. Only background
// owners take owner_mutex_; public API hooks use request() alone.
class Owner {
public:
    explicit Owner(frame_bindings::Journal& journal) noexcept : journal_(journal) {}
    bool initialize_before_threads(bool enabled) noexcept;
    bool register_receipts_before_threads(Callbacks callbacks) noexcept;
    bool request(std::uint64_t device, VkImage image, VkImageView view,
                 std::uint64_t tag, std::uint64_t cookie) noexcept;
    // Background writer only. The typed drain callback only copies immutable
    // receipts. Currentness is checked after drain() returns, before delivery.
    // The notification may enqueue a fixed owned value, but must not reenter
    // validate(), drain() or stop() while this background-owner lock is held.
    std::size_t drain(SnapshotNotify, void* context, std::size_t max_records = 256) noexcept;
    // Other background consumer only, immediately before publishing a joined
    // diagnostic. Serializes against the journal owner; pending/unreplayed
    // lifecycles, concurrency, closure and retirement all fail closed.
    bool validate(const Receipt&) noexcept;
    // Background teardown only. On return no callback can retain/use context.
    void stop() noexcept;
    // pthread_atfork child only. No inherited owner mutex, allocation, I/O,
    // waiting or thread teardown; the parent address space is unchanged.
    void retire_child_atomically() noexcept;
    bool notification_loss() const noexcept { return lost_.load(std::memory_order_acquire); }
    Counters counters() const noexcept;
private:
    frame_bindings::Journal& journal_;
    std::mutex owner_mutex_;
    std::atomic<unsigned> initialization_{0}, registration_{0};
    std::atomic<bool> lost_{false}, stopped_{false}, child_retired_{false};
    Callbacks callbacks_{};
    std::array<Receipt, frame_bindings::ImportTagCapacity> pending_{};
    std::size_t pending_count_ = 0;
    std::atomic<std::uint64_t> requested_{0}, delivered_{0}, invalid_{0}, refused_{0};
    void refuse() noexcept;
};

// Combined layer/preload DSO only: the root constructor calls initialize
// before native MAIN, so no first graphics callback allocates/opens files or
// starts a thread. Registration also precedes native thread admission.
bool initialize_before_main() noexcept;
bool register_receipts_before_threads(Callbacks) noexcept;
bool request(std::uint64_t device, VkImage image, VkImageView view,
             std::uint64_t tag, std::uint64_t cookie) noexcept;
bool validate(const Receipt&) noexcept;
void stop() noexcept;
void retire_child_atomically() noexcept;
bool notification_loss() noexcept;
Counters counters() noexcept;
} // namespace frame_compositor_journal
