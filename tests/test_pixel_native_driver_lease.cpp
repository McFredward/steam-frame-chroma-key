// This TU deliberately includes the implementation to test its internal cold
// file/ELF/map verifier with an owned host DSO. No public alternate profile API
// is compiled into the diagnostic layer.
#define FRAME_PIXEL_NATIVE_DRIVER_LEASE_TEST 1
#include "../layer/pixel_native_driver_lease.cpp"
#include "pixel_native_driver_lease_fixture.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/mman.h>
using namespace frame_pixel_native_driver;
namespace {
unsigned checks=0;
void check(bool value,const char* what) {++checks;if(!value){std::cerr<<"FAILED: "<<what<<"\n";std::exit(1);}}
std::string toHex(const unsigned char* p,std::size_t n){constexpr char h[]="0123456789abcdef";std::string out;for(std::size_t i=0;i<n;++i){out+=h[p[i]>>4];out+=h[p[i]&15];}return out;}
std::string buildIdOf(int fd) {
    Elf64_Ehdr h{};check(exactRead(fd,&h,sizeof(h),0),"fixture ELF header");
    for(unsigned i=0;i<h.e_phnum;++i){Elf64_Phdr ph{};check(exactRead(fd,&ph,sizeof(ph),h.e_phoff+i*sizeof(ph)),"fixture PHDR");if(ph.p_type!=PT_NOTE)continue;
        std::vector<unsigned char> notes(ph.p_filesz);check(exactRead(fd,notes.data(),notes.size(),ph.p_offset),"fixture notes");std::size_t off=0;
        while(off+sizeof(Elf64_Nhdr)<=notes.size()){Elf64_Nhdr n{};std::memcpy(&n,notes.data()+off,sizeof(n));off+=sizeof(n);const auto names=(n.n_namesz+3u)&~3u,desc=(n.n_descsz+3u)&~3u;
            check(names<=notes.size()-off && desc<=notes.size()-off-names,"fixture note bounds");if(n.n_type==NT_GNU_BUILD_ID && n.n_namesz==4 && !std::memcmp(notes.data()+off,"GNU",4))return toHex(notes.data()+off+names,n.n_descsz);off+=names+desc;}}
    return {};
}
void callbackFixture() {
    Lease lease; Inputs in{reinterpret_cast<VkDevice>(0x1000),reinterpret_cast<PFN_vkVoidFunction>(0x2000),reinterpret_cast<PFN_vkVoidFunction>(0x2000),reinterpret_cast<PFN_vkVoidFunction>(0x3000),reinterpret_cast<PFN_vkVoidFunction>(0x3000)};
    check(LeaseTestAccess::fixture(lease,in),"explicit synthetic callback fixture");
    auto p=lease.acquire(in.device,Route::Begin2,in.begin2);check(authorizeValveSrgb(p,false),"begin permit");check(!authorizeValveSrgb(p,true),"begin cannot authorize next");
    check(!authorizeValveSrgb(lease.acquire(reinterpret_cast<VkDevice>(0x1001),Route::Begin2,in.begin2),false),"wrong device");
    check(!authorizeValveSrgb(lease.acquire(in.device,Route::Begin2,in.next2),false),"wrong PFN");
    check(!authorizeValveSrgb(lease.acquire(in.device,static_cast<Route>(99),in.begin2),false),"invalid route");
    LeaseTestAccess::advanceEpoch(lease);check(!authorizeValveSrgb(p,false),"old epoch refused while ready");p=lease.acquire(in.device,Route::Begin2,in.begin2);check(authorizeValveSrgb(p,false),"fresh epoch permit works");
    check(!lease.releaseCold(),"ready cannot release");auto moved=std::move(p);check(!authorizeValveSrgb(p,false)&&authorizeValveSrgb(moved,false),"move transfers ownership");
    check(lease.status().activePermits==1,"one active moved permit");lease.revoke();check(!authorizeValveSrgb(moved,false),"revocation invalidates permit");check(!lease.releaseCold(),"live permit prevents release");
    moved={};check(lease.releaseCold(),"release after permit ends");check(!lease.prepareCold(in),"closed lease cannot prepare");check(lease.status().state==State::Released,"closed cannot revive");
}
void staticNative(const char* path) {
    Profile profile=NativeProfile;profile.path=path;profile.owner=getuid();profile.protectedDirectories=false; // Acquired local copy, not a native file-owner claim.
    File file;const auto result=verifyFile(profile,file);check(result==Refusal::None,"exact acquired ARM ICD file pin and ELF/buildID");
    check(file.eh.e_machine==EM_AARCH64 && file.eh.e_phnum==11,"exact acquired ELF shape");
    check(hexDigest(file.hashes[3],"dad9646385df1b875fec532cf373da4753c14d1e248a1bad4a5bf6ca58962aae"),"acquired RX hash independently pinned");
    close(file.fd);
}
void mountControls() {
    const std::string root="32 1 0:29 / / ro - btrfs /dev/nvme rw,subvolid=5,subvol=/\n";
    auto valid=[&](std::string x){std::vector<char> text(x.begin(),x.end());text.push_back(0);return btrfsMountShape(text.data(),DriverPath,0,29);};
    check(valid(root),"exact reviewed Btrfs mount shape");
    auto mutate=[&](const char* old,const char* replacement){std::string x=root;x.replace(x.find(old),std::strlen(old),replacement);return x;};
    check(!valid(mutate("subvolid=5","subvolid=50")),"Btrfs subvolid suffix refused");
    check(!valid(mutate("subvol=/","subvol=/other")),"Btrfs subvolume suffix refused");
    check(!valid(root+"33 32 0:30 / /usr ro - btrfs /dev/nvme rw,subvolid=5,subvol=/\n"),"nested mount takes precedence and refuses");
    check(!valid(root+root),"duplicate root mount ambiguous");
    check(!valid(mutate("subvolid=5","subvolid=5,subvolid=5")),"duplicate subvolid refused");
    check(!valid(mutate("0:29","0:30")),"wrong map device refused");
    check(!valid(mutate("btrfs","ext4")),"non Btrfs refused");
    check(!valid(mutate("/ / ro","/other / ro")),"wrong mount root refused");
}
void ownedDso(const char* path) {
    void* original=dlopen(path,RTLD_NOW|RTLD_LOCAL);check(original!=nullptr,"owned fixture loads");
    auto begin=reinterpret_cast<PFN_vkVoidFunction>(dlsym(original,"lease_fixture_begin"));auto next=reinterpret_cast<PFN_vkVoidFunction>(dlsym(original,"lease_fixture_next"));auto wrapper=reinterpret_cast<PFN_vkVoidFunction>(dlsym(original,"lease_fixture_wrapper"));
    check(begin&&next&&wrapper,"owned exports");Dl_info info{};check(dladdr(reinterpret_cast<void*>(begin),&info)!=0,"owned module base");const auto base=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    const int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);check(fd>=0,"owned file");struct stat st{};check(fstat(fd,&st)==0,"owned stat");std::array<std::uint8_t,32> sha{};check(hashFileRange(fd,0,st.st_size,sha),"owned hash");const auto shaText=toHex(sha.data(),sha.size()),id=buildIdOf(fd);close(fd);check(id.size()==40,"owned SHA1 build ID");
    const auto beginRva=reinterpret_cast<std::uintptr_t>(begin)-base,nextRva=reinterpret_cast<std::uintptr_t>(next)-base;
    Profile profile{path,shaText.c_str(),id.c_str(),static_cast<std::uint64_t>(st.st_size),EM_X86_64,static_cast<std::uint32_t>(getuid()),false,{{beginRva,nextRva},{beginRva+1,nextRva+1},{beginRva+2,nextRva+2}}};
    Inputs input{reinterpret_cast<VkDevice>(0xfeed),begin,begin,next,next};
    Lease lease;check(detail::Preparation::run(lease,input,profile),"real owned file/maps/loader/RX verification");check(lease.status().state==State::Ready&&lease.status().chip==6&&lease.status().routeMask==15,"owned verifier exact metadata");
    for(unsigned i=0;i<4;++i){auto permit=lease.acquire(input.device,static_cast<Route>(i),function(input,i));check(authorizeValveSrgb(permit,i>=2),"all four owned exact routes");}
    dlclose(original);original=nullptr; // Only the lease now holds its RTLD reference.
    {auto permit=lease.acquire(input.device,Route::Begin2,begin);check(authorizeValveSrgb(permit,false),"lease retains module after original close");}
    check(!lease.prepareCold(input),"cannot reprepare live lease");
    lease.revoke();check(lease.releaseCold(),"owned module explicit release");
    // All negatives run through the same cold entry and preserve refusal.
    original=dlopen(path,RTLD_NOW|RTLD_LOCAL);check(original!=nullptr,"fixture reload");
    auto reject=[&](Inputs in,Profile p,Refusal expected,const char* label){Lease bad;check(!detail::Preparation::run(bad,in,p),label);check(bad.status().refusal==expected,"precise owned refusal");bad.revoke();check(bad.releaseCold(),"refused resource cleanup");};
    Inputs changed=input;changed.begin2=wrapper;reject(changed,profile,Refusal::EntryPoint,"wrapper entry refused");
    changed=input;changed.next2=nullptr;changed.next2KHR=nullptr;reject(changed,profile,Refusal::Inputs,"missing next refused");
    changed=input;changed.device=nullptr;reject(changed,profile,Refusal::Inputs,"missing device refused");
    Profile bad=profile;bad.bytes=MaximumFileBytes+1;reject(input,bad,Refusal::FileBounds,"oversize expected file refused");
    std::string zeros(64,'0');bad=profile;bad.sha=zeros.c_str();reject(input,bad,Refusal::FileHash,"changed bytes hash refused");
    std::string wrongId(40,'0');bad=profile;bad.buildId=wrongId.c_str();reject(input,bad,Refusal::Elf,"wrong build ID refused");
    bad=profile;bad.machine=EM_AARCH64;reject(input,bad,Refusal::Elf,"wrong ELF architecture refused");
    bad=profile;bad.owner=static_cast<std::uint32_t>(getuid()+1);reject(input,bad,Refusal::FileIdentity,"wrong owner refused");
    // Actual file metadata change is observed independently of its content hash.
    File observed;check(verifyFile(profile,observed)==Refusal::None,"observe owned file stable");struct stat prior=observed.identity;check(chmod(path,0744)==0,"mutate file mode");struct stat after{};check(fstat(observed.fd,&after)==0&&!sameStat(prior,after),"changed identity rejected");check(chmod(path,0755)==0,"restore mode");close(observed.fd);
    std::string link=std::string(path)+".link";unlink(link.c_str());check(symlink(path,link.c_str())==0,"make symlink negative");check(secureOpen(link.c_str())<0,"final symlink refused");unlink(link.c_str());
    check(secureOpen("/tmp/../etc/passwd")<0,"dot traversal refused");check(secureOpen("relative")<0,"relative path refused");
    // Full cold run raced with close, while cold status is read concurrently.
    for(unsigned round=0;round<8;++round){Lease raced;bool ready=false;std::thread worker([&]{ready=detail::Preparation::run(raced,input,profile);});while(raced.status().state==State::Empty)std::this_thread::yield();raced.revoke();for(unsigned j=0;j<128;++j){const auto status=raced.status();check(status.state!=State::Ready,"revocation cannot resurrect publication");}worker.join();check(!ready&&raced.status().state==State::Revoked,"prepare loses to revoke");check(raced.releaseCold(),"raced cold resources reaped");}
    // Loaded byte mismatch is checked against the already hashed file. No write
    // is made to an external/native module: only this owned fixture's RX page.
    File file;check(verifyFile(profile,file)==Refusal::None,"owned file for RX mutant");Maps maps;check(loadMaps(path,file.fd,file.identity,maps),"owned map identity");
    const auto pageSize=static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));const auto page=reinterpret_cast<std::uintptr_t>(begin)&~(pageSize-1);auto* byte=reinterpret_cast<unsigned char*>(begin);const auto old=*byte;
    check(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_WRITE|PROT_EXEC)==0,"owned RX mutant writable");*byte^=1;
    check(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC)==0,"owned RX mutant restored permissions");Loaded loaded{file,maps,base};check(dl_iterate_phdr(Loaded::visit,&loaded)==1&&loaded.refusal==Refusal::LoadedCode,"loaded code mutant refused");
    check(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_WRITE|PROT_EXEC)==0,"restore owned RX writable");*byte=old;check(mprotect(reinterpret_cast<void*>(page),pageSize,PROT_READ|PROT_EXEC)==0,"restore exact owned RX");close(file.fd);dlclose(original);
}
}
int main(int argc,char** argv){callbackFixture();mountControls();if(argc>1)ownedDso(argv[1]);if(argc>2)staticNative(argv[2]);std::cout<<checks<<" native-driver lease checks passed; owned host fixture only, native visual result unverified\n";}
