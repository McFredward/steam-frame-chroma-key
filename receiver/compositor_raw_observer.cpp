#include "compositor_raw_runtime.h"
#include "compositor_arm_marker.h"
#include "sha256.h"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern "C" {
// The generated selector is a separately reviewed launch artifact. Weak imports
// keep an ordinary/unsupported loader configuration inert; there is no fallback
// to broad service-preload observation or late environment mutation.
__attribute__((weak)) int frame_compositor_launch_freeze_environment_v1(unsigned);
__attribute__((weak)) unsigned frame_compositor_launch_provider_mask(void);
__attribute__((weak)) unsigned frame_compositor_launch_frozen_status_v1(void);
__attribute__((visibility("hidden"),used)) extern const unsigned char frame_compositor_raw_scope[16]={
    'F','C','M','R','A','W','0','5',5,0,0,0,0x50,0x20,0,0};
__attribute__((visibility("hidden"))) std::atomic<unsigned> frame_compositor_raw_ready{0};
__attribute__((visibility("hidden"))) void frame_import_lock_entry();
__attribute__((visibility("hidden"))) void frame_import_unlock_entry();
__attribute__((visibility("hidden"))) void frame_import_write_entry();
int __register_atfork(void(*)(void),void(*)(void),void(*)(void),void*) noexcept;
}

