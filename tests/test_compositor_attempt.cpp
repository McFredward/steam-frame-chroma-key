#include "import/compositor_attempt.h"
#include "layer/passive_binding_journal.h"

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
unsigned checks = 0;
thread_local bool forbid_allocations = false;
std::atomic<unsigned> producer_allocations{0};
void check_at(bool ok, unsigned line) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "compositor attempt check %u failed at %u\n", checks, line); std::abort(); }
}
#define check(ok) check_at((ok), __LINE__)
}
void* operator new(std::size_t size) {
    if (forbid_allocations) producer_allocations.fetch_add(1);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace frame_bindings {
// This worktree predates the root-owned public Journal validator. The fallback
// uses precisely its background-owner pending-publication/lifecycle contract.
// Integrated builds select the actual production method through the overload.
struct JournalTestAccess {
    static bool validate(Journal& j, const ImportResourceReceipt& r) noexcept {
        const auto ticket = j.tickets_.load(std::memory_order_acquire);
        if (j.closed_.load() || j.halted_.load() || j.active_.load() || !j.empty() ||
            j.expected_ != ticket || j.lostState()) return false;
        const auto valid = j.core_.validate(r);
        return valid && !j.active_.load() && j.empty() && j.expected_ == ticket &&
            j.tickets_.load() == ticket && !j.closed_.load() && !j.halted_.load() && !j.lostState();
    }
};
}
namespace {
using namespace frame_compositor_attempt;
using Boundary = frame_import_native::Boundary;
constexpr std::uintptr_t base = 0x100000, bias = 0x500000, pipe = 0x700000, dispatch = base + 1232;
constexpr std::uint32_t sequence = 0xfedcba98;
// Deliberately distinct domains: the actual layer uses a public native VkDevice
// pointer and its own Journal Device::id. Equal small fixture integers hid this.
constexpr std::uint64_t journal_device = 7, native_device = 0xabc0;
template<class T> T h(std::uint64_t v) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<std::uintptr_t>(v));
    else return static_cast<T>(v);
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }
template<class T> auto owner_validate(T& j, const frame_bindings::ImportResourceReceipt& r, int)
    -> decltype(j.validateImportResourceReceipt(r)) { return j.validateImportResourceReceipt(r); }
