#include "import/native_import_route.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace frame_import_native;
namespace {
unsigned checks = 0;
void require(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "route check %u failed\n", checks); std::abort(); } }
constexpr std::uintptr_t kBase = 0x100000, kBias = 0x500000;
constexpr std::uintptr_t kPipe = 0x700000, kDispatch = kBase + 1232;
constexpr std::uint32_t kSequence = 0xffffffff;
struct Fixture {
    std::array<std::uint8_t, 1552> bytes{};
    template<class T> void put(std::uintptr_t at, T value) {
        require(at >= kBase && at-kBase <= bytes.size()-sizeof(T));
        std::memcpy(bytes.data() + at-kBase, &value, sizeof(T));
    }
    StackBytes stack() const { return {bytes.data(), kBase, bytes.size()}; }
    void link(std::uintptr_t frame, std::uintptr_t parent, std::uint64_t lr) {
        put(frame, std::uint64_t{parent}); put(frame+8, std::uint64_t{kBias+lr});
    }
    Fixture() { put(kDispatch+108, kSequence); }
};
Expected expected() { return {kDispatch, kPipe, kSequence}; }
Entry entry(Boundary stage) {
    Entry e{};
    constexpr std::uint64_t sites[] = {0x153fc0,0x1540b0,0x2c75ac,0x2c7684,0x2c7804,0x172ea0,0x172c00,0x172fbc,0x1722f4};
    e.x[30] = kBias + sites[static_cast<unsigned>(stage)];
    return e;
}
Entry creation(Fixture& f, Boundary stage) {
    auto e = entry(stage); e.sp = kBase; e.x[29] = kBase+96;
    f.link(kBase+96, kBase+752, 0x70bc8);
    f.link(kBase+752, kBase+912, 0x13bbdc);
    f.link(kBase+912, kBase+1008, 0x13beb0);
    f.link(kBase+1008, kDispatch, 0x154194);
    f.put(kBase+152, std::uint64_t{0x1234567800000000}); // Client ID zero is real if available.
    f.put(kBase+1024, std::uint64_t{kPipe});
    return e;
}
Entry sender(Fixture& f, Boundary stage) {
    constexpr auto send = kDispatch-80;
    auto e = entry(stage); e.x[29] = send; e.sp = send;
    f.link(send, kDispatch, 0x1542b0); f.put(send+64, std::uint64_t{kPipe});
    if (stage == Boundary::SendLock) {
        e.x[0]=e.x[19]=kPipe; e.x[20]=9; e.x[21]=0x235; e.x[22]=kSequence;
    } else if (stage == Boundary::Write) {
        const auto writer=send-112;
        e.x[29]=e.sp=writer; f.link(writer, send, 0x172f34);
        e.x[0]=7; e.x[20]=21; e.x[22]=kPipe; e.x[23]=send-32;
        e.x[1]=send-32; e.x[2]=21;
    } else {
        e.sp=send-32; e.x[0]=kPipe; e.x[20]=1;
        f.put(send+24, std::uint64_t{777}); // Saved x20 must not become live outcome.
    }
    return e;
}
}

