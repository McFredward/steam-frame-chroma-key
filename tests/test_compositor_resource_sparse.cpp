#include "import/compositor_resource_sparse.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
using namespace frame_import_native;
using Result=frame_compositor_sparse::Result;
unsigned checks=0;
void check(bool ok) {
    ++checks;
    if(!ok) {std::fprintf(stderr,"compositor sparse check %u failed\n",checks);std::abort();}
}
constexpr std::uintptr_t base=0x100000,bias=0x500000,pipe=0x700000,dispatch=base+1232;
constexpr std::uint32_t sequence=0xffffffff;
struct Fixture {
    std::array<std::uint8_t,1856> bytes{};
    CreationRoute selected=CreationRoute::Vulkan;
    std::uintptr_t dispatch=::dispatch;
    explicit Fixture(CreationRoute route=CreationRoute::Vulkan):selected(route),
        dispatch(base+creation_dispatch_offset(route)){}
    unsigned reads=0,fail_at=0;
    std::size_t requested=0,confirmed=0;
    template<class T> void put(std::uintptr_t at,T value) {
        check(at>=base && at-base<=bytes.size()-sizeof(T));
        std::memcpy(bytes.data()+at-base,&value,sizeof(T));
    }
    void link(std::uintptr_t frame,std::uintptr_t parent,std::uint64_t lr) {
        put(frame,std::uint64_t{parent});put(frame+8,std::uint64_t{bias+lr});
    }
    Expected expected() const {return {dispatch,pipe,sequence};}
    Entry entry(Boundary boundary,unsigned offset=0) {
        bytes.fill(0xa5);put(dispatch+108,sequence);
        constexpr std::uintptr_t sites[]{0x153fc0,0x1540b0,0x2c75ac,0x2c7684,
                                       0x2c7804,0x172ea0,0x172c00,0x172fbc};
        Entry e{};e.x[30]=bias+sites[static_cast<unsigned>(boundary)];
        if(boundary==Boundary::Begin || boundary==Boundary::EarlyUnlock) {
            e.sp=e.x[29]=dispatch;e.x[0]=e.x[19]=pipe;e.x[25]=1;
            put(dispatch+128,std::uint64_t{pipe});
        } else if(boundary==Boundary::Image || boundary==Boundary::Bind || boundary==Boundary::View) {
            e.sp=base;
            if(selected==CreationRoute::Facet) {
                constexpr std::uintptr_t pcs[]{0x3076a0,0x307874,0x30735c};
                e.x[30]=bias+pcs[static_cast<unsigned>(boundary)-2];e.x[29]=base;
                link(base,base+688,0x2ab100);link(base+688,base+1056,0x70bc8);
                link(base+1056,base+1216,0x13bbdc);link(base+1216,base+1312,0x13beb0);
                link(base+1312,dispatch,0x154194);
                put(base+744,std::uint32_t{0});put(base+1328,std::uint64_t{pipe});
            } else {
                e.x[29]=base+96;
                link(base+96,base+752,0x70bc8);link(base+752,base+912,0x13bbdc);
                link(base+912,base+1008,0x13beb0);link(base+1008,dispatch,0x154194);
                put(base+152,std::uint32_t{0});put(base+1024,std::uint64_t{pipe});
            }
        } else {
            const auto send=dispatch-80;e.sp=e.x[29]=send;
            link(send,dispatch,0x1542b0);put(send+64,std::uint64_t{pipe});
            if(boundary==Boundary::SendLock) {
                e.x[0]=e.x[19]=pipe;e.x[20]=9;e.x[21]=0x235;e.x[22]=sequence;
            } else if(boundary==Boundary::Write) {
                e.sp=e.x[29]=send-112;link(send-112,send,0x172f34);
                e.x[0]=7;e.x[19]=offset;e.x[20]=21;e.x[22]=pipe;e.x[23]=send-32;
                e.x[1]=send-32+offset;e.x[2]=21-offset;
            } else {e.sp=send-32;e.x[0]=pipe;e.x[20]=1;}
        }
        return e;
    }
    static bool reader(std::uintptr_t at,void* out,std::size_t count,void* owner) noexcept {
        auto& f=*static_cast<Fixture*>(owner);
        ++f.reads;f.requested+=count;
        if(f.fail_at==f.reads)return false;
        if(at<base || at-base>f.bytes.size() || count>f.bytes.size()-(at-base))return false;
        std::memcpy(out,f.bytes.data()+at-base,count);f.confirmed+=count;return true;
    }
    void reset() {reads=0;fail_at=0;requested=confirmed=0;}
    Result sparse(Boundary b,const Entry& e,const Expected& ex) {
        return frame_compositor_sparse::decode(b,e,bias,ex,reader,this);
    }
    Scalars full(Boundary b,const Entry& e,const Expected& ex) {
        return frame_import_native::decode(b,e,{bytes.data(),base,selected==CreationRoute::Facet?bytes.size():1552},bias,ex);
    }
};
void equivalent(const Scalars& a,const Scalars& b) {
    check(a.refusal==b.refusal);
    check(a.invocation.dispatch_frame==b.invocation.dispatch_frame &&
          a.invocation.pipe==b.invocation.pipe && a.invocation.sequence==b.invocation.sequence);
    check(a.creation_route==b.creation_route && a.client==b.client && a.client_available==b.client_available &&
          a.native_payload_read==b.native_payload_read && a.native_send_success==b.native_send_success);
    check(a.packet_base==b.packet_base && a.packet_offset==b.packet_offset &&
          a.remaining==b.remaining && a.fd==b.fd);
}
}

