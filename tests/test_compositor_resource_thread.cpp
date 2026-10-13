#include "import/compositor_resource_thread.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <pthread.h>
#include <thread>

namespace {
using namespace frame_compositor_resource_thread;
std::atomic<unsigned> checks{0};
thread_local unsigned char marker=0; // Models the root's existing TLS byte.
void check_at(bool value,unsigned line) {
    const auto number=checks.fetch_add(1)+1;
    if(!value){std::fprintf(stderr,"compositor thread check %u failed at line %u\n",number,line);std::abort();}
}
#define check(v) check_at((v),__LINE__)
constexpr std::uintptr_t bias=0x500000;
frame_import_startup::PreparedImage image() {
    frame_import_startup::PreparedImage result{bias,bias+0x10000,bias+0x300000,{}};
    constexpr char text[]="0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96";
    const auto nibble=[](char c){return unsigned(c>='a'?c-'a'+10:c-'0');};
    for(unsigned i=0;i<32;++i)result.file_identity[i]=std::uint8_t((nibble(text[i*2])<<4)|nibble(text[i*2+1]));
    return result;
}
Stack actual_stack() {
    pthread_attr_t attributes;check(::pthread_getattr_np(::pthread_self(),&attributes)==0);
    void* base=nullptr;std::size_t size=0;
    check(::pthread_attr_getstack(&attributes,&base,&size)==0);
    check(::pthread_attr_destroy(&attributes)==0);
    const auto low=reinterpret_cast<std::uintptr_t>(base);
    check(size<=std::numeric_limits<std::uintptr_t>::max()-low);
    const auto result=Stack::prepare_current(low,low+size);check(result.current());return result;
}
struct Fixture {
    alignas(16)std::array<std::uint8_t,1024> bytes{};
    Entry poll{},begin{};
    Fixture() {
        const auto base=reinterpret_cast<std::uintptr_t>(bytes.data());
        poll.sp=poll.x[29]=base+320;poll.x[30]=bias+kPollReturn;
        poll.x[0]=poll.sp+56;poll.x[1]=1;poll.x[2]=poll.x[20]=200;
        begin.sp=begin.x[29]=base+160;begin.x[30]=bias+kBeginReturn;
        begin.x[0]=begin.x[19]=0x700000;
        put(poll.sp,poll.sp+64);put(poll.sp+8,bias+0x1727a4);
        put(poll.sp+64,poll.sp+160);put(poll.sp+72,bias+0x24fc54);
        put(begin.sp,poll.sp+160);put(begin.sp+8,bias+0x24fee0);
    }
    void put(std::uintptr_t at,std::uint64_t value) {
        check(at>=reinterpret_cast<std::uintptr_t>(bytes.data()) &&
              at-reinterpret_cast<std::uintptr_t>(bytes.data())<=bytes.size()-8);
        std::memcpy(reinterpret_cast<void*>(at),&value,8);
    }
};
enum class Mode { Normal, Empty, Foreign, ReenterPrepare, CancelPrepare,
                  RetirePrepare, ChangePrepare, RetireRead, ChangeRead, ChangeMarker };
struct Control {
    Gate* gate=nullptr;Fixture* fixture=nullptr;Mode mode=Mode::Normal;
    Stack foreign{};unsigned prepares=0,reads=0,fail_at=0,partial=0;
    std::size_t actual_copied=0;
    Entry* live_entry=nullptr;
};
struct Concurrent {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered=false,resume=false;
    std::atomic<unsigned> prepares{0},reads{0};
};
Stack prepare_blocked(void* owner) noexcept {
    auto& c=*static_cast<Concurrent*>(owner);c.prepares.fetch_add(1);
    {std::unique_lock<std::mutex> lock(c.mutex);c.entered=true;c.changed.notify_one();
     c.changed.wait(lock,[&]{return c.resume;});}
    return actual_stack();
}
bool never_read(std::uintptr_t,void*,std::size_t,void* owner) noexcept {
    static_cast<Concurrent*>(owner)->reads.fetch_add(1);return false;
}
bool read(std::uintptr_t at,void* output,std::size_t count,void* owner) noexcept {
    auto& c=*static_cast<Control*>(owner);++c.reads;check(count==16);
    const auto base=reinterpret_cast<std::uintptr_t>(c.fixture->bytes.data());
    check(at>=base && at-base<=c.fixture->bytes.size() && count<=c.fixture->bytes.size()-(at-base));
    const bool fail=c.reads==c.fail_at;
    const auto copied=fail?c.partial:count;check(copied<=count);
    if(copied)std::memcpy(output,reinterpret_cast<const void*>(at),copied);
    c.actual_copied+=copied;
    if(c.mode==Mode::RetireRead)c.gate->retire();
    if(c.mode==Mode::ChangeRead){check(c.live_entry);c.live_entry->x[0]^=8;}
    if(c.mode==Mode::ChangeMarker)marker^=kMarkerBit;
    errno=EDOM;return !fail;
}
Stack prepare(void* owner) noexcept {
    auto& c=*static_cast<Control*>(owner);++c.prepares;
    if(c.mode==Mode::ReenterPrepare) {
        const auto nested=c.gate->before_first_poll(c.fixture->poll,marker,prepare,owner,read,owner);
        check(nested.kind==Kind::Refused && nested.reason==Reason::Preparing);
    }
    if(c.mode==Mode::CancelPrepare)c.gate->cancel();
    if(c.mode==Mode::RetirePrepare)c.gate->retire();
    if(c.mode==Mode::ChangePrepare){check(c.live_entry);c.live_entry->x[0]^=8;}
    errno=EIO;
    if(c.mode==Mode::Empty)return {};
    if(c.mode==Mode::Foreign)return c.foreign;
    return actual_stack();
}
void unavailable(Gate& gate) {
    check(!gate.current(marker));check(!gate.begin_frame_current(marker));
    const auto region=gate.region_current(marker);check(!region.begin && !region.end);
    check(!gate.window_current(marker,1,1));
}
Outcome cold(Gate& gate,Control& c) {return gate.before_first_poll(c.fixture->poll,marker,prepare,&c,read,&c);}
Outcome begin(Gate& gate,Control& c,int result=0) {return gate.after_first_begin(c.fixture->begin,result,marker,read,&c);}
template<class Work>void child(Work work){std::thread thread(work);thread.join();}
}

