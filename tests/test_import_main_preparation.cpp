#if defined(IMPORT_MAIN_TEST_DSO)
extern "C" __attribute__((visibility("default"))) int main_preparation_test_anchor(int value) { return value+791; }
#else
#include "../import/import_main_preparation.h"
#include "../receiver/sha256.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <sys/mman.h>

namespace {
using namespace frame_import_main;
std::size_t checks=0;
void expect(bool yes,const char* label) { ++checks;if(!yes){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);} }
ProfileBlob load(const std::string& path) {
    ProfileBlob b{};std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),b.size());
    expect(bool(f),"owned descriptor file available");return b;
}
Digest identity(const ProfileBlob& blob) { Digest d{};std::memcpy(d.data(),blob.data()+2000,32);return d; }
void put_scalar(ProfileBlob& b,std::size_t offset,std::uint64_t value) {
    for(unsigned i=0;i<8;++i)b[offset+i]=static_cast<std::uint8_t>(value>>(8*i));
}
void rehash(ProfileBlob& b) {
    frame_receiver::Sha256 h;h.update(b.data(),2000);const auto d=h.finish();std::memcpy(b.data()+2000,d.data(),32);
}
void change_mapped_byte(std::uintptr_t address,const Profile& p,const Digest& reviewed,std::uintptr_t anchor,
                        Refusal expected,const char* label) {
    const auto pagesize=static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto page=address&~(pagesize-1);
    // An intentional out-of-contract text/header mutation is a negative control,
    // performed by the fixture owner outside the protected reader callback.
    expect(mprotect(reinterpret_cast<void*>(page),pagesize,PROT_READ|PROT_WRITE|PROT_EXEC)==0,"fixture page temporarily writable");
    auto* byte=reinterpret_cast<unsigned char*>(address);const auto original=*byte;*byte^=1;
    const auto proof=verify_loaded(p,reviewed,anchor);
    *byte=original;
    expect(mprotect(reinterpret_cast<void*>(page),pagesize,address==anchor ? PROT_READ|PROT_EXEC : PROT_READ)==0,"fixture permissions restored");
    expect(proof.refusal==expected&&!proof.valid(),label);
}
}
int main(int argc,char** argv) {
    if(argc!=3 && argc!=4) {std::cerr<<"Usage: main-preparation fixture.so fixture-profile.bin [private-native-profile-dir]\n";return 2;}
    static_assert(!std::is_default_constructible<Prepared>::value,"Prepared must only be produced by the real preparer");
    const auto blob=load(argv[2]);const auto review=identity(blob);
    Profile p{};expect(decode_profile(blob,review,p),"independently reviewed host fixture descriptor decoded");
    expect(p.role==6,"host fixture scope remains explicit");
    expect(!decode_profile(blob,{},p),"self-stored fixture digest cannot authorize its descriptor");
    for(std::size_t i=0;i<blob.size();++i) {auto bad=blob;bad[i]^=1;Profile out{};
        expect(!decode_profile(bad,review,out),"every descriptor byte mutation fails independent identity");}
    auto forged=blob;put_scalar(forged,8,1);put_scalar(forged,16,183);
    const unsigned char labelled_file[]{0x31,0xad,0xde,0xf0,0xc9,0x55,0x3d,0x37,0x70,0xb5,0x57,0x04,0x87,0x69,0x9e,0x96,
        0xd7,0x02,0x71,0xd4,0x07,0x3c,0xd6,0xc0,0x3b,0x08,0x0f,0xde,0xe6,0xae,0x21,0x53};
    std::memcpy(forged.data()+176,labelled_file,32);rehash(forged);Profile false_native{};
    expect(!decode_profile(forged,identity(forged),false_native),"changing both expected hashes while preserving native file label cannot replace canonical native profile");
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);expect(handle!=nullptr,"actual host DSO loaded by fixture owner");
    auto anchor=reinterpret_cast<std::uintptr_t>(dlsym(handle,"main_preparation_test_anchor"));expect(anchor!=0,"actual anchor resolved");
    Dl_info info{};expect(dladdr(reinterpret_cast<const void*>(anchor),&info)!=0,"actual original public module result");
    errno=ERANGE;const auto proof=verify_loaded(p,review,anchor);
    expect(proof.valid()&&proof.image.role==Role::Fixture,"actual mapped header/PHDR/build-ID/RX authenticated under loader lease");
    expect(errno==ERANGE,"cold verification leaves errno unchanged");
    expect(proof.image.bias==reinterpret_cast<std::uintptr_t>(info.dli_fbase)&&proof.image.rx_begin<=anchor&&anchor<proof.image.rx_end&&
        proof.image.mapped_code_identity==p.code_digest&&proof.image.profile_identity==review,"actual geometry and code digest are copied, not guessed");
    expect(verify_loaded(p,review,0).refusal==Refusal::Lookup,"missing source anchor does not authorize a read");
    expect(!verify_loaded(p,review,reinterpret_cast<std::uintptr_t>(&::dl_iterate_phdr)).valid(),"real foreign provider cannot satisfy fixture identity");
    auto changed=p;changed.code_digest[0]^=1;
    expect(verify_loaded(changed,review,anchor).refusal==Refusal::InvalidProfile,"changed expected code digest refused before loaded proof");
    change_mapped_byte(anchor,p,review,anchor,Refusal::MappedIdentity,"changed actual loaded executable byte rejected");
    const auto base=reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    change_mapped_byte(base,p,review,anchor,Refusal::MappedIdentity,"changed actual mapped ELF header rejected");
    change_mapped_byte(base+p.notes[0].rva+12,p,review,anchor,Refusal::MappedIdentity,"changed actual mapped build note rejected");
    expect(verify_loaded(p,review,anchor).valid(),"restored owned fixture matches again");
    Profiles invalid{};const auto refused=prepare(invalid);
    expect(!refused.ready()&&refused.refusal()==Refusal::InvalidProfile&&!refused.lookup()&&!refused.iterate()&&
           !refused.native_init()&&!refused.native_shutdown(),"unprepared/fixture input never yields provider capability");
    for(auto& item:invalid.modules)item=p;
    invalid.reviewed_observer_identity=review;
    expect(prepare(invalid).refusal()==Refusal::InvalidProfile,"fixture proofs cannot be relabelled into Ready preparation");
    expect(dlclose(handle)==0,"fixture releases its own DSO handle");
    expect(verify_loaded(p,review,anchor).refusal==Refusal::Lookup,"unloaded old anchor never becomes mapped proof");
    if(argc==4) {
        Profiles native{};
        for(std::size_t i=0;i<kModules;++i) {const auto b=load(std::string(argv[3])+"/module-"+std::to_string(i+1)+".bin");
            const auto d=identity(b);expect(decode_profile(b,i==4 ? d : Digest{},native.modules[i]),"private actual native descriptor passes canonical pins");
            if(i==4)native.reviewed_observer_identity=d;
            if(i<4)for(auto pos:{80u,144u,176u,272u,1808u}) {auto delta=b;delta[pos]^=1;rehash(delta);Profile tamper{};
                expect(!decode_profile(delta,identity(delta),tamper),"rehashing an altered native descriptor does not replace its hard pin");}
        }
        const auto wrong=prepare(native);
        expect(!wrong.ready()&&wrong.refusal()==Refusal::WrongExecutable&&!wrong.lookup()&&!wrong.iterate()&&
               !wrong.native_init()&&!wrong.native_shutdown(),"complete native metadata does not authorize an unrelated host executable");
        expect(dlsym(RTLD_DEFAULT,"VR_InitInternal2")==nullptr&&dlsym(RTLD_DEFAULT,"VRClientCoreFactory")==nullptr,
               "failed preflight neither forces API/client load nor exports their names");
    }
    std::cout<<"main preparation: "<<checks<<" checks passed; host proofs do not enable Frame preparation\n";
}
#endif
