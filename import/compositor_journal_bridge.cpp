#include "compositor_journal_bridge.h"

namespace frame_compositor_journal {
static_assert(std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<bool>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::is_trivially_copyable_v<Receipt> && sizeof(Receipt) < 4096);

void Owner::refuse() noexcept {
    refused_.fetch_add(1, std::memory_order_relaxed);
    lost_.store(true, std::memory_order_release);
}
bool Owner::initialize_before_threads(bool enabled) noexcept {
    unsigned inactive = 0;
    if (!enabled || !initialization_.compare_exchange_strong(inactive, 1)) return false;
    initialization_.store(2, std::memory_order_release);
    return true;
}
bool Owner::register_receipts_before_threads(Callbacks callbacks) noexcept {
    if (initialization_.load(std::memory_order_acquire) != 2 ||
        stopped_.load(std::memory_order_acquire) || !callbacks.notify || !callbacks.context) return false;
    unsigned inactive = 0;
    if (!registration_.compare_exchange_strong(inactive, 1)) return false;
    callbacks_ = callbacks;
    registration_.store(2, std::memory_order_release);
    return true;
}
bool Owner::request(std::uint64_t device, VkImage image, VkImageView view,
                    std::uint64_t tag, std::uint64_t cookie) noexcept {
    if (initialization_.load(std::memory_order_acquire) != 2 ||
        registration_.load(std::memory_order_acquire) != 2 || stopped_.load(std::memory_order_acquire)) {
        refuse(); return false;
    }
    if (!journal_.requestImportResourceReceipt(device, image, view, tag, cookie)) {
        refuse(); return false;
    }
    requested_.fetch_add(1, std::memory_order_relaxed);
    return true;
}
std::size_t Owner::drain(SnapshotNotify snapshot, void* context, std::size_t max_records) noexcept {
    if (child_retired_.load(std::memory_order_acquire)) return 0;
    std::lock_guard<std::mutex> owner(owner_mutex_);
    struct Collector {
        Owner& bridge; SnapshotNotify snapshot; void* context;
        void operator()(std::uint32_t cookie, const frame_bindings::Snapshot& value) noexcept {
            if (snapshot) snapshot(cookie, value, context);
        }
        void operator()(std::uint64_t cookie, const frame_bindings::ImportResourceReceipt& value) noexcept {
            if (cookie != value.cookie || bridge.pending_count_ >= bridge.pending_.size()) {
                bridge.refuse(); return;
            }
            bridge.pending_[bridge.pending_count_++].resource = value;
        }
    };
    const auto drained = journal_.drain(Collector{*this, snapshot, context}, max_records);
    // A partial replay batch is not the current resource state. Keep the
    // bounded immutable copies until the owning journal catches up.
    if (!journal_.empty()) return drained;
    for (std::size_t i = 0; i < pending_count_; ++i) {
        auto& receipt = pending_[i];
        receipt.current_after_drain = journal_.validateImportResourceReceipt(receipt.resource);
        if (!receipt.current_after_drain) invalid_.fetch_add(1, std::memory_order_relaxed);
        if (registration_.load(std::memory_order_acquire) == 2 &&
            !stopped_.load(std::memory_order_acquire)) {
            callbacks_.notify(receipt, callbacks_.context);
            delivered_.fetch_add(1, std::memory_order_relaxed);
        } else refuse();
        receipt = {};
    }
    pending_count_ = 0;
    return drained;
}
bool Owner::validate(const Receipt& receipt) noexcept {
    if (child_retired_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> owner(owner_mutex_);
    return initialization_.load(std::memory_order_acquire) == 2 &&
        !stopped_.load(std::memory_order_acquire) && !lost_.load(std::memory_order_acquire) &&
        receipt.current_after_drain && journal_.validateImportResourceReceipt(receipt.resource);
}
void Owner::stop() noexcept {
    if (child_retired_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> owner(owner_mutex_);
    stopped_.store(true, std::memory_order_release);
    registration_.store(0, std::memory_order_release);
    callbacks_ = {};
}
void Owner::retire_child_atomically() noexcept {
    child_retired_.store(true, std::memory_order_release);
    stopped_.store(true, std::memory_order_release);
    registration_.store(0, std::memory_order_release);
}
Counters Owner::counters() const noexcept {
    return {requested_.load(std::memory_order_relaxed), delivered_.load(std::memory_order_relaxed),
            invalid_.load(std::memory_order_relaxed), refused_.load(std::memory_order_relaxed)};
}
} // namespace frame_compositor_journal
