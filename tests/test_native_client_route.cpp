#include "import/native_client_route.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace frame_client_native;
namespace {
unsigned checks=0;
void check(bool v){++checks;if(!v){std::fprintf(stderr,"client route check %u failed\n",checks);std::abort();}}
constexpr std::uintptr_t kBase=0x100000,kClient=0x500000,kReceiver=0x900000;
constexpr std::uintptr_t kImport=kBase+864,kCaller=kImport+352,kPipe=0x700000,kRecord=0x800000;
constexpr std::uint32_t kSequence=0xffffffff;
constexpr std::uint64_t kToken=0x8877665544332211ULL;
struct Fixture {
    std::array<std::uint8_t,1552> bytes{};
    template<class T>void put(std::uintptr_t at,T v){check(at>=kBase&&at-kBase<=bytes.size()-sizeof(T));std::memcpy(bytes.data()+at-kBase,&v,sizeof(T));}
    StackBytes stack()const{return{bytes.data(),kBase,bytes.size()};}
    void link(std::uintptr_t fp,std::uintptr_t parent,std::uintptr_t bias,std::uint64_t rva){put(fp,std::uint64_t{parent});put(fp+8,std::uint64_t{bias+rva});}
    Fixture(){link(kImport,kCaller,kReceiver,0x34608);put(kImport+136,std::uint64_t{kPipe});put(kImport+16,std::uint64_t{kRecord});}
};
Expected expected(){return{{kImport,kCaller,kPipe,kRecord},kSequence,true};}
Entry entry(Boundary b,std::uintptr_t fp){Entry e;constexpr std::uint64_t sites[]={0x3623c0,0x2456c8,0x245334,0x245460,0x3624c4,0x362d58,0x244dbc};e.x[30]=kClient+sites[static_cast<unsigned>(b)];e.x[29]=e.sp=fp;return e;}
Entry direct(Fixture& f,Boundary b){auto e=entry(b,kImport);e.x[0]=e.x[21]=kPipe;e.x[20]=kRecord+32;e.x[25]=0x123456780000beef;
    if(b==Boundary::Pending){f.put(kImport+224,std::uint32_t{1});f.put(kImport+232,kToken);e.x[24]=1;e.x[21]=0xaaaa;e.x[25]=0xbbbb;}return e;}
Entry writer(Fixture& f,unsigned total,unsigned offset){const auto rounded=(total+15)&~15U;const auto send=kImport-80;const auto packet=send-rounded;const auto fp=packet-80;
    f.link(fp,send,kClient,0x2459fc);f.link(send,kImport,kClient,0x362418);f.put(fp+32,std::uint32_t{0x234});f.put(fp+40,kSequence);
    f.put(send+64,std::uint64_t{kPipe});f.put(send+24,std::uint64_t{kRecord+32});auto e=entry(Boundary::Write,fp);
    e.x[0]=7;e.x[19]=offset;e.x[20]=total;e.x[21]=total-offset;e.x[22]=kPipe;e.x[23]=packet;e.x[1]=packet+offset;e.x[2]=total-offset;return e;}
Entry reader(Fixture& f,Boundary b,unsigned offset){const auto wait=kImport-192,fp=wait-96;f.link(fp,wait,kClient,b==Boundary::HeaderRead?0x364b58:0x364ba8);
    f.link(wait,kImport,kClient,0x362440);f.put(wait+116,kSequence);f.put(fp+16,std::uint64_t{kPipe});auto e=entry(b,fp);e.x[0]=7;e.x[19]=offset;e.x[20]=kPipe;
    if(b==Boundary::HeaderRead){e.x[24]=12;e.x[25]=fp+80;e.x[1]=fp+80+offset;e.x[2]=12-offset;}
    else{f.put(wait+156,std::uint32_t{9});e.x[21]=e.x[23]=9;e.x[22]=0xa00000;e.x[1]=e.x[22]+offset;e.x[2]=9-offset;}return e;}
Entry ref(Fixture& f){const auto fp=kCaller-240;f.link(fp,kCaller,kReceiver,0x3462c);f.put(fp+104,std::uint64_t{kPipe});f.put(fp+16,std::uint64_t{kRecord});f.put(fp+224,kToken);
    auto e=entry(Boundary::Reference,fp);e.x[0]=e.x[19]=kPipe;e.x[24]=kToken;return e;}
}
int main(){
    for(auto b:{Boundary::Begin,Boundary::Pending}){Fixture f;auto e=direct(f,b);const auto v=decode(b,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&!v.sequence_available&&v.invocation.import_frame==kImport&&v.invocation.receiver_frame==kCaller&&v.invocation.record==kRecord);
    if(b==Boundary::Pending){check(v.native_wait_success&&v.token_present&&v.token==kToken);e.x[24]=0;check(decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    e.x[20]++;check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    for(unsigned total:{13U,21U,255U,333U,512U})for(unsigned offset=0;offset<total;++offset){Fixture f;auto e=writer(f,total,offset);auto v=decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&v.offset==offset&&v.total==total&&v.remaining==total-offset&&v.native_sequence==kSequence&&v.fd==7);
    check(v.invocation.import_frame==kImport&&v.buffer_base==e.x[29]+80);e.x[1]++;check(!decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    for(auto b:{Boundary::HeaderRead,Boundary::PayloadRead})for(unsigned offset=0;offset<(b==Boundary::HeaderRead?12U:9U);++offset){Fixture f;auto e=reader(f,b,offset);auto v=decode(b,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&v.offset==offset&&v.remaining==v.total-offset&&v.invocation.import_frame==kImport&&v.sequence_available&&v.native_sequence==kSequence);
    auto unknown=expected();unknown.sequence_available=false;check(!decode(b,e,f.stack(),kClient,kReceiver,unknown).valid());
    e.x[2]++;check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=ref(f);auto v=decode(Boundary::Reference,e,f.stack(),kClient,kReceiver,expected());check(v.valid()&&v.token==kToken&&!v.sequence_available);
    e.x[24]++;check(!decode(Boundary::Reference,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;const auto fp=kBase;f.link(fp,fp+32,kClient,0x24500c);auto e=entry(Boundary::AcceptedInit,fp);e.x[0]=e.x[19]=e.x[23]=kPipe;e.x[1]=kPipe+48;e.x[20]=7;
    auto v=decode(Boundary::AcceptedInit,e,f.stack(),kClient,kReceiver);check(v.valid()&&v.accepted_fd_pending&&v.fd==7&&!v.sequence_available&&!v.invocation.import_frame);
    e.x[20]=UINT64_MAX;check(!decode(Boundary::AcceptedInit,e,f.stack(),kClient,kReceiver).valid());}
    // Break each source-pinned live chain or scalar while retaining valid bytes
    // everywhere else. A generic stack walk or guessed parent cannot pass.
    for(auto b:{Boundary::Begin,Boundary::Write,Boundary::HeaderRead,Boundary::PayloadRead,Boundary::Pending,Boundary::Reference}){
        Fixture f;Entry e=b==Boundary::Begin||b==Boundary::Pending?direct(f,b):b==Boundary::Write?writer(f,333,0):b==Boundary::Reference?ref(f):reader(f,b,0);
        check(decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());
        auto changed=expected();changed.invocation.record+=8;if(b!=Boundary::Begin)check(!decode(b,e,f.stack(),kClient,kReceiver,changed).valid());
        changed=expected();changed.invocation.pipe+=16;if(b!=Boundary::Begin)check(!decode(b,e,f.stack(),kClient,kReceiver,changed).valid());
        auto saved=e.x[30];e.x[30]+=4;check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());e.x[30]=saved;
        const auto at=static_cast<std::uintptr_t>(e.x[29]);std::uint64_t old=0;std::memcpy(&old,f.bytes.data()+at-kBase,8);
        f.put(at,old+16);check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());f.put(at,old);
        std::memcpy(&old,f.bytes.data()+at+8-kBase,8);f.put(at+8,old+4);check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());f.put(at+8,old);
        auto truncated=f.stack();truncated.size=64;check(!decode(b,e,truncated,kClient,kReceiver,expected()).valid());
        truncated=f.stack();truncated.data=nullptr;check(!decode(b,e,truncated,kClient,kReceiver,expected()).valid());
        e.sp-=16;check(!decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());
    }
    {Fixture f;auto e=writer(f,333,0);const auto send=kImport-80;f.put(send+8,std::uint64_t{kClient+0x36241c});check(!decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=writer(f,333,0);auto changed=expected();changed.sequence=0;check(!decode(Boundary::Write,e,f.stack(),kClient,kReceiver,changed).valid());
    changed.sequence_available=false;check(decode(Boundary::Write,e,f.stack(),kClient,kReceiver,changed).valid());}
    {Fixture f;auto e=writer(f,333,0);e.x[20]=513;check(!decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=reader(f,Boundary::PayloadRead,0);f.put(kImport-192+156,std::uint32_t{10});check(!decode(Boundary::PayloadRead,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=direct(f,Boundary::Begin);auto bad=f.stack();bad.size=1553;check(!decode(Boundary::Begin,e,bad,kClient,kReceiver).valid());
    bad=f.stack();bad.base=UINTPTR_MAX-15;check(!decode(Boundary::Begin,e,bad,kClient,kReceiver).valid());}
    std::printf("%u native client route checks passed; fixed owned windows only, no runtime ownership claim\n",checks);
}