namespace {
namespace runtime=frame_compositor_runtime;
constexpr unsigned LaunchFreezeAbi=1,LaunchProviderMask=63;
constexpr std::size_t EnvelopeBytes=8272,OutputBytes=65536;
constexpr std::uint64_t DurationNs=120ULL*1000000000;
constexpr unsigned JoinAttemptLimit=64;
constexpr std::uint64_t JoinRetryDelayNs=1000000;
constexpr std::size_t FooterReserve=4096;
static_assert(frame_compositor_journal::DeliveryValidationAttemptLimit==64);
constexpr char ResourceJoinValidation[]=
    "{\"schema\":\"frame-compositor-resource-join-validation-v1\",\"attempt_limit\":64,\"delivery_attempt_limit\":64,"
    "\"retry_delay_ns\":1000000,\"same_capture_deadline\":true,\"immutable_snapshot\":true,"
    "\"unchanged_owning_journal_validator\":true,\"journal_refusal_cause_classified\":true}";
constexpr char CandidateSelection[]=
    "{\"schema\":\"frame-compositor-public-image-selection-v1\","
    "\"public_formats\":[1000156003,1000156013],\"even_extent\":true,"
    "\"depth\":1,\"layers\":1,\"private_acquisition\":\"image-before\","
    "\"begin_early_private_replay\":false,\"thread_slots\":16,\"stream_origin_proven\":false}";
std::atomic<bool> stopping{false};
pthread_t writer{};
bool writer_started=false;
pid_t owner_pid=0;
int output_fd=-1;
char output_path[1024]{};
char arm_path[1024]{};
std::uint64_t capture_started_ns=0;
frame_compositor_main::Digest observer_identity{};

using LaunchFreeze=int(*)(unsigned);
using LaunchProviders=unsigned(*)(void);
using LaunchFrozen=unsigned(*)(void);
bool freeze_before_observation(LaunchFreeze freeze,LaunchProviders providers,LaunchFrozen frozen) noexcept {
    const int saved=errno;
    // Do not assume constructor order between the two preload objects. The
    // selector owns provider initialization and proves single-threaded target
    // admission inside this explicit cold handshake. No getenv/Journal/thread
    // work in this observer may precede successful, owner-PID-local freezing.
    const bool accepted=freeze && providers && frozen && freeze(LaunchFreezeAbi)==0 &&
        providers()==LaunchProviderMask && frozen()==1;
    errno=saved;return accepted;
}

bool owned() noexcept {return owner_pid>0 && getpid()==owner_pid;}
void child_retire() noexcept {
    const auto saved=errno;
    // No inherited writer join, Journal lock, filesystem access or producer wait.
    runtime::finalize();frame_compositor_journal::retire_child_atomically();
    stopping.store(true,std::memory_order_release);writer_started=false;
    errno=saved;
}
std::uint64_t little(const unsigned char* p,std::size_t count) noexcept {
    std::uint64_t value=0;for(std::size_t i=0;i<count;++i)value|=std::uint64_t(p[i])<<(8*i);return value;
}
bool path(const char* source,char* target,std::size_t capacity) noexcept {
    if(!source || source[0]!='/')return false;
    const auto n=strnlen(source,capacity);if(!n || n>=capacity)return false;
    std::memcpy(target,source,n+1);return true;
}
bool stable(const struct stat& a,const struct stat& b) noexcept {
    return a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_mode==b.st_mode &&
        a.st_uid==b.st_uid && a.st_gid==b.st_gid && a.st_nlink==b.st_nlink && a.st_size==b.st_size &&
        a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool envelope(const char* name,frame_compositor_main::Profiles& profiles) noexcept {
    const int fd=open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;
    struct stat a{},b{};std::array<unsigned char,EnvelopeBytes> bytes{};
    bool ok=!fstat(fd,&a) && S_ISREG(a.st_mode) && a.st_uid==geteuid() && a.st_nlink==1 &&
        (a.st_mode&07777)==0600 && a.st_size==EnvelopeBytes;
    std::size_t copied=0;
    for(unsigned tries=0;ok && copied<bytes.size() && tries<32;++tries) {
        const auto n=read(fd,bytes.data()+copied,bytes.size()-copied);
        if(n<=0)ok=false;else copied+=static_cast<std::size_t>(n);
    }
    ok=ok && copied==bytes.size() && !fstat(fd,&b) && stable(a,b);close(fd);
    if(!ok || std::memcmp(bytes.data(),frame_compositor_raw_scope,16))return false;
    frame_receiver::Sha256 hash;hash.update(bytes.data(),bytes.size()-32);
    if(std::memcmp(hash.finish().data(),bytes.data()+bytes.size()-32,32))return false;
    constexpr std::array<std::uint64_t,8> limits{{frame_compositor_raw::kMaxScalarBytes,21,16,8,32,65536,120,4}};
    for(std::size_t i=0;i<limits.size();++i)if(little(bytes.data()+48+8*i,8)!=limits[i])return false;
    std::memcpy(profiles.reviewed_observer_identity.data(),bytes.data()+16,32);
    constexpr unsigned roles[]{7,3,4,5};
    for(unsigned i=0;i<4;++i) {
        frame_import_main::ProfileBlob blob{};std::memcpy(blob.data(),bytes.data()+112+i*blob.size(),blob.size());
        if(!frame_import_main::decode_profile(blob,i==3?profiles.reviewed_observer_identity:frame_import_main::Digest{},profiles.modules[i]) ||
           profiles.modules[i].role!=roles[i])return false;
    }
    return true;
}
template<class Function> bool pin(const frame_compositor_main::Prepared& prepared,
    const char* name,const char* version,std::uintptr_t rva,Function& result) noexcept {
    // Startup only: provider discovery never occurs on a graphics/native hook.
    void* symbol=dlvsym(RTLD_NEXT,name,version);const auto address=reinterpret_cast<std::uintptr_t>(symbol);
    const auto libc=prepared.images()[1];Dl_info info{};
    if(!symbol || address!=libc.bias+rva || address<libc.rx_begin || address>=libc.rx_end ||
       !prepared.lookup()(symbol,&info) || reinterpret_cast<std::uintptr_t>(info.dli_fbase)!=libc.bias)return false;
    result=reinterpret_cast<Function>(symbol);return true;
}
bool setup(const frame_compositor_main::Prepared& prepared,const frame_compositor_main::Profiles& profiles,runtime::Setup& s) noexcept {
    s.lock_entry=reinterpret_cast<std::uintptr_t>(&frame_import_lock_entry);
    s.unlock_entry=reinterpret_cast<std::uintptr_t>(&frame_import_unlock_entry);
    s.write_entry=reinterpret_cast<std::uintptr_t>(&frame_import_write_entry);
    if(!pin(prepared,"pthread_self","GLIBC_2.17",0x8b090,s.self) ||
       !pin(prepared,"pthread_getattr_np","GLIBC_2.17",0x85140,s.attributes) ||
       !pin(prepared,"pthread_attr_getstack","GLIBC_2.17",0x816d0,s.get_stack) ||
       !pin(prepared,"pthread_attr_destroy","GLIBC_2.17",0x81420,s.destroy_attributes))return false;
    s.main_thread=s.self();
    const auto images=prepared.images();const auto marker=reinterpret_cast<std::uintptr_t>(&frame_compositor_raw_ready);
    unsigned found=0;
    for(std::size_t i=0;i<profiles.modules[3].phnum;++i) {
        const auto& p=profiles.modules[3].headers[i];
        if(p.type==PT_LOAD && (p.flags&(PF_R|PF_W|PF_X))==(PF_R|PF_W) &&
           marker>=images[3].bias+p.virtual_address && marker-images[3].bias-p.virtual_address<=p.memory_size &&
           sizeof(frame_compositor_raw_ready)<=p.memory_size-(marker-images[3].bias-p.virtual_address)) {
            s.self_data_begin=images[3].bias+p.virtual_address;s.self_data_end=s.self_data_begin+p.memory_size;++found;
        }
    }
    const auto registration=reinterpret_cast<std::uintptr_t>(&::__register_atfork);
    const auto atfork=reinterpret_cast<std::uintptr_t>(&::pthread_atfork);Dl_info info{};
    if(found!=1 || registration!=images[1].bias+0xd35c0 || !prepared.lookup()(reinterpret_cast<void*>(registration),&info) ||
       reinterpret_cast<std::uintptr_t>(info.dli_fbase)!=images[1].bias)return false;
    const bool own_atfork=atfork>=images[3].rx_begin && atfork<images[3].rx_end;
    const bool libc_atfork=atfork>=images[1].rx_begin && atfork<images[1].rx_end;
    return (own_atfork||libc_atfork) && ::pthread_atfork(nullptr,nullptr,child_retire)==0;
}
int open_output() noexcept {
    char parent[1024]{};std::memcpy(parent,output_path,sizeof(parent));
    char* slash=std::strrchr(parent,'/');if(!slash || !slash[1])return -1;
    const char* basename=output_path+(slash-parent)+1;
    if(slash==parent)slash[1]=0;else *slash=0;
    const int dir=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir<0)return -1;
    struct stat s{};int fd=-1;
    if(!fstat(dir,&s) && S_ISDIR(s.st_mode) && s.st_uid==geteuid() && (s.st_mode&07777)==0700)
        fd=openat(dir,basename,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    close(dir);
    if(fd>=0 && flock(fd,LOCK_EX|LOCK_NB)){close(fd);fd=-1;}
    return fd;
}
struct Line {
    std::array<char,8192> bytes{};std::size_t used=0;bool valid=true;
    void add(const char* format,...) noexcept {
        if(!valid)return;
        va_list args;va_start(args,format);
        const int n=vsnprintf(bytes.data()+used,bytes.size()-used,format,args);va_end(args);
        if(n<0 || static_cast<std::size_t>(n)>=bytes.size()-used)valid=false;else used+=static_cast<std::size_t>(n);
    }
    template<class T> void hex(const T& data) noexcept {for(auto v:data)add("%02x",unsigned(v));}
};
unsigned long long u(std::uint64_t v) noexcept {return static_cast<unsigned long long>(v);}
bool write_line(const Line& line,std::size_t& total,std::size_t reserve=FooterReserve) noexcept {
    if(!owned() || !line.valid || total>OutputBytes || reserve>OutputBytes-total || line.used>OutputBytes-total-reserve)return false;
    std::size_t sent=0;
    for(unsigned tries=0;sent<line.used && tries<32;++tries) {
        const auto n=write(output_fd,line.bytes.data()+sent,line.used-sent);if(n<=0)return false;sent+=static_cast<std::size_t>(n);
    }
    if(sent!=line.used)return false;
    total+=sent;return true;
}
void proofs(Line& l) noexcept {
    l.add(",\"native_cross_endpoint_associated\":false,\"native_namespace_associated\":false,\"source_frame_associated\":false,"
        "\"uv_owned\":false,\"accepted_submit\":false,\"gpu_consumed\":false,\"mask_enabled\":false");
}
void validation_details(Line&,const char*,const frame_compositor_journal::Validation&,bool) noexcept;
void record(Line& l,const runtime::Record& r) noexcept {
    l.add("{\"schema\":1,\"ordinal\":%llu,\"kind\":%u,\"refusal\":%u,\"thread_reason\":%u,\"forward_only\":%s,"
        "\"entry_lr\":\"%llx\",\"entry_sp\":\"%llx\",\"entry_fp\":\"%llx\",\"audit_function\":%u,\"audit_original\":\"%llx\"",
        u(r.ordinal),unsigned(r.kind),unsigned(r.refusal),r.thread_reason,r.forward_only?"true":"false",u(r.entry.x[30]),u(r.entry.sp),u(r.entry.x[29]),unsigned(r.bindings.function),u(r.bindings.original));
    if(r.kind==runtime::Kind::Attached || r.kind==runtime::Kind::Bound) {
        l.add(",\"audit_views\":[");unsigned count=0;
        for(const auto& v:{r.bindings.compositor,r.bindings.observer,r.bindings.libc}) {
            l.add("%s{\"bias\":\"%llx\",\"rx\":[\"%llx\",\"%llx\"],\"loader_open_generation\":%llu,\"rw\":[\"%llx\",\"%llx\"]}",
                count++?",":"",u(v.bias),u(v.rx_begin),u(v.rx_end),u(v.observed_open_generation),u(v.data_begin),u(v.data_end));
        }
        l.add("]");
    }
    if(r.kind==runtime::Kind::RawMatched || r.kind==runtime::Kind::RawRefused) {
        const auto& c=r.raw;
        l.add(",\"raw\":{\"matched\":%s,\"reason\":%llu,\"decoder_refusal\":%u,"
            "\"last_boundary\":%u,\"last_phase\":%u,\"creation_route\":%u,\"last_native_result\":%lld,\"last_native_result_available\":%s,\"client\":%u,\"fd\":%d,\"writes\":%u,"
            "\"reply_token\":\"%llx\",\"reply_bytes\":%u,\"scalar_copies\":%zu,\"wire_copies\":%zu,\"reply\":\"",
            c.matched?"true":"false",u(c.reason),unsigned(c.decoder_refusal),unsigned(c.last_boundary),unsigned(c.last_phase),unsigned(c.creation_route),
            static_cast<long long>(c.last_native_result),c.last_native_result_available?"true":"false",c.client,c.fd,c.writes,u(c.reply_token),c.reply_bytes,c.scalar_bytes_copied,c.wire_bytes_copied);
        l.hex(c.reply);
        l.add("\",\"sequence\":%u,\"pipe\":\"%llx\",\"dispatch_frame\":\"%llx\",\"device\":%llu,\"native_device\":\"%llx\",\"image\":\"%llx\",\"memory\":\"%llx\",\"memory_offset\":%llu,"
            "\"view\":\"%llx\",\"format\":%u,\"extent\":[%u,%u,%u],\"layers\":%u,\"native_payload_read\":%s,"
            "\"begin_lock_succeeded\":%s,\"early_unlock_succeeded\":%s,\"send_lock_succeeded\":%s,\"finish_unlock_succeeded\":%s,\"native_send_success\":%s}",
            c.invocation.sequence,u(c.invocation.pipe),u(c.invocation.dispatch_frame),u(c.image.device),u(c.image.native_device),u(c.image.image),u(c.bind.memory),u(c.bind.offset),u(c.view.view),c.image.format,
            c.image.width,c.image.height,c.image.depth,c.image.layers,c.native_payload_read?"true":"false",c.begin_lock_succeeded?"true":"false",
            c.early_unlock_succeeded?"true":"false",c.send_lock_succeeded?"true":"false",c.finish_unlock_succeeded?"true":"false",c.native_send_success?"true":"false");
        if(c.selected_at_image) {
            l.add(",\"public_image_selection\":{\"selected_at_image\":true,\"private_acquisition_started_at_image\":%s,"
                "\"begin_early_private_replay\":false,\"format\":%u,\"extent\":[%u,%u,%u],\"layers\":%u,"
                "\"begin\":{\"sp\":\"%llx\",\"lr\":\"%llx\",\"pipe\":\"%llx\",\"result\":%lld},"
                "\"early\":{\"sp\":\"%llx\",\"lr\":\"%llx\",\"pipe\":\"%llx\",\"result\":%lld},"
                "\"scope\":\"retained_actual_public_pairs_and_current_creation_scalars\",\"stream_origin_proven\":false}",
                c.private_acquisition_started_at_image?"true":"false",c.selected_image.format,
                c.selected_image.width,c.selected_image.height,c.selected_image.depth,c.selected_image.layers,
                u(c.selected_begin_entry.sp),u(c.selected_begin_entry.x[30]),u(c.selected_begin_entry.x[0]),
                static_cast<long long>(c.selected_begin_result),u(c.selected_early_entry.sp),
                u(c.selected_early_entry.x[30]),u(c.selected_early_entry.x[0]),static_cast<long long>(c.selected_early_result));
        }
    }
    if(r.kind==runtime::Kind::Receipt) {
        const auto& c=r.receipt.resource;
        l.add(",\"receipt\":{\"journal\":%llu,\"replay_order\":%llu,\"resource_cursor\":%llu,\"tag\":\"%llx\",\"cookie\":\"%llx\","
            "\"device\":%llu,\"image\":\"%llx\",\"image_generation\":%llu,\"view\":\"%llx\",\"view_generation\":%llu,"
            "\"memory\":\"%llx\",\"memory_generation\":%llu,\"complete\":%s,\"errors\":%u,\"loss\":%llu,\"current_after_drain\":%s,"
            "\"requested_image\":\"%llx\",\"requested_view\":\"%llx\",\"image_tag\":\"%llx\",\"view_tag\":\"%llx\","
            "\"image_memory\":{\"handle\":\"%llx\",\"generation\":%llu},"
            "\"view_image\":{\"handle\":\"%llx\",\"generation\":%llu},\"memory_offset\":%llu,"
            "\"image_format\":%u,\"view_format\":%u,\"extent\":[%u,%u,%u],\"layers\":%u,\"reason\":%u,"
            "\"image_complete\":%s,\"view_complete\":%s,\"memory_complete\":%s,\"allocation_size\":%llu,"
            "\"native_import_accepted\":%s,\"source_frame_associated\":%s,\"native_cross_process_namespace\":%s,"
            "\"accepted_submit\":%s,\"gpu_consumed_coherent\":%s",
            u(c.journalInstance),u(c.localReplayOrder),u(c.resourceGenerationCursor),u(c.invocationTag),u(c.cookie),u(c.device),u(c.image.identity.handle),u(c.image.identity.generation),
            u(c.view.identity.handle),u(c.view.identity.generation),u(c.memory.identity.handle),u(c.memory.identity.generation),c.resourceComplete?"true":"false",c.errors,u(c.lossCount),r.receipt.current_after_drain?"true":"false",
            u(c.requestedImage),u(c.requestedView),u(c.imageInvocationTag),u(c.viewInvocationTag),u(c.image.memory.handle),u(c.image.memory.generation),
            u(c.view.image.handle),u(c.view.image.generation),u(c.image.memoryOffset),unsigned(c.image.format),unsigned(c.view.format),
            c.image.extent.width,c.image.extent.height,c.image.extent.depth,c.image.arrayLayers,unsigned(c.reason),
            c.image.complete?"true":"false",c.view.complete?"true":"false",c.memory.complete?"true":"false",u(c.memory.allocationSize),
            c.nativeImportAccepted?"true":"false",c.sourceFrameAssociated?"true":"false",c.nativeCrossProcessNamespace?"true":"false",c.acceptedSubmit?"true":"false",c.gpuConsumedCoherent?"true":"false");
        l.add(",\"delivery_validation_attempts\":%u",r.receipt.delivery_validation_attempts);
        validation_details(l,"delivery_validation",r.receipt.delivery_validation,r.receipt.delivery_validation_attempts!=0);
        l.add("}");
    }
    proofs(l);l.add("}\n");
}
// Immutable compatibility and live currentness are deliberately separate. A
// typed owning validation separates momentary publication/replay changes from
// permanent refusal; only that unchanged validator may later accept the same
// immutable receipt. No retry can refresh a handle, generation or private read.
enum class JoinPredicate : unsigned {
    RawMatched,NativeSendSuccess,ReplyToken,ReceiptCurrentAfterDrain,ResourceComplete,ReceiptErrors,ReceiptLoss,
    NativeImportAccepted,SourceFrameAssociated,NativeCrossProcessNamespace,AcceptedSubmit,GpuConsumedCoherent,
    InvocationTag,Cookie,ImageInvocationTag,ViewInvocationTag,Device,RequestedImage,RequestedView,
    ImageHandle,ViewHandle,ViewImageIdentity,MemoryHandle,ImageMemoryIdentity,MemoryOffset,
    ImageFormat,ViewFormat,ExtentWidth,ExtentHeight,ExtentDepth,ArrayLayers
};
constexpr std::uint64_t join_bit(JoinPredicate predicate) noexcept {return std::uint64_t(1)<<unsigned(predicate);}
std::uint64_t compatibility(const frame_compositor_raw::RawRecord& raw,
    const frame_compositor_journal::Receipt& value) noexcept {
    const auto& r=value.resource;std::uint64_t refused=0;
    const auto require=[&](bool accepted,JoinPredicate predicate) {if(!accepted)refused|=join_bit(predicate);};
    require(raw.matched,JoinPredicate::RawMatched);
    require(raw.native_send_success,JoinPredicate::NativeSendSuccess);
    require(raw.reply_token,JoinPredicate::ReplyToken);
    require(value.current_after_drain,JoinPredicate::ReceiptCurrentAfterDrain);
    require(r.resourceComplete,JoinPredicate::ResourceComplete);
    require(!r.errors,JoinPredicate::ReceiptErrors);require(!r.lossCount,JoinPredicate::ReceiptLoss);
    require(!r.nativeImportAccepted,JoinPredicate::NativeImportAccepted);
    require(!r.sourceFrameAssociated,JoinPredicate::SourceFrameAssociated);
    require(!r.nativeCrossProcessNamespace,JoinPredicate::NativeCrossProcessNamespace);
    require(!r.acceptedSubmit,JoinPredicate::AcceptedSubmit);require(!r.gpuConsumedCoherent,JoinPredicate::GpuConsumedCoherent);
    require(r.invocationTag==runtime::kTag,JoinPredicate::InvocationTag);
    require(r.cookie==runtime::kCookie,JoinPredicate::Cookie);
    require(r.imageInvocationTag==runtime::kTag,JoinPredicate::ImageInvocationTag);
    require(r.viewInvocationTag==runtime::kTag,JoinPredicate::ViewInvocationTag);
    require(r.device==raw.image.device,JoinPredicate::Device);
    require(r.requestedImage==raw.image.image,JoinPredicate::RequestedImage);
    require(r.requestedView==raw.view.view,JoinPredicate::RequestedView);
    require(r.image.identity.handle==raw.image.image,JoinPredicate::ImageHandle);
    require(r.view.identity.handle==raw.view.view,JoinPredicate::ViewHandle);
    require(frame_bindings::same(r.view.image,r.image.identity),JoinPredicate::ViewImageIdentity);
    require(r.memory.identity.handle==raw.bind.memory,JoinPredicate::MemoryHandle);
    require(frame_bindings::same(r.image.memory,r.memory.identity),JoinPredicate::ImageMemoryIdentity);
    require(r.image.memoryOffset==raw.bind.offset,JoinPredicate::MemoryOffset);
    require(unsigned(r.image.format)==raw.image.format,JoinPredicate::ImageFormat);
    require(unsigned(r.view.format)==raw.view.format,JoinPredicate::ViewFormat);
    require(r.image.extent.width==raw.image.width,JoinPredicate::ExtentWidth);
    require(r.image.extent.height==raw.image.height,JoinPredicate::ExtentHeight);
    require(r.image.extent.depth==raw.image.depth,JoinPredicate::ExtentDepth);
    require(r.image.arrayLayers==raw.image.layers,JoinPredicate::ArrayLayers);
    return refused;
}
enum class JoinEnd : unsigned {Unobserved,Joined,Incompatible,RuntimeBefore,RuntimeAfter,
    JournalCurrentness,AttemptLimit,Deadline,ClockInvalid,Stopped,ForeignOwner};
const char* join_end(JoinEnd end) noexcept {
    switch(end) {
    case JoinEnd::Unobserved:return "unobserved";case JoinEnd::Joined:return "joined";
    case JoinEnd::Incompatible:return "incompatible";case JoinEnd::RuntimeBefore:return "runtime_currentness_before";
    case JoinEnd::RuntimeAfter:return "runtime_currentness_after";case JoinEnd::JournalCurrentness:return "journal_currentness";
    case JoinEnd::AttemptLimit:return "journal_validation_limit";case JoinEnd::Deadline:return "capture_deadline";
    case JoinEnd::ClockInvalid:return "invalid_monotonic_clock";case JoinEnd::Stopped:return "stopped";
    case JoinEnd::ForeignOwner:return "foreign_owner";
    }return "invalid";
}
struct JoinValidation {
    std::uint64_t compatibility_reason_bits=0,first_ns=0,finished_ns=0;
    unsigned attempts=0,journal_refusals=0,transient_refusals=0;
    frame_compositor_journal::Validation journal_validation{};
    bool compatibility_checked=false;
    bool runtime_before_checked=false,runtime_current_before=false;
    bool journal_checked=false,journal_current=false;
    bool runtime_after_checked=false,runtime_current_after=false;
    bool joined=false;
    JoinEnd end=JoinEnd::Unobserved;
};
bool join(const frame_compositor_raw::RawRecord& raw,const frame_compositor_journal::Receipt& value,
    JoinValidation* output=nullptr) noexcept {
    JoinValidation local;auto& validation=output?*output:local;
    validation.joined=false;
    validation.runtime_before_checked=validation.runtime_current_before=false;
    validation.journal_checked=validation.journal_current=false;
    validation.journal_validation={};
    validation.runtime_after_checked=validation.runtime_current_after=false;
    validation.compatibility_checked=true;
    validation.compatibility_reason_bits=compatibility(raw,value);
    if(validation.compatibility_reason_bits){validation.end=JoinEnd::Incompatible;return false;}
    validation.runtime_before_checked=true;
    validation.runtime_current_before=runtime::association_current();
    if(!validation.runtime_current_before){validation.end=JoinEnd::RuntimeBefore;return false;}
    ++validation.attempts;validation.journal_checked=true;
    validation.journal_validation=frame_compositor_journal::validate_detailed(value);
    validation.journal_current=validation.journal_validation.valid;
    if(!validation.journal_current) {
        ++validation.journal_refusals;
        if(validation.journal_validation.transient)++validation.transient_refusals;
    }
    validation.runtime_after_checked=true;
    validation.runtime_current_after=runtime::association_current();
    if(!validation.runtime_current_after){validation.end=JoinEnd::RuntimeAfter;return false;}
    validation.joined=validation.journal_current;
    validation.end=validation.joined?JoinEnd::Joined:JoinEnd::JournalCurrentness;
    return validation.joined;
}
std::uint64_t now() noexcept {timespec t{};return clock_gettime(CLOCK_MONOTONIC,&t)?0:std::uint64_t(t.tv_sec)*1000000000+std::uint64_t(t.tv_nsec);}
bool join_window(JoinValidation& validation,std::uint64_t start,std::uint64_t time) noexcept {
    if(!owned()){validation.end=JoinEnd::ForeignOwner;return false;}
    if(stopping.load(std::memory_order_acquire)){validation.end=JoinEnd::Stopped;return false;}
    if(!start || !time || time<start || start>UINT64_MAX-DurationNs){validation.end=JoinEnd::ClockInvalid;return false;}
    if(time-start>=DurationNs){validation.end=JoinEnd::Deadline;return false;}
    return true;
}
JoinValidation join_bounded(const frame_compositor_raw::RawRecord& raw,
    const frame_compositor_journal::Receipt& receipt,std::uint64_t capture_start) noexcept {
    JoinValidation validation;validation.compatibility_checked=true;
    validation.compatibility_reason_bits=compatibility(raw,receipt);
    // This function runs only on the existing background owner. The raw record
    // and receipt remain the same immutable values through all 64 validations.
    for(;;) {
        const auto time=now();validation.finished_ns=time;
        if(!validation.first_ns)validation.first_ns=time;
        if(!join_window(validation,capture_start,time))return validation;
        const bool accepted=join(raw,receipt,&validation);
        const auto after=now();validation.finished_ns=after;
        // Validation may wait on the owning background drain mutex. A success
        // that returns after expiry must never revive the observation window.
        if(!join_window(validation,capture_start,after)){validation.joined=false;return validation;}
        if(accepted || validation.end!=JoinEnd::JournalCurrentness || !validation.journal_validation.transient)return validation;
        if(validation.attempts>=JoinAttemptLimit){validation.end=JoinEnd::AttemptLimit;return validation;}
        const auto retry_at=after+std::min(JoinRetryDelayNs,capture_start+DurationNs-after);
        for(;;) {
            const auto observed=now();validation.finished_ns=observed;
            if(!join_window(validation,capture_start,observed))return validation;
            if(observed>=retry_at)break;
            const auto remaining=std::min(retry_at-observed,capture_start+DurationNs-observed);
            const timespec pause{static_cast<time_t>(remaining/1000000000),static_cast<long>(remaining%1000000000)};
            nanosleep(&pause,nullptr); // Interrupted sleeps recheck the same deadline.
        }
    }
}
const char* bridge_reason(frame_compositor_journal::BridgeValidationReason reason) noexcept {
    using Reason=frame_compositor_journal::BridgeValidationReason;
    switch(reason) {
    case Reason::None:return "valid";case Reason::ChildRetired:return "child_retired";
    case Reason::Uninitialized:return "uninitialized";case Reason::Stopped:return "stopped";
    case Reason::Loss:return "notification_loss";case Reason::ReceiptNotCurrent:return "receipt_not_current";
    case Reason::Journal:return "journal";
    }return "invalid";
}
const char* journal_reason(frame_bindings::ImportReceiptValidationReason reason) noexcept {
    using Reason=frame_bindings::ImportReceiptValidationReason;
    switch(reason) {
    case Reason::Valid:return "valid";case Reason::Closed:return "closed";case Reason::Halted:return "halted";
    case Reason::JournalLoss:return "journal_loss";case Reason::ActivePublication:return "active_publication";
    case Reason::PendingReplay:return "pending_replay";case Reason::PublicationChanged:return "publication_changed";
    case Reason::CoreGenerationMismatch:return "core_generation_mismatch";
    }return "invalid";
}
void validation_details(Line& l,const char* label,const frame_compositor_journal::Validation& decision,bool checked) noexcept {
    const auto& journal=decision.journal;
    // The Journal itself marks which short-circuit stages were observed. Later
    // default fields are never mislabeled as sampled state or inferred checks.
    l.add(",\"%s\":{\"checked\":%s,\"valid\":%s,\"transient\":%s,"
        "\"bridge_reason\":%u,\"bridge_reason_name\":\"%s\",\"journal_reason\":%u,\"journal_reason_name\":\"%s\","
        "\"before_checks_observed\":%s,\"after_checks_observed\":%s,\"core_checked\":%s,"
        "\"active_before\":%llu,\"active_after\":%llu,\"expected_before\":%llu,\"expected_after\":%llu,"
        "\"tickets_before\":%llu,\"tickets_after\":%llu,\"loss_before\":%llu,\"loss_after\":%llu,"
        "\"empty_before\":%s,\"empty_after\":%s,\"closed_before\":%s,\"closed_after\":%s,"
        "\"halted_before\":%s,\"halted_after\":%s}",
        label,checked?"true":"false",decision.valid?"true":"false",decision.transient?"true":"false",
        unsigned(decision.bridge_reason),bridge_reason(decision.bridge_reason),unsigned(journal.reason),journal_reason(journal.reason),
        journal.before_observed?"true":"false",journal.after_observed?"true":"false",journal.core_checked?"true":"false",
        u(journal.active_before),u(journal.active_after),u(journal.expected_before),u(journal.expected_after),u(journal.tickets_before),u(journal.tickets_after),
        u(journal.loss_before),u(journal.loss_after),journal.empty_before?"true":"false",journal.empty_after?"true":"false",
        journal.closed_before?"true":"false",journal.closed_after?"true":"false",journal.halted_before?"true":"false",journal.halted_after?"true":"false");
}
void join_diagnostics(Line& l,const JoinValidation& validation) noexcept {
    l.add(",\"join_validation\":{\"schema\":\"frame-compositor-resource-join-validation-v1\","
        "\"compatibility_checked\":%s,\"compatibility_reason_bits\":%llu,\"compatibility_predicate_version\":1,\"attempt_count\":%u,"
        "\"attempt_limit\":%u,\"retry_delay_ns\":%llu,\"journal_validation_refusals\":%u,"
        "\"transient_validation_refusals\":%u,\"recovered_validation_refusals\":%u,\"journal_refusal_cause_classified\":true,"
        "\"runtime_before_checked\":%s,\"runtime_current_before\":%s,\"journal_validation_checked\":%s,"
        "\"journal_current\":%s,\"runtime_after_checked\":%s,\"runtime_current_after\":%s,"
        "\"last_outcome\":\"%s\",\"retry_limit_reached\":%s,\"deadline_reached\":%s,"
        "\"first_ns\":%llu,\"finished_ns\":%llu,\"same_capture_deadline\":true}",
        validation.compatibility_checked?"true":"false",u(validation.compatibility_reason_bits),validation.attempts,JoinAttemptLimit,u(JoinRetryDelayNs),validation.journal_refusals,
        validation.transient_refusals,validation.joined?validation.transient_refusals:0,validation.runtime_before_checked?"true":"false",
        validation.runtime_current_before?"true":"false",validation.journal_checked?"true":"false",validation.journal_current?"true":"false",
        validation.runtime_after_checked?"true":"false",validation.runtime_current_after?"true":"false",join_end(validation.end),
        validation.end==JoinEnd::AttemptLimit?"true":"false",validation.end==JoinEnd::Deadline?"true":"false",
        u(validation.first_ns),u(validation.finished_ns));
    validation_details(l,"owning_journal_validation",validation.journal_validation,validation.journal_checked);
}
void footer_record(Line& footer,const runtime::Status& s,const JoinValidation& validation,
    const char* end,unsigned records,std::size_t total,bool quiet,bool joined,const frame_compositor_journal::Counters& progress) noexcept {
    footer.add("{\"schema\":1,\"end\":\"%s\",\"records\":%u,\"bytes_before_footer\":%zu,\"queued\":%llu,\"drained\":%llu,"
        "\"started\":%s,\"raw_terminal\":%s,\"incomplete\":%s,\"refusal\":%u,\"parent_bytes\":%zu,\"scalar_bytes\":%zu,\"wire_bytes\":%zu,"
        "\"quiescent\":%s,\"local_cpu_resource_join\":%s,\"operator_arm_required\":%s,\"candidate_armed\":%s,\"candidate_claimed\":%s",
        end,records,total,u(s.queued),u(s.drained),s.started?"true":"false",s.terminal?"true":"false",s.started&&!s.terminal?"true":"false",unsigned(s.refusal),
        s.parent_bytes,s.scalar_bytes,s.wire_bytes,quiet?"true":"false",joined?"true":"false",
        s.candidate_arm_required?"true":"false",s.candidate_armed?"true":"false",s.candidate_claimed?"true":"false");
    footer.add(",\"public_begin_counts\":{\"before_candidate_arm\":%llu,\"after_candidate_arm\":%llu,\"skipped_prearm_afters\":%llu,"
        "\"overflow\":%s,\"scope\":\"validated_supplied_notifications_not_packet_or_frame_proof\"}",
        u(s.begins_before_arm),u(s.begins_after_arm),u(s.skipped_prearm_afters),s.begin_counts_overflow?"true":"false");
    footer.add(",\"image_selection_counts\":{\"required\":%s,\"staged_begins\":%llu,\"excluded_images\":%llu,"
        "\"eligible_images\":%llu,\"staging_refusals\":%llu,\"reserved_thread_slots\":%llu,\"thread_limit\":16,"
        "\"abandoned_zero_read_stages\":%llu,"
        "\"scope\":\"public_creation_eligibility_not_stream_origin_proof\"}",
        s.stream_image_required?"true":"false",u(s.staged_begins),u(s.excluded_images),
        u(s.eligible_images),u(s.staging_refusals),u(s.staging_threads),u(s.abandoned_stages));
    footer.add(",\"initial_receipt_delivery\":{\"attempt_limit\":%u,\"requested\":%llu,\"delivered\":%llu,"
        "\"invalid_after_drain\":%llu,\"refused\":%llu,\"transient_deferred\":%llu,\"delivery_exhausted\":%llu,\"pending_waits\":%llu}",
        frame_compositor_journal::DeliveryValidationAttemptLimit,u(progress.requested),u(progress.delivered),u(progress.invalid_after_drain),u(progress.refused),
        u(progress.transient_deferred),u(progress.delivery_exhausted),u(progress.pending_waits));
    join_diagnostics(footer,validation);proofs(footer);footer.add("}\n");
}
void* write_background(void* argument) noexcept {
    auto total=reinterpret_cast<std::uintptr_t>(argument);
    const auto start=capture_started_ns;const char* end="stopped";unsigned records=0;
    std::uint64_t arm_check=0;JoinValidation validation;
    frame_compositor_raw::RawRecord raw{};frame_compositor_journal::Receipt receipt{};bool got_raw=false,got_receipt=false,joined=false;
    while(owned() && !stopping.load(std::memory_order_acquire)) {
        const auto time=now();if(!start || !time || time<start || time-start>=DurationNs){end="deadline";break;}
        const auto state=runtime::status();
        if(state.candidate_arm_required && !state.candidate_armed && !state.retired && time>=arm_check) {
            // Only this existing background writer checks the protected control
            // file. Native callbacks never perform filesystem access or waits.
            const auto signal=frame_compositor_arm::read(arm_path);
            if(signal==frame_compositor_arm::Result::Invalid){end="invalid_operator_marker";break;}
            const auto observed=now();
            if(!observed || observed<start || observed-start>=DurationNs){end="deadline";break;}
            if(signal==frame_compositor_arm::Result::Valid && runtime::arm_candidate_once()) {
                Line armed;
                armed.add("{\"schema\":1,\"operator_armed\":true,\"operator_arm_ns\":%llu,\"pid\":%ld,\"pre_arm_private_bytes\":0",
                    u(observed),long(owner_pid));proofs(armed);armed.add("}\n");
                if(!write_line(armed,total)){end="write_or_byte_limit";break;}
                if(++records==runtime::kRecordLimit){end="record_limit";break;}
            }
            arm_check=observed+100000000;
        }
        runtime::Record r{};
        if(runtime::pop(r)) {
            Line line;record(line,r);if(!write_line(line,total)){end="write_or_byte_limit";break;}++records;
            if(r.kind==runtime::Kind::RawMatched){raw=r.raw;got_raw=true;}
            if(r.kind==runtime::Kind::Receipt){receipt=r.receipt;got_receipt=true;}
            if(got_raw && got_receipt) {
                validation=join_bounded(raw,receipt,start);joined=validation.joined;Line association;
                association.add("{\"schema\":1,\"local_cpu_resource_join\":%s,\"reply_token\":\"%llx\",\"journal\":%llu,\"image_generation\":%llu,\"view_generation\":%llu",
                    joined?"true":"false",u(raw.reply_token),u(receipt.resource.journalInstance),u(receipt.resource.image.identity.generation),u(receipt.resource.view.identity.generation));
                join_diagnostics(association,validation);proofs(association);association.add("}\n");
                if(!write_line(association,total))end="write_or_byte_limit";else end=joined?"local_resource_join":
                    validation.end==JoinEnd::Deadline?"deadline":"join_refused";
                break;
            }
            if(records==runtime::kRecordLimit){end="record_limit";break;}
        } else if(runtime::status().retired){end="retired";break;}
        else {const timespec pause{0,10000000};nanosleep(&pause,nullptr);}
    }
    if(!owned())return nullptr;
    // Expiry disarms capture only. Installation and Steam services are untouched.
    runtime::finalize();
    for(unsigned i=0;i<32 && !runtime::quiescent();++i){const timespec pause{0,1000000};nanosleep(&pause,nullptr);}
    const bool quiet=runtime::quiescent();runtime::Record tail{};
    while(quiet && records<runtime::kRecordLimit && runtime::pop(tail)) {
        Line line;record(line,tail);if(!write_line(line,total))break;++records;
    }
    const auto s=runtime::status();Line footer;
    footer_record(footer,s,validation,end,records,total,quiet,joined,frame_compositor_journal::counters());write_line(footer,total,0);close(output_fd);output_fd=-1;return nullptr;
}
void finish_owned() noexcept {
    if(!owned())return;
    runtime::finalize();stopping.store(true,std::memory_order_release);
    if(writer_started){pthread_join(writer,nullptr);writer_started=false;}
}
__attribute__((constructor)) void start() noexcept {
    const int saved=errno;char input[1024]{};
    if(!freeze_before_observation(frame_compositor_launch_freeze_environment_v1,
        frame_compositor_launch_provider_mask,frame_compositor_launch_frozen_status_v1)){errno=saved;return;}
    const auto frozen_observed_ns=now();if(!frozen_observed_ns){errno=saved;return;}
    if(!path(std::getenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"),input,sizeof(input)) ||
       !path(std::getenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"),output_path,sizeof(output_path)) ||
       !frame_compositor_arm::path(output_path,arm_path,sizeof(arm_path)) ||
       frame_compositor_arm::read(arm_path)!=frame_compositor_arm::Result::Missing){errno=saved;return;}
    frame_compositor_main::Profiles profiles{};
    if(!envelope(input,profiles)){errno=saved;return;}
    const auto prepared=frame_compositor_main::prepare(profiles);runtime::Setup s{};
    if(!prepared.ready() || !setup(prepared,profiles,s)){errno=saved;return;}
    owner_pid=getpid();observer_identity=profiles.reviewed_observer_identity;
    s.require_operator_arm=true;
    s.require_stream_image=true;
    output_fd=open_output();
    if(output_fd<0 || !frame_compositor_journal::initialize_before_main() || !runtime::initialize(prepared,s)) {
        runtime::finalize();if(output_fd>=0)close(output_fd);output_fd=-1;errno=saved;return;
    }
    capture_started_ns=now();
    if(!capture_started_ns || capture_started_ns>UINT64_MAX-DurationNs) {
        runtime::finalize();close(output_fd);output_fd=-1;errno=saved;return;
    }
    Line header;header.add("{\"schema\":1,\"capture\":\"compositor_raw_import\",\"pid\":%ld,\"observer_profile\":\"",long(owner_pid));
    header.hex(observer_identity);
    header.add("\",\"launch_environment_frozen\":true,\"launch_freeze_abi\":%u,\"launch_provider_mask\":%u,\"launch_frozen_owner_pid\":%ld,\"launch_frozen_observed_ns\":%llu",LaunchFreezeAbi,LaunchProviderMask,long(owner_pid),u(frozen_observed_ns));
    header.add(",\"modules\":[");unsigned module=0;
    for(const auto& image:prepared.images()) {
        header.add("%s{\"role\":%u,\"file_sha256\":\"",module++?",":"",unsigned(image.role));header.hex(image.expected_file_identity);
        header.add("\",\"profile_sha256\":\"");header.hex(image.profile_identity);header.add("\"}");
    }
    header.add("]");
    header.add(",\"operator_arm_required\":true,\"operator_armed\":false,\"pre_arm_private_bytes\":0,\"capture_started_ns\":%llu,\"capture_deadline_ns\":%llu",
        u(capture_started_ns),u(capture_started_ns+DurationNs));
    header.add(",\"candidate_selection\":%s",CandidateSelection);
    header.add(",\"resource_join_validation\":%s",ResourceJoinValidation);
    header.add(",\"transactions\":1,\"parent_byte_limit\":16,\"scalar_byte_limit\":%llu,\"wire_byte_limit\":21,\"private_byte_limit\":%llu,"
        "\"creation_routes\":[\"Vulkan\",\"Facet\"],\"write_limit\":8,\"record_limit\":32,\"byte_limit\":65536,\"seconds_limit\":120",
        u(frame_compositor_raw::kMaxScalarBytes),u(16+frame_compositor_raw::kMaxScalarBytes+21));proofs(header);header.add("}\n");
    std::size_t total=0;
    // Register after the owning layer's function-static Capture construction.
    // Stop our consumer before that Journal and its writer are destroyed.
    if(write_line(header,total) && !std::atexit(finish_owned) &&
       !pthread_create(&writer,nullptr,write_background,reinterpret_cast<void*>(total))) {
        writer_started=true;frame_compositor_raw_ready.store(1,std::memory_order_release);
    } else {runtime::finalize();close(output_fd);output_fd=-1;}
    errno=saved;
}
__attribute__((destructor)) void finish() noexcept {
    const int saved=errno;
    finish_owned();
    errno=saved;
}
}
extern "C" __attribute__((visibility("default"))) frame_compositor_audit::BridgeResponse
frame_compositor_raw_bridge(frame_compositor_audit::BridgeRequest request) noexcept {
    const int saved=errno;auto result=runtime::bridge(request);errno=saved;return result;
}
