#include "pixel_native_driver_lease.h"
#include "../receiver/sha256.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <limits>
#include <link.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace frame_pixel_native_driver {
namespace {
constexpr std::uint64_t MaximumFileBytes=32u*1024u*1024u;
constexpr std::uint64_t MaximumMappedBytes=32u*1024u*1024u;
constexpr unsigned MaximumHeaders=16,MaximumModules=256;
constexpr char DriverPath[]="/usr/lib/libvulkan_freedreno.so";
constexpr char FileSha[]="dfed0652b592b263a400ba0a64fbdd421870a938abed4d447a9aafa6a4ad789a";
constexpr char BuildId[]="b7e5995d0b202f06e81cd5a0f809ec1b744a0cb8";
constexpr std::uint64_t EntryRvas[3][2]={{0xb113e0,0xb11940},{0xb12180,0xb11d60},{0xb18040,0xb185a0}};
std::atomic<std::uint64_t> nextEpoch{1};
#if defined(FRAME_PIXEL_NATIVE_DRIVER_LEASE_TEST)
// Cold-only deterministic adversarial hooks; absent from the runtime library.
void (*testBeforeStable)(Lease&) noexcept=nullptr;
void (*testBeforeReady)(Lease&) noexcept=nullptr;
#endif
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"Callback readers require lock-free atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Callback epoch requires lock-free atomics");
unsigned routeIndex(Route r) noexcept { return static_cast<unsigned>(r); }
PFN_vkVoidFunction function(const Inputs& x,unsigned i) noexcept {
    const PFN_vkVoidFunction all[]{x.begin2,x.begin2KHR,x.next2,x.next2KHR}; return i<4?all[i]:nullptr;
}
std::uint64_t freshEpoch() noexcept {
    auto value=nextEpoch.load(std::memory_order_relaxed);
    while(value && value<std::numeric_limits<std::uint64_t>::max())
        if(nextEpoch.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) return value;
    return 0;
}
bool sameStat(const struct stat& a,const struct stat& b) noexcept {
    return a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_size==b.st_size &&
        a.st_mode==b.st_mode && a.st_uid==b.st_uid && a.st_gid==b.st_gid && a.st_nlink==b.st_nlink &&
        a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec &&
        a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;
}
bool hexDigest(const std::array<std::uint8_t,32>& digest,const char* expected) noexcept {
    constexpr char hex[]="0123456789abcdef";
    if(!expected || std::strlen(expected)!=64) return false;
    for(unsigned i=0;i<32;++i) if(expected[i*2]!=hex[digest[i]>>4] || expected[i*2+1]!=hex[digest[i]&15]) return false;
    return true;
}
bool exactRead(int fd,void* destination,std::size_t n,std::uint64_t offset) noexcept {
    auto* out=static_cast<unsigned char*>(destination);
    while(n) {
        const auto got=pread(fd,out,n,static_cast<off_t>(offset));
        if(got<0 && errno==EINTR) continue;
        if(got<=0) return false;
        out+=got; n-=static_cast<std::size_t>(got); offset+=static_cast<std::size_t>(got);
    }
    return true;
}
bool hashFileRange(int fd,std::uint64_t offset,std::uint64_t n,std::array<std::uint8_t,32>& digest) noexcept {
    std::array<unsigned char,4096> buffer{}; frame_receiver::Sha256 hash;
    while(n) {
        const auto count=n<buffer.size()?static_cast<std::size_t>(n):buffer.size();
        if(!exactRead(fd,buffer.data(),count,offset)) return false;
        hash.update(buffer.data(),count); offset+=count; n-=count;
    }
    digest=hash.finish(); return true;
}
// Each path component is opened independently. An ordinary symlink (including a
// substituted parent) is never followed; proc links are used only for maps data.
int secureOpen(const char* path,bool protectedDirectories=false) noexcept {
    if(!path || path[0]!='/' || std::strlen(path)>511) return -1;
    int parent=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(parent<0) return -1;
    struct stat directory{};
    if(protectedDirectories && (fstat(parent,&directory)!=0 || directory.st_uid!=0 || (directory.st_mode&0022))) {close(parent);return -1;}
    const char* cursor=path+1;
    while(*cursor) {
        const char* slash=std::strchr(cursor,'/'); const auto count=slash?static_cast<std::size_t>(slash-cursor):std::strlen(cursor);
        char name[256]{};
        if(!count || count>=sizeof(name)) { close(parent); return -1; }
        std::memcpy(name,cursor,count);
        if(!std::strcmp(name,".") || !std::strcmp(name,"..")) { close(parent); return -1; }
        const int flags=O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|(slash?O_DIRECTORY:0);
        const int next=openat(parent,name,flags); close(parent); parent=next;
        if(parent<0) return -1;
        if(!slash) return parent;
        if(protectedDirectories && (fstat(parent,&directory)!=0 || directory.st_uid!=0 || (directory.st_mode&0022))) {close(parent);return -1;}
        cursor=slash+1;
    }
    close(parent); return -1;
}
bool readText(const char* path,char* buffer,std::size_t capacity,std::size_t& used) noexcept {
    const int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW); if(fd<0) return false;
    used=0; bool good=true;
    while(used<capacity-1) {
        const auto n=read(fd,buffer+used,capacity-1-used);
        if(n<0 && errno==EINTR) continue;
        if(n<0) { good=false; break; }
        if(!n) break;
        used+=static_cast<std::size_t>(n);
    }
    char extra{};
    if(good && used==capacity-1 && read(fd,&extra,1)!=0) good=false;
    close(fd); buffer[used]=0; return good;
}
struct Map {std::uint64_t begin{},end{},offset{},inode{};unsigned major{},minor{};char perm[5]{};};
struct Maps {
    std::array<Map,32> rows{}; unsigned count{};
    bool covers(std::uint64_t begin,std::uint64_t bytes,bool executable,bool readonly) const noexcept {
        if(bytes>UINT64_MAX-begin) return false;
        const auto end=begin+bytes;
        while(begin<end) {
            bool found=false;
            for(unsigned i=0;i<count;++i) {
                const auto& m=rows[i];
                if(begin<m.begin || begin>=m.end) continue;
                if(m.perm[0]!='r' || (readonly && m.perm[1]=='w') || (executable && m.perm[2]!='x')) return false;
                begin=m.end<end?m.end:end; found=true;break;
            }
            if(!found) return false;
        }
        return true;
    }
};
bool sameMaps(const Maps& a,const Maps& b) noexcept {
    if(a.count!=b.count) return false;
    for(unsigned i=0;i<a.count;++i) {
        const auto& x=a.rows[i];const auto& y=b.rows[i];
        if(x.begin!=y.begin||x.end!=y.end||x.offset!=y.offset||x.inode!=y.inode||x.major!=y.major||x.minor!=y.minor||std::strcmp(x.perm,y.perm))return false;
    }
    return true;
}
bool exactOption(const char* options,const char* prefix,const char* exact) noexcept {
    unsigned count=0;bool matches=false;const auto prefixBytes=std::strlen(prefix),exactBytes=std::strlen(exact);
    const char* token=options;
    while(*token) {
        const char* comma=std::strchr(token,',');const auto bytes=comma?static_cast<std::size_t>(comma-token):std::strlen(token);
        if(bytes>=prefixBytes && !std::memcmp(token,prefix,prefixBytes)){++count;matches=bytes==exactBytes&&!std::memcmp(token,exact,exactBytes);}
        if(!comma)break;
        token=comma+1;
    }
    return count==1&&matches;
}
bool btrfsMountShape(char* text,const char* path,unsigned mapMajor,unsigned mapMinor) noexcept {
    // Choose the single longest path-prefix mount. A nested bind/subvolume or
    // duplicate mount must never be justified by the less-specific root row.
    bool found=false,admitted=false;std::size_t longest=0;char* line=text;
    while(*line) {
        char* end=std::strchr(line,'\n');if(end)*end=0;
        unsigned id=0,parent=0,a=0,b=0;char rootName[256]{},point[256]{};
        if(std::sscanf(line,"%u %u %u:%u %255s %255s",&id,&parent,&a,&b,rootName,point)!=6)return false;
        if(std::strchr(rootName,'\\')||std::strchr(point,'\\'))return false;
        const auto len=std::strlen(point),pathLen=std::strlen(path);
        const bool matches=point[0]=='/' && len<=pathLen && !std::memcmp(point,path,len) && (len==1 || len==pathLen || path[len]=='/');
        if(matches) {
            if(found && len==longest)return false;
            if(!found || len>longest) {
                const auto* sep=std::strstr(line," - ");char type[32]{},source[256]{},options[2048]{};
                if(!sep||std::sscanf(sep+3,"%31s %255s %2047s",type,source,options)!=3)return false;
                found=true;longest=len;
                admitted=a==mapMajor&&b==mapMinor&&!std::strcmp(rootName,"/")&&!std::strcmp(point,"/")&&
                    !std::strcmp(type,"btrfs")&&exactOption(options,"subvolid=","subvolid=5")&&exactOption(options,"subvol=","subvol=/");
            }
        }
        if(!end)break;
        line=end+1;
    }
    return found&&admitted;
}
bool btrfsRootIdentity(int fd,unsigned mapMajor,unsigned mapMinor,const struct stat& st) noexcept {
    if(mapMajor!=0 || !mapMinor || major(st.st_dev)!=0 || !minor(st.st_dev) || mapMinor==minor(st.st_dev)) return false;
    struct statfs fileFs{},rootFs{};struct stat rootStat{};
    const int root=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(root<0)return false;
    const bool fs=fstat(root,&rootStat)==0 && rootStat.st_ino==256 && rootStat.st_dev==st.st_dev &&
        fstatfs(fd,&fileFs)==0 && fstatfs(root,&rootFs)==0 &&
        static_cast<unsigned long>(fileFs.f_type)==0x9123683eul && fileFs.f_type==rootFs.f_type &&
        std::memcmp(&fileFs.f_fsid,&rootFs.f_fsid,sizeof(fileFs.f_fsid))==0;
    close(root);if(!fs)return false;
    std::array<char,65536> text{};std::size_t used=0;if(!readText("/proc/self/mountinfo",text.data(),text.size(),used))return false;
    return btrfsMountShape(text.data(),DriverPath,mapMajor,mapMinor);
}
bool loadMaps(const char* path,int fd,const struct stat& st,Maps& out) noexcept {
    std::array<char,262144> text{};std::size_t used=0;if(!readText("/proc/self/maps",text.data(),text.size(),used))return false;
    char* line=text.data();
    while(*line) {
        char* end=std::strchr(line,'\n');if(end)*end=0;
        unsigned long long begin=0,finish=0,offset=0,inode=0;unsigned a=0,b=0;char perm[5]{},name[512]{};
        const int parsed=std::sscanf(line,"%llx-%llx %4s %llx %x:%x %llu %511[^\n]",&begin,&finish,perm,&offset,&a,&b,&inode,name);
        if(parsed>=7 && parsed==8 && !std::strcmp(name,path)) {
            if(out.count==out.rows.size() || begin>=finish || inode!=static_cast<std::uint64_t>(st.st_ino) ||
                ((a!=major(st.st_dev)||b!=minor(st.st_dev)) && !btrfsRootIdentity(fd,a,b,st)))return false;
            auto& row=out.rows[out.count++];row={begin,finish,offset,inode,a,b,{}};std::memcpy(row.perm,perm,sizeof(perm));
        }
        if(!end)break;
        line=end+1;
    }
    return out.count>0;
}
struct Profile {const char* path;const char* sha;const char* buildId;std::uint64_t bytes;std::uint16_t machine;std::uint32_t owner;bool protectedDirectories;std::uint64_t rvas[3][2];};
const Profile NativeProfile{DriverPath,FileSha,BuildId,18850240,EM_AARCH64,0,true,{{0xb113e0,0xb11940},{0xb12180,0xb11d60},{0xb18040,0xb185a0}}};
struct File {
    int fd{-1};struct stat identity{};Elf64_Ehdr eh{};std::array<Elf64_Phdr,MaximumHeaders> ph{};
    std::array<std::array<std::uint8_t,32>,MaximumHeaders> hashes{};
};
Refusal verifyFile(const Profile& profile,File& out) noexcept {
    out.fd=secureOpen(profile.path,profile.protectedDirectories);if(out.fd<0)return Refusal::FileOpen;
    if(fstat(out.fd,&out.identity)!=0 || !S_ISREG(out.identity.st_mode) || out.identity.st_uid!=profile.owner ||
        (out.identity.st_mode&0022) || out.identity.st_nlink!=1)return Refusal::FileIdentity;
    if(out.identity.st_size<=0 || static_cast<std::uint64_t>(out.identity.st_size)>MaximumFileBytes ||
        static_cast<std::uint64_t>(out.identity.st_size)!=profile.bytes)return Refusal::FileBounds;
    std::array<std::uint8_t,32> digest{};
    if(!hashFileRange(out.fd,0,profile.bytes,digest)||!hexDigest(digest,profile.sha))return Refusal::FileHash;
    auto& eh=out.eh;
    if(!exactRead(out.fd,&eh,sizeof(eh),0) || std::memcmp(eh.e_ident,ELFMAG,SELFMAG) ||
       eh.e_ident[EI_CLASS]!=ELFCLASS64 || eh.e_ident[EI_DATA]!=ELFDATA2LSB || eh.e_ident[EI_VERSION]!=EV_CURRENT ||
       eh.e_type!=ET_DYN || eh.e_machine!=profile.machine || eh.e_version!=EV_CURRENT || eh.e_ehsize!=sizeof(eh) ||
       eh.e_phentsize!=sizeof(Elf64_Phdr) || !eh.e_phnum || eh.e_phnum>MaximumHeaders ||
       eh.e_phoff>profile.bytes || std::uint64_t(eh.e_phnum)*sizeof(Elf64_Phdr)>profile.bytes-eh.e_phoff ||
       !exactRead(out.fd,out.ph.data(),eh.e_phnum*sizeof(Elf64_Phdr),eh.e_phoff))return Refusal::Elf;
    bool buildId=false,rx=false;
    for(unsigned i=0;i<eh.e_phnum;++i) {
        const auto& ph=out.ph[i];
        if(ph.p_offset>profile.bytes || ph.p_filesz>profile.bytes-ph.p_offset)return Refusal::Elf;
        if(ph.p_type==PT_LOAD && (ph.p_memsz<ph.p_filesz || ph.p_vaddr>MaximumMappedBytes || ph.p_memsz>MaximumMappedBytes-ph.p_vaddr ||
            !(ph.p_flags&PF_R) || ((ph.p_flags&PF_X)&&(ph.p_flags&PF_W))))return Refusal::Elf;
        if(ph.p_type==PT_LOAD && (ph.p_flags&PF_X)) rx=true;
        if(ph.p_type==PT_NOTE) {
            if(ph.p_filesz>4096)return Refusal::Elf;
            std::array<unsigned char,4096> notes{};if(!exactRead(out.fd,notes.data(),ph.p_filesz,ph.p_offset))return Refusal::Elf;
            std::size_t off=0;
            while(off+sizeof(Elf64_Nhdr)<=ph.p_filesz) {
                Elf64_Nhdr note{};std::memcpy(&note,notes.data()+off,sizeof(note));off+=sizeof(note);
                if(note.n_namesz>4096 || note.n_descsz>4096)return Refusal::Elf;
                const std::size_t names=(note.n_namesz+3u)&~std::size_t(3),desc=(note.n_descsz+3u)&~std::size_t(3);
                if(names>ph.p_filesz-off || desc>ph.p_filesz-off-names)return Refusal::Elf;
                if(note.n_type==NT_GNU_BUILD_ID && note.n_namesz==4 && !std::memcmp(notes.data()+off,"GNU",4)) {
                    if(buildId || note.n_descsz!=20 || !profile.buildId || std::strlen(profile.buildId)!=40)return Refusal::Elf;
                    constexpr char hex[]="0123456789abcdef";
                    for(unsigned j=0;j<20;++j) {const auto value=notes[off+names+j];if(profile.buildId[2*j]!=hex[value>>4]||profile.buildId[2*j+1]!=hex[value&15])return Refusal::Elf;}
                    buildId=true;
                }
                off+=names+desc;
            }
            if(off!=ph.p_filesz)return Refusal::Elf;
        }
        if((ph.p_type==PT_LOAD && (ph.p_flags&PF_X)) || ph.p_type==PT_NOTE)
            if(!hashFileRange(out.fd,ph.p_offset,ph.p_filesz,out.hashes[i]))return Refusal::FileHash;
    }
    struct stat after{};if(fstat(out.fd,&after)!=0||!sameStat(out.identity,after))return Refusal::Changed;
    return buildId&&rx?Refusal::None:Refusal::Elf;
}
struct Loaded {
    const File& file;const Maps& maps;std::uintptr_t base;unsigned visited{};bool found{};Refusal refusal{Refusal::Enumeration};
    static int visit(dl_phdr_info* info,std::size_t bytes,void* opaque) noexcept {
        auto& x=*static_cast<Loaded*>(opaque);
        if(++x.visited>MaximumModules){x.refusal=Refusal::Enumeration;return 1;}
        if(!info||bytes<offsetof(dl_phdr_info,dlpi_phnum)+sizeof(info->dlpi_phnum))return 1;
        if(info->dlpi_addr!=x.base)return 0;
        x.found=true;
        if(!info->dlpi_phdr || info->dlpi_phnum!=x.file.eh.e_phnum){x.refusal=Refusal::LoadedIdentity;return 1;}
        for(unsigned i=0;i<info->dlpi_phnum;++i) {
            Elf64_Phdr actual{};std::memcpy(&actual,info->dlpi_phdr+i,sizeof(actual));
            if(std::memcmp(&actual,&x.file.ph[i],sizeof(actual))){x.refusal=Refusal::LoadedIdentity;return 1;}
        }
        if(!x.maps.covers(x.base,sizeof(Elf64_Ehdr),false,true) ||
           std::memcmp(reinterpret_cast<const void*>(x.base),&x.file.eh,sizeof(Elf64_Ehdr))){x.refusal=Refusal::LoadedIdentity;return 1;}
        for(unsigned i=0;i<info->dlpi_phnum;++i) {
            const auto& ph=x.file.ph[i];
            const bool rx=ph.p_type==PT_LOAD && (ph.p_flags&PF_X);
            if(!rx && ph.p_type!=PT_NOTE)continue;
            if(ph.p_vaddr>UINTPTR_MAX-x.base || !x.maps.covers(x.base+ph.p_vaddr,ph.p_filesz,rx,true)) {x.refusal=Refusal::Maps;return 1;}
            frame_receiver::Sha256 hash;std::array<unsigned char,4096> buffer{};std::uint64_t n=ph.p_filesz,offset=0;
            while(n) {
                const auto count=n<buffer.size()?static_cast<std::size_t>(n):buffer.size();
                // Only verified readable, nonwritable file-backed ELF note/RX
                // ranges, while the real loader enumeration lock is held.
                std::memcpy(buffer.data(),reinterpret_cast<const void*>(x.base+ph.p_vaddr+offset),count);
                hash.update(buffer.data(),count);offset+=count;n-=count;
            }
            if(hash.finish()!=x.file.hashes[i]){x.refusal=rx?Refusal::LoadedCode:Refusal::LoadedIdentity;return 1;}
        }
        x.refusal=Refusal::None;return 1;
    }
};
} // namespace
namespace detail {
// Kept internal: only the production constant profile can reach this through
// Lease's public method. Owned tests exercise this same verifier with their own
// ELF and entrypoints; that is not evidence about the native provider.
struct Preparation {
    [[maybe_unused]] static bool run(Lease& lease,const Inputs& input,const Profile& profile) noexcept;
};
}
Permit::Permit(const Lease* lease,std::uint64_t epoch,Route route) noexcept:lease_(lease),epoch_(epoch),route_(route){}
Permit::Permit(Permit&& other) noexcept:lease_(other.lease_),epoch_(other.epoch_),route_(other.route_){other.lease_=nullptr;}
Permit& Permit::operator=(Permit&& other) noexcept {
    if(this!=&other){reset();lease_=other.lease_;epoch_=other.epoch_;route_=other.route_;other.lease_=nullptr;}return *this;
}
Permit::~Permit(){reset();}
void Permit::reset() noexcept {if(lease_){lease_->readers_.fetch_sub(1,std::memory_order_release);lease_=nullptr;}}
bool authorizeValveSrgb(const Permit& p,bool next) noexcept {
    return p.lease_ && p.lease_->state_.load(std::memory_order_acquire)==State::Ready &&
        p.epoch_!=0 && p.epoch_==p.lease_->epoch_.load(std::memory_order_acquire) &&
        ((routeIndex(p.route_)>=2)==next);
}
Permit Lease::acquire(VkDevice device,Route route,PFN_vkVoidFunction saved) const noexcept {
    const auto i=routeIndex(route);
    if(i>=4 || !device || !saved || state_.load(std::memory_order_acquire)!=State::Ready)return {};
    const auto generation=epoch_.load(std::memory_order_acquire);
    // Saturating increment avoids a theoretical wrap becoming false quiescence.
    auto readers=readers_.load(std::memory_order_relaxed);bool acquired=false;
    for(unsigned attempt=0;attempt<8 && readers!=UINT32_MAX;++attempt) {
        if(readers_.compare_exchange_strong(readers,readers+1,std::memory_order_acquire)){acquired=true;break;}
    }
    if(!acquired){acquireMisses_.fetch_add(1,std::memory_order_relaxed);return {};}
    if(state_.load(std::memory_order_acquire)!=State::Ready || !generation || epoch_.load(std::memory_order_acquire)!=generation ||
       inputs_.device!=device || function(inputs_,i)!=saved || !(routeMask_&(1u<<i))) {
        readers_.fetch_sub(1,std::memory_order_release);return {};
    }
    return Permit(this,generation,route);
}
void Lease::revoke() noexcept {
    auto state=state_.load(std::memory_order_acquire);
    while(state!=State::Revoked && state!=State::Released)
        if(state_.compare_exchange_weak(state,State::Revoked,std::memory_order_acq_rel))return;
}
bool Lease::fail(Refusal r) noexcept {
    auto none=Refusal::None;refusal_.compare_exchange_strong(none,r,std::memory_order_release);
    // Called only at the terminal cold edge; no metadata is changed afterward.
    published_.store(true,std::memory_order_release);
    auto preparing=State::Preparing;state_.compare_exchange_strong(preparing,State::Refused,std::memory_order_release);return false;
}
bool Lease::releaseCold() noexcept {
    if(cold_.test_and_set(std::memory_order_acquire))return false;
    const auto state=state_.load(std::memory_order_acquire);
    if((state!=State::Revoked && state!=State::Refused) || readers_.load(std::memory_order_acquire)!=0){cold_.clear(std::memory_order_release);return false;}
    if(module_){dlclose(module_);module_=nullptr;}
    if(fd_>=0){close(fd_);fd_=-1;}
    state_.store(State::Released,std::memory_order_release);cold_.clear(std::memory_order_release);return true;
}
Status Lease::status() const noexcept {
    Status out;out.state=state_.load(std::memory_order_acquire);out.refusal=refusal_.load(std::memory_order_acquire);
    out.epoch=epoch_.load(std::memory_order_acquire);out.activePermits=readers_.load(std::memory_order_acquire);out.acquireMisses=acquireMisses_.load(std::memory_order_relaxed);
    if(published_.load(std::memory_order_acquire)) {
        out.device=reinterpret_cast<std::uintptr_t>(inputs_.device);out.moduleBase=base_;out.inode=inode_;out.deviceMajor=devMajor_;out.deviceMinor=devMinor_;
        out.chip=chip_;out.routeMask=routeMask_;out.observedRoute=observedRoute_;std::memcpy(out.providerPath,providerPath_,sizeof(providerPath_));for(unsigned i=0;i<4;++i)out.rvas[i]=rvas_[i];
    }
    return out;
}
bool Lease::prepareCold(const Inputs& input) noexcept {
#if !defined(__aarch64__)
    (void)input;
    auto empty=State::Empty;
    if(!state_.compare_exchange_strong(empty,State::Preparing,std::memory_order_acq_rel))return false;
    return fail(Refusal::Architecture);
#else
    return detail::Preparation::run(*this,input,NativeProfile);
#endif
}
bool detail::Preparation::run(Lease& lease,const Inputs& input,const Profile& profile) noexcept {
    if(lease.cold_.test_and_set(std::memory_order_acquire))return false;
    struct Unlock{Lease& lease;~Unlock(){lease.cold_.clear(std::memory_order_release);}} unlock{lease};
    auto empty=State::Empty;if(!lease.state_.compare_exchange_strong(empty,State::Preparing,std::memory_order_acq_rel))return false;
    if(!input.device || !(input.begin2||input.begin2KHR) || !(input.next2||input.next2KHR))return lease.fail(Refusal::Inputs);
    lease.inputs_=input;
    Dl_info info{};std::uintptr_t base=0;std::uint64_t rvas[4]{};unsigned mask=0,chip=UINT32_MAX;
    for(unsigned i=0;i<4;++i) {
        const auto p=function(input,i);if(!p)continue;
        lease.observedRoute_=i;lease.base_=0;lease.providerPath_[0]=0;
        Dl_info current{};
        if(!dladdr(reinterpret_cast<const void*>(p),&current) || !current.dli_fbase || !current.dli_fname)return lease.fail(Refusal::Resolver);
        const auto pathBytes=strnlen(current.dli_fname,sizeof(lease.providerPath_));
        if(pathBytes<sizeof(lease.providerPath_))std::memcpy(lease.providerPath_,current.dli_fname,pathBytes+1);
        const auto currentBase=reinterpret_cast<std::uintptr_t>(current.dli_fbase),currentAddress=reinterpret_cast<std::uintptr_t>(p);
        lease.base_=currentBase;if(currentAddress>=currentBase)lease.rvas_[i]=currentAddress-currentBase;
        if(pathBytes==sizeof(lease.providerPath_) || std::strcmp(current.dli_fname,profile.path))return lease.fail(Refusal::ModulePath);
        const auto module=reinterpret_cast<std::uintptr_t>(current.dli_fbase),address=reinterpret_cast<std::uintptr_t>(p);
        if(address<module || (base&&base!=module))return lease.fail(Refusal::EntryPoint);
        rvas[i]=address-module;base=module;info=current;mask|=1u<<i;
        unsigned matched=UINT32_MAX;
        for(unsigned c=0;c<3;++c)if(rvas[i]==profile.rvas[c][i>=2?1:0])matched=c;
        if(matched==UINT32_MAX || (chip!=UINT32_MAX&&chip!=matched))return lease.fail(Refusal::EntryPoint);
        chip=matched;
    }
    lease.module_=dlopen(info.dli_fname,RTLD_NOW|RTLD_LOCAL|RTLD_NOLOAD);
    if(!lease.module_)return lease.fail(Refusal::ModulePin);
    File file;const auto fileResult=verifyFile(profile,file);lease.fd_=file.fd;
    if(fileResult!=Refusal::None)return lease.fail(fileResult);
    Maps before;if(!loadMaps(profile.path,file.fd,file.identity,before))return lease.fail(Refusal::MapIdentity);
    for(unsigned i=0;i<4;++i)if(mask&(1u<<i))if(!before.covers(base+rvas[i],4,true,true))return lease.fail(Refusal::EntryPoint);
    Loaded loaded{file,before,base};
    const int result=dl_iterate_phdr(Loaded::visit,&loaded);
    if(result!=1 || !loaded.found || loaded.refusal!=Refusal::None)return lease.fail(loaded.refusal);
    #if defined(FRAME_PIXEL_NATIVE_DRIVER_LEASE_TEST)
    if(testBeforeStable)testBeforeStable(lease);
#endif
    Maps after;struct stat statAfter{},pathAfter{};const int pathFd=secureOpen(profile.path,profile.protectedDirectories);
    const bool stable=pathFd>=0 && fstat(pathFd,&pathAfter)==0 && fstat(file.fd,&statAfter)==0 &&
        sameStat(file.identity,pathAfter)&&sameStat(file.identity,statAfter)&&loadMaps(profile.path,file.fd,file.identity,after)&&sameMaps(before,after);
    if(pathFd>=0)close(pathFd);
    if(!stable)return lease.fail(Refusal::Changed);
    const auto generation=freshEpoch();if(!generation)return lease.fail(Refusal::EpochExhausted);
    lease.inputs_=input;lease.base_=base;lease.inode_=file.identity.st_ino;lease.devMajor_=major(file.identity.st_dev);lease.devMinor_=minor(file.identity.st_dev);
    lease.chip_=chip+6;lease.routeMask_=mask;for(unsigned i=0;i<4;++i)lease.rvas_[i]=rvas[i];
    lease.epoch_.store(generation,std::memory_order_release);lease.published_.store(true,std::memory_order_release);
    #if defined(FRAME_PIXEL_NATIVE_DRIVER_LEASE_TEST)
    if(testBeforeReady)testBeforeReady(lease);
#endif
    auto preparing=State::Preparing;
    if(!lease.state_.compare_exchange_strong(preparing,State::Ready,std::memory_order_release))return lease.fail(Refusal::Closed);
    return true;
}
const char* stateName(State s) noexcept {
    switch(s){case State::Empty:return "empty";case State::Preparing:return "preparing";case State::Ready:return "ready";case State::Refused:return "refused";case State::Revoked:return "revoked";case State::Released:return "released";}return "invalid";
}
const char* refusalName(Refusal r) noexcept {
    switch(r){
#define R(x,s) case Refusal::x:return s;
    R(None,"none") R(Inputs,"inputs") R(Reentry,"reentry") R(Closed,"closed") R(Architecture,"architecture") R(Resolver,"resolver") R(ModulePath,"module_path") R(ModulePin,"module_pin") R(FileOpen,"file_open") R(FileIdentity,"file_identity") R(FileBounds,"file_bounds") R(FileHash,"file_hash") R(Elf,"elf") R(Maps,"maps") R(MapIdentity,"map_identity") R(EntryPoint,"entry_point") R(Enumeration,"enumeration") R(LoadedIdentity,"loaded_identity") R(LoadedCode,"loaded_code") R(Changed,"changed") R(EpochExhausted,"epoch_exhausted")
#undef R
    }return "invalid";
}
} // namespace frame_pixel_native_driver
