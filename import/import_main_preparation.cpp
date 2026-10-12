#include "import_main_preparation.h"
#include "../receiver/sha256.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>

namespace frame_import_main {
namespace {
constexpr Digest from_hex(const char* text) noexcept {
    Digest d{};
    for(unsigned i=0;i<32;++i) {
        auto digit=[](char c) constexpr { return c<='9' ? c-'0' : c-'a'+10; };
        d[i]=static_cast<std::uint8_t>((digit(text[2*i])<<4)|digit(text[2*i+1]));
    }
    return d;
}
constexpr std::array<Digest,4> kNative{{
    from_hex("2f5ccf52a5ebbb4b7d36a9508451247bd370b03c0c1b3a4d4b18dcb9322b655c"),
    from_hex("e5513ef3746d6bffe1eb9b22b08ce5c964cbc870c2644cd8abfaf44ba7bc445c"),
    from_hex("48edb257be746f755a00ff564c6db4284526b15ffc7e6bad5b8d6723b1891512"),
    from_hex("51af1fa58396e7a1ab48760ce90d90e919db3e42653504ad550eb54e129a7405")}};
std::uint64_t little(const std::uint8_t* data,std::size_t n) noexcept {
    std::uint64_t value=0;
    for(std::size_t i=0;i<n;++i) value|=std::uint64_t(data[i])<<(i*8);
    return value;
}
void hash_scalar(frame_receiver::Sha256& h,std::uint64_t value) noexcept {
    std::uint8_t b[8]{};
    for(unsigned i=0;i<8;++i)b[i]=static_cast<std::uint8_t>(value>>(8*i));
    h.update(b,sizeof(b));
}
Digest fingerprint(const Profile& p) noexcept {
    frame_receiver::Sha256 h;
    const std::uint64_t scalars[]{p.schema,p.role,p.machine,p.elf_type,p.file_size,p.phoff,p.phnum,p.note_count,p.build_id_size,p.mapped_span};
    for(auto s:scalars)hash_scalar(h,s);
    for(const auto* d:{&p.header_digest,&p.phdr_digest,&p.code_digest,&p.file_digest})h.update(d->data(),d->size());
    h.update(p.build_id.data(),p.build_id.size());
    for(const auto& ph:p.headers) {
        const std::uint64_t fields[]{ph.type,ph.flags,ph.file_offset,ph.virtual_address,ph.physical_address,ph.file_size,ph.memory_size,ph.alignment};
        for(auto s:fields)hash_scalar(h,s);
    }
    for(const auto& d:p.rx_digests)h.update(d.data(),d.size());
    for(const auto& n:p.notes) { hash_scalar(h,n.rva);hash_scalar(h,n.size);h.update(n.digest.data(),n.digest.size()); }
    return h.finish();
}
bool authenticated(const Profile& p,const Digest& reviewed) noexcept {
    const auto native=pinned_identity(static_cast<Role>(p.role));
    const auto expected=native==Digest{} ? reviewed : native;
    if(expected==Digest{} || p.fingerprint!=expected || fingerprint(p)!=expected)return false;
    if(p.schema!=1 || p.role<1 || p.role>6 || (p.machine!=62 && p.machine!=183) ||
       (p.role!=6 && p.machine!=183) || p.elf_type!=3 || p.file_size<64 || p.file_size>8*1024*1024 ||
       !p.phnum || p.phnum>kHeaders || p.phoff>p.file_size || p.phnum*56>p.file_size-p.phoff ||
       !p.note_count || p.note_count>kNotes || !p.build_id_size || p.build_id_size>64 || p.mapped_span>16*1024*1024)return false;
    return true;
}
Digest bytes_digest(const void* data,std::size_t size) noexcept { frame_receiver::Sha256 h;h.update(data,size);return h.finish(); }
bool same(const frame_import_module::ProgramHeader& a,const frame_import_module::ProgramHeader& b) noexcept {
    return a.type==b.type && a.flags==b.flags && a.file_offset==b.file_offset && a.virtual_address==b.virtual_address &&
           a.physical_address==b.physical_address && a.file_size==b.file_size && a.memory_size==b.memory_size && a.alignment==b.alignment;
}
bool add(std::uintptr_t base,std::uint64_t offset,std::uintptr_t& out) noexcept {
    if(offset>std::numeric_limits<std::uintptr_t>::max()-base)return false;
    out=base+static_cast<std::uintptr_t>(offset);return true;
}
struct Check {
    const Profile& p;
    Proof proof{};
    static void verify(const frame_import_loader::ScopedModule& m,void* context) noexcept {
        auto& c=*static_cast<Check*>(context);const auto& p=c.p;
        c.proof.refusal=Refusal::Metadata;
        if(m.program_header_count()!=p.phnum)return;
        for(std::size_t i=0;i<p.phnum;++i) { frame_import_module::ProgramHeader ph{};if(!m.program_header(i,ph)||!same(ph,p.headers[i]))return; }
        std::array<std::uint8_t,4096> buffer{};
        auto copy=[&](std::uint64_t rva,std::size_t size) noexcept {
            std::uintptr_t at=0;return add(m.base(),rva,at)&&m.copy(at,buffer.data(),size);
        };
        c.proof.refusal=Refusal::MappedIdentity;
        if(!copy(0,64)||bytes_digest(buffer.data(),64)!=p.header_digest)return;
        if(buffer[0]!=0x7f || buffer[1]!='E'||buffer[2]!='L'||buffer[3]!='F'||buffer[4]!=2||buffer[5]!=1||
           little(buffer.data()+16,2)!=p.elf_type||little(buffer.data()+18,2)!=p.machine||
           little(buffer.data()+32,8)!=p.phoff||little(buffer.data()+52,2)!=64||
           little(buffer.data()+54,2)!=56||little(buffer.data()+56,2)!=p.phnum)return;
        const auto phsize=static_cast<std::size_t>(p.phnum*56);
        if(!copy(p.phoff,phsize)||bytes_digest(buffer.data(),phsize)!=p.phdr_digest)return;
        bool found_id=false;
        for(std::size_t i=0;i<p.note_count;++i) {
            const auto& n=p.notes[i];
            if(n.size<12||n.size>256||!copy(n.rva,n.size)||bytes_digest(buffer.data(),n.size)!=n.digest)return;
            std::size_t cursor=0;
            while(cursor<n.size) {
                if(n.size-cursor<12)return;
                const auto names=little(buffer.data()+cursor,4), desc=little(buffer.data()+cursor+4,4), type=little(buffer.data()+cursor+8,4);
                if(names>256 || desc>256)return;
                const auto name_at=cursor+12, desc_at=name_at+((names+3)&~std::uint64_t(3)),end=desc_at+((desc+3)&~std::uint64_t(3));
                if(end>n.size)return;
                if(type==3 && names==4 && std::memcmp(buffer.data()+name_at,"GNU\0",4)==0) {
                    if(found_id||desc!=p.build_id_size||std::memcmp(buffer.data()+desc_at,p.build_id.data(),desc)!=0)return;
                    found_id=true;
                }
                cursor=static_cast<std::size_t>(end);
            }
        }
        if(!found_id)return;
        frame_receiver::Sha256 all;
        std::uint64_t total=0;
        for(std::size_t i=0;i<p.phnum;++i) {
            const auto& ph=p.headers[i];if(ph.type!=PT_LOAD || !(ph.flags&PF_X))continue;
            if(ph.file_size>8*1024*1024-total)return;
            total+=ph.file_size;frame_receiver::Sha256 segment;
            for(std::uint64_t cursor=0;cursor<ph.file_size;) {
                const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),ph.file_size-cursor));
                if(ph.virtual_address>std::numeric_limits<std::uint64_t>::max()-cursor || !copy(ph.virtual_address+cursor,size))return;
                segment.update(buffer.data(),size);all.update(buffer.data(),size);cursor+=size;
            }
            if(segment.finish()!=p.rx_digests[i])return;
        }
        const auto actual=all.finish();if(!total || actual!=p.code_digest)return;
        c.proof.refusal=Refusal::None;
        c.proof.image={m.base(),m.code_begin(),m.code_end(),static_cast<Role>(p.role),actual,p.file_digest,p.fingerprint};
    }
};
struct Discovery {
    std::uintptr_t main=0;
    unsigned visits=0;
    bool client=false,complete=true;
    static int visit(dl_phdr_info* info,std::size_t size,void* pointer) noexcept {
        auto& d=*static_cast<Discovery*>(pointer);
        if(++d.visits>256 || !info || size<offsetof(dl_phdr_info,dlpi_phnum)+sizeof(info->dlpi_phnum) ||
           !info->dlpi_phdr || !info->dlpi_phnum || info->dlpi_phnum>16) { d.complete=false;return 1; }
        const auto* name=info->dlpi_name;
        if(!name) { d.complete=false;return 1; }
        if(!name[0]) { if(d.main) { d.complete=false;return 1; }d.main=info->dlpi_addr; }
        std::size_t length=0,last=0;
        while(length<256 && name[length]) { if(name[length]=='/')last=length+1;++length; }
        if(length==256) { d.complete=false;return 1; }
        if(length-last==11 && std::memcmp(name+last,"vrclient.so",11)==0)d.client=true;
        for(std::size_t i=0;i<info->dlpi_phnum;++i) {
            ElfW(Phdr) ph{};std::memcpy(&ph,info->dlpi_phdr+i,sizeof(ph));
            // A renamed copy of the reviewed client remains a conservative reject.
            if(ph.p_type==PT_LOAD && ph.p_vaddr==0 && ph.p_filesz==0x60699a && ph.p_flags==5)d.client=true;
        }
        return 0;
    }
};
} // namespace
Digest pinned_identity(Role role) noexcept {
    const auto value=static_cast<std::uint64_t>(role);
    return value>=1&&value<=4 ? kNative[value-1] : Digest{};
}
bool decode_profile(const ProfileBlob& blob,const Digest& reviewed,Profile& output) noexcept {
    Profile p{};std::size_t at=0;
    auto scalar=[&]() noexcept {const auto v=little(blob.data()+at,8);at+=8;return v;};
    p.schema=scalar();p.role=scalar();p.machine=scalar();p.elf_type=scalar();p.file_size=scalar();p.phoff=scalar();
    p.phnum=scalar();p.note_count=scalar();p.build_id_size=scalar();p.mapped_span=scalar();
    auto bytes=[&](auto& value) noexcept {std::memcpy(value.data(),blob.data()+at,value.size());at+=value.size();};
    bytes(p.header_digest);bytes(p.phdr_digest);bytes(p.code_digest);bytes(p.file_digest);bytes(p.build_id);
    for(auto& ph:p.headers) {ph.type=scalar();ph.flags=scalar();ph.file_offset=scalar();ph.virtual_address=scalar();
        ph.physical_address=scalar();ph.file_size=scalar();ph.memory_size=scalar();ph.alignment=scalar();}
    for(auto& d:p.rx_digests)bytes(d);
    for(auto& n:p.notes) {n.rva=scalar();n.size=scalar();bytes(n.digest);}
    bytes(p.fingerprint);if(at!=blob.size()||!authenticated(p,reviewed))return false;output=p;return true;
}
Proof verify_loaded(const Profile& p,const Digest& reviewed,std::uintptr_t anchor) noexcept {
    Proof out;if(!authenticated(p,reviewed))return out;
    if(!anchor) {out.refusal=Refusal::Lookup;return out;}
    const auto saved=errno;Dl_info info{};
    if(!::dladdr(reinterpret_cast<const void*>(anchor),&info)) {errno=saved;out.refusal=Refusal::Lookup;return out;}
    frame_import_loader::Reader reader({},::dl_iterate_phdr,1); // Label is local bookkeeping, not ownership.
    Check check{p,{}};
    const auto lease=reader.inspect_cold(info,anchor,Check::verify,&check);errno=saved;
    if(lease!=frame_import_loader::Refusal::None) {out.refusal=Refusal::LoaderLease;return out;}
    return check.proof;
}
Prepared prepare(const Profiles& profiles) noexcept {
    Prepared out;const auto saved=errno;
    auto finish=[&](Refusal why) noexcept {out.refusal_=why;
        if(why!=Refusal::None) {out.lookup_=nullptr;out.iterate_=nullptr;out.native_init_=out.native_shutdown_=0;}
        errno=saved;return out;};
    for(std::size_t i=0;i<kModules;++i) {
        const auto& p=profiles.modules[i];
        if(p.role!=i+1 || !authenticated(p,i==4 ? profiles.reviewed_observer_identity : Digest{}))return finish(Refusal::InvalidProfile);
    }
    if(sizeof(void*)!=8)return finish(Refusal::UnsupportedPlatform);
    // Trust only the main namespace's ordinary linked public glibc bootstrap;
    // no injected providers, private loader functions or forced library loading.
    Discovery discovery;
    if(::dl_iterate_phdr(Discovery::visit,&discovery)!=0 || !discovery.complete || !discovery.main)
        return finish(Refusal::DiscoveryIncomplete);
    out.client_present_=discovery.client;
    if(discovery.client)return finish(Refusal::ClientAlreadyPresent);
    std::uintptr_t receiver_anchor=0;
    if(!add(discovery.main,0x2ae94,receiver_anchor))return finish(Refusal::WrongExecutable);
    auto receiver=verify_loaded(profiles.modules[0],{},receiver_anchor);
    if(!receiver.valid())return finish(Refusal::WrongExecutable);
    out.images_[0]=receiver.image;
    auto libc=verify_loaded(profiles.modules[2],{},reinterpret_cast<std::uintptr_t>(&::dl_iterate_phdr));
    if(!libc.valid())return finish(Refusal::ProviderMismatch);
    out.images_[2]=libc.image;
    if(reinterpret_cast<std::uintptr_t>(&::dl_iterate_phdr)!=libc.image.bias+0x134b20 ||
       reinterpret_cast<std::uintptr_t>(&::dladdr)!=libc.image.bias+0x7f320)return finish(Refusal::ProviderMismatch);
    const auto versioned_iterate=reinterpret_cast<frame_import_loader::Iterate>(::dlvsym(RTLD_DEFAULT,"dl_iterate_phdr","GLIBC_2.17"));
    const auto versioned_lookup=reinterpret_cast<int (*)(const void*,Dl_info*)>(::dlvsym(RTLD_DEFAULT,"dladdr","GLIBC_2.34"));
    if(versioned_iterate!=::dl_iterate_phdr || versioned_lookup!=::dladdr)return finish(Refusal::ProviderMismatch);
    out.lookup_=versioned_lookup;out.iterate_=versioned_iterate;
    const auto init=reinterpret_cast<std::uintptr_t>(::dlsym(RTLD_DEFAULT,"VR_InitInternal2"));
    const auto shutdown=reinterpret_cast<std::uintptr_t>(::dlsym(RTLD_DEFAULT,"VR_ShutdownInternal"));
    const auto tls=reinterpret_cast<std::uintptr_t>(::dlvsym(RTLD_DEFAULT,"__tls_get_addr","GLIBC_2.17"));
    if(!init||!shutdown)return finish(Refusal::MissingApi);
    if(!tls)return finish(Refusal::ProviderMismatch);
    auto api=verify_loaded(profiles.modules[1],{},init);
    auto loader=verify_loaded(profiles.modules[3],{},tls);
    auto self=verify_loaded(profiles.modules[4],profiles.reviewed_observer_identity,reinterpret_cast<std::uintptr_t>(&prepare));
    if(!api.valid()||!loader.valid())return finish(Refusal::ProviderMismatch);
    if(!self.valid())return finish(Refusal::SelfUnavailable);
    if(init!=api.image.bias+0x5ab8||shutdown!=api.image.bias+0x5680||tls!=loader.image.bias+0x10180)
        return finish(Refusal::ProviderMismatch);
    Dl_info second{};
    if(!out.lookup_(reinterpret_cast<const void*>(shutdown),&second)||reinterpret_cast<std::uintptr_t>(second.dli_fbase)!=api.image.bias)
        return finish(Refusal::ProviderMismatch);
    // dlsym only searches already-loaded modules and never invokes the factory.
    if(::dlsym(RTLD_DEFAULT,"VRClientCoreFactory")) {out.client_present_=true;return finish(Refusal::ClientAlreadyPresent);}
    out.images_[1]=api.image;out.images_[3]=loader.image;out.images_[4]=self.image;
    out.native_init_=init;out.native_shutdown_=shutdown;
    return finish(Refusal::None);
}
} // namespace frame_import_main
