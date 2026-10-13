#include "import/compositor_journal_bridge.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>

namespace {
thread_local bool inHook = false;
std::atomic<std::uint64_t> hookAllocations{0};
std::uint64_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
#if defined(__GNUC__) || defined(__clang__)
#define RECEIPT_NOINLINE __attribute__((noinline))
#else
#define RECEIPT_NOINLINE
#endif
RECEIPT_NOINLINE void* operator new(std::size_t n) {
    if (inHook) hookAllocations.fetch_add(1);
    if (auto* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
RECEIPT_NOINLINE void* operator new[](std::size_t n) { return ::operator new(n); }
RECEIPT_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
RECEIPT_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
RECEIPT_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
RECEIPT_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#undef RECEIPT_NOINLINE

namespace frame_bindings {
struct JournalTestAccess {
    static void delayed(Journal& journal, std::atomic<bool>& entered, std::atomic<bool>& release) {
        journal.publish(Journal::Op::Snapshot, 1, [&](Journal::Record& record) {
            record.payload.snapshot = {};
            entered.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    }
};
}
namespace {
using namespace frame_compositor_journal;
using namespace frame_bindings;
template<class T> T handle(std::uint64_t n) { return (T)(std::uintptr_t)n; }
template<class T> T info(VkStructureType kind) { T value{}; value.sType = kind; return value; }
struct Results {
    std::array<Receipt, ImportTagCapacity> receipts{};
    std::size_t count = 0, snapshots = 0;
    static void receive(const Receipt& value, void* context) noexcept {
        auto& out = *static_cast<Results*>(context);
        require(out.count < out.receipts.size(), "unbounded receipt callback");
        out.receipts[out.count++] = value;
    }
    static void snapshot(std::uint32_t, const Snapshot&, void* context) noexcept {
        ++static_cast<Results*>(context)->snapshots;
    }
};
struct Fixture {
    std::unique_ptr<Journal> journal = std::make_unique<Journal>();
    Owner bridge{*journal};
    Results results;
    const std::uint64_t device = 71, tag = UINT64_C(0x880000001), cookie = UINT64_C(0x990000002);
    VkImage image = handle<VkImage>(10);
    VkDeviceMemory memory = handle<VkDeviceMemory>(20);
    VkImageView view = handle<VkImageView>(30);
    VkImageCreateInfo imageInfo = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    VkExternalMemoryImageCreateInfo external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    VkImportMemoryFdInfoKHR imported = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    VkMemoryDedicatedAllocateInfo dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    VkMemoryAllocateInfo memoryInfo = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    VkImageViewCreateInfo viewInfo = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    Fixture() {
        imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {1152, 4608, 1}; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        imageInfo.pNext = &external;
        imported.fd = 19; imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        dedicated.image = image; imported.pNext = &dedicated;
        memoryInfo.allocationSize = 32 * 1024 * 1024; memoryInfo.memoryTypeIndex = 2; memoryInfo.pNext = &imported;
        viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = imageInfo.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        require(bridge.initialize_before_threads(true), "owning bridge initialization failed");
        require(bridge.register_receipts_before_threads({Results::receive, &results}), "owning callback registration failed");
    }
    void create(bool tagged = true) {
        if (tagged) require(journal->createImageTagged(device, image, imageInfo, tag), "tagged image failed");
        else journal->createImage(device, image, imageInfo);
        journal->allocateMemory(device, memory, memoryInfo);
        journal->bindImageMemory(device, image, memory, 0);
        if (tagged) require(journal->createViewTagged(device, view, viewInfo, tag), "tagged view failed");
        else journal->createView(device, view, viewInfo);
    }
    void request() { require(bridge.request(device, image, view, tag, cookie), "actual receipt request failed"); }
    void drain() { while (bridge.drain(Results::snapshot, &results)) {} }
};
void denyProof(const Receipt& value) {
    const auto& r = value.resource;
    require(!r.nativeImportAccepted && !r.sourceFrameAssociated && !r.nativeCrossProcessNamespace &&
            !r.acceptedSubmit && !r.gpuConsumedCoherent, "CPU receipt promoted unproved identity/GPU flags");
}
void realJournalSuccess() {
    Fixture f;
    inHook = true; f.create(); f.request(); inHook = false;
    require(!hookAllocations.load(), "hook allocated or created an independent resource cache");
    require(!f.results.count, "request synchronously replayed resource metadata");
    f.external.handleTypes = 0; f.imported.fd = 999; f.viewInfo.image = handle<VkImage>(999);
    f.drain();
    require(f.results.count == 1, "actual owning journal did not deliver one receipt");
    const auto& r = f.results.receipts[0];
    require(r.current_after_drain && r.resource.resourceComplete && f.bridge.validate(r),
            "complete actual lifecycle could not be revalidated after drainage");
    require(r.resource.cookie == f.cookie && r.resource.invocationTag == f.tag &&
            r.resource.imageInvocationTag == f.tag && r.resource.viewInvocationTag == f.tag,
            "receipt association truncated or replaced actual creation tag/cookie");
    require(r.resource.journalInstance && r.resource.localReplayOrder == 5 &&
            r.resource.image.identity.generation == 1 && r.resource.memory.identity.generation == 2 &&
            r.resource.view.identity.generation == 3 && r.resource.resourceGenerationCursor == 3 &&
            r.resource.resourceGenerationCursor != f.tag,
            "invocation/cookie/local order substituted for actual Journal generations");
    require(r.resource.memory.extensions.inputFD == 19 && r.resource.image.extent.width == 1152 &&
            r.resource.image.extent.height == 4608 && r.resource.view.image.handle == 10,
            "receipt retained borrowed public input pointers");
    denyProof(r);
    require(f.bridge.counters().requested == 1 && f.bridge.counters().delivered == 1 &&
            !f.bridge.counters().invalid_after_drain && !f.bridge.notification_loss(), "successful bridge counters incorrect");
    Fixture other; other.create(); other.request(); other.drain();
    require(!other.bridge.validate(r), "different owning journal accepted equal numeric image handles");
    auto forged = r; forged.resource.sourceFrameAssociated = true;
    require(!f.bridge.validate(forged), "CPU bridge accepted invented source association");
    forged = r; ++forged.resource.image.identity.generation;
    require(!f.bridge.validate(forged), "changed lifecycle generation accepted");
}
void deferredReplayAndRetirement() {
    Fixture f; f.create(); f.request();
    f.journal->destroyView(f.device, f.view);
    require(f.bridge.drain(Results::snapshot, &f.results, 5) == 5 && !f.results.count,
            "receipt delivered during partial batch before queued view destruction");
    require(f.bridge.drain(Results::snapshot, &f.results, 1) == 1 && f.results.count == 1,
            "drained retirement did not settle the pending receipt");
    require(!f.results.receipts[0].current_after_drain && !f.bridge.validate(f.results.receipts[0]),
            "retirement after receipt request was certified");
    require(f.bridge.counters().invalid_after_drain == 1, "post-drain invalidity not recorded");
    denyProof(f.results.receipts[0]);
    Fixture g; g.create(); g.request(); g.drain();
    const auto receipt = g.results.receipts[0];
    g.journal->destroyImage(g.device, g.image);
    require(!g.bridge.validate(receipt), "unreplayed destroy accepted before publish-time revalidation");
    g.drain(); require(!g.bridge.validate(receipt), "retired image accepted after drain");
    g.create(); g.drain(); require(!g.bridge.validate(receipt), "destroy/recreate revived old resource generation");
}
void missingTagsAndClosure() {
    Fixture untagged; untagged.create(false); untagged.request(); untagged.drain();
    require(untagged.results.count == 1 && !untagged.results.receipts[0].current_after_drain &&
            !untagged.results.receipts[0].resource.resourceComplete, "untagged native creation certified");
    denyProof(untagged.results.receipts[0]);
    Fixture closed; closed.create(); closed.request(); closed.drain();
    const auto receipt = closed.results.receipts[0];
    closed.journal->close(); require(!closed.bridge.validate(receipt), "closed observation window certified");
    Fixture stopped; stopped.create(); stopped.request(); stopped.bridge.stop(); stopped.drain();
    require(!stopped.results.count && stopped.bridge.notification_loss(), "callback retained stopped context");
    require(!stopped.bridge.request(stopped.device, stopped.image, stopped.view, stopped.tag, stopped.cookie),
            "stopped bridge still admitted hook work");
    Fixture invalid;
    require(!invalid.bridge.request(invalid.device, invalid.image, invalid.view, 0, invalid.cookie) &&
            invalid.bridge.notification_loss(), "missing source invocation admitted");
}
void concurrentUnpublishedPredecessor() {
    Fixture f;
    std::atomic<bool> entered{false}, release{false};
    std::thread preceding([&] { JournalTestAccess::delayed(*f.journal, entered, release); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    f.create(); f.request();
    require(!f.bridge.drain(Results::snapshot, &f.results) && !f.results.count,
            "owner replay crossed an unpublished earlier hook ticket");
    release.store(true, std::memory_order_release); preceding.join();
    f.drain();
    require(f.results.count == 1 && f.results.snapshots == 1 && f.results.receipts[0].current_after_drain &&
            f.bridge.validate(f.results.receipts[0]), "owned receipt failed after real producer quiescence");
}
void registrationAndBoundedRequests() {
    auto journal = std::make_unique<Journal>(); Owner inactive{*journal}; Results results;
    require(!inactive.initialize_before_threads(false), "disabled bridge initialized");
    require(!inactive.register_receipts_before_threads({Results::receive, &results}), "unprepared bridge registered");
    require(!inactive.request(1, handle<VkImage>(1), handle<VkImageView>(2), 3, 4), "unprepared hook admitted");
    require(inactive.notification_loss(), "missed expected hook request not recorded");
    Fixture f;
    require(!f.bridge.initialize_before_threads(true), "initialization repeated");
    require(!f.bridge.register_receipts_before_threads({Results::receive, &f.results}), "registration overwritten");
    f.create();
    inHook = true;
    for (std::size_t i = 0; i < ImportTagCapacity; ++i)
        require(f.bridge.request(f.device, f.image, f.view, f.tag, f.cookie + i), "bounded receipt capacity too small");
    require(!f.bridge.request(f.device, f.image, f.view, f.tag, f.cookie + ImportTagCapacity), "receipt capacity exceeded");
    inHook = false;
    f.drain();
    require(f.results.count == ImportTagCapacity && f.bridge.notification_loss() &&
            f.bridge.counters().requested == ImportTagCapacity && f.bridge.counters().refused == 1,
            "actual fixed request/delivery budget ignored");
    require(!hookAllocations.load(), "bounded publication allocated");
}
}
int main() {
    realJournalSuccess(); deferredReplayAndRetirement(); missingTagsAndClosure();
    concurrentUnpublishedPredecessor(); registrationAndBoundedRequests();
    std::cout << "compositor raw owning-journal bridge: " << checks << " checks passed\n";
}
