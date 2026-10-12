#include "import/client_resource_sparse.h"
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
namespace {
struct OwnedRead { StackBytes bytes; std::size_t calls=0, copied=0; bool fail=false; };
bool owned_read(std::uintptr_t at,void* dest,std::size_t n,void* context) noexcept {
    auto& owned=*static_cast<OwnedRead*>(context);++owned.calls;
    if(owned.fail || !owned.bytes.data || at<owned.bytes.base ||
       at-owned.bytes.base>owned.bytes.size || n>owned.bytes.size-(at-owned.bytes.base))return false;
    std::memcpy(dest,owned.bytes.data+at-owned.bytes.base,n);owned.copied+=n;return true;
}
Scalars sparse_decode(Boundary b,const Entry& e,StackBytes bytes,std::uintptr_t client,std::uintptr_t receiver,
                      const Expected& expected={}) {
    const auto full=frame_client_native::decode(b,e,bytes,client,receiver,expected);
    if(b==Boundary::AcceptedInit)return full; // Outside this new raw scope.
    OwnedRead read{bytes};const auto got=frame_client_sparse::decode(b,e,client,receiver,expected,owned_read,&read);
    check(got.bytes_copied==read.copied);check(got.valid()==full.valid());
    if(got.valid()) {
        const auto& a=got.scalars;const auto& f=full;
        check(a.invocation.import_frame==f.invocation.import_frame && a.invocation.receiver_frame==f.invocation.receiver_frame &&
              a.invocation.pipe==f.invocation.pipe && a.invocation.record==f.invocation.record);
        check(a.native_sequence==f.native_sequence && a.sequence_available==f.sequence_available);
        check(a.buffer_base==f.buffer_base && a.offset==f.offset && a.total==f.total && a.remaining==f.remaining && a.fd==f.fd);
        check(a.token==f.token && a.token_present==f.token_present && a.native_wait_success==f.native_wait_success);
        constexpr std::size_t budgets[]{32,88,76,80,44,40};check(got.bytes_copied==budgets[static_cast<unsigned>(b)]);
    }
    return got.scalars;
}
}