bool owner_validate(frame_bindings::Journal& j, const frame_bindings::ImportResourceReceipt& r, long) {
    return frame_bindings::JournalTestAccess::validate(j, r);
}
struct ValidatorOwner {
    frame_bindings::Journal* journal = nullptr;
    frame_import_pipe::Registry* registry = nullptr;
    unsigned calls = 0, close_at = 0, destroy_at = 0;
    static bool current(void* opaque, const frame_bindings::ImportResourceReceipt& receipt) noexcept {
        auto& self = *static_cast<ValidatorOwner*>(opaque);
        if (++self.calls == self.close_at) self.registry->close_fd(7);
        if (self.calls == self.destroy_at) {
            self.journal->destroyImage(journal_device, h<VkImage>(10));
            auto ignore = [](auto, const auto&) {};
            while (self.journal->drain(ignore)) {}
        }
        return owner_validate(*self.journal, receipt, 0);
    }
};
struct Fixture {
    frame_import_pipe::Registry registry{500};
    frame_import_pipe::ObjectToken object{};
    frame_import_pipe::Lease lease{};
    std::array<std::uint8_t, 1552> stack{};
    Capture capture{registry};
    std::unique_ptr<frame_bindings::Journal> journal = std::make_unique<frame_bindings::Journal>();
    ValidatorOwner validator{journal.get(), &registry};
    Replay replay{{&validator, ValidatorOwner::current}};
    std::uint64_t tag = 0x800001, cookie = 0x100000009;
    VkImageCreateInfo image_info = info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    VkExternalMemoryImageCreateInfo external = info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    VkMemoryAllocateInfo memory_info = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    VkImportMemoryFdInfoKHR imported = info<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    VkMemoryDedicatedAllocateInfo dedicated = info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    VkImageViewCreateInfo view_info = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    template<class T> void put(std::uintptr_t at, T value) {
        check(at >= base && at - base <= stack.size() - sizeof(T));
        std::memcpy(stack.data() + at - base, &value, sizeof(T));
    }
    void link(std::uintptr_t fp, std::uintptr_t parent, std::uint64_t lr) {
        put(fp, std::uint64_t{parent}); put(fp + 8, std::uint64_t{bias + lr});
    }
    frame_import_pipe::BoundaryProof boundary(bool payload) {
        frame_import_pipe::BoundaryProof p{};
        p.object = object; p.entry_return = payload ? 0x1540b0 : 0x153fc0;
        p.pinned_schema_verified = p.active_owner_verified = p.copied_under_native_mutex = p.native_lock_succeeded = true;
        p.native_payload_read_succeeded = payload; return p;
    }
    Fixture() {
        frame_import_pipe::BirthProof b{};
        b.process_generation = 500; b.pipe = b.mutex_argument = pipe; b.attributes_argument = pipe + 48;
        b.domain = frame_import_pipe::Domain::Compositor045; b.kind = frame_import_pipe::BirthKind::Accepted;
        b.pending_accepted_fd = 7; b.entry_return = 0x1722f4; b.constructor_parent_return = 0x172544;
        b.pinned_schema_verified = b.active_constructor_verified = b.constructor_initializes_peer_pid_zero = true;
        const auto reservation = registry.reserve_birth(b); check(reservation.valid());
        object = registry.created(reservation, 0); check(object.valid());
        lease = registry.activate(boundary(false), 7, 0); check(lease.valid());
        put(dispatch + 108, sequence);
        image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {1152, 4608, 1}; image_info.mipLevels = image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT; image_info.tiling = VK_IMAGE_TILING_LINEAR;
        image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT; image_info.pNext = &external;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        imported.fd = 19; imported.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        dedicated.image = h<VkImage>(10); imported.pNext = &dedicated;
        memory_info.allocationSize = 32 * 1024 * 1024; memory_info.memoryTypeIndex = 2; memory_info.pNext = &imported;
        view_info.image = h<VkImage>(10); view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = image_info.format; view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // A preexisting public resource ensures actual generations differ from
        // import-call ordinals as well as the externally assigned tag/cookie.
        journal->createImage(journal_device, h<VkImage>(99), image_info);
    }
    NativeInput input(Boundary boundary, unsigned offset = 0) {
        NativeInput in{}; in.load_bias = bias; in.lease = lease; in.stack = {stack.data(), base, stack.size()};
        auto& e = in.entry;
        constexpr std::uint64_t sites[] = {0x153fc0,0x1540b0,0x2c75ac,0x2c7684,0x2c7804,0x172ea0,0x172c00,0x172fbc};
        e.x[30] = bias + sites[static_cast<unsigned>(boundary)];
        if (boundary == Boundary::Begin || boundary == Boundary::EarlyUnlock) {
            e.sp = e.x[29] = dispatch; e.x[0] = e.x[19] = pipe; e.x[25] = 1;
            put(dispatch + 128, std::uint64_t{pipe});
        } else if (boundary == Boundary::Image || boundary == Boundary::Bind || boundary == Boundary::View) {
            e.sp = base; e.x[29] = base + 96; e.x[0] = native_device;
            link(base + 96, base + 752, 0x70bc8); link(base + 752, base + 912, 0x13bbdc);
            link(base + 912, base + 1008, 0x13beb0); link(base + 1008, dispatch, 0x154194);
            put(base + 152, std::uint64_t{0xdeadbeef00000000}); put(base + 1024, std::uint64_t{pipe});
            if (boundary == Boundary::Bind) { e.x[1] = 10; e.x[2] = 20; e.x[3] = 0; }
        } else {
            const auto send = dispatch - 80;
            e.sp = e.x[29] = send; link(send, dispatch, 0x1542b0); put(send + 64, std::uint64_t{pipe});
            if (boundary == Boundary::SendLock) {
                e.x[0] = e.x[19] = pipe; e.x[20] = 9; e.x[21] = 0x235; e.x[22] = sequence;
            } else if (boundary == Boundary::Write) {
                e.sp = e.x[29] = send - 112; link(send - 112, send, 0x172f34);
                e.x[0] = 7; e.x[19] = offset; e.x[20] = 21; e.x[22] = pipe; e.x[23] = send - 32;
                e.x[1] = send - 32 + offset; e.x[2] = 21 - offset;
            } else { e.sp = send - 32; e.x[0] = pipe; e.x[20] = 1; }
        }
        return in;
    }
    void begin() { check(capture.begin(input(Boundary::Begin), 0, tag, cookie)); }
    void early() {
        lease = registry.activate(boundary(true), 7, 7123); check(lease.valid());
        check(capture.early_unlock(input(Boundary::EarlyUnlock), 0));
    }
    void image(std::uint64_t observed_device = journal_device) {
        check(capture.prepare_vulkan(Boundary::Image, input(Boundary::Image)) == tag);
        check(journal->createImageTagged(journal_device, h<VkImage>(10), image_info, tag));
        check(capture.image_result({observed_device,10,0,static_cast<std::uint32_t>(image_info.format),1152,4608,1,1,0,false,native_device}));
    }
    void bind(std::uint64_t observed_device = journal_device) {
        journal->allocateMemory(journal_device, h<VkDeviceMemory>(20), memory_info);
        check(capture.prepare_vulkan(Boundary::Bind, input(Boundary::Bind)) == tag);
        journal->bindImageMemory(journal_device, h<VkImage>(10), h<VkDeviceMemory>(20), 0);
        check(capture.bind_result({observed_device,10,20,0,0,native_device}));
    }
    void view(std::uint64_t observed_device = journal_device) {
        check(capture.prepare_vulkan(Boundary::View, input(Boundary::View)) == tag);
        check(journal->createViewTagged(journal_device, h<VkImageView>(30), view_info, tag));
        check(capture.view_result({observed_device,10,30,static_cast<std::uint32_t>(view_info.format),0,native_device}));
    }
    void through_view() { begin(); early(); image(); bind(); view(); }
    std::array<std::uint8_t,21> packet() {
        std::array<std::uint8_t,21> p{};
        const std::uint32_t words[] = {0x235, sequence, 9};
        for (unsigned w=0; w<3; ++w) for (unsigned b=0;b<4;++b) p[w*4+b]=words[w]>>(8*b);
        p[12] = 9; const std::uint64_t token = 0x123456789abcdef0;
        for (unsigned i=0;i<8;++i) p[13+i]=token>>(8*i);
        return p;
    }
    void writes(unsigned chunk = 21, bool corrupt = false) {
        check(capture.send_lock(input(Boundary::SendLock), 0));
        auto p = packet(); if (corrupt) p[4] ^= 1;
        for (unsigned offset=0;offset<21;) {
            const auto count = std::min(chunk,21-offset); std::array<std::uint8_t,21> prefix{};
            std::memcpy(prefix.data(),p.data()+offset,count);
            check(capture.prepare_write(input(Boundary::Write,offset)));
            check(capture.write_result(prefix,count,count)); offset+=count;
        }
    }
    OwnedAttempt completed(unsigned chunk = 21, bool corrupt = false) {
        through_view(); writes(chunk,corrupt); check(capture.finish(input(Boundary::Finish),0));
        OwnedAttempt a; check(capture.take(a)); check(a.complete()); return a;
    }
    frame_bindings::ImportResourceReceipt receipt() {
        check(journal->requestImportResourceReceipt(journal_device,h<VkImage>(10),h<VkImageView>(30),tag,cookie));
        frame_bindings::ImportResourceReceipt result{}; unsigned count=0;
        struct Collector {
            frame_bindings::ImportResourceReceipt& result; unsigned& count;
            void operator()(std::uint32_t,const frame_bindings::Snapshot&) {}
            void operator()(std::uint64_t,const frame_bindings::ImportResourceReceipt& r) { result=r; ++count; }
        } collector{result,count};
        while(journal->drain(collector)) {}
        check(count == 1 && result.resourceComplete); return result;
    }
    Refusal run(const OwnedAttempt& attempt, const frame_bindings::ImportResourceReceipt& receipt,
                frame_import::Record& result) { return replay.replay(attempt,receipt,registry,result); }
};
void denials(const frame_import::Record& r) {
    check(!r.native_cross_process_namespace && !r.source_frame_associated && !r.accepted_submit && !r.gpu_consumed_coherent);
}
void device_identity_controls() {
    { Fixture f; const auto a=f.completed(); const auto receipt=f.receipt(); frame_import::Record r;
      check(a.device()==journal_device && journal_device!=native_device);
      check(receipt.device==journal_device && f.run(a,receipt,r)==Refusal::None);
      check(r.image.device==journal_device && r.binding.device==journal_device && r.view.device==journal_device);
      denials(r); }
    { Fixture f; f.begin(); f.early();
      check(f.capture.prepare_vulkan(Boundary::Image,f.input(Boundary::Image))==f.tag);
      check(!f.capture.image_result({journal_device,10,0,37,1152,4608,1,1,0,false,native_device+1}));
      check(f.capture.refusal()==Refusal::PublicMetadata); }
    { Fixture f; f.begin(); f.early(); f.image();
      check(f.capture.prepare_vulkan(Boundary::Bind,f.input(Boundary::Bind))==f.tag);
      check(!f.capture.bind_result({journal_device+1,10,20,0,0,native_device})); }
    { Fixture f; f.begin(); f.early(); f.image();
      auto in=f.input(Boundary::Bind); in.entry.x[0]=native_device+16;
      check(f.capture.prepare_vulkan(Boundary::Bind,in)==f.tag);
      // Equal current entry handles still cannot migrate an existing attempt
      // to another device after its image was created.
      check(!f.capture.bind_result({journal_device,10,20,0,0,native_device+16})); }
    { Fixture f; f.begin(); f.early(); f.image(); f.bind();
      check(f.capture.prepare_vulkan(Boundary::View,f.input(Boundary::View))==f.tag);
      check(!f.capture.view_result({journal_device+1,10,30,37,0,native_device})); }
    { Fixture f; f.begin(); f.early(); f.image(); f.bind();
      auto in=f.input(Boundary::View); in.entry.x[0]=native_device+16;
      check(f.capture.prepare_vulkan(Boundary::View,in)==f.tag);
      check(!f.capture.view_result({journal_device,10,30,37,0,native_device+16})); }
    { Fixture f; f.begin(); f.early(); f.image(journal_device+1); f.bind(journal_device+1); f.view(journal_device+1);
      f.writes(); check(f.capture.finish(f.input(Boundary::Finish),0)); OwnedAttempt a;
      check(f.capture.take(a)); const auto receipt=f.receipt(); frame_import::Record r;
      // A consistently mislabelled copied Journal ID is refused by the real
      // receipt's independently owned device identity, not its native handle.
      check(f.run(a,receipt,r)==Refusal::Receipt && !r.import_reply_complete); }
}
}