int main() {
    {
        Fixture f; auto e=entry(Boundary::AcceptedInit);
        e.sp=e.x[29]=kBase;
        e.x[0]=e.x[19]=e.x[23]=kPipe; e.x[1]=kPipe+48; e.x[20]=7;
        f.link(kBase,kBase+32,0x172544);
        auto out=decode(Boundary::AcceptedInit,e,f.stack(),kBias);
        require(out.valid() && out.invocation.pipe==kPipe && out.fd==7);
        require(!out.client_available && !out.invocation.dispatch_frame && !out.native_payload_read);
        for (auto reg : {0U,1U,19U,23U}) {
            auto saved=e.x[reg];e.x[reg]+=16;
            require(!decode(Boundary::AcceptedInit,e,f.stack(),kBias).valid());e.x[reg]=saved;
        }
        e.x[20]=0;require(decode(Boundary::AcceptedInit,e,f.stack(),kBias).valid());
        e.x[20]=UINT64_MAX;require(!decode(Boundary::AcceptedInit,e,f.stack(),kBias).valid());
        e.x[20]=7;f.put(kBase+8,std::uint64_t{kBias+0x17253c});
        require(!decode(Boundary::AcceptedInit,e,f.stack(),kBias).valid());
    }
    for (auto stage : {Boundary::Begin, Boundary::EarlyUnlock}) {
        Fixture f; auto e=entry(stage); e.sp=e.x[29]=kDispatch;
        e.x[0]=e.x[19]=kPipe; e.x[25]=1; f.put(kDispatch+128,std::uint64_t{kPipe});
        auto out=decode(stage,e,f.stack(),kBias,expected());
        require(out.valid() && out.invocation.dispatch_frame==kDispatch && out.invocation.sequence==kSequence);
        require(!out.client_available);
        require(out.native_payload_read == (stage==Boundary::EarlyUnlock));
        e.x[25]=0;
        require(!decode(stage,e,f.stack(),kBias,expected()).native_payload_read);
        e.x[0]++; require(!decode(stage,e,f.stack(),kBias,expected()).valid()); e.x[0]--;
        e.sp-=16; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
    }
    for (auto stage : {Boundary::Image, Boundary::Bind, Boundary::View}) {
        Fixture f; auto e=creation(f,stage);
        auto out=decode(stage,e,f.stack(),kBias,expected());
        require(out.valid() && out.client_available && out.client==0 && out.invocation.pipe==kPipe);
        const Expected public_prelude{kDispatch,kPipe,0};
        if(stage==Boundary::Image) {
            f.put(kDispatch+128,std::uint64_t{0xdeadbeef});
            const auto selected=decode_selected_image(e,f.stack(),kBias,public_prelude);
            require(selected.valid() && selected.invocation.sequence==kSequence && selected.creation_route==CreationRoute::Vulkan);
            for(unsigned mutation=0;mutation<4;++mutation) {
                auto wrong=public_prelude;
                if(mutation==0)wrong.pipe+=16;else if(mutation==1)wrong.dispatch_frame+=16;
                else if(mutation==2)wrong.sequence=1;else wrong.pipe=0;
                require(!decode_selected_image(e,f.stack(),kBias,wrong).valid());
            }
            f.put(kDispatch+108,std::uint32_t{0});
            require(decode_selected_image(e,f.stack(),kBias,public_prelude).valid());
            require(decode_selected_image(e,f.stack(),kBias,public_prelude).invocation.sequence==0);
            f.put(kDispatch+108,kSequence);
        } else require(!decode_selected_image(e,f.stack(),kBias,public_prelude).valid());
        f.put(kBase+152,std::uint64_t{0x87654321fedcba98});
        require(decode(stage,e,f.stack(),kBias,expected()).client==0xfedcba98);
        f.put(kBase+760,std::uint64_t{kBias+0x13bc8c});
        require(decode(stage,e,f.stack(),kBias,expected()).valid());
        // Independently break every exact parent link and return, then restore.
        for (auto at : {kBase+96,kBase+752,kBase+912,kBase+1008}) {
            std::uint64_t saved=0; std::memcpy(&saved,f.bytes.data()+at-kBase,8);
            f.put(at,saved+16); require(!decode(stage,e,f.stack(),kBias,expected()).valid()); f.put(at,saved);
            std::memcpy(&saved,f.bytes.data()+at+8-kBase,8);
            f.put(at+8,saved+4); require(!decode(stage,e,f.stack(),kBias,expected()).valid()); f.put(at+8,saved);
            if(stage==Boundary::Image) {
                f.put(at+8,saved+4);require(!decode_selected_image(e,f.stack(),kBias,public_prelude).valid());f.put(at+8,saved);
            }
        }
        auto bad=expected(); bad.sequence=0; require(!decode(stage,e,f.stack(),kBias,bad).valid());
        bad=expected(); bad.pipe++; require(!decode(stage,e,f.stack(),kBias,bad).valid());
        bad=expected(); bad.dispatch_frame+=16; require(!decode(stage,e,f.stack(),kBias,bad).valid());
        auto truncated=f.stack(); truncated.size=1233; require(!decode(stage,e,truncated,kBias,expected()).valid());
        e.x[29]+=16; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
    }
    // The source-pinned Facet implementation has a separate allocator frame;
    // its layout and scalar location cannot be inferred from the old route.
    for (auto stage : {Boundary::Image, Boundary::Bind, Boundary::View}) {
        std::array<std::uint8_t,1856> bytes{};
        const auto put = [&](std::size_t at, auto value) { std::memcpy(bytes.data()+at,&value,sizeof(value)); };
        const auto link = [&](std::size_t at, std::size_t parent, std::uintptr_t lr) {
            put(at,std::uint64_t{kBase+parent}); put(at+8,std::uint64_t{kBias+lr});
        };
        link(0,688,0x2ab100); link(688,1056,0x70bc8);
        link(1056,1216,0x13bbdc); link(1216,1312,0x13beb0); link(1312,1536,0x154194);
        put(744,std::uint64_t{0x12345678fedcba98}); put(1328,std::uint64_t{kPipe}); put(1644,kSequence);
        // The allocator's own saved x24 is not the client's identifier.
        put(56,std::uint64_t{37});
        const Expected ex{kBase+1536,kPipe,kSequence};
        Entry e{}; e.sp=e.x[29]=kBase;
        constexpr std::uintptr_t pcs[]{0x3076a0,0x307874,0x30735c};
        e.x[30]=kBias+pcs[static_cast<unsigned>(stage)-2];
        const StackBytes stack{bytes.data(),kBase,bytes.size()};
        const auto run=[&] {return decode(stage,e,stack,kBias,ex);};
        auto r=run();require(r.valid() && r.creation_route==CreationRoute::Facet && r.client_available && r.client==0xfedcba98);
        const Expected public_prelude{kBase+1536,kPipe,0};
        if(stage==Boundary::Image) {
            put(1664,std::uint64_t{0xdeadbeef});
            const auto selected=decode_selected_image(e,stack,kBias,public_prelude);
            require(selected.valid() && selected.invocation.sequence==kSequence && selected.creation_route==CreationRoute::Facet);
            auto wrong=public_prelude;wrong.pipe+=16;require(!decode_selected_image(e,stack,kBias,wrong).valid());
            wrong=public_prelude;wrong.dispatch_frame+=16;require(!decode_selected_image(e,stack,kBias,wrong).valid());
            wrong=public_prelude;wrong.sequence=1;require(!decode_selected_image(e,stack,kBias,wrong).valid());
        } else require(!decode_selected_image(e,stack,kBias,public_prelude).valid());
        require(r.invocation.dispatch_frame==ex.dispatch_frame && r.invocation.pipe==ex.pipe && r.invocation.sequence==ex.sequence);
        require(creation_route(stage,e,kBias)==CreationRoute::Facet && creation_dispatch_offset(CreationRoute::Facet)==1536);
        for (std::size_t at : {0U,688U,1056U,1216U,1312U}) {
            for (std::size_t field : {0U,8U}) {
                std::uint64_t saved=0;std::memcpy(&saved,bytes.data()+at+field,8);
                put(at+field,saved+16);require(!run().valid());
                if(stage==Boundary::Image)require(!decode_selected_image(e,stack,kBias,public_prelude).valid());
                put(at+field,saved);
            }
        }
        put(1064,std::uint64_t{kBias+0x13bc8c});require(run().valid());
        auto wrong=ex;wrong.sequence=0;require(!decode(stage,e,stack,kBias,wrong).valid());
        wrong=ex;wrong.pipe+=16;require(!decode(stage,e,stack,kBias,wrong).valid());
        wrong=ex;wrong.dispatch_frame+=16;require(!decode(stage,e,stack,kBias,wrong).valid());
        auto truncated=stack;truncated.size=1647;require(!decode(stage,e,truncated,kBias,ex).valid());
        auto oversized=stack;oversized.size=1857;require(!decode(stage,e,oversized,kBias,ex).valid());
        e.x[29]+=96;require(creation_route(stage,e,kBias)==CreationRoute::None && !run().valid());
        e.x[29]=e.sp;e.x[30]+=4;require(!run().valid());
        e.x[30]=entry(stage).x[30];e.x[29]=e.sp+96;require(!run().valid());
    }
    for (auto stage : {Boundary::SendLock, Boundary::Write, Boundary::Finish}) {
        Fixture f; auto e=sender(f,stage);
        auto out=decode(stage,e,f.stack(),kBias,expected());
        require(out.valid() && out.invocation.sequence==kSequence && !out.client_available);
        f.put(kDispatch-72,std::uint64_t{kBias+0x1542b4});
        require(!decode(stage,e,f.stack(),kBias,expected()).valid());
        f.put(kDispatch-72,std::uint64_t{kBias+0x1542b0});
        if (stage==Boundary::Finish) {
            require(out.native_send_success); e.x[20]=0;
            out=decode(stage,e,f.stack(),kBias,expected()); require(out.valid() && !out.native_send_success);
            e.x[20]=256; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
        } else if (stage==Boundary::SendLock) {
            for (auto reg : {20U,21U,22U}) { auto saved=e.x[reg]; e.x[reg]++; require(!decode(stage,e,f.stack(),kBias,expected()).valid()); e.x[reg]=saved; }
        } else {
            for (unsigned offset=0;offset<21;++offset) {
                e.x[19]=offset; e.x[1]=e.x[23]+offset; e.x[2]=21-offset;
                out=decode(stage,e,f.stack(),kBias,expected());
                require(out.valid() && out.packet_offset==offset && out.remaining==21-offset && out.fd==7);
                e.x[1]++; require(!decode(stage,e,f.stack(),kBias,expected()).valid()); e.x[1]--;
                e.x[2]++; require(!decode(stage,e,f.stack(),kBias,expected()).valid()); e.x[2]--;
            }
            e.x[19]=21; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
            e=sender(f,stage); e.x[0]=UINT64_MAX; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
            e=sender(f,stage); e.x[23]+=16; require(!decode(stage,e,f.stack(),kBias,expected()).valid());
        }
    }
    Fixture f; auto e=creation(f,Boundary::Image);
    auto invalid=f.stack(); invalid.data=nullptr; require(!decode(Boundary::Image,e,invalid,kBias,expected()).valid());
    invalid=f.stack(); invalid.size=1553; require(!decode(Boundary::Image,e,invalid,kBias,expected()).valid());
    invalid=f.stack(); invalid.base=std::numeric_limits<std::uintptr_t>::max()-15;
    require(!decode(Boundary::Image,e,invalid,kBias,expected()).valid());
    e.x[30]++; require(!decode(Boundary::Image,e,f.stack(),kBias,expected()).valid());
    require(!decode(static_cast<Boundary>(255),e,f.stack(),kBias,expected()).valid());
    std::printf("%u offline native route checks passed; no live entry or ownership proof\n",checks);
}