int main(){
    for(auto b:{Boundary::Begin,Boundary::Pending}){Fixture f;auto e=direct(f,b);const auto v=sparse_decode(b,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&!v.sequence_available&&v.invocation.import_frame==kImport&&v.invocation.receiver_frame==kCaller&&v.invocation.record==kRecord);
    if(b==Boundary::Pending){check(v.native_wait_success&&v.token_present&&v.token==kToken);e.x[24]=0;check(sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    e.x[20]++;check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    for(unsigned total=13;total<=512;++total)for(unsigned offset=0;offset<total;++offset){Fixture f;auto e=writer(f,total,offset);auto v=sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&v.offset==offset&&v.total==total&&v.remaining==total-offset&&v.native_sequence==kSequence&&v.fd==7);
    check(v.invocation.import_frame==kImport&&v.buffer_base==e.x[29]+80);e.x[1]++;check(!sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    for(auto b:{Boundary::HeaderRead,Boundary::PayloadRead})for(unsigned offset=0;offset<(b==Boundary::HeaderRead?12U:9U);++offset){Fixture f;auto e=reader(f,b,offset);auto v=sparse_decode(b,e,f.stack(),kClient,kReceiver,expected());
    check(v.valid()&&v.offset==offset&&v.remaining==v.total-offset&&v.invocation.import_frame==kImport&&v.sequence_available&&v.native_sequence==kSequence);
    auto unknown=expected();unknown.sequence_available=false;check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,unknown).valid());
    e.x[2]++;check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=ref(f);auto v=sparse_decode(Boundary::Reference,e,f.stack(),kClient,kReceiver,expected());check(v.valid()&&v.token==kToken&&!v.sequence_available);
    e.x[24]++;check(!sparse_decode(Boundary::Reference,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;const auto fp=kBase;f.link(fp,fp+32,kClient,0x24500c);auto e=entry(Boundary::AcceptedInit,fp);e.x[0]=e.x[19]=e.x[23]=kPipe;e.x[1]=kPipe+48;e.x[20]=7;
    auto v=sparse_decode(Boundary::AcceptedInit,e,f.stack(),kClient,kReceiver);check(v.valid()&&v.accepted_fd_pending&&v.fd==7&&!v.sequence_available&&!v.invocation.import_frame);
    e.x[20]=UINT64_MAX;check(!sparse_decode(Boundary::AcceptedInit,e,f.stack(),kClient,kReceiver).valid());}
    // Break each source-pinned live chain or scalar while retaining valid bytes
    // everywhere else. A generic stack walk or guessed parent cannot pass.
    for(auto b:{Boundary::Begin,Boundary::Write,Boundary::HeaderRead,Boundary::PayloadRead,Boundary::Pending,Boundary::Reference}){
        Fixture f;Entry e=b==Boundary::Begin||b==Boundary::Pending?direct(f,b):b==Boundary::Write?writer(f,333,0):b==Boundary::Reference?ref(f):reader(f,b,0);
        check(sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());
        auto changed=expected();changed.invocation.record+=8;if(b!=Boundary::Begin)check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,changed).valid());
        changed=expected();changed.invocation.pipe+=16;if(b!=Boundary::Begin)check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,changed).valid());
        auto saved=e.x[30];e.x[30]+=4;check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());e.x[30]=saved;
        const auto at=static_cast<std::uintptr_t>(e.x[29]);std::uint64_t old=0;std::memcpy(&old,f.bytes.data()+at-kBase,8);
        f.put(at,old+16);check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());f.put(at,old);
        std::memcpy(&old,f.bytes.data()+at+8-kBase,8);f.put(at+8,old+4);check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());f.put(at+8,old);
        auto truncated=f.stack();truncated.size=64;check(!sparse_decode(b,e,truncated,kClient,kReceiver,expected()).valid());
        truncated=f.stack();truncated.data=nullptr;check(!sparse_decode(b,e,truncated,kClient,kReceiver,expected()).valid());
        e.sp-=16;check(!sparse_decode(b,e,f.stack(),kClient,kReceiver,expected()).valid());
    }
    {Fixture f;auto e=writer(f,333,0);const auto send=kImport-80;f.put(send+8,std::uint64_t{kClient+0x36241c});check(!sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=writer(f,333,0);auto changed=expected();changed.sequence=0;check(!sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,changed).valid());
    changed.sequence_available=false;check(sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,changed).valid());}
    {Fixture f;auto e=writer(f,333,0);e.x[20]=513;check(!sparse_decode(Boundary::Write,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=reader(f,Boundary::PayloadRead,0);f.put(kImport-192+156,std::uint32_t{10});check(!sparse_decode(Boundary::PayloadRead,e,f.stack(),kClient,kReceiver,expected()).valid());}
    {Fixture f;auto e=direct(f,Boundary::Begin);auto bad=f.stack();bad.base=UINTPTR_MAX-15;check(!sparse_decode(Boundary::Begin,e,bad,kClient,kReceiver).valid());}
    for(auto b:{Boundary::Begin,Boundary::Write,Boundary::HeaderRead,Boundary::PayloadRead,Boundary::Pending,Boundary::Reference}) {
        Fixture f;Entry e=b==Boundary::Begin||b==Boundary::Pending?direct(f,b):b==Boundary::Write?writer(f,512,0):b==Boundary::Reference?ref(f):reader(f,b,0);
        OwnedRead read{f.stack()};auto unrelated=e;unrelated.x[30]+=4;
        auto bad=frame_client_sparse::decode(b,unrelated,kClient,kReceiver,expected(),owned_read,&read);
        check(!bad.valid() && !read.calls && !bad.bytes_copied);
        bad=frame_client_sparse::decode(b,e,kClient,kReceiver,expected(),nullptr,&read);check(!bad.valid() && !read.calls);
        read.fail=true;bad=frame_client_sparse::decode(b,e,kClient,kReceiver,expected(),owned_read,&read);
        check(!bad.valid() && read.calls==1 && !bad.bytes_copied);
        read.calls=0;bad=frame_client_sparse::decode(Boundary::AcceptedInit,e,kClient,kReceiver,expected(),owned_read,&read);
        check(!bad.valid() && !read.calls && !bad.bytes_copied);
    }
    std::printf("%u sparse/full native decoder equivalence checks passed; fixed owned scalar slots, no runtime permission\n",checks);
}