int main() {
    child([] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        marker=7;errno=E2BIG;
        for(unsigned i=0;i<32;++i) {
            auto unrelated=f.poll;unrelated.x[30]+=4;
            check(gate.before_first_poll(unrelated,marker,prepare,&c,read,&c).kind==Kind::None);
        }
        check(!c.prepares && !c.reads && marker==7);
        const auto p=cold(gate,c);
        check(p.kind==Kind::ColdPrepared && p.reason==Reason::None && p.bytes_requested==32 && p.bytes_confirmed==32);
        check(p.entry.sp==f.poll.sp && p.entry.x==f.poll.x && errno==E2BIG);
        check(c.prepares==1 && c.reads==2 && c.actual_copied==32 && marker==15);
        unavailable(gate);
        for(unsigned i=0;i<16;++i)check(cold(gate,c).kind==Kind::None);
        check(c.prepares==1 && c.reads==2);
        auto unrelated=f.begin;unrelated.x[30]+=4;
        check(gate.after_first_begin(unrelated,0,marker,read,&c).kind==Kind::None);
        const auto b=begin(gate,c);
        check(b.kind==Kind::BeginAdmitted && b.bytes_requested==16 && b.bytes_confirmed==16);
        check(b.entry.sp==f.begin.sp && b.entry.x==f.begin.x && errno==E2BIG);
        check(gate.current(marker) && gate.begin_frame_current(marker)==f.begin.sp);
        check(gate.bytes_requested()==48 && gate.bytes_confirmed()==48 && c.actual_copied==48);
        check(gate.window_current(marker,f.begin.sp,320));
        check(!gate.window_current(marker,std::numeric_limits<std::uintptr_t>::max()-8,16));
        unsigned char copy=marker;check(!gate.current(copy));
        child([&]{check(!gate.current(marker));check(!gate.current(copy));});
        check(gate.current(marker));
        for(unsigned i=0;i<16;++i)check(cold(gate,c).kind==Kind::None);
        check(c.reads==3 && c.prepares==1);
        check(begin(gate,c).reason==Reason::RepeatedBegin);unavailable(gate);
        check(c.reads==3 && !gate.arm_before_compositor_threads());
        check(begin(gate,c).kind==Kind::Refused && c.reads==3);
    });
    for(unsigned mode=0;mode<8;++mode)child([mode] {
        auto p=image();
        switch(mode) {
        case 0:p.file_identity[0]^=1;break;case 1:p.bias+=16;break;case 2:p.bias=0;break;
        case 3:p.bias=std::numeric_limits<std::uintptr_t>::max()-15;break;
        case 4:p.code_begin=bias+0x153f74;break;case 5:p.code_end=bias+0x24fee0;break;
        case 6:p.code_begin=1;break;case 7:p.code_end=1ULL<<48;break;
        }
        Fixture f;Gate gate(p);Control c{&gate,&f};check(!gate.arm_before_compositor_threads());
        check(gate.reason()==Reason::UnpreparedImage);unavailable(gate);
        check(cold(gate,c).kind!=Kind::ColdPrepared && c.prepares==0 && c.reads==0);
    });
    for(unsigned mode=0;mode<9;++mode)child([mode] {
        Fixture f;Gate gate(image());Control c{&gate,&f};check(gate.arm_before_compositor_threads());
        switch(mode) {
        case 0:f.poll.x[0]+=8;break;case 1:f.poll.x[1]=2;break;case 2:f.poll.x[2]=201;break;
        case 3:f.poll.x[20]=199;break;case 4:f.poll.x[29]+=16;break;case 5:f.poll.sp+=8;break;
        case 6:f.poll.sp=std::numeric_limits<std::uintptr_t>::max()-15;f.poll.x[29]=f.poll.sp;break;
        case 7:f.poll.sp=0;f.poll.x[29]=0;break;case 8:marker=kMarkerBit;break;
        }
        const auto result=cold(gate,c);check(result.kind==Kind::Refused);
        check(result.reason==(mode==8?Reason::MarkerAlreadyUsed:Reason::Entry));
        check(!c.prepares && !c.reads && !gate.bytes_requested());unavailable(gate);
        check(cold(gate,c).kind==Kind::Refused && !gate.arm_before_compositor_threads());
    });
    for(unsigned missing=0;missing<5;++missing)child([missing] {
        Fixture f;Gate gate(image());Control c{&gate,&f};
        if(missing!=0)check(gate.arm_before_compositor_threads());
        const auto result=gate.before_first_poll(f.poll,marker,missing==1?nullptr:prepare,missing==2?nullptr:&c,
                                                missing==3?nullptr:read,missing==4?nullptr:&c);
        check(result.reason==(missing==0?Reason::NotArmed:missing<3?Reason::MissingPrepare:Reason::MissingReader));
        check(!c.prepares && !c.reads);unavailable(gate);
    });
    for(const auto mode:{Mode::Empty,Mode::ReenterPrepare,Mode::CancelPrepare,Mode::RetirePrepare,Mode::ChangePrepare})child([mode] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f,mode};
        c.live_entry=&f.poll;
        const auto result=cold(gate,c);check(result.kind==Kind::Refused && c.prepares==1 && c.reads==0);
        check(result.reason==(mode==Mode::Empty?Reason::Stack:mode==Mode::ReenterPrepare?Reason::Preparing:
              mode==Mode::CancelPrepare?Reason::Cancelled:mode==Mode::RetirePrepare?Reason::Retired:Reason::ChangedEntry));
        check(!gate.bytes_requested() && marker==0);unavailable(gate);
        check(cold(gate,c).kind==Kind::Refused && c.prepares==1 && !gate.arm_before_compositor_threads());
    });
    const auto foreign=actual_stack();
    child([&] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f,Mode::Foreign,foreign};
        check(cold(gate,c).reason==Reason::Stack && c.reads==0 && marker==0);
    });
    for(unsigned failed=1;failed<=3;++failed)child([failed] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        c.fail_at=failed;c.partial=7;
        const auto p=cold(gate,c);const auto result=failed==3?begin(gate,c):p;
        check(result.reason==Reason::Reader && c.reads==failed && c.prepares==1);
        check(gate.bytes_requested()==16*failed && gate.bytes_confirmed()==16*(failed-1));
        check(c.actual_copied==16*(failed-1)+7); // Actual partial copies belong to the root ledger.
        check(result.bytes_requested==(failed==3?16:16*failed));
        unavailable(gate);const auto before=c.reads;
        check(cold(gate,c).kind==Kind::Refused && begin(gate,c).kind==Kind::Refused && c.reads==before);
    });
    for(unsigned byte=0;byte<48;++byte)child([byte] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        const auto at=byte<16?f.poll.sp+byte:byte<32?f.poll.sp+64+byte-16:f.begin.sp+byte-32;
        *reinterpret_cast<std::uint8_t*>(at)^=0x80;
        const auto p=cold(gate,c);const auto result=byte<32?p:begin(gate,c);
        check(result.reason==Reason::SourceLink && c.prepares==1 && c.reads==(byte<32?2:3));
        check(gate.bytes_requested()==(byte<32?32:48));unavailable(gate);
    });
    for(const auto mode:{Mode::RetireRead,Mode::ChangeRead,Mode::ChangeMarker})
        for(unsigned phase=0;phase<2;++phase)child([mode,phase] {
            Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
            if(phase)check(cold(gate,c).kind==Kind::ColdPrepared);
            c.mode=mode;c.live_entry=phase?&f.begin:&f.poll;errno=E2BIG;
            const auto result=phase?begin(gate,c):cold(gate,c);
            check(result.kind==Kind::Refused && result.bytes_requested==16 && result.bytes_confirmed==16);
            check(result.reason==(mode==Mode::RetireRead?Reason::Retired:Reason::ChangedEntry));
            check(errno==E2BIG && gate.bytes_confirmed()==(phase?48:16));unavailable(gate);
        });
    for(unsigned invalid=0;invalid<7;++invalid)child([invalid] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        if(invalid!=0)check(cold(gate,c).kind==Kind::ColdPrepared);
        int result=0;
        if(invalid==1)result=1;
        if(invalid==2){f.begin.sp+=16;f.begin.x[29]=f.begin.sp;}
        if(invalid==3)f.begin.x[29]+=16;
        if(invalid==4)marker^=kMarkerBit;
        const auto b=gate.after_first_begin(f.begin,result,marker,invalid==5?nullptr:read,invalid==6?nullptr:&c);
        check(b.kind==Kind::Refused && b.bytes_requested==0 && b.bytes_confirmed==0);
        check(c.reads==(invalid?2:0));unavailable(gate);
    });
    child([] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        check(cold(gate,c).kind==Kind::ColdPrepared);
        child([&] {check(begin(gate,c).reason==Reason::WrongThread && c.reads==2);});
        unavailable(gate);check(begin(gate,c).kind==Kind::Refused && c.reads==2);
    });
    child([] {
        Fixture f;Gate gate(image());check(gate.arm_before_compositor_threads());Control c{&gate,&f};
        check(!gate.arm_before_compositor_threads() && gate.reason()==Reason::LateArm);
        check(cold(gate,c).kind==Kind::Refused && !c.prepares && !c.reads);
    });
    {
        Gate gate(image());check(gate.arm_before_compositor_threads());Concurrent c;
        std::thread first([&] {
            Fixture f;
            const auto r=gate.before_first_poll(f.poll,marker,prepare_blocked,&c,never_read,&c);
            check(r.reason==Reason::Preparing && marker==0);unavailable(gate);
        });
        {std::unique_lock<std::mutex> lock(c.mutex);c.changed.wait(lock,[&]{return c.entered;});}
        child([&] {
            Fixture f;
            const auto r=gate.before_first_poll(f.poll,marker,prepare_blocked,&c,never_read,&c);
            check(r.reason==Reason::Preparing && !r.bytes_requested && marker==0);
        });
        {std::lock_guard<std::mutex> lock(c.mutex);c.resume=true;c.changed.notify_one();}
        first.join();check(c.prepares.load()==1 && c.reads.load()==0);
        check(!gate.bytes_requested() && !gate.arm_before_compositor_threads());
    }
    std::printf("%u compositor cold-thread controls passed; synthetic native frames, no Frame deployment or namespace proof\n",checks.load());
}
