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
struct TestAccess { static std::uint64_t serial(Core& core) noexcept { return core.serial_; } };
struct JournalTestAccess {
    static std::uint64_t coreSerial(Journal& journal) noexcept { return TestAccess::serial(journal.core_); }
    static bool validate(Journal& journal, const ImportResourceReceipt& previous) noexcept { return journal.core_.validate(previous); }
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
    void operator()(std::uint32_t, const Snapshot&) noexcept {}
    void operator()(std::uint64_t, const ImportResourceReceipt&) noexcept {}
    void operator()(std::uint32_t, const TargetSnapshotProbeResult& value) noexcept {
        require(value.replayOrder>lastOrder,"probe replay order regressed");lastOrder=value.replayOrder;
        if(value.outcome==TargetProbeOutcome::ExactMatch) {
            require(value.snapshot,"exact match omitted owned replay snapshot");++matched;selected.push_back(*value.snapshot);
        } else {
            require(!value.snapshot,"nonmatch constructed a full snapshot");
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
    unsupportedAndLoss(); duplicateCapacityAndMissingConsumer(); orderedInterleavedScopes(); backgroundOwnerValidation(); targetSnapshotSelection();
    std::cout << checks << " import resource receipt checks passed; " << sizeof(Journal)
              << " bytes fixed state; no hook allocations; offline lifecycle proof only\n";
}