int main(int argc, char** argv) {
    device_identity_controls();
    if(argc==2 && std::strcmp(argv[1],"--device-identity-only")==0) {
        std::printf("%u focused native-handle/Journal-device identity checks passed; Journal7 and VkDevice0xabc0 are distinct\n",checks);
        return 0;
    }
    for (unsigned chunk : {21U, 7U, 1U}) {
        Fixture f; forbid_allocations=true;
        const auto a=f.completed(chunk); forbid_allocations=false;
        check(!producer_allocations.load());
        const auto receipt=f.receipt(); frame_import::Record r;
        check(f.run(a,receipt,r) == Refusal::None && r.import_reply_complete && !r.reason);
        check(r.token == 0x123456789abcdef0 && r.written_bytes == 21 && r.successful_writes == (21+chunk-1)/chunk);
        check(r.image.image_generation == receipt.image.identity.generation && r.image.image_generation == 2);
        check(r.binding.memory_generation == receipt.memory.identity.generation && r.view.view_generation == receipt.view.identity.generation);
        check(r.route.client_identity_available && r.route.client_identity == 0 && r.route.client_generation == a.tag());
        check(a.tag() != r.image.image_generation && a.cookie() != r.binding.memory_generation);
        denials(r); check(f.run(a,receipt,r) == Refusal::RepeatedReplay);
        OwnedAttempt empty; check(!f.capture.take(empty));
    }
    { Fixture f; f.begin(); check(!f.capture.prepare_vulkan(Boundary::Image,f.input(Boundary::Image)));
      f.capture.abandon(); OwnedAttempt a; check(f.capture.take(a) && !a.complete() && a.refusal()==Refusal::Ordering); }
    { Fixture f; f.begin(); check(!f.capture.begin(f.input(Boundary::Begin),0,f.tag+1,f.cookie+1));
      f.capture.abandon(); OwnedAttempt a; check(f.capture.take(a) && a.refusal()==Refusal::Nested); }
    { Fixture f; f.begin(); f.lease=f.registry.activate(f.boundary(true),7,7123);
      auto in=f.input(Boundary::EarlyUnlock); in.entry.x[25]=0;
      check(!f.capture.early_unlock(in,0)); f.capture.abandon(); OwnedAttempt a; check(f.capture.take(a)&&!a.complete()); }
    { Fixture f; f.begin(); f.early(); auto in=f.input(Boundary::Image); in.entry.x[30]+=4;
      check(!f.capture.prepare_vulkan(Boundary::Image,in)); check(f.capture.refusal()==Refusal::NativeRoute); }
    { Fixture f; f.begin(); f.early(); f.registry.close_fd(7);
      check(!f.capture.prepare_vulkan(Boundary::Image,f.input(Boundary::Image)));
      check(f.capture.refusal()==Refusal::Lease); }
    { Fixture f; auto in=f.input(Boundary::Begin); ++in.lease.socket_generation;
      check(!f.capture.begin(in,0,f.tag,f.cookie) && f.capture.refusal()==Refusal::Lease); }
    { Fixture f; f.begin(); f.early(); auto in=f.input(Boundary::Image); f.put(dispatch+108,std::uint32_t{sequence+1});
      check(!f.capture.prepare_vulkan(Boundary::Image,in)); check(f.capture.refusal()==Refusal::NativeRoute); }
    { Fixture f; f.begin(); f.early(); f.image(); auto in=f.input(Boundary::Bind); f.put(base+152,std::uint64_t{1});
      check(!f.capture.prepare_vulkan(Boundary::Bind,in)); check(f.capture.refusal()==Refusal::NativeRoute); }
    { Fixture f; f.begin(); f.early(); auto in=f.input(Boundary::Image); in.entry.x[0]=2;
      check(f.capture.prepare_vulkan(Boundary::Image,in));
      check(!f.capture.image_result({journal_device,10,0,37,1152,4608,1,1,0,false,native_device})); }
    { Fixture f; f.begin(); f.early(); f.image(); auto in=f.input(Boundary::Bind); in.entry.x[2]=21;
      check(f.capture.prepare_vulkan(Boundary::Bind,in)); check(!f.capture.bind_result({journal_device,10,20,0,0,native_device})); }
    { Fixture f; f.begin(); f.early(); check(f.capture.prepare_vulkan(Boundary::Image,f.input(Boundary::Image)));
      check(!f.capture.image_result({journal_device,10,0,37,1152,4608,1,1,-1,false,native_device})); }
    { Fixture f; f.through_view(); check(f.capture.send_lock(f.input(Boundary::SendLock),0));
      check(f.capture.prepare_write(f.input(Boundary::Write)));
      check(!f.capture.write_result(f.packet(),20,21));
      check(!f.capture.finish(f.input(Boundary::Finish),0)); OwnedAttempt a; check(f.capture.take(a)&&!a.complete());
      check(a.native_send_success() && a.native_unlock_result()==0); }
    { Fixture f; f.through_view(); check(f.capture.send_lock(f.input(Boundary::SendLock),0));
      check(f.capture.prepare_write(f.input(Boundary::Write))); check(!f.capture.write_result({},0,-1));
      auto end=f.input(Boundary::Finish); end.entry.x[20]=0; check(!f.capture.finish(end,0)); }
    { Fixture f; f.through_view(); check(f.capture.send_lock(f.input(Boundary::SendLock),0));
      check(!f.capture.prepare_write(f.input(Boundary::Write,1))); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt(); f.registry.close_fd(7);
      frame_import::Record out; check(f.run(a,r,out)==Refusal::Lease && !out.import_reply_complete); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt(); f.registry.observer_loss();
      frame_import::Record out; check(f.run(a,r,out)==Refusal::Lease); }
    { Fixture f; f.through_view(); f.capture.observer_loss(); f.capture.abandon();
      OwnedAttempt a; check(f.capture.take(a)&&a.refusal()==Refusal::ObserverLoss);
      check(!f.capture.begin(f.input(Boundary::Begin),0,f.tag+1,f.cookie+1)); }
    { Fixture f; f.capture.observer_loss(); check(!f.capture.begin(f.input(Boundary::Begin),0,f.tag,f.cookie)); }
    { Fixture f; auto a=f.completed(7,true); auto r=f.receipt(); frame_import::Record out;
      check(f.run(a,r,out)==Refusal::Incomplete && (out.reason&frame_import::BadReply)); denials(out); }
    for (unsigned kind=0;kind<10;++kind) {
        Fixture f; const auto a=f.completed(); auto r=f.receipt();
        if(kind==0) ++r.invocationTag;
        if(kind==1) ++r.cookie;
        if(kind==2) ++r.imageInvocationTag;
        if(kind==3) ++r.viewInvocationTag;
        if(kind==4) ++r.image.identity.generation;
        if(kind==5) ++r.memory.identity.generation;
        if(kind==6) ++r.view.identity.generation;
        if(kind==7) ++r.image.extent.width;
        if(kind==8) r.nativeCrossProcessNamespace=true;
        if(kind==9) ++r.lossCount;
        frame_import::Record out; check(f.run(a,r,out)!=Refusal::None && !out.import_reply_complete); denials(out);
    }
    { Fixture f; const auto a=f.completed(); auto r=f.receipt();
      f.journal->destroyImage(journal_device,h<VkImage>(10)); // Not replayed yet: pending destroy must already refuse.
      frame_import::Record out; check(f.run(a,r,out)==Refusal::ResourceNotCurrent); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt();
      f.journal->createImage(journal_device,h<VkImage>(777),f.image_info); // Unrelated pending work: defer, then permit after drain.
      frame_import::Record out; check(f.run(a,r,out)==Refusal::ResourceNotCurrent);
      auto ignore=[](auto,const auto&) {}; while(f.journal->drain(ignore)) {}
      check(f.run(a,r,out)==Refusal::None && out.import_reply_complete); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt();
      f.validator.destroy_at=2; frame_import::Record out;
      check(f.run(a,r,out)==Refusal::ResourceNotCurrent && !out.import_reply_complete && (out.reason&frame_import::ResourceRetired)); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt();
      f.validator.close_at=2; frame_import::Record out;
      check(f.run(a,r,out)==Refusal::Lease && !out.import_reply_complete && (out.reason&frame_import::SocketInvalidated)); }
    { Fixture f; const auto a=f.completed(); const auto r=f.receipt();
      Replay unsupported{{}}; frame_import::Record out; check(unsupported.replay(a,r,f.registry,out)==Refusal::Receipt); }
    { Fixture f; f.through_view(); f.writes(); auto end=f.input(Boundary::Finish); end.entry.x[20]=0;
      check(!f.capture.finish(end,0)); OwnedAttempt a; check(f.capture.take(a)&&!a.complete()); }
    { Fixture f; f.through_view(); f.writes(); f.registry.close_fd(7);
      check(!f.capture.finish(f.input(Boundary::Finish),0)); OwnedAttempt a; check(f.capture.take(a)&&a.refusal()==Refusal::Lease); }
    check(!producer_allocations.load());
    std::printf("%u copy-only compositor attempt checks passed; synthetic native windows, real Journal generations; no live observer or namespace/source/GPU proof\n",checks);
}
