#include "../import/import_module_identity.h"
#include "../receiver/sha256.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <sys/mman.h>

namespace {
using namespace frame_import_module;
std::size_t checks=0;
void expect(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
std::string hex(const Digest& d) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;
    for (auto b:d) { result.push_back(digits[b>>4]); result.push_back(digits[b&15]); }
    return result;
}
Digest sha(const void* bytes,std::size_t size,std::size_t chunk) {
    frame_receiver::Sha256 hash;
    auto* data=static_cast<const std::uint8_t*>(bytes);
    while (size) { auto n=std::min(size,chunk); hash.update(data,n); size-=n; data+=n; }
    return hash.finish();
}
void sha_vectors() {
    expect(hex(sha("",0,1))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA empty");
    for (auto chunk:{1u,2u,3u,7u,64u,4096u}) {
        expect(hex(sha("abc",3,chunk))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA abc");
        const char longtext[]="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        expect(hex(sha(longtext,56,chunk))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","SHA padding block");
    }
    std::vector<std::uint8_t> million(1000000,'a');
    expect(hex(sha(million.data(),million.size(),4096))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA million a");
}
struct OwnedMapping {
    std::uint8_t* base=nullptr;
    std::size_t size=0, reads=0, bytes=0, outside=0, fail_at=0;
    ~OwnedMapping() { if(base) munmap(base,size); }
    static bool read(void* pointer,std::uintptr_t address,void* destination,std::size_t count) noexcept {
        auto& self=*static_cast<OwnedMapping*>(pointer);
        ++self.reads;
        const auto begin=reinterpret_cast<std::uintptr_t>(self.base);
        if (address<begin || address-begin>self.size || count>self.size-(address-begin)) {
            ++self.outside; return false;
        }
        if (self.fail_at && self.reads==self.fail_at) return false;
        self.bytes+=count;
        std::memcpy(destination,reinterpret_cast<const void*>(address),count);
        return true;
    }
    void clear() { reads=bytes=outside=fail_at=0; }
};
std::vector<std::uint8_t> file(const char* path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    expect(bool(stream),"fixture file available");
    const auto size=stream.tellg();
    expect(size>=0 && size<=static_cast<std::streamoff>(kMaxModuleBytes),"bounded fixture file");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(result.data()),size);
    expect(bool(stream),"fixture file copied completely");
    return result;
}
void refused(const Profile& p,const ModuleRead& m,OwnedMapping& owned,Refusal why,const char* label,
             bool before_read=false) {
    owned.clear();
    const auto result=verify_startup(p,m);
    expect(result.refusal==why,label);
    expect(!result.valid() && !result.load_bias && !result.code_begin && !result.code_end &&
           !result.module_lifetime_label && result.code_identity==Digest{},"failure leaks no validated facts");
    expect(owned.outside==0,"reader never receives an outside-owned span");
    if(before_read) expect(owned.reads==0,"invalid preconditions rejected before read");
}
void mapped_checks(const char* profile_path,const char* native_path) {
    const auto prepared=file(profile_path);
    expect(prepared.size()==kProfileBytes,"actual prepared profile size");
    ProfileBlob blob{}; std::copy(prepared.begin(),prepared.end(),blob.begin());
    Profile p{};
    expect(decode_profile(blob,p),"actual prepared native profile decoded");
    const auto native=file(native_path);
    expect(native.size()==p.file_size,"actual native file matches profile size");
    expect(sha(native.data(),native.size(),4096)==p.file_digest,"actual file hashlib crosscheck");
    OwnedMapping owned; owned.size=kMaxModuleBytes;
    owned.base=static_cast<std::uint8_t*>(mmap(nullptr,owned.size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    expect(owned.base!=MAP_FAILED,"fixture owns independent mapped storage");
    // Model the real ELF LOAD virtual layout, including the RW file/virtual offset
    // delta. This fixture owns its pages but is not a native loader or headset claim.
    for(const auto& ph:p.segments)
        if(ph.type==1) std::memcpy(owned.base+ph.virtual_address,native.data()+ph.file_offset,ph.file_size);
    const auto base=reinterpret_cast<std::uintptr_t>(owned.base);
    ModuleRead m; m.image_base=base; m.caller_pc=base+p.caller_rva; m.module_lifetime_label=77;
    m.range_count=2; m.ranges[0]={base,base+p.segments[0].file_size};
    m.ranges[1]={base+p.segments[1].virtual_address,base+p.segments[1].virtual_address+p.segments[1].memory_size};
    m.read=OwnedMapping::read; m.context=&owned;
    const auto good=verify_startup(p,m);
    expect(good.valid(),"actual native bytes mapped under owned fixture lease match");
    expect(good.load_bias==base && good.code_begin==base && good.code_end==base+p.segments[0].file_size &&
        good.module_lifetime_label==77 && good.code_identity==p.code_digest,"only exact mapped RX result exported");
    for(auto pc:{0x244dbcu,0x1b957cu,0xa9250u})
        expect(base+pc>=good.code_begin && base+pc<good.code_end,"all gate caller PCs in verified RX");
    expect(owned.outside==0 && owned.reads<2055 && owned.bytes<kMaxModuleBytes+1024,"bounded startup reads");
    for(std::size_t i=0;i<blob.size();++i) {
        auto changed=blob; changed[i]^=1;
        Profile output=p;
        expect(!decode_profile(changed,output),"every single-byte profile mutation rejected");
        expect(output.fingerprint==p.fingerprint,"failed decode preserves prior output");
    }
    auto changed_p=p; changed_p.code_digest[0]^=1;
    refused(changed_p,m,owned,Refusal::ProfileIdentity,"caller cannot replace expected code digest",true);
    changed_p=p; changed_p.phnum=9;
    refused(changed_p,m,owned,Refusal::ProfileIdentity,"oversize program table rejected before read",true);
    auto changed=m; changed.image_base=0;
    refused(p,changed,owned,Refusal::MissingLease,"missing public module base",true);
    changed=m; changed.module_lifetime_label=0;
    refused(p,changed,owned,Refusal::MissingLease,"missing local immutable lease label",true);
    changed=m; changed.read=nullptr;
    refused(p,changed,owned,Refusal::MissingLease,"missing owned reader",true);
    changed=m; changed.range_count=0;
    refused(p,changed,owned,Refusal::InvalidRanges,"no actual mappings",true);
    changed=m; changed.range_count=9;
    refused(p,changed,owned,Refusal::InvalidRanges,"excess actual mapping count",true);
    changed=m; changed.ranges[0].end=changed.ranges[0].begin;
    refused(p,changed,owned,Refusal::InvalidRanges,"empty actual range",true);
    changed=m; changed.ranges[1]=changed.ranges[0];
    refused(p,changed,owned,Refusal::InvalidRanges,"overlapping actual mappings",true);
    changed=m; changed.caller_pc+=4;
    refused(p,changed,owned,Refusal::CallerMismatch,"other instruction cannot impersonate exact caller",true);
    changed=m; changed.image_base+=4096;
    refused(p,changed,owned,Refusal::CallerMismatch,"wrong module bias rejected",true);
    changed=m; changed.image_base=std::numeric_limits<std::uintptr_t>::max()-32;
    refused(p,changed,owned,Refusal::AddressOverflow,"overflowed module extent",true);
    changed=m; changed.ranges[0].begin+=1;
    refused(p,changed,owned,Refusal::Unreadable,"ELF header outside loader-owned range",true);
    changed=m; changed.ranges[0].end=base+64;
    refused(p,changed,owned,Refusal::Unreadable,"PHDR not owned despite readable header");
    changed=m; changed.ranges[0].end=base+512;
    refused(p,changed,owned,Refusal::Unreadable,"note not owned despite readable header and PHDR");
    for(auto position:{0u,4u,18u,32u,52u,56u}) {
        owned.base[position]^=1;
        refused(p,m,owned,Refusal::HeaderIdentity,"mutated actual mapped header");
        owned.base[position]^=1;
    }
    for(std::size_t i=0;i<8;++i) {
        const auto position=p.phoff+56*i+16;
        owned.base[position]^=1;
        refused(p,m,owned,Refusal::ProgramHeadersIdentity,"mutated mapped program header");
        owned.base[position]^=1;
    }
    for(std::size_t i=0;i<36;++i) {
        const auto position=p.build_note_rva+i;
        owned.base[position]^=1;
        refused(p,m,owned,Refusal::BuildIdentity,"mutated mapped GNU note");
        owned.base[position]^=1;
    }
    for(auto position:{0x913b0u,0x244dbcu,0x1b957cu,0xa9250u,0x606999u,0x4f0e6cu}) {
        owned.base[position]^=1;
        refused(p,m,owned,Refusal::CodeIdentity,"mutated mapped executable/file-backed bytes");
        owned.base[position]^=1;
    }
    const auto last_read=static_cast<unsigned>(3+(p.segments[0].file_size+4095)/4096);
    for(auto step:{1u,2u,3u,4u,100u,1000u,last_read}) {
        owned.clear(); owned.fail_at=step;
        const auto r=verify_startup(p,m);
        expect(r.refusal==Refusal::ReadFailed,"owned reader loss fails closed");
        expect(owned.outside==0,"reader loss never broadens read span");
    }
    // Mutable RW and zero-filled memory are deliberately outside code identity.
    // They are not source/object ownership proof and are never acquired here.
    owned.clear(); owned.base[p.segments[1].virtual_address]^=1;
    expect(verify_startup(p,m).valid(),"mutable RW data is not misrepresented as code identity");
    expect(owned.outside==0,"all final reads bounded");
}
} // namespace
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--sha-length") {
        const auto length=std::strtoul(argv[2],nullptr,10);
        if(length>kMaxModuleBytes) return 2;
        std::vector<std::uint8_t> data(length);
        for(std::size_t i=0;i<length;++i) data[i]=static_cast<std::uint8_t>((i*17+31)&255);
        std::cout<<hex(sha(data.data(),data.size(),7))<<'\n'; return 0;
    }
    if(argc!=3) { std::cerr<<"Usage: test_import_module_identity profile.bin private-client.so\n"; return 2; }
    sha_vectors(); mapped_checks(argv[1],argv[2]);
    std::cout<<"import module identity: "<<checks<<" checks passed; owned offline mapping only\n";
}
