#include "client_startup_runtime.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
extern "C" __attribute__((visibility("hidden"),used))
const std::uint8_t frame_client_resource_raw_scope[16]={
    'F','C','R','A','W','0','0','1',5,0,0,0,0x18,0x2e,0,0};
#elif defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
extern "C" __attribute__((visibility("hidden"),used))
const std::uint8_t frame_client_resource_thread_scope[16]={
    'F','C','S','T','A','K','0','1',4,0,0,0,0x18,0x2e,0,0};
#elif defined(FRAME_CLIENT_RESOURCE_CENSUS)
// Retained, hidden read-only opt-in evidence for offline preparation/staging.
extern "C" __attribute__((visibility("hidden"),used))
const std::uint8_t frame_client_resource_census_scope[16]={
    'F','C','C','E','N','S','0','1',3,0,0,0,0x18,0x2e,0,0};
#endif
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
extern "C" __attribute__((visibility("hidden"))) void frame_client_startup_epoll_wait_entry();
#endif
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
extern "C" __attribute__((visibility("hidden"))) void frame_import_read_entry();
extern "C" __attribute__((visibility("hidden"))) void frame_import_write_entry();
#endif
extern "C" __attribute__((weak,visibility("hidden"))) void frame_import_vr_init_entry();
extern "C" __attribute__((weak,visibility("hidden"))) void frame_import_vr_shutdown_entry();
extern "C" __attribute__((weak,visibility("hidden"))) void frame_import_init_entry();
extern "C" __attribute__((weak,visibility("hidden"))) void frame_import_destroy_entry();
#if defined(FRAME_CLIENT_CONNECT_OBSERVATION)
extern "C" __attribute__((visibility("hidden"))) void frame_import_lock_entry();
extern "C" __attribute__((visibility("hidden"))) void frame_import_unlock_entry();
extern "C" __attribute__((visibility("hidden"))) void frame_import_connect_entry();
#endif
// glibc's public pthread_atfork ABI uses its own libpthread_nonshared wrapper
// on the reviewed build. Validate the real registration provider cold, too.
extern "C" int __register_atfork(void(*)(void),void(*)(void),void(*)(void),void*) noexcept;


