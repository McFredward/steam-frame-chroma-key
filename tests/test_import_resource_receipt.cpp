#include "layer/passive_binding_journal.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {
thread_local bool monitorHookAllocations = false;
std::atomic<std::uint64_t> hookAllocations{0};
std::uint64_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
#if defined(__GNUC__) || defined(__clang__)
#define RECEIPT_TEST_ALLOCATION __attribute__((noinline))
#else
#define RECEIPT_TEST_ALLOCATION
#endif
RECEIPT_TEST_ALLOCATION void* operator new(std::size_t size) {
    if (monitorHookAllocations) hookAllocations.fetch_add(1);
    if (auto* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
RECEIPT_TEST_ALLOCATION void* operator new[](std::size_t size) { return ::operator new(size); }
RECEIPT_TEST_ALLOCATION void operator delete(void* p) noexcept { std::free(p); }
RECEIPT_TEST_ALLOCATION void operator delete[](void* p) noexcept { std::free(p); }
RECEIPT_TEST_ALLOCATION void operator delete(void* p, std::size_t) noexcept { std::free(p); }
RECEIPT_TEST_ALLOCATION void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#undef RECEIPT_TEST_ALLOCATION

namespace frame_bindings {
struct TestAccess {
    static std::uint64_t serial(Core& core) noexcept { return core.serial_; }
    static void mixedBindingRefusals(Core& core) {
        auto* set = Core::find(core.sets_,1,104); auto* layout=Core::find(core.layouts_,1,101);
        layout->count=2;layout->bindings[1]=layout->bindings[0];layout->bindings[1].binding=31;
        set->values[1]=set->values[0];set->values[1].view=Core::find(core.views_,1,111)->id;
        set->values[0].written=false;
    }
    static std::mutex& mutex(Core& core) { return core.mutex_; }
    // Deliberately isolate normally co-occurring incomplete metadata gates so
    // a diagnostic label cannot silently move to a different refusal branch.
    static void damageTargetState(Core& core, TargetProbeReason reason) {
        auto* command = Core::find(core.commands_, 1, 105);
        auto* layout = Core::find(core.pipelineLayouts_, 1, 102);
        auto* set = Core::find(core.sets_, 1, 104);
        auto* setLayout = Core::find(core.layouts_, 1, 101);
        auto* aliasView = Core::find(core.views_, 1, 111);
        auto* unrelatedView = Core::find(core.views_, 1, 108);
        auto* unrelatedImage = Core::find(core.images_, 1, 107);
        switch (reason) {
        case TargetProbeReason::CommandAbsent: core.commands_.erase(1, 105); break;
        case TargetProbeReason::LayoutAbsent: core.pipelineLayouts_.erase(1, 102); break;
        case TargetProbeReason::CommandIncomplete: command->complete = false; break;
        case TargetProbeReason::LayoutIncomplete: layout->complete = false; break;
        case TargetProbeReason::LayoutEmpty: layout->count = 0; break;
        case TargetProbeReason::CommandLayoutMismatch: ++command->layout.generation; break;
        case TargetProbeReason::BoundSetIncomplete: command->sets[0].complete = false; break;
        case TargetProbeReason::SetAbsentOrStale: ++command->sets[0].set.generation; break;
        case TargetProbeReason::SetIncomplete: set->complete = false; break;
        case TargetProbeReason::SetLayoutAbsentOrStale: core.layouts_.erase(1, 101); break;
        case TargetProbeReason::SetLayoutIncomplete: setLayout->complete = false; break;
        case TargetProbeReason::SetLayoutMismatch: ++set->layout.generation; break;
        case TargetProbeReason::PoolAbsentOrStale: ++set->pool.generation; break;
        case TargetProbeReason::DescriptorUnwritten: set->values[0].written = false; break;
        case TargetProbeReason::DescriptorIncomplete: set->values[0].complete = false; break;
        case TargetProbeReason::ViewAbsentOrStale: ++set->values[0].view.generation; break;
        case TargetProbeReason::ViewIncomplete: set->values[0].view = aliasView->id; aliasView->complete = false; break;
        case TargetProbeReason::ImageAbsentOrStale: set->values[0].view = aliasView->id; ++aliasView->metadata.image.generation; break;
        case TargetProbeReason::ImageIncomplete: set->values[0].view = unrelatedView->id; unrelatedImage->complete = false; break;
        case TargetProbeReason::ExactBindingIncomplete: setLayout->bindings[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
        case TargetProbeReason::NoSupportedImageBinding: setLayout->bindings[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
        default: require(false, "unhandled isolated target diagnostic fixture");
        }
    }
};
struct JournalTestAccess {
    static std::uint64_t coreSerial(Journal& journal) noexcept { return TestAccess::serial(journal.core_); }
    static bool validate(Journal& journal, const ImportResourceReceipt& previous) noexcept { return journal.core_.validate(previous); }
    static TargetProbeDiagnostics target(Journal& journal, const ImportResourceReceipt& receipt) noexcept {
        return journal.core_.targetSnapshotMatchDetailed(1, reinterpret_cast<VkCommandBuffer>(105),
                                                         reinterpret_cast<VkPipelineLayout>(102), receipt);
    }
    static void damageTargetState(Journal& journal, TargetProbeReason reason) { TestAccess::damageTargetState(journal.core_, reason); }
    static void mixedBindingRefusals(Journal& journal) { TestAccess::mixedBindingRefusals(journal.core_); }
    static void lockedTarget(Journal& journal, const ImportResourceReceipt& receipt, std::atomic<bool>& entered,
                             std::atomic<bool>& release) {
        std::lock_guard<std::mutex> lock(TestAccess::mutex(journal.core_));
        (void)receipt; entered.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    static void abort(Journal& journal) { journal.aborted_ = true; }
    static void gap(Journal& journal) noexcept { journal.tickets_.fetch_add(1); }
    static void delayedSnapshot(Journal& journal, std::atomic<bool>& entered, std::atomic<bool>& release) {
        journal.publish(Journal::Op::Snapshot, 1, [&](Journal::Record& record) {
            record.payload.snapshot = {};
            entered.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    }
    static void corruptReceipt(Journal& journal, unsigned kind) {
        auto* lane = journal.producerLane();
        auto& record = lane->records[(lane->write.load() - 1) % Journal::LaneCapacity];
        if (kind == 0) record.bytes = 1;
        if (kind == 1) ++record.byteStart;
    }
};
}

namespace {
using namespace frame_bindings;
template<class T> T h(std::uint64_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(value));
    else return static_cast<T>(value);
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

struct Collector {
    std::vector<ImportResourceReceipt> receipts;
    unsigned snapshots = 0;
    void operator()(std::uint32_t, const Snapshot&) noexcept { ++snapshots; }
    void operator()(std::uint64_t cookie, const ImportResourceReceipt& receipt) noexcept {
        require(receipt.cookie == cookie, "owned receipt cookie changed at delivery");
        receipts.push_back(receipt);
    }
};
struct Fixture {
    std::unique_ptr<Journal> journal = std::make_unique<Journal>();
    std::uint64_t device = 1, tag = 0x800001, cookie = 0x100000009ULL;
    VkImage image = h<VkImage>(10);
    VkDeviceMemory memory = h<VkDeviceMemory>(20);
    VkImageView view = h<VkImageView>(30);
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
    }
    void create() {
        require(journal->createImageTagged(device, image, imageInfo, tag), "tagged image publication failed");
        journal->allocateMemory(device, memory, memoryInfo);
        journal->bindImageMemory(device, image, memory, 0);
        require(journal->createViewTagged(device, view, viewInfo, tag), "tagged view publication failed");
    }
    void request() {
        require(journal->requestImportResourceReceipt(device, image, view, tag, cookie), "receipt publication failed");
    }
    ImportResourceReceipt drainOne() {
        Collector collector;
        while (journal->drain(collector)) {}
        require(collector.receipts.size() == 1, "expected exactly one typed receipt");
        return collector.receipts[0];
    }
    ImportResourceReceipt receipt() { request(); return drainOne(); }
};
void denied(const ImportResourceReceipt& receipt) {
    require(!receipt.nativeImportAccepted, "receipt claims native registration accepted");
    require(!receipt.sourceFrameAssociated, "receipt invented a source frame");
    require(!receipt.nativeCrossProcessNamespace, "receipt invented a native namespace");
    require(!receipt.acceptedSubmit, "receipt claims accepted submission");
    require(!receipt.gpuConsumedCoherent, "receipt claims GPU consumption");
}
void reject(const ImportResourceReceipt& receipt, std::uint32_t errors) {
    require(!receipt.resourceComplete && (receipt.errors & errors), "invalid resource receipt certified");
    denied(receipt);
}
void orderedOwnedSuccess() {
    Fixture f;
    monitorHookAllocations = true;
    f.create(); f.request();
    monitorHookAllocations = false;
    require(!JournalTestAccess::coreSerial(*f.journal), "hook touched the background Core");
    require(!hookAllocations.load(), "hook allocated metadata storage");
    // Input structures/extension nodes may be overwritten as soon as hooks
    // return; the complete receipt must use the original owned journal copies.
    f.external.handleTypes = 0; f.imported.fd = 42; f.dedicated.image = h<VkImage>(999);
    f.imageInfo.extent = {1, 1, 1}; f.viewInfo.image = h<VkImage>(999);
    const auto receipt = f.drainOne();
    require(receipt.resourceComplete && !receipt.errors && receipt.reason == Reason::None, "ordered native lifecycle metadata refused");
    require(receipt.journalInstance && receipt.localReplayOrder == 5 && receipt.resourceGenerationCursor == 3,
            "journal lifecycle cursor/order not retained");
    require(receipt.image.identity.handle == 10 && receipt.image.identity.generation == 1 &&
            receipt.memory.identity.handle == 20 && receipt.memory.identity.generation == 2 &&
            receipt.view.identity.handle == 30 && receipt.view.identity.generation == 3,
            "receipt replaced actual journal resource generations with tag/callback ordinals");
    require(receipt.imageInvocationTag == f.tag && receipt.viewInvocationTag == f.tag && receipt.invocationTag == f.tag,
            "creation tags were not independently matched");
    require(receipt.image.extent.width == 1152 && receipt.image.extent.height == 4608 && receipt.image.extent.depth == 1,
            "owned image extent was read after caller release");
    require(receipt.image.extensions.externalHandleTypes == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
            receipt.memory.extensions.inputFD == 19 && same(receipt.memory.dedicatedImage, receipt.image.identity) &&
            same(receipt.image.memory, receipt.memory.identity) && same(receipt.view.image, receipt.image.identity),
            "allocation/bind/view generation links missing");
    require(receipt.cookie == f.cookie && receipt.requestedImage == 10 && receipt.requestedView == 30,
            "64-bit owned cookie/request handles truncated");
    require(JournalTestAccess::validate(*f.journal, receipt), "current receipt lifecycle failed validation");
    {
        auto foreign = receipt; ++foreign.journalInstance;
        require(!JournalTestAccess::validate(*f.journal, foreign), "foreign journal revived equal numeric resource identities");
        auto future = receipt; ++future.resourceGenerationCursor;
        require(!JournalTestAccess::validate(*f.journal, future), "future worker lifecycle cursor accepted");
        auto missingCursor = receipt; missingCursor.resourceGenerationCursor = 0;
        require(!JournalTestAccess::validate(*f.journal, missingCursor), "missing worker lifecycle cursor accepted");
        auto forged = receipt; forged.nativeImportAccepted = true;
        require(!JournalTestAccess::validate(*f.journal, forged), "CPU receipt validated invented native registration proof");
    }
    require(f.journal->receiptCounters().requested == 1 && f.journal->receiptCounters().delivered == 1 &&
            !f.journal->receiptCounters().unconsumed, "typed receipt delivery not counted");
    denied(receipt);
    f.journal->close(); require(f.journal->finished(), "successful receipt journal did not finish");
}
void unmatchedAndMissing() {
    for (unsigned kind = 0; kind < 8; ++kind) {
        Fixture f;
        if (kind != 0 && kind != 7) f.journal->createImageTagged(f.device, f.image, f.imageInfo, f.tag);
        if (kind == 7) f.journal->createImage(f.device, f.image, f.imageInfo);
        if (kind != 1) f.journal->allocateMemory(f.device, f.memory, f.memoryInfo);
        if (kind != 2) f.journal->bindImageMemory(f.device, f.image, f.memory, 0);
        if (kind != 3) {
            if (kind == 4) f.journal->createView(f.device, f.view, f.viewInfo);
            else f.journal->createViewTagged(f.device, f.view, f.viewInfo, kind == 5 ? f.tag + 1 : f.tag);
        }
        if (kind == 6) ++f.tag;
        const auto receipt = f.receipt();
        const auto expected = kind == 0 ? ImportMissingImage : kind == 1 ? ImportMissingMemory :
            kind == 2 ? ImportMissingBinding : kind == 3 ? ImportMissingView :
            kind == 4 || kind == 7 ? ImportUntagged : kind == 5 ? ImportTagMismatch : ImportUnknownTag;
        reject(receipt, expected);
    }
}
void retireReuseAndDeviceIsolation() {
    for (unsigned kind = 0; kind < 8; ++kind) {
        Fixture f; f.create();
        if (kind == 0 || kind == 1) {
            f.journal->destroyImage(f.device, f.image);
            if (kind == 1) f.journal->createImageTagged(f.device, f.image, f.imageInfo, f.tag);
        }
        if (kind == 2 || kind == 3) {
            f.journal->destroyView(f.device, f.view);
            if (kind == 3) f.journal->createViewTagged(f.device, f.view, f.viewInfo, f.tag);
        }
        if (kind == 4 || kind == 5) {
            f.journal->freeMemory(f.device, f.memory);
            if (kind == 5) f.journal->allocateMemory(f.device, f.memory, f.memoryInfo);
        }
        if (kind == 6) f.journal->deviceDestroyed(f.device);
        if (kind == 7) ++f.device;
        const auto receipt = f.receipt();
        const auto expected = kind == 0 || kind == 6 || kind == 7 ? ImportMissingImage :
            kind == 1 || kind == 3 ? ImportAmbiguousTag : kind == 2 ? ImportMissingView : ImportMissingMemory;
        reject(receipt, expected);
    }
    {
        Fixture f; f.create(); const auto receipt = f.receipt();
        f.journal->freeMemory(f.device, f.memory); f.journal->drain(Collector{});
        require(!JournalTestAccess::validate(*f.journal, receipt), "freed memory kept old receipt valid");
    }
    {
        Fixture f; f.create(); const auto receipt = f.receipt();
        f.journal->destroyView(f.device, f.view); f.journal->createViewTagged(f.device, f.view, f.viewInfo, f.tag + 1);
        f.journal->drain(Collector{});
        require(!JournalTestAccess::validate(*f.journal, receipt), "reused view kept old receipt valid");
    }
    {
        Fixture f; f.create(); const auto receipt = f.receipt();
        f.journal->deviceDestroyed(f.device); f.journal->drain(Collector{});
        require(!JournalTestAccess::validate(*f.journal, receipt), "retired device kept old receipt valid");
    }
    {
        Fixture f; f.create();
        f.journal->createImageTagged(f.device + 1, f.image, f.imageInfo, f.tag);
        f.journal->createViewTagged(f.device + 1, f.view, f.viewInfo, f.tag);
        reject(f.receipt(), ImportTagMismatch);
    }
}
void unsupportedAndLoss() {
    for (unsigned kind = 0; kind < 7; ++kind) {
        Fixture f;
        VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
        if (kind == 0) f.imageInfo.pNext = &unknown;
        if (kind == 1) f.memoryInfo.pNext = &unknown;
        if (kind == 2) f.viewInfo.pNext = &unknown;
        if (kind == 3) f.external.pNext = &f.external;
        if (kind == 4) f.imageInfo.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
        f.create();
        if (kind == 5) f.journal->bindImageMemory(f.device, f.image, f.memory, 0); // Multiple bindings are not certifiable.
        if (kind == 6) f.journal->reportUnsupported(f.device);
        reject(f.receipt(), kind == 6 ? ImportJournalLoss : ImportUnsupported);
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
        Fixture f; f.create(); f.request(); JournalTestAccess::corruptReceipt(*f.journal, kind);
        reject(f.drainOne(), ImportJournalLoss);
        f.journal->close(); while (!f.journal->finished()) f.journal->drain(Collector{});
    }
    {
        Fixture f; f.create(); f.request(); JournalTestAccess::gap(*f.journal);
        f.journal->close(); f.drainOne();
        while (!f.journal->finished()) f.journal->drain(Collector{});
        require(f.journal->lostState() && f.journal->transportCounters().orderGap, "unpublished ordering gap was hidden");
    }
}
void duplicateCapacityAndMissingConsumer() {
    {
        Fixture f; f.create(); f.request(); f.request();
        Collector collector; f.journal->drain(collector);
        require(collector.receipts.size() == 2 && collector.receipts[0].resourceComplete, "first receipt unexpectedly missing");
        reject(collector.receipts[1], ImportRepeatedReceipt);
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
        Fixture f; f.create();
        if (kind == 0) {
            f.journal->destroyImage(f.device, f.image);
            f.journal->createImageTagged(f.device, f.image, f.imageInfo, f.tag);
        } else f.journal->createViewTagged(f.device, h<VkImageView>(31), f.viewInfo, f.tag);
        reject(f.receipt(), ImportAmbiguousTag);
    }
    {
        Fixture f; f.create(); f.request();
        f.journal->drain([](std::uint32_t, const Snapshot&) noexcept {});
        const auto counts = f.journal->receiptCounters();
        require(counts.requested == 1 && !counts.delivered && counts.unconsumed == 1,
                "Snapshot-only consumer silently certified a dropped import receipt");
    }
    {
        Fixture f;
        for (unsigned i = 0; i < 32; ++i)
            require(f.journal->requestImportResourceReceipt(f.device, f.image, f.view, f.tag + i, f.cookie + i), "receipt32 bound too small");
        require(!f.journal->requestImportResourceReceipt(f.device, f.image, f.view, f.tag + 32, f.cookie + 32), "receipt cap exceeded");
        Collector collector; f.journal->drain(collector);
        require(collector.receipts.size() == 32 && f.journal->receiptCounters().capRefused == 1, "cap refusal lost bounded records");
        for (const auto& receipt : collector.receipts) reject(receipt, ImportJournalLoss);
    }
    {
        Fixture f;
        for (unsigned i = 0; i < 33; ++i) f.journal->createImageTagged(f.device, h<VkImage>(100 + i), f.imageInfo, f.tag + i);
        f.image = h<VkImage>(132); f.tag += 32;
        reject(f.receipt(), ImportJournalLoss);
    }
    {
        Fixture f; f.create();
        // Fill ordinary scalar headers without consumer backpressure, then a
        // receipt cannot reserve storage. The existing journal loss is explicit.
        for (std::size_t i = 4; i < Journal::LaneCapacity; ++i) f.journal->deviceDestroyed(999);
        require(!f.journal->requestImportResourceReceipt(f.device, f.image, f.view, f.tag, f.cookie), "full lane accepted receipt");
        require(f.journal->operationCounters()[37].publicationLoss == 1 && f.journal->lostState(), "receipt queue loss unreported");
        f.journal->close(); while (!f.journal->finished()) f.journal->drain(Collector{});
    }
    {
        Fixture f;
        for (unsigned bad = 0; bad < 5; ++bad)
            require(!f.journal->requestImportResourceReceipt(bad == 0 ? 0 : f.device,
                bad == 1 ? VK_NULL_HANDLE : f.image, bad == 2 ? VK_NULL_HANDLE : f.view,
                bad == 3 ? 0 : f.tag, bad == 4 ? 0 : f.cookie), "incomplete owned request admitted");
        require(!f.journal->receiptCounters().requested, "invalid requests consumed a successful reservation");
    }
}
void orderedInterleavedScopes() {
    Fixture f;
    // A later visible receipt cannot outrun an earlier unfinished publication.
    std::atomic<bool> entered{false}, release{false};
    std::thread first([&] { JournalTestAccess::delayedSnapshot(*f.journal, entered, release); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    f.create(); f.request();
    Collector collector;
    require(!f.journal->drain(collector) && collector.receipts.empty(), "receipt crossed an unpublished predecessor");
    release.store(true, std::memory_order_release); first.join();
    f.journal->drain(collector);
    require(collector.receipts.size() == 1 && collector.receipts[0].resourceComplete && collector.snapshots == 1,
            "owned delayed predecessor did not restore exact receipt association");
    denied(collector.receipts[0]);
    {
        auto journal = std::make_unique<Journal>();
        std::vector<std::thread> producers;
        for (unsigned i = 0; i < 4; ++i) producers.emplace_back([&, i] {
            Fixture source;
            source.device = 1 + i; source.tag += i; source.cookie += i;
            source.journal.reset(); // Fixture inputs remain owned by this caller.
            monitorHookAllocations = true;
            if (!journal->createImageTagged(source.device, source.image, source.imageInfo, source.tag)) std::abort();
            journal->allocateMemory(source.device, source.memory, source.memoryInfo);
            journal->bindImageMemory(source.device, source.image, source.memory, 0);
            if (!journal->createViewTagged(source.device, source.view, source.viewInfo, source.tag)) std::abort();
            if (!journal->requestImportResourceReceipt(source.device, source.image, source.view, source.tag, source.cookie)) std::abort();
            monitorHookAllocations = false;
        });
        for (auto& producer : producers) producer.join();
        Collector receipts; journal->drain(receipts);
        require(receipts.receipts.size() == 4 && !hookAllocations.load(), "interleaved native scopes lost receipts or allocated on hooks");
        for (const auto& receipt : receipts.receipts) {
            require(receipt.resourceComplete && receipt.cookie - 0x100000009ULL == receipt.device - 1 &&
                    receipt.invocationTag - 0x800001 == receipt.device - 1, "interleaved device/tag/cookie association changed");
            denied(receipt);
        }
        journal->close(); require(journal->finished(), "interleaved receipt journal remained pending");
    }
}
struct ProbeCollector {
    std::uint64_t filtered=0, unknown=0, matched=0, omitted=0, lastOrder=0;
    std::vector<Snapshot> selected;
    TargetProbeDiagnostics last{};
    std::array<std::uint64_t, static_cast<std::size_t>(TargetProbeReason::Count)> reasons{};
    void operator()(std::uint32_t, const Snapshot&) noexcept {}
    void operator()(std::uint64_t, const ImportResourceReceipt&) noexcept {}
    void operator()(std::uint32_t, const TargetSnapshotProbeResult& value) noexcept {
        require(value.replayOrder>lastOrder,"probe replay order regressed");lastOrder=value.replayOrder;
        last = value.diagnostics;
        require(last.outcome == value.outcome && last.reason != TargetProbeReason::None &&
                (last.reasonMask & targetProbeReasonBit(last.reason)), "typed probe diagnostic disagrees with actual result");
        ++reasons[static_cast<std::size_t>(last.reason)];
        if(value.outcome==TargetProbeOutcome::ExactMatch) {
            require(value.snapshot && value.snapshotProvenance == SnapshotProvenance::ExactTarget,
                    "exact match omitted owned replay snapshot/provenance");++matched;selected.push_back(*value.snapshot);
        } else {
            require(!value.snapshot && value.snapshotProvenance == SnapshotProvenance::None,
                    "default nonmatch constructed a full snapshot");
            if(value.outcome==TargetProbeOutcome::Filtered)++filtered;
            else if(value.outcome==TargetProbeOutcome::QuotaOmitted)++omitted;
            else ++unknown;
        }
    }
};
void targetSnapshotSelection() {
    Fixture f;f.create();const auto target=f.receipt();
    const auto layout=h<VkDescriptorSetLayout>(101);const auto pipeline=h<VkPipelineLayout>(102);const auto pool=h<VkDescriptorPool>(103);
    const auto set=h<VkDescriptorSet>(104);const auto command=h<VkCommandBuffer>(105);const auto commandPool=h<VkCommandPool>(106);
    const auto unrelatedImage=h<VkImage>(107);const auto unrelatedView=h<VkImageView>(108);const auto unrelatedMemory=h<VkDeviceMemory>(109);
    auto unrelatedCreate=f.imageInfo;unrelatedCreate.pNext=nullptr;
    f.journal->createImage(f.device,unrelatedImage,unrelatedCreate);
    auto allocation=f.memoryInfo;allocation.pNext=nullptr;
    f.journal->allocateMemory(f.device,unrelatedMemory,allocation);
    f.journal->bindImageMemory(f.device,unrelatedImage,unrelatedMemory,0);
    auto unrelatedViewInfo=f.viewInfo;unrelatedViewInfo.image=unrelatedImage;
    f.journal->createView(f.device,unrelatedView,unrelatedViewInfo);
    VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    auto sli=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    sli.bindingCount=1;sli.pBindings=&binding;f.journal->createSetLayout(f.device,layout,sli);
    auto pli=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pli.setLayoutCount=1;pli.pSetLayouts=&layout;f.journal->createPipelineLayout(f.device,pipeline,pli);
    auto pci=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pci.maxSets=1;
    f.journal->createPool(f.device,pool,pci);
    auto sai=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    sai.descriptorPool=pool;sai.descriptorSetCount=1;sai.pSetLayouts=&layout;f.journal->allocateSets(f.device,sai,&set);
    f.journal->allocateCommands(f.device,commandPool,1,&command);f.journal->beginCommand(f.device,command);
    f.journal->bindSets(f.device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline,0,1,&set,0,nullptr);
    const auto writeView=[&](VkImageView value) {
        VkDescriptorImageInfo di{VK_NULL_HANDLE,value,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet=set;write.dstBinding=9;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo=&di;f.journal->updateSets(f.device,1,&write,0,nullptr);
    };
    const auto probe=[&](std::uint64_t tag,std::uint64_t cookie) {
        monitorHookAllocations=true;
        const auto published=f.journal->requestTargetSnapshotProbe(f.device,command,pipeline,tag,cookie,7);
        monitorHookAllocations=false;require(published,"target probe publication failed");
    };
    ProbeCollector collector;writeView(unrelatedView);
    for(unsigned batch=0;batch<64;++batch) {
        for(unsigned i=0;i<64;++i)probe(f.tag,f.cookie);
        while(f.journal->drain(collector)){}
    }
    require(collector.filtered==4096 && !collector.matched && !collector.unknown && !collector.omitted,
            "unrelated UI exhausted target snapshot slots or masqueraded as unknown/loss");
    // Both matches resolve from their own exact ordered state, even though the
    // later descriptor update is already published before replay begins.
    writeView(f.view);probe(f.tag,f.cookie);writeView(unrelatedView);probe(f.tag,f.cookie);
    writeView(f.view);probe(f.tag,f.cookie);probe(f.tag+1,f.cookie);probe(f.tag,f.cookie+1);
    while(f.journal->drain(collector)){}
    require(collector.matched==2 && collector.filtered==4097 && collector.unknown==2,"ordered target selection lost identity gates");
    require(collector.reasons[static_cast<std::size_t>(TargetProbeReason::JournalTargetTagMismatch)] == 1 &&
            collector.reasons[static_cast<std::size_t>(TargetProbeReason::JournalTargetCookieMismatch)] == 1,
            "different owning target/tag cookie refusals collapsed");
    require(collector.selected[0].setRevision<collector.selected[1].setRevision,
            "later descriptor revision replaced earlier draw state");
    for(const auto& selected:collector.selected) {
        require(selected.sourceComplete && !selected.gpuConsumedCoherent,"target selection promoted GPU proof");
        const auto& selectedBinding=selected.bindings[5];
        require(same(selectedBinding.image.identity,target.image.identity) && same(selectedBinding.view.identity,target.view.identity) &&
                same(selectedBinding.image.allocation.identity,target.memory.identity),"selected snapshot lost exact imported generations");
    }
    for(unsigned i=0;i<40;++i)probe(f.tag,f.cookie);
    while(f.journal->drain(collector)){}
    require(collector.matched==32 && collector.omitted==10,"target snapshot output quota exceeded 32");
    f.journal->destroyView(f.device,f.view);f.journal->destroyImage(f.device,f.image);
    require(f.journal->createImageTagged(f.device,f.image,f.imageInfo,f.tag+5),"recreated image failed");
    f.journal->bindImageMemory(f.device,f.image,f.memory,0);
    require(f.journal->createViewTagged(f.device,f.view,f.viewInfo,f.tag+5),"recreated view failed");
    writeView(f.view);probe(f.tag,f.cookie);while(f.journal->drain(collector)){}
    require(collector.unknown==3,"recycled handles revived selected import generations");
    f.journal->reportUnsupported(f.device);probe(f.tag,f.cookie);while(f.journal->drain(collector)){}
    require(collector.unknown==4 && f.journal->lostState()>0,"unsupported lifecycle loss was hidden as ordinary filter/quota");
    require(!hookAllocations.load(),"target probe callback allocated on hook thread");
    f.journal->close();require(f.journal->finished(),"filtered probes left a blocking queue head");
}
struct TargetFixture : Fixture {
    const VkDescriptorSetLayout layout = h<VkDescriptorSetLayout>(101);
    const VkPipelineLayout pipeline = h<VkPipelineLayout>(102);
    const VkDescriptorPool pool = h<VkDescriptorPool>(103);
    const VkDescriptorSet set = h<VkDescriptorSet>(104);
    const VkCommandBuffer command = h<VkCommandBuffer>(105);
    const VkCommandPool commandPool = h<VkCommandPool>(106);
    const VkImage unrelatedImage = h<VkImage>(107);
    const VkImageView unrelatedView = h<VkImageView>(108), aliasView = h<VkImageView>(111);
    const VkDeviceMemory unrelatedMemory = h<VkDeviceMemory>(109);
    ImportResourceReceipt target{};
    TargetFixture() {
        create(); target = receipt();
        auto unrelatedCreate = imageInfo; unrelatedCreate.pNext = nullptr;
        journal->createImage(device, unrelatedImage, unrelatedCreate);
        auto allocation = memoryInfo; allocation.pNext = nullptr;
        journal->allocateMemory(device, unrelatedMemory, allocation);
        journal->bindImageMemory(device, unrelatedImage, unrelatedMemory, 0);
        auto vi = viewInfo; vi.image = unrelatedImage; journal->createView(device, unrelatedView, vi);
        journal->createView(device, aliasView, viewInfo);
        VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        auto sli=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        sli.bindingCount=1;sli.pBindings=&binding;journal->createSetLayout(device,layout,sli);
        auto pli=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pli.setLayoutCount=1;pli.pSetLayouts=&layout;journal->createPipelineLayout(device,pipeline,pli);
        auto pci=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pci.maxSets=1;
        journal->createPool(device,pool,pci);
        auto sai=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        sai.descriptorPool=pool;sai.descriptorSetCount=1;sai.pSetLayouts=&layout;journal->allocateSets(device,sai,&set);
        journal->allocateCommands(device,commandPool,1,&command);journal->beginCommand(device,command);
        journal->bindSets(device,command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline,0,1,&set,0,nullptr);
        write(view); drain();
    }
    void write(VkImageView value) {
        VkDescriptorImageInfo di{VK_NULL_HANDLE,value,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto write=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet=set;write.dstBinding=9;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo=&di;journal->updateSets(device,1,&write,0,nullptr);
    }
    void drain() { Collector collector; while(journal->drain(collector)){} }
    TargetProbeDiagnostics probe(std::uint64_t selectedTag=0, std::uint64_t selectedCookie=0, std::uint64_t selectedDevice=0) {
        require(journal->requestTargetSnapshotProbe(selectedDevice ? selectedDevice : device,command,pipeline,
                    selectedTag ? selectedTag : tag,selectedCookie ? selectedCookie : cookie,7),"typed probe publication failed");
        ProbeCollector collector; while(journal->drain(collector)){}
        require(collector.lastOrder,"typed probe was not delivered");return collector.last;
    }
};
void targetDenialDiagnostics() {
    for (const auto reason : {TargetProbeReason::CommandAbsent, TargetProbeReason::LayoutAbsent,
            TargetProbeReason::CommandIncomplete, TargetProbeReason::LayoutIncomplete, TargetProbeReason::LayoutEmpty,
            TargetProbeReason::CommandLayoutMismatch, TargetProbeReason::BoundSetIncomplete,
            TargetProbeReason::SetAbsentOrStale, TargetProbeReason::SetIncomplete,
            TargetProbeReason::SetLayoutAbsentOrStale, TargetProbeReason::SetLayoutIncomplete,
            TargetProbeReason::SetLayoutMismatch, TargetProbeReason::PoolAbsentOrStale,
            TargetProbeReason::DescriptorUnwritten, TargetProbeReason::DescriptorIncomplete,
            TargetProbeReason::ViewAbsentOrStale, TargetProbeReason::ImageAbsentOrStale,
            TargetProbeReason::ViewIncomplete, TargetProbeReason::ImageIncomplete,
            TargetProbeReason::ExactBindingIncomplete, TargetProbeReason::NoSupportedImageBinding}) {
        TargetFixture f;
        JournalTestAccess::damageTargetState(*f.journal,reason);
        const auto result=f.probe();
        require(result.reason==reason,"isolated existing refusal gate reported a different cause");
        require(result.outcome==(reason==TargetProbeReason::NoSupportedImageBinding ? TargetProbeOutcome::Filtered : TargetProbeOutcome::Unknown),
                "diagnostic changed existing refusal outcome");
        require(result.requestedCommand==105 && result.requestedLayout==102 && result.observedTargetTag==f.tag &&
                result.observedTargetCookie==f.cookie && (result.observations & ProbeJournalTargetObserved),
                "probe dropped actual owning request/target identities");
    }
    {
        TargetFixture f;
        for(const auto reason : {TargetProbeReason::TargetIncomplete,TargetProbeReason::TargetDeviceMismatch,
                TargetProbeReason::TargetJournalMismatch,TargetProbeReason::TargetImageStale,TargetProbeReason::TargetViewStale,
                TargetProbeReason::TargetMemoryStale,TargetProbeReason::TargetBindingStale,TargetProbeReason::TargetOffsetMismatch}) {
            auto stale=f.target;
            if(reason==TargetProbeReason::TargetIncomplete)stale.resourceComplete=false;
            if(reason==TargetProbeReason::TargetDeviceMismatch)++stale.device;
            if(reason==TargetProbeReason::TargetJournalMismatch)++stale.journalInstance;
            if(reason==TargetProbeReason::TargetImageStale)++stale.image.identity.generation;
            if(reason==TargetProbeReason::TargetViewStale)++stale.view.identity.generation;
            if(reason==TargetProbeReason::TargetMemoryStale)++stale.memory.identity.generation;
            if(reason==TargetProbeReason::TargetBindingStale)++stale.image.memory.generation;
            if(reason==TargetProbeReason::TargetOffsetMismatch)++stale.image.memoryOffset;
            const auto result=JournalTestAccess::target(*f.journal,stale);
            require(result.outcome==TargetProbeOutcome::Unknown && result.reason==reason,
                    "permanent target generation/ownership mismatch lost its exact refusal label");
        }
    }
    {
        TargetFixture f;
        JournalTestAccess::mixedBindingRefusals(*f.journal);
        const auto result=f.probe();
        require(result.outcome==TargetProbeOutcome::Unknown && result.reason==TargetProbeReason::DescriptorUnwritten &&
                result.binding==9 && result.examinedImageBindings==2 &&
                (result.reasonMask & targetProbeReasonBit(TargetProbeReason::DifferentViewSameImage)),
                "multiple binding refusals discarded decisive identities or secondary evidence");
    }
    {
        TargetFixture f; std::atomic<bool> entered{false},release{false};
        std::thread owner([&]{JournalTestAccess::lockedTarget(*f.journal,f.target,entered,release);});
        while(!entered.load(std::memory_order_acquire))std::this_thread::yield();
        const auto result=JournalTestAccess::target(*f.journal,f.target);
        release.store(true,std::memory_order_release);owner.join();
        require(result.outcome==TargetProbeOutcome::Unknown && result.reason==TargetProbeReason::CoreOwnerUnavailable,
                "Core ownership refusal was silently classified as missing metadata");
        require(JournalTestAccess::target(*f.journal,f.target).reason==TargetProbeReason::CoreLoss,
                "permanent Core loss after contention was hidden");
    }
    {
        TargetFixture f;
        f.write(f.aliasView); const auto sameImage=f.probe();
        require(sameImage.outcome==TargetProbeOutcome::Filtered && sameImage.reason==TargetProbeReason::DifferentViewSameImage,
                "same image under a different view was admitted as an exact imported view");
        require(sameImage.descriptorView.handle==111 && sameImage.candidateView.handle==111 &&
                same(sameImage.candidateImage,f.target.image.identity) && same(sameImage.candidateMemory,f.target.memory.identity) &&
                sameImage.binding==9 && sameImage.examinedImageBindings==1 &&
                (sameImage.observations & (ProbeDescriptorObserved | ProbeViewObserved | ProbeImageObserved)) ==
                    (ProbeDescriptorObserved | ProbeViewObserved | ProbeImageObserved),
                "same-image refusal did not retain actual candidate generations");
        f.write(f.unrelatedView); const auto unrelated=f.probe();
        require(unrelated.outcome==TargetProbeOutcome::Filtered && unrelated.reason==TargetProbeReason::NoMatch &&
                (unrelated.reasonMask & targetProbeReasonBit(TargetProbeReason::DifferentImage)),
                "genuine supported-subset nonmatch lacks its typed binding evidence");
        f.write(f.view); const auto exact=f.probe();
        require(exact.outcome==TargetProbeOutcome::ExactMatch && exact.reason==TargetProbeReason::ExactMatch &&
                same(exact.candidateView,f.target.view.identity),"diagnostics broke exact ordered generation matching");
    }
    {
        TargetFixture f;
        require(f.probe(f.tag+1).reason==TargetProbeReason::JournalTargetTagMismatch,"tag denial not classified");
        require(f.probe(0,f.cookie+1).reason==TargetProbeReason::JournalTargetCookieMismatch,"cookie denial not classified");
        require(f.probe(0,0,f.device+1).reason==TargetProbeReason::JournalTargetDeviceMismatch,"device denial not classified");
        f.journal->destroyView(f.device,f.view); const auto retired=f.probe();
        require(retired.reason==TargetProbeReason::TargetCurrentIncomplete &&
                (retired.currentTargetErrors & ImportMissingView) && retired.outcome==TargetProbeOutcome::Unknown,
                "retired target view was hidden as an ordinary nonmatch");
    }
    {
        TargetFixture f; f.journal->reportUnsupported(f.device);
        require(f.probe().reason==TargetProbeReason::JournalLoss,"permanent owning-journal loss not classified");
    }
    {
        Fixture f;
        require(f.journal->requestTargetSnapshotProbe(f.device,h<VkCommandBuffer>(105),h<VkPipelineLayout>(102),f.tag,f.cookie,7),"missing-target probe failed");
        ProbeCollector c; while(f.journal->drain(c)){}
        require(c.last.reason==TargetProbeReason::JournalTargetMissing && !c.last.observations,
                "missing target acquired invented observed identities");
    }
    {
        TargetFixture f; JournalTestAccess::abort(*f.journal);
        require(f.probe().reason==TargetProbeReason::JournalAborted,"aborted replay not classified");
    }
    {
        TargetFixture f;
        require(f.journal->requestTargetSnapshotProbe(f.device,f.command,f.pipeline,f.tag,f.cookie,7),"corrupt target probe publication failed");
        JournalTestAccess::corruptReceipt(*f.journal,0);
        ProbeCollector c; while(f.journal->drain(c)){}
        require(c.last.reason==TargetProbeReason::JournalRestoreFailed && !c.last.requestedCommand &&
                !c.last.requestedLayout,"unrestored probe read invented request identities");
    }
    for(unsigned i=0;i<static_cast<unsigned>(TargetProbeReason::Count);++i)
        require(std::string(targetProbeReasonName(static_cast<TargetProbeReason>(i)))!="invalid","typed probe name missing");
}
void incompleteDescriptorObservations() {
    {
        TargetFixture f;
        auto unknown = info<VkBaseInStructure>(static_cast<VkStructureType>(0x7ffffffe));
        auto vi = f.viewInfo; vi.image = f.unrelatedImage; vi.pNext = &unknown;
        const auto badView = h<VkImageView>(120);
        f.journal->createView(f.device,badView,vi); f.write(badView);
        const auto result = f.probe();
        require(result.reason == TargetProbeReason::DescriptorIncomplete && result.outcome == TargetProbeOutcome::Unknown,
                "observed unsupported view revived an incomplete descriptor");
        require(result.descriptorView.handle == 120 && same(result.descriptorView,result.candidateView) &&
                result.candidateImage.handle == handle(f.unrelatedImage) &&
                (result.observations & (ProbeViewObserved|ProbeImageObserved)) == (ProbeViewObserved|ProbeImageObserved),
                "incomplete descriptor hid actual current view/image generations");
        require(!(result.descriptorStateFlags & (DescriptorWriteComplete|DescriptorCurrentViewComplete|DescriptorViewChainComplete)) &&
                (result.descriptorStateFlags & (DescriptorCurrentImageComplete|DescriptorImageChainComplete)) ==
                    (DescriptorCurrentImageComplete|DescriptorImageChainComplete) &&
                result.descriptorViewChain.count == 1 && result.descriptorViewChain.types[0] == unknown.sType &&
                result.descriptorViewChain.error == extensions::ChainError::Unsupported && !result.descriptorViewChain.complete,
                "unsupported view creation chain lost its truthful refusal metadata");
        f.journal->destroyView(f.device,badView);
        vi.pNext = nullptr; f.journal->createView(f.device,badView,vi);
        const auto stale = f.probe();
        require(stale.reason == TargetProbeReason::DescriptorIncomplete && !stale.candidateView.generation &&
                !stale.candidateImage.generation && !(stale.observations & (ProbeViewObserved|ProbeImageObserved)),
                "recycled view handle was looked up as the old descriptor generation");
    }
    {
        TargetFixture f;
        auto unknown = info<VkBaseInStructure>(static_cast<VkStructureType>(0x7ffffffd));
        auto ii = f.imageInfo; ii.pNext = &unknown;
        const auto badImage = h<VkImage>(121);
        const auto badView = h<VkImageView>(122);
        f.journal->createImage(f.device,badImage,ii);
        auto vi = f.viewInfo; vi.image = badImage; f.journal->createView(f.device,badView,vi); f.write(badView);
        const auto result = f.probe();
        require(result.reason == TargetProbeReason::DescriptorIncomplete && result.candidateImage.handle == 121 &&
                !(result.descriptorStateFlags & (DescriptorCurrentImageComplete|DescriptorImageChainComplete)) &&
                result.descriptorImageChain.count == 1 && result.descriptorImageChain.types[0] == unknown.sType &&
                result.descriptorImageChain.error == extensions::ChainError::Unsupported,
                "incomplete image creation chain was hidden behind descriptor incompleteness");
    }
}
struct PublicProbeCollector {
    TargetSnapshotProbeResult last{};
    Snapshot selected{};
    bool hasSnapshot = false;
    void operator()(std::uint32_t, const Snapshot&) noexcept {}
    void operator()(std::uint64_t, const ImportResourceReceipt&) noexcept {}
    void operator()(std::uint32_t, const TargetSnapshotProbeResult& value) noexcept {
        last = value; hasSnapshot = value.snapshot != nullptr;
        if (value.snapshot) selected = *value.snapshot;
    }
};
PublicProbeCollector publicProbe(TargetFixture& f, bool enabled = true, std::uint64_t tag = 0,
                                 std::uint64_t cookie = 0, std::uint64_t device = 0) {
    monitorHookAllocations = true;
    const auto published = f.journal->requestTargetSnapshotProbe(device ? device : f.device,f.command,f.pipeline,
        tag ? tag : f.tag,cookie ? cookie : f.cookie,7,enabled);
    monitorHookAllocations = false;
    require(published,"independent public probe publication failed");
    PublicProbeCollector collector; while(f.journal->drain(collector)) {}
    require(collector.last.replayOrder,"independent public probe was not delivered"); return collector;
}
void independentPublicDrawSnapshots() {
    {
        TargetFixture f; f.write(f.unrelatedView); f.drain();
        f.journal->destroyView(f.device,f.view); f.journal->destroyImage(f.device,f.image);
        const auto disabled = publicProbe(f,false);
        require(!disabled.hasSnapshot && disabled.last.snapshotProvenance == SnapshotProvenance::None &&
                disabled.last.diagnostics.reason == TargetProbeReason::TargetCurrentIncomplete,
                "default target retirement behavior changed");
        const auto enabled = publicProbe(f);
        require(enabled.hasSnapshot && enabled.last.snapshotProvenance == SnapshotProvenance::UnassociatedPublicDraw &&
                enabled.last.outcome == TargetProbeOutcome::Unknown &&
                enabled.last.diagnostics.reason == TargetProbeReason::TargetCurrentIncomplete &&
                enabled.selected.sourceComplete && !enabled.selected.gpuConsumedCoherent &&
                enabled.selected.bindings[5].view.identity.handle == handle(f.unrelatedView),
                "retired target blocked independent current draw or promoted it to an association");
        for(unsigned i=0;i<15;++i) require(publicProbe(f).hasSnapshot,"public snapshot subquota was shortened");
        const auto omitted = publicProbe(f);
        require(!omitted.hasSnapshot && omitted.last.publicSnapshotQuotaOmitted &&
                omitted.last.outcome == TargetProbeOutcome::Unknown &&
                omitted.last.diagnostics.reason == TargetProbeReason::TargetCurrentIncomplete,
                "public snapshot subquota changed target outcome or exceeded16");
    }
    {
        TargetFixture f; f.write(f.unrelatedView); f.drain();
        for(unsigned i=0;i<16;++i) require(publicProbe(f).hasSnapshot,"public prefix snapshot missing");
        f.write(f.view);
        for(unsigned i=0;i<16;++i) {
            const auto exact = publicProbe(f);
            require(exact.hasSnapshot && exact.last.snapshotProvenance == SnapshotProvenance::ExactTarget &&
                    exact.last.outcome == TargetProbeOutcome::ExactMatch,"shared quota misclassified an exact target");
        }
        const auto exactOmitted = publicProbe(f);
        require(!exactOmitted.hasSnapshot && exactOmitted.last.outcome == TargetProbeOutcome::QuotaOmitted &&
                exactOmitted.last.diagnostics.reason == TargetProbeReason::SnapshotQuota,
                "combined public and exact snapshots exceeded32");
    }
    {
        TargetFixture f;
        for(unsigned i=0;i<32;++i) require(publicProbe(f).hasSnapshot,"exact shared quota was shortened");
        f.write(f.unrelatedView);
        const auto omitted = publicProbe(f);
        require(!omitted.hasSnapshot && omitted.last.publicSnapshotQuotaOmitted,
                "independent diagnostic bypassed the shared full-snapshot ceiling");
    }
    {
        TargetFixture f; f.write(f.unrelatedView);
        for(const auto& result : {publicProbe(f,true,f.tag+1),publicProbe(f,true,0,f.cookie+1),
                                 publicProbe(f,true,0,0,f.device+1)})
            require(!result.hasSnapshot && result.last.snapshotProvenance == SnapshotProvenance::None,
                    "independent diagnostic bypassed retained target ownership");
        f.journal->reportUnsupported(f.device);
        require(!publicProbe(f).hasSnapshot,"independent diagnostic bypassed journal loss");
    }
    {
        TargetFixture f; f.write(f.unrelatedView); f.drain();
        JournalTestAccess::damageTargetState(*f.journal,TargetProbeReason::DescriptorIncomplete);
        const auto incomplete = publicProbe(f);
        require(incomplete.hasSnapshot && !incomplete.selected.sourceComplete &&
                incomplete.last.snapshotProvenance == SnapshotProvenance::UnassociatedPublicDraw &&
                incomplete.last.outcome == TargetProbeOutcome::Unknown,
                "independent incomplete public metadata was suppressed or marked coherent");
    }
    {
        TargetFixture f; JournalTestAccess::abort(*f.journal);
        require(!publicProbe(f).hasSnapshot,"aborted journal produced independent public metadata");
    }
    {
        TargetFixture f;
        require(f.journal->requestTargetSnapshotProbe(f.device,f.command,f.pipeline,f.tag,f.cookie,7,true),
                "unrestored independent probe publication failed");
        JournalTestAccess::corruptReceipt(*f.journal,0);
        PublicProbeCollector collector; while(f.journal->drain(collector)) {}
        require(!collector.hasSnapshot && collector.last.diagnostics.reason == TargetProbeReason::JournalRestoreFailed,
                "unrestored journal input produced independent public metadata");
    }
    {
        Fixture f; f.create();
        require(f.journal->requestTargetSnapshotProbe(f.device,h<VkCommandBuffer>(105),h<VkPipelineLayout>(102),f.tag,f.cookie,7,true),
                "missing target independent probe publication failed");
        PublicProbeCollector collector; while(f.journal->drain(collector)) {}
        require(!collector.hasSnapshot && collector.last.diagnostics.reason == TargetProbeReason::JournalTargetMissing,
                "missing retained target produced independent public metadata");
    }
    require(!hookAllocations.load(),"independent public probe hook allocated");
}
void exactTargetRetirementObservations() {
    {
        TargetFixture f;
        const auto before = f.journal->transportCounters().published;
        f.journal->destroyView(f.device,f.view); f.journal->destroyImage(f.device,f.image); f.journal->freeMemory(f.device,f.memory);
        f.drain(); const auto result = f.journal->targetRetirementDiagnostics();
        const std::array<Identity,3> expected{{f.target.image.identity,f.target.view.identity,f.target.memory.identity}};
        const std::array<std::uint64_t,3> orders{{before+2,before+1,before+3}};
        const std::array<TargetRetirementOperation,3> operations{{TargetRetirementOperation::DestroyImageCall,
            TargetRetirementOperation::DestroyViewCall,TargetRetirementOperation::FreeMemoryCall}};
        for(unsigned i=0;i<3;++i) require(result.resources[i].resourceKind == static_cast<TrackedResourceKind>(i) &&
            same(result.resources[i].identity,expected[i]) && result.resources[i].replayOrder == orders[i] &&
            result.resources[i].operation == operations[i],"retirement lost exact generation/public call replay order");
        // Recycled handles carry different generations and must not overwrite
        // the first observation of the original public destroy/free calls.
        require(f.journal->createImageTagged(f.device,f.image,f.imageInfo,f.tag+5),"replacement image publication failed");
        f.journal->allocateMemory(f.device,f.memory,f.memoryInfo); f.journal->bindImageMemory(f.device,f.image,f.memory,0);
        require(f.journal->createViewTagged(f.device,f.view,f.viewInfo,f.tag+5),"replacement view publication failed");
        f.journal->destroyView(f.device,f.view); f.journal->destroyImage(f.device,f.image); f.journal->freeMemory(f.device,f.memory);
        f.drain(); const auto repeated = f.journal->targetRetirementDiagnostics();
        for(unsigned i=0;i<3;++i) require(repeated.resources[i].replayOrder == orders[i] &&
            same(repeated.resources[i].identity,expected[i]),"recycled resource replaced original retirement evidence");
    }
    {
        TargetFixture f; const auto before = f.journal->transportCounters().published;
        f.journal->deviceDestroyed(f.device); f.drain(); const auto result = f.journal->targetRetirementDiagnostics();
        for(const auto& resource : result.resources) require(resource.replayOrder == before+1 && resource.identity.generation &&
            resource.operation == TargetRetirementOperation::DestroyDeviceCall,"device destroy lost exact live target generations");
    }
    {
        TargetFixture f; f.journal->destroyView(f.device,f.unrelatedView); f.drain();
        for(const auto& resource : f.journal->targetRetirementDiagnostics().resources)
            require(!resource.replayOrder,"unrelated resource was recorded as target retirement");
    }
}
void backgroundOwnerValidation() {
    Fixture f; f.create();
    const auto receipt = f.receipt();
    require(f.journal->validateImportResourceReceipt(receipt), "caught-up background receipt refused");
    auto alien = std::make_unique<Journal>();
    require(!alien->validateImportResourceReceipt(receipt), "different owning journal accepted receipt");
    std::atomic<bool> entered{false}, release{false};
    std::thread producer([&] { JournalTestAccess::delayedSnapshot(*f.journal, entered, release); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    require(!f.journal->validateImportResourceReceipt(receipt), "active unpublished predecessor ignored");
    const auto active=f.journal->validateImportResourceReceiptDetailed(receipt);
    require(!active.valid && active.transient && active.before_observed && !active.after_observed && !active.core_checked && active.reason==ImportReceiptValidationReason::ActivePublication,
            "active publication was classified permanent");
    release.store(true, std::memory_order_release); producer.join();
    require(!f.journal->validateImportResourceReceipt(receipt), "unreplayed lifecycle ticket ignored");
    const auto pending=f.journal->validateImportResourceReceiptDetailed(receipt);
    require(!pending.valid && pending.transient && pending.before_observed && !pending.after_observed && !pending.core_checked && pending.reason==ImportReceiptValidationReason::PendingReplay,
            "pending replay was classified permanent");
    Collector collector; while (f.journal->drain(collector)) {}
    require(f.journal->validateImportResourceReceipt(receipt), "unchanged resources refused after replay");
    f.journal->destroyImage(f.device, f.image);
    require(!f.journal->validateImportResourceReceipt(receipt), "queued image retirement ignored");
    while (f.journal->drain(collector)) {}
    require(!f.journal->validateImportResourceReceipt(receipt), "retired resource accepted by owner API");
    const auto stale=f.journal->validateImportResourceReceiptDetailed(receipt);
    require(!stale.valid && !stale.transient && stale.before_observed && stale.after_observed && stale.core_checked && stale.reason==ImportReceiptValidationReason::CoreGenerationMismatch,
            "retired generation was classified retryable");
    Fixture closed; closed.create(); const auto closed_receipt = closed.receipt(); closed.journal->close();
    require(!closed.journal->validateImportResourceReceipt(closed_receipt), "closed observation range accepted");
}
}
int main() {
    orderedOwnedSuccess(); unmatchedAndMissing(); retireReuseAndDeviceIsolation();
    unsupportedAndLoss(); duplicateCapacityAndMissingConsumer(); orderedInterleavedScopes(); backgroundOwnerValidation(); targetSnapshotSelection(); targetDenialDiagnostics();
    incompleteDescriptorObservations(); independentPublicDrawSnapshots(); exactTargetRetirementObservations();
    std::cout << checks << " import resource receipt checks passed; " << sizeof(Journal)
              << " bytes fixed state; diagnostics=" << sizeof(TargetProbeDiagnostics)
              << ", retirement=" << sizeof(TargetRetirementDiagnostics)
              << "; no hook allocations; offline lifecycle proof only\n";
}