int main() {
    for (auto b : {Boundary::Image,Boundary::Bind,Boundary::View}) {
        Fixture f(CreationRoute::Facet);const auto e=f.entry(b);const auto ex=f.expected();
        const auto good=f.sparse(b,e,ex);check(good.valid());equivalent(good.scalars,f.full(b,e,ex));
        check(good.bytes_requested==96 && good.bytes_confirmed==96 && f.reads==8);
        check(frame_compositor_sparse::scalar_budget(b,CreationRoute::Facet)==96);
        const auto calls=f.reads;
        for (unsigned fail=1;fail<=calls;++fail) {
            f.reset();f.fail_at=fail;const auto bad=f.sparse(b,e,ex);
            check(!bad.valid() && bad.refusal==frame_compositor_sparse::Refusal::Reader);
            check(f.reads==fail && bad.bytes_requested==f.requested && bad.bytes_confirmed==f.confirmed);
            check(bad.bytes_requested<=96 && bad.bytes_confirmed<bad.bytes_requested);
        }
        // All copied dependencies and all unreferenced holes are checked.
        for (std::size_t at=0;at<f.bytes.size();++at) {
            f.bytes[at]^=0xff;f.reset();const auto s=f.sparse(b,e,ex);
            equivalent(s.scalars,f.full(b,e,ex));check(s.bytes_requested==96);
            f.bytes[at]^=0xff;
        }
        for (auto reg : {29U,30U}) {auto bad=e;bad.x[reg]+=4;f.reset();check(!f.sparse(b,bad,ex).valid() && !f.reads);}
        auto old=e;old.x[29]+=96;old.x[30]=bias+(b==Boundary::Image?0x2c75ac:b==Boundary::Bind?0x2c7684:0x2c7804);
        f.reset();check(!f.sparse(b,old,ex).valid());
        check(frame_compositor_sparse::scalar_budget(b,CreationRoute::None)==0);
    }
    for(unsigned index=0;index<8;++index) {
        const auto b=static_cast<Boundary>(index);Fixture f;auto e=f.entry(b);const auto ex=f.expected();
        const auto good=f.sparse(b,e,ex);check(good.valid());equivalent(good.scalars,f.full(b,e,ex));
        const auto budget=frame_compositor_sparse::scalar_budget(b);
        check(good.bytes_requested==budget && good.bytes_confirmed==budget);
        check(f.requested==budget && f.confirmed==budget);const auto calls=f.reads;
        // Every reader failure is bounded, and charges the attempted span.
        for(unsigned failed=1;failed<=calls;++failed) {
            f.reset();f.fail_at=failed;const auto r=f.sparse(b,e,ex);
            check(!r.valid() && r.refusal==frame_compositor_sparse::Refusal::Reader);
            check(f.reads==failed && r.bytes_requested==f.requested && r.bytes_confirmed==f.confirmed);
            check(r.bytes_requested<=budget && r.bytes_confirmed<r.bytes_requested);
        }
        // Flip every byte, including all holes, and compare to the established
        // full-window native decoder. Fixed spans must contain every dependency.
        for(std::size_t at=0;at<f.bytes.size();++at) {
            f.bytes[at]^=0xff;f.reset();const auto s=f.sparse(b,e,ex);
            equivalent(s.scalars,f.full(b,e,ex));check(s.bytes_requested==budget);
            f.bytes[at]^=0xff;
        }
        for(auto field : {0U,1U,19U,20U,21U,22U,23U,25U}) {
            auto mutated=e;mutated.x[field]^=16;f.reset();
            equivalent(f.sparse(b,mutated,ex).scalars,f.full(b,mutated,ex));
        }
        for(auto reg : {29U,30U}) {
            auto bad=e;bad.x[reg]+=4;f.reset();const auto r=f.sparse(b,bad,ex);
            check(!r.valid() && !f.reads && !r.bytes_requested);
        }
        auto bad=e;bad.sp+=8;f.reset();check(!f.sparse(b,bad,ex).valid() && !f.reads);
        bad=e;bad.sp=std::numeric_limits<std::uintptr_t>::max()-15;bad.x[29]=bad.sp;
        f.reset();check(!f.sparse(b,bad,ex).valid() && !f.reads);
        f.reset();check(!frame_compositor_sparse::decode(b,e,bias+1,ex,Fixture::reader,&f).valid() && !f.reads);
        check(!frame_compositor_sparse::decode(b,e,bias,ex,nullptr,&f).valid());
        check(!frame_compositor_sparse::decode(b,e,bias,ex,Fixture::reader,nullptr).valid());
        // Mismatched native invocation is refused by the reused source decoder.
        if(b!=Boundary::Begin) {
            auto wrong=ex;++wrong.sequence;f.reset();equivalent(f.sparse(b,e,wrong).scalars,f.full(b,e,wrong));
            check(!f.sparse(b,e,wrong).valid());
        }
    }
    for(unsigned offset=0;offset<21;++offset) {
        Fixture f;const auto e=f.entry(Boundary::Write,offset);const auto r=f.sparse(Boundary::Write,e,f.expected());
        check(r.valid() && r.scalars.packet_offset==offset && r.scalars.remaining==21-offset && r.scalars.fd==7);
    }
    for(auto b : {Boundary::AcceptedInit,static_cast<Boundary>(255)}) {
        Fixture f;f.reset();check(!f.sparse(b,Entry{},f.expected()).valid() && !f.reads);
        check(frame_compositor_sparse::scalar_budget(b)==0);
    }
    std::printf("%u compositor sparse/full decoder controls passed; no live memory or namespace ownership proof\n",checks);
}