namespace {
constexpr std::size_t kEnvelopeBytes=11800,kOutputBytes=64*1024,kRecordLimit=32;
constexpr std::uint64_t kDurationNs=120ULL*1000*1000*1000;
std::atomic<bool> stopping{false},active{false};
pthread_t writer{};
bool writer_started=false;
char output_path[1024]{};
frame_import_main::Digest reviewed_observer{};
std::uint64_t local_process_label=0;
bool owned_process() noexcept {
    // A real fork copies globals but not the parent writer. This PID is only
    // local observer ownership, never a native/source frame generation.
    return local_process_label!=0&&static_cast<std::uint64_t>(getpid())==local_process_label;
}
void retire_child() noexcept {
    const auto saved=errno;
    // No locks/I/O/joins: finalize uses fixed atomic retirement and makes
    // already bound native notification subscribers inert in the child.
    frame_client_startup::finalize();
    active.store(false,std::memory_order_release);
    stopping.store(true,std::memory_order_release);writer_started=false;
    errno=saved;
}
bool register_child_retirement(const frame_import_main::Prepared& prepared) noexcept {
    const auto images=prepared.images();
    const auto atfork=reinterpret_cast<std::uintptr_t>(&::pthread_atfork);
    const auto registration=reinterpret_cast<std::uintptr_t>(&::__register_atfork);
    auto mapped=[&](std::uintptr_t address,const frame_import_main::Image& image) noexcept {
        Dl_info info{};
        return address>=image.rx_begin&&address<image.rx_end&&
            prepared.lookup()(reinterpret_cast<const void*>(address),&info)&&
            reinterpret_cast<std::uintptr_t>(info.dli_fbase)==image.bias;
    };
    // Actual export RVA from the independently pinned current Frame libc.
    // The wrapper may live in the reviewed own artifact; the stateful real
    // registration provider must be exactly the authenticated native libc.
    if((!mapped(atfork,images[2])&&!mapped(atfork,images[4]))||
       !mapped(registration,images[2])||registration<images[2].bias||
       registration-images[2].bias!=0xd35c0)return false;
    return ::pthread_atfork(nullptr,nullptr,retire_child)==0;
}
void disarm_owned() noexcept {
    if(owned_process())frame_client_startup::finalize();
    active.store(false,std::memory_order_release);
}
std::uint64_t little(const std::uint8_t* data,std::size_t n) noexcept {
    std::uint64_t value=0;for(std::size_t i=0;i<n;++i)value|=std::uint64_t(data[i])<<(8*i);return value;
}
bool path(const char* source,char* target,std::size_t capacity) noexcept {
    if(!source||source[0]!='/')return false;
    std::size_t i=0;while(i<capacity && source[i])++i;
    if(i==0||i==capacity)return false;
    std::memcpy(target,source,i+1);return true;
}
bool stable(const struct stat& a,const struct stat& b) noexcept {
    return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&
        a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
        a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool read_envelope(const char* name,std::array<std::uint8_t,kEnvelopeBytes>& bytes) noexcept {
    const int fd=open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;
    struct stat before{},after{};bool good=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&before.st_uid==geteuid()&&
        before.st_nlink==1&&(before.st_mode&07777)==0600&&before.st_size==kEnvelopeBytes;
    std::size_t offset=0;unsigned attempts=0;
    while(good&&offset<bytes.size()&&attempts++<32) {
        const auto n=read(fd,bytes.data()+offset,bytes.size()-offset);
        if(n<=0) {good=false;break;}offset+=static_cast<std::size_t>(n);
    }
    good=good&&offset==bytes.size()&&fstat(fd,&after)==0&&stable(before,after);close(fd);return good;
}
bool decode(const std::array<std::uint8_t,kEnvelopeBytes>& b,frame_import_main::Profiles& profiles,
            frame_import_module::ProfileBlob& client,frame_client_startup::ClientSlot& slot) noexcept {
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    const auto* envelope_magic=frame_client_resource_raw_scope;
    constexpr unsigned envelope_schema=5;
#elif defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    const auto* envelope_magic=frame_client_resource_thread_scope;
    constexpr unsigned envelope_schema=4;
#elif defined(FRAME_CLIENT_RESOURCE_CENSUS)
    const auto* envelope_magic=frame_client_resource_census_scope;
    constexpr unsigned envelope_schema=3;
#elif defined(FRAME_CLIENT_CONNECT_OBSERVATION)
    constexpr auto envelope_magic="FCCONN01";constexpr unsigned envelope_schema=2;
#else
    constexpr auto envelope_magic="FCSTART1";constexpr unsigned envelope_schema=1;
#endif
    if(std::memcmp(b.data(),envelope_magic,8)!=0||little(b.data()+8,4)!=envelope_schema||little(b.data()+12,4)!=b.size())return false;
    std::memcpy(profiles.reviewed_observer_identity.data(),b.data()+16,32);
    std::memcpy(slot.loader_name.data(),b.data()+48,slot.loader_name.size());
    std::size_t end=0;while(end<slot.loader_name.size()&&slot.loader_name[end])++end;
    if(end==0||end==slot.loader_name.size())return false;
    for(std::size_t i=end;i<slot.loader_name.size();++i)if(slot.loader_name[i])return false;
    slot.rx_rva_begin=little(b.data()+560,8);slot.rx_rva_end=little(b.data()+568,8);
    if(slot.rx_rva_begin>=slot.rx_rva_end)return false;
    for(std::size_t i=0;i<5;++i) {
        frame_import_main::ProfileBlob blob{};std::memcpy(blob.data(),b.data()+576+i*blob.size(),blob.size());
        if(!frame_import_main::decode_profile(blob,i==4?profiles.reviewed_observer_identity:frame_import_main::Digest{},profiles.modules[i])||
           profiles.modules[i].role!=i+1)return false;
    }
    std::memcpy(client.data(),b.data()+10736,client.size());frame_import_module::Profile parsed{};
    if(!frame_import_module::decode_profile(client,parsed))return false;
    bool exact_rx=false;
    for(const auto& ph:parsed.segments)
        if(ph.type==PT_LOAD&&(ph.flags&PF_X)&&parsed.caller_rva>=ph.virtual_address&&parsed.caller_rva-ph.virtual_address<ph.file_size)
            exact_rx=slot.rx_rva_begin==ph.virtual_address&&slot.rx_rva_end==ph.virtual_address+ph.file_size;
    return exact_rx;
}
std::uint64_t now() noexcept {timespec t{};return clock_gettime(CLOCK_MONOTONIC,&t)==0?
    std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec):0;}
int open_output() noexcept {
    if(!owned_process())return -1;
    char parent[1024]{};std::memcpy(parent,output_path,sizeof(parent));
    char* slash=std::strrchr(parent,'/');if(!slash||!slash[1])return -1;
    const char* basename=output_path+(slash-parent)+1;
    if(slash==parent)slash[1]=0;else *slash=0;
    const int dir=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir<0)return -1;
    struct stat s{};int fd=-1;
    if(fstat(dir,&s)==0&&S_ISDIR(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&022))
        fd=openat(dir,basename,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    close(dir);return fd;
}
bool write_owned(int fd,const char* bytes,std::size_t size) noexcept {
    std::size_t sent=0;unsigned attempts=0;
    while(sent<size&&attempts++<32) {if(!owned_process())return false;const auto n=write(fd,bytes+sent,size-sent);if(n<=0)return false;sent+=static_cast<std::size_t>(n);}
    return sent==size;
}
struct Line {
    std::array<char,4096> bytes{};std::size_t used=0;bool valid=true;
    template<class... A> void add(const char* format,A... args) noexcept {
        if(!valid)return;
        int n=0;
        if constexpr(sizeof...(A)==0)n=std::snprintf(bytes.data()+used,bytes.size()-used,"%s",format);
        else n=std::snprintf(bytes.data()+used,bytes.size()-used,format,args...);
        if(n<0||static_cast<std::size_t>(n)>=bytes.size()-used){valid=false;return;}used+=static_cast<std::size_t>(n);
    }
    template<std::size_t N> void hex(const std::array<std::uint8_t,N>& digest) noexcept {for(auto c:digest)add("%02x",unsigned(c));}
    void string(const std::array<char,512>& text) noexcept {
        add("\"");bool terminated=false;
        for(unsigned i=0;i<text.size();++i) {
            const auto c=static_cast<unsigned char>(text[i]);if(!c){terminated=true;break;}
            if(c=='"'||c=='\\')add("\\%c",c);else if(c<0x20||c>=0x7f)add("\\u%04x",unsigned(c));else add("%c",c);
        }
        if(!terminated)valid=false;
        add("\"");
    }
};
void record(Line& line,const frame_client_startup::Record& r) noexcept {
    auto u=[](auto value) noexcept {return static_cast<unsigned long long>(value);};
    line.add("{\"schema\":1,\"kind\":%u,\"ordinal\":%llu,\"local_label\":%llu,\"entry\":{\"sp\":\"%llx\",\"x\":[",
        unsigned(r.kind),u(r.ordinal),u(r.local_verification_label),u(r.entry.sp));
    for(unsigned i=0;i<31;++i)line.add("%s\"%llx\"",i?",":"",u(r.entry.x[i]));
    line.add("]},\"mapped\":{\"bias\":\"%llx\",\"rx_begin\":\"%llx\",\"rx_end\":\"%llx\",\"local_life\":%llu,\"code\":\"",
        u(r.module.load_bias),u(r.module.code_begin),u(r.module.code_end),u(r.module.module_lifetime_label));line.hex(r.module.code_identity);
    line.add("\"},\"object\":{\"registry\":\"%llx\",\"pipe\":\"%llx\",\"local_process\":%llu,\"generation\":%llu,\"domain\":%u,\"reason\":%u},\"loader_name\":",
        u(r.object.registry_identity),u(r.object.pipe),u(r.object.process_generation),u(r.object.object_generation),unsigned(r.object.domain),unsigned(r.object.reason));
    line.string(r.verified_loader_name);
    auto view=[&](const char* name,const frame_import_startup_audit::ModuleView& v) noexcept {
        line.add(",\"%s\":{\"bias\":\"%llx\",\"rx_begin\":\"%llx\",\"rx_end\":\"%llx\",\"audit_open_generation\":%llu}",
            name,u(v.bias),u(v.rx_begin),u(v.rx_end),u(v.observed_open_generation));
    };
    view("audit_receiver",r.audit_bindings.receiver);view("audit_api",r.audit_bindings.api);
    view("audit_observer",r.audit_bindings.observer);view("audit_libc",r.audit_bindings.libc);
    view("audit_client",r.audit_bindings.client);view("retired_module",r.retired_view);
    line.add(",\"audit_function\":%u,\"audit_original\":\"%llx\"",unsigned(r.audit_bindings.function),u(r.audit_bindings.original));
    const bool connect_observed=r.connection.kind==frame_client_connect::Kind::NativeConnectCall;
    line.add(",\"connection\":{\"kind\":%u,\"supported_connect_observed\":%s,\"native_connect_succeeded\":%s,"
             "\"argument_fd\":%d,\"native_result\":%d,\"errno\":%d,\"reason\":%u,"
             "\"continuous_connection_owned\":false,\"local_socket_lifetime_observed\":false,\"credentials_observed\":false,"
             "\"native_cross_process_namespace\":false,\"private_bytes_read\":0}",
        unsigned(r.connection.kind),connect_observed?"true":"false",
        connect_observed&&r.connection.native_result==0?"true":"false",r.connection.argument_fd,r.connection.native_result,
        r.connection.preserved_errno,unsigned(r.connection.reason));
    line.add(",\"actual_init_token\":%u,\"actual_native_result\":%d,\"errno\":%d,\"gate_refusal\":%u,\"refusal\":%u,"
             "\"source_frame_proven\":false,\"native_namespace_proven\":false,\"accepted_submit_proven\":false,\"gpu_proven\":false",
        r.actual_init_token,r.actual_native_result,r.preserved_errno,unsigned(r.gate_refusal),unsigned(r.refusal));
    if(r.kind==frame_client_startup::Kind::ResourceObserved || r.kind==frame_client_startup::Kind::ResourceRefused) {
        const auto& c=r.resource;
        line.add(",\"resource_census\":{\"boundary\":%u,\"phase\":%u,\"native_result_available\":%s,"
                 "\"native_result\":%d,\"errno\":%d,\"reason\":%u,\"startup_thread_stack_owned\":%s,"
                 "\"private_bytes_read\":0,\"wire_bytes_read\":0,\"transaction_accepted\":false,\"resource_namespace_associated\":false}",
            unsigned(c.boundary),unsigned(c.phase),c.native_result_available?"true":"false",
            c.native_result,c.preserved_errno,unsigned(c.reason),c.startup_thread_stack_owned?"true":"false");
    }
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    if(r.kind==frame_client_startup::Kind::ResourceThreadPrepared ||
       r.kind==frame_client_startup::Kind::ResourceThreadRefused ||
       r.kind==frame_client_startup::Kind::ResourceObserved || r.kind==frame_client_startup::Kind::ResourceRefused) {
        line.add(",\"resource_thread\":{\"stack_owned_current\":%s,\"reason\":%u,"
                 "\"usable_stack_begin\":\"%llx\",\"usable_stack_end\":\"%llx\","
                 "\"private_bytes_read\":0,\"wire_bytes_read\":0,\"namespace_proven\":false}",
            r.resource_thread_stack_owned?"true":"false",unsigned(r.resource_thread_reason),
            u(r.resource_stack.begin),u(r.resource_stack.end));
    }
#endif
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    if(r.kind==frame_client_startup::Kind::ResourceRawTransaction || r.kind==frame_client_startup::Kind::ResourceRawRefused) {
        const auto& c=r.raw;
        line.add(",\"raw_resource\":{\"raw_only\":true,\"matched\":%s,\"terminal\":%s,"
            "\"reason\":%llu,\"decoder_refusal\":%u,\"thread_reason\":%u,"
            "\"import_frame\":\"%llx\",\"receiver_frame\":\"%llx\",\"pipe\":\"%llx\",\"record\":\"%llx\","
            "\"fd\":%d,\"sequence\":%u,\"sequence_available\":%s,\"request_bytes\":%u,"
            "\"reply_header_bytes\":%u,\"reply_payload_bytes\":%u,\"writes\":%u,\"header_reads\":%u,\"payload_reads\":%u,"
            "\"scalar_bytes_copied\":%zu,\"wire_bytes_copied\":%zu,\"reply_token\":\"%llx\","
            "\"parsed_token\":\"%llx\",\"referenced_token\":\"%llx\",\"begin_lock_succeeded\":%s,"
            "\"request_complete\":%s,\"reply_complete\":%s,\"pending_unlock_succeeded\":%s,"
            "\"native_wait_success\":%s,\"matching_pending\":%s,\"reference_lock_succeeded\":%s,"
            "\"following_reference_observed\":%s,\"last_boundary\":%u,\"last_phase\":%u,"
            "\"last_native_result\":%lld,\"last_errno\":%d,\"last_native_result_available\":%s,"
            "\"reference_rpc_succeeded\":false,\"continuous_connection_owned\":false,"
            "\"native_namespace_associated\":false,\"source_frame_associated\":false,\"uv_owned\":false,"
            "\"accepted_submit\":false,\"gpu_consumed\":false,\"request_header\":\"",
            c.matched?"true":"false",c.terminal?"true":"false",u(c.reason),unsigned(c.decoder_refusal),c.thread_reason,
            u(c.invocation.import_frame),u(c.invocation.receiver_frame),u(c.invocation.pipe),u(c.invocation.record),
            c.fd,c.sequence,c.sequence_available?"true":"false",c.request_bytes,c.reply_header_bytes,c.reply_payload_bytes,
            c.writes,c.header_reads,c.payload_reads,c.scalar_bytes_copied,c.wire_bytes_copied,
            u(c.reply_token),u(c.parsed_token),u(c.referenced_token),c.begin_lock_succeeded?"true":"false",
            c.request_complete?"true":"false",c.reply_complete?"true":"false",c.pending_unlock_succeeded?"true":"false",
            c.native_wait_success?"true":"false",c.matching_pending?"true":"false",c.reference_lock_succeeded?"true":"false",
            c.following_reference_observed?"true":"false",unsigned(c.last_boundary),unsigned(c.last_phase),
            static_cast<long long>(c.last_native_result),c.last_preserved_errno,c.last_native_result_available?"true":"false");
        line.hex(c.request_header);line.add("\",\"reply_header\":\"");line.hex(c.reply_header);
        line.add("\",\"reply_payload\":\"");line.hex(c.reply_payload);line.add("\"}");
    }
#endif
    line.add("}\n");
}
void* write_background(void*) noexcept {
    const int fd=open_output();if(fd<0){disarm_owned();return nullptr;}
    std::size_t bytes=0,records=0;const auto started=now();const char* end="stopped";
    Line header;header.add("{\"schema\":1,\"capture\":\"client_startup\",\"pid\":%ld,\"local_process_label\":%llu,\"reviewed_observer_profile\":\"",long(getpid()),static_cast<unsigned long long>(local_process_label));
    header.hex(reviewed_observer);header.add("\",\"record_limit\":32,\"byte_limit\":65536,\"seconds_limit\":120,\"mask_enabled\":false");
#if defined(FRAME_CLIENT_CONNECT_OBSERVATION)
    header.add(",\"connect_register_observation\":true,\"connect_private_bytes_read\":0");
#else
    header.add(",\"connect_register_observation\":false");
#endif
#if defined(FRAME_CLIENT_RESOURCE_CENSUS)
    header.add(",\"resource_register_census\":true,\"resource_private_bytes_read\":0");
#endif
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    header.add(",\"resource_thread_preparation\":true,\"resource_thread_private_bytes_read\":0");
#endif
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    header.add(",\"resource_raw_transaction\":true,\"raw_transaction_limit\":1,\"raw_transfer_limit\":24,"
        "\"raw_per_transfer_kind_limit\":8,\"raw_scalar_byte_limit\":2068,\"raw_wire_byte_limit\":33,"
        "\"raw_additional_private_byte_limit\":2101,\"raw_namespace_proven\":false");
#endif
    header.add("}\n");
    if(!header.valid||!write_owned(fd,header.bytes.data(),header.used)){close(fd);disarm_owned();return nullptr;}bytes=header.used;
    while(owned_process()&&!stopping.load(std::memory_order_acquire)) {
        const auto time=now();if(!started||!time||time<started||time-started>=kDurationNs){end="deadline";break;}
        frame_client_startup::Record value{};
        if(frame_client_startup::pop(value)) {
            Line line;record(line,value);
            if(!line.valid||line.used>kOutputBytes-bytes-1024){end="byte_limit";break;}
            if(!write_owned(fd,line.bytes.data(),line.used)){end="write_error";break;}
            bytes+=line.used;if(++records==kRecordLimit){end="record_limit";break;}
        } else {timespec pause{0,10000000};nanosleep(&pause,nullptr);}
    }
    // This deadline disarms recording only; it never restarts Steam/SteamVR,
    // changes loader configuration, rolls back installation or affects pixels.
    if(!owned_process()){close(fd);active.store(false,std::memory_order_release);return nullptr;}
    frame_client_startup::finalize();
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    // Wait only on the writer, bounded to short callback completion. A native
    // blocked read is outside callback activity and is never waited on here.
    for(unsigned attempt=0;attempt<32 && !frame_client_startup::raw_quiescent();++attempt) {
        const timespec pause{0,1000000};nanosleep(&pause,nullptr);
    }
    const bool raw_totals_quiescent=frame_client_startup::raw_quiescent();
#endif
    const auto status=frame_client_startup::status();
    Line footer;footer.add("{\"schema\":1,\"end\":\"%s\",\"records\":%zu,\"bytes_before_footer\":%zu,\"queued\":%llu,\"drained\":%llu,\"startup_completed\":%s,\"refusal\":%u,\"finalized\":true,"
        "\"connect_sample_completed\":%s,\"resource_sample_completed\":%s",
        end,records,bytes,static_cast<unsigned long long>(status.queued),static_cast<unsigned long long>(status.drained),status.startup_completed?"true":"false",unsigned(status.refusal),
        status.connect_sample_completed?"true":"false",status.resource_sample_completed?"true":"false");
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    footer.add(",\"resource_thread_stack_prepared\":%s",status.resource_thread_stack_prepared?"true":"false");
#endif
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    footer.add(",\"resource_raw_started\":%s,\"resource_raw_completed\":%s,\"resource_raw_abandoned\":%s,"
        "\"resource_scalar_bytes_copied\":%zu,\"resource_wire_bytes_copied\":%zu,\"resource_raw_totals_quiescent\":%s",
        status.resource_raw_started?"true":"false",status.resource_raw_completed?"true":"false",
        status.resource_raw_started&&!status.resource_raw_completed?"true":"false",
        status.resource_scalar_bytes_copied,status.resource_wire_bytes_copied,raw_totals_quiescent?"true":"false");
#endif
    footer.add("}\n");
    if(footer.valid&&footer.used<=kOutputBytes-bytes)write_owned(fd,footer.bytes.data(),footer.used);
    close(fd);active.store(false,std::memory_order_release);return nullptr;
}
__attribute__((constructor)) void begin_observer() noexcept {
    const auto saved=errno;
    char envelope_name[1024]{};
    if(!path(std::getenv("GHVR_FRAME_CLIENT_STARTUP_ENVELOPE"),envelope_name,sizeof(envelope_name))||
       !path(std::getenv("GHVR_FRAME_CLIENT_STARTUP_OUTPUT"),output_path,sizeof(output_path))){errno=saved;return;}
    std::array<std::uint8_t,kEnvelopeBytes> bytes{};frame_import_main::Profiles profiles{};
    frame_import_module::ProfileBlob client{};frame_client_startup::ClientSlot slot{};
    if(!read_envelope(envelope_name,bytes)||!decode(bytes,profiles,client,slot)){errno=saved;return;}
    const auto prepared=frame_import_main::prepare(profiles);if(!prepared.ready()){errno=saved;return;}
    frame_client_startup::Setup setup{};setup.client=slot;
    local_process_label=static_cast<std::uint64_t>(getpid());setup.process_generation=local_process_label;
    setup.local_verification_label=1; // A local observer label, not a native/audit/source generation.
    setup.hidden_init_entry=reinterpret_cast<std::uintptr_t>(&frame_import_vr_init_entry);
    setup.hidden_shutdown_entry=reinterpret_cast<std::uintptr_t>(&frame_import_vr_shutdown_entry);
    setup.hidden_mutex_init_entry=reinterpret_cast<std::uintptr_t>(&frame_import_init_entry);
    setup.hidden_mutex_destroy_entry=reinterpret_cast<std::uintptr_t>(&frame_import_destroy_entry);
#if defined(FRAME_CLIENT_CONNECT_OBSERVATION)
    setup.hidden_mutex_lock_entry=reinterpret_cast<std::uintptr_t>(&frame_import_lock_entry);
    setup.hidden_mutex_unlock_entry=reinterpret_cast<std::uintptr_t>(&frame_import_unlock_entry);
    setup.hidden_connect_entry=reinterpret_cast<std::uintptr_t>(&frame_import_connect_entry);
#endif
#if defined(FRAME_CLIENT_RESOURCE_CENSUS)
    setup.resource_register_census=true;
#endif
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    setup.resource_thread_preparation=true;
    setup.hidden_epoll_wait_entry=reinterpret_cast<std::uintptr_t>(&frame_client_startup_epoll_wait_entry);
#endif
#if defined(FRAME_CLIENT_RESOURCE_RAW_TRANSACTION)
    setup.resource_raw_transaction=true;
    setup.hidden_read_entry=reinterpret_cast<std::uintptr_t>(&frame_import_read_entry);
    setup.hidden_write_entry=reinterpret_cast<std::uintptr_t>(&frame_import_write_entry);
#endif
    if(!frame_client_startup::initialize_once(prepared,client,setup)){errno=saved;return;}
    if(!register_child_retirement(prepared)){frame_client_startup::finalize();errno=saved;return;}
    reviewed_observer=profiles.reviewed_observer_identity;active.store(true,std::memory_order_release);
    if(pthread_create(&writer,nullptr,write_background,nullptr)==0)writer_started=true;
    else {frame_client_startup::finalize();active.store(false,std::memory_order_release);}
    errno=saved;
}
__attribute__((destructor)) void finish_observer() noexcept {
    const auto saved=errno;
    if(local_process_label!=0&&!owned_process()) {
        // Never acknowledge a copied prepared runtime or join the parent's
        // pthread_t in the child. Parent address space/stream remains untouched.
        active.store(false,std::memory_order_release);errno=saved;return;
    }
    frame_client_startup::finalize();stopping.store(true,std::memory_order_release);
    if(writer_started){pthread_join(writer,nullptr);writer_started=false;}
    active.store(false,std::memory_order_release);errno=saved;
}
} // namespace
extern "C" __attribute__((visibility("default"))) frame_import_startup_audit::BridgeResponse
frame_client_startup_bridge(frame_import_startup_audit::BridgeRequest request) noexcept {
    const auto saved=errno;
    const auto response=active.load(std::memory_order_acquire)&&owned_process()?frame_client_startup::bridge(request):frame_import_startup_audit::BridgeResponse{};
    errno=saved;return response;
}
