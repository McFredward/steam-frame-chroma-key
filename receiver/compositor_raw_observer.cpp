#include "compositor_raw_runtime.h"
#include "sha256.h"
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
    'F','C','M','R','A','W','0','2',2,0,0,0,0x50,0x20,0,0};
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
std::atomic<bool> stopping{false};
pthread_t writer{};
bool writer_started=false;
pid_t owner_pid=0;
int output_fd=-1;
char output_path[1024]{};
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
bool write_line(const Line& line,std::size_t& total,std::size_t reserve=1024) noexcept {
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
    }
    if(r.kind==runtime::Kind::Receipt) {
        const auto& c=r.receipt.resource;
        l.add(",\"receipt\":{\"journal\":%llu,\"replay_order\":%llu,\"resource_cursor\":%llu,\"tag\":\"%llx\",\"cookie\":\"%llx\","
            "\"device\":%llu,\"image\":\"%llx\",\"image_generation\":%llu,\"view\":\"%llx\",\"view_generation\":%llu,"
            "\"memory\":\"%llx\",\"memory_generation\":%llu,\"complete\":%s,\"errors\":%u,\"loss\":%llu,\"current_after_drain\":%s}",
            u(c.journalInstance),u(c.localReplayOrder),u(c.resourceGenerationCursor),u(c.invocationTag),u(c.cookie),u(c.device),u(c.image.identity.handle),u(c.image.identity.generation),
            u(c.view.identity.handle),u(c.view.identity.generation),u(c.memory.identity.handle),u(c.memory.identity.generation),c.resourceComplete?"true":"false",c.errors,u(c.lossCount),r.receipt.current_after_drain?"true":"false");
    }
    proofs(l);l.add("}\n");
}
bool join(const frame_compositor_raw::RawRecord& raw,const frame_compositor_journal::Receipt& value) noexcept {
    const auto& r=value.resource;
    return runtime::association_current() && raw.matched && raw.native_send_success && raw.reply_token && value.current_after_drain && r.resourceComplete && !r.errors && !r.lossCount &&
        !r.nativeImportAccepted && !r.sourceFrameAssociated && !r.nativeCrossProcessNamespace && !r.acceptedSubmit && !r.gpuConsumedCoherent &&
        r.invocationTag==runtime::kTag && r.cookie==runtime::kCookie && r.imageInvocationTag==runtime::kTag && r.viewInvocationTag==runtime::kTag &&
        r.device==raw.image.device && r.requestedImage==raw.image.image && r.requestedView==raw.view.view &&
        r.image.identity.handle==raw.image.image && r.view.identity.handle==raw.view.view && frame_bindings::same(r.view.image,r.image.identity) &&
        r.memory.identity.handle==raw.bind.memory && frame_bindings::same(r.image.memory,r.memory.identity) && r.image.memoryOffset==raw.bind.offset &&
        unsigned(r.image.format)==raw.image.format && unsigned(r.view.format)==raw.view.format &&
        r.image.extent.width==raw.image.width && r.image.extent.height==raw.image.height && r.image.extent.depth==raw.image.depth && r.image.arrayLayers==raw.image.layers &&
        frame_compositor_journal::validate(value) && runtime::association_current();
}
std::uint64_t now() noexcept {timespec t{};return clock_gettime(CLOCK_MONOTONIC,&t)?0:std::uint64_t(t.tv_sec)*1000000000+std::uint64_t(t.tv_nsec);}
void* write_background(void* argument) noexcept {
    auto total=reinterpret_cast<std::uintptr_t>(argument);
    const auto start=now();const char* end="stopped";unsigned records=0;
    frame_compositor_raw::RawRecord raw{};frame_compositor_journal::Receipt receipt{};bool got_raw=false,got_receipt=false,joined=false;
    while(owned() && !stopping.load(std::memory_order_acquire)) {
        const auto time=now();if(!start || !time || time<start || time-start>=DurationNs){end="deadline";break;}
        runtime::Record r{};
        if(runtime::pop(r)) {
            Line line;record(line,r);if(!write_line(line,total)){end="write_or_byte_limit";break;}++records;
            if(r.kind==runtime::Kind::RawMatched){raw=r.raw;got_raw=true;}
            if(r.kind==runtime::Kind::Receipt){receipt=r.receipt;got_receipt=true;}
            if(got_raw && got_receipt) {
                joined=join(raw,receipt);Line association;
                association.add("{\"schema\":1,\"local_cpu_resource_join\":%s,\"reply_token\":\"%llx\",\"journal\":%llu,\"image_generation\":%llu,\"view_generation\":%llu",
                    joined?"true":"false",u(raw.reply_token),u(receipt.resource.journalInstance),u(receipt.resource.image.identity.generation),u(receipt.resource.view.identity.generation));
                proofs(association);association.add("}\n");
                if(!write_line(association,total))end="write_or_byte_limit";else end=joined?"local_resource_join":"join_refused";
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
    footer.add("{\"schema\":1,\"end\":\"%s\",\"records\":%u,\"bytes_before_footer\":%zu,\"queued\":%llu,\"drained\":%llu,"
        "\"started\":%s,\"raw_terminal\":%s,\"incomplete\":%s,\"refusal\":%u,\"parent_bytes\":%zu,\"scalar_bytes\":%zu,\"wire_bytes\":%zu,"
        "\"quiescent\":%s,\"local_cpu_resource_join\":%s",
        end,records,total,u(s.queued),u(s.drained),s.started?"true":"false",s.terminal?"true":"false",s.started&&!s.terminal?"true":"false",unsigned(s.refusal),
        s.parent_bytes,s.scalar_bytes,s.wire_bytes,quiet?"true":"false",joined?"true":"false");
    proofs(footer);footer.add("}\n");write_line(footer,total,0);close(output_fd);output_fd=-1;return nullptr;
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
    if(!path(std::getenv("GHVR_FRAME_COMPOSITOR_RAW_ENVELOPE"),input,sizeof(input)) ||
       !path(std::getenv("GHVR_FRAME_COMPOSITOR_RAW_OUTPUT"),output_path,sizeof(output_path))){errno=saved;return;}
    frame_compositor_main::Profiles profiles{};
    if(!envelope(input,profiles)){errno=saved;return;}
    const auto prepared=frame_compositor_main::prepare(profiles);runtime::Setup s{};
    if(!prepared.ready() || !setup(prepared,profiles,s)){errno=saved;return;}
    owner_pid=getpid();observer_identity=profiles.reviewed_observer_identity;
    output_fd=open_output();
    if(output_fd<0 || !frame_compositor_journal::initialize_before_main() || !runtime::initialize(prepared,s)) {
        runtime::finalize();if(output_fd>=0)close(output_fd);output_fd=-1;errno=saved;return;
    }
    Line header;header.add("{\"schema\":1,\"capture\":\"compositor_raw_import\",\"pid\":%ld,\"observer_profile\":\"",long(owner_pid));
    header.hex(observer_identity);
    header.add("\",\"launch_environment_frozen\":true,\"launch_freeze_abi\":%u,\"launch_provider_mask\":%u,\"launch_frozen_owner_pid\":%ld,\"launch_frozen_observed_ns\":%llu",LaunchFreezeAbi,LaunchProviderMask,long(owner_pid),u(now()));
    header.add(",\"modules\":[");unsigned module=0;
    for(const auto& image:prepared.images()) {
        header.add("%s{\"role\":%u,\"file_sha256\":\"",module++?",":"",unsigned(image.role));header.hex(image.expected_file_identity);
        header.add("\",\"profile_sha256\":\"");header.hex(image.profile_identity);header.add("\"}");
    }
    header.add("]");
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
