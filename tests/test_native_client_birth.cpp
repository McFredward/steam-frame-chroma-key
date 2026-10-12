#include "import/native_client_birth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace frame_native_client_birth;
namespace {
unsigned checks=0;
void check_at(bool ok,unsigned line) { ++checks; if(!ok) { std::fprintf(stderr,"native birth check %u failed at%u\n",checks,line);std::abort(); } }
#define check(ok) check_at((ok),__LINE__)
constexpr std::uintptr_t bias=0x500000,sp=0x100000;
struct Fixture {
    frame_import_native::Entry entry{};
    std::array<std::uint8_t,kPrefixBytes> bytes{};
    CodeRegion code{bias,bias+0x90000,bias+0x480000};
    void put(unsigned at,std::uint64_t value) { std::memcpy(bytes.data()+at,&value,8); }
    Fixture() {
        entry.sp=entry.x[29]=sp;entry.x[30]=bias+kInitReturn;
        entry.x[0]=entry.x[19]=bias+kPipeObject;entry.x[1]=entry.x[0]+48;
        put(0,sp+32);put(8,bias+kHmdReturn);put(16,bias+kHmdObject);
        put(32,sp+416);put(40,bias+kGlobalReturn);
    }
    Facts run() { return Reader::decode(entry,bytes,code); }
};
}
int main() {
    { Fixture f; const auto result=f.run();check(result.valid());check(Reader::candidate(f.entry));
      check(result.pipe==bias+kPipeObject && result.frame==sp && result.hmd_frame==sp+32 && result.global_frame==sp+416);
      f.put(24,UINT64_MAX);check(f.run().valid()); }
    for(unsigned at:{0U,8U,16U,32U,40U}) { Fixture f;f.put(at,1);check(!f.run().valid()); }
    for(unsigned reg:{0U,1U,19U,29U,30U}) { Fixture f; ++f.entry.x[reg];check(!f.run().valid()); }
    { Fixture f;f.entry.sp+=16;check(!f.run().valid()); }
    { Fixture f;f.code.bias+=4096;check(!f.run().valid()); }
    { Fixture f;f.code.begin=bias+kHmdReturn+4;check(!f.run().valid()); }
    { Fixture f;f.code.end=bias+kInitReturn;check(!f.run().valid()); }
    { Fixture f;f.entry.x[0]=f.entry.x[19]=bias+kPipeObject+16;f.entry.x[1]=f.entry.x[0]+48;
      check(Reader::candidate(f.entry));check(f.run().refusal==Refusal::Singleton); }
    { Fixture f;f.entry.x[30]=(1ULL<<48)+kInitReturn;check(!Reader::candidate(f.entry)); }
    { Fixture f;f.entry.sp=f.entry.x[29]=std::numeric_limits<std::uintptr_t>::max()-15;check(!f.run().valid()); }
    { Fixture f;f.put(8,bias+0x245018);check(f.run().refusal==Refusal::ParentReturn); }
    std::printf("%u offline native HMD-birth checks passed; no native read or live bootstrap\n",checks);
}
