#include "import/compositor_import_thread.h"

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
using namespace frame_compositor_import_thread;
std::atomic<unsigned> checks{0};
thread_local unsigned char marker=0; // The root's existing marker, not gate TLS.
int marker_owner=0;
void check_at(bool v,unsigned line) {
    const auto n=checks.fetch_add(1)+1;
    if(!v){std::fprintf(stderr,"first-import check %u failed at line %u\n",n,line);std::abort();}
}
#define check(v) check_at((v),__LINE__)
constexpr std::uintptr_t bias=0x500000;
unsigned char* current_marker(void* owner) noexcept {
    check(owner==&marker_owner);errno=EDOM;return &marker;
}
frame_import_startup::PreparedImage image() {
    frame_import_startup::PreparedImage p{bias,bias+0x10000,bias+0x400000,{}};
    constexpr char text[]="0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96";
    const auto nibble=[](char c){return unsigned(c>='a'?c-'a'+10:c-'0');};
    for(unsigned i=0;i<32;++i)p.file_identity[i]=std::uint8_t((nibble(text[i*2])<<4)|nibble(text[i*2+1]));
    return p;
}
Stack actual_stack() {
    pthread_attr_t a;check(::pthread_getattr_np(::pthread_self(),&a)==0);
    void* base=nullptr;std::size_t size=0;
    check(::pthread_attr_getstack(&a,&base,&size)==0);check(::pthread_attr_destroy(&a)==0);
    const auto low=reinterpret_cast<std::uintptr_t>(base);
    check(size<=std::numeric_limits<std::uintptr_t>::max()-low);
    const auto s=Stack::prepare_current(low,low+size);check(s.current());return s;
}
struct Fixture {
    alignas(16)std::array<std::uint8_t,512> bytes{};
    Entry begin{};
    Fixture() {
        begin.sp=begin.x[29]=reinterpret_cast<std::uintptr_t>(bytes.data());
        begin.x[30]=bias+kBeginReturn;begin.x[0]=begin.x[19]=0x700000;
        put(begin.sp,begin.sp+kBeginFrameBytes);put(begin.sp+8,bias+kDispatchReturn);
    }
    void put(std::uintptr_t at,std::uint64_t v){std::memcpy(reinterpret_cast<void*>(at),&v,8);}
    Entry current_image(frame_import_native::CreationRoute route=frame_import_native::CreationRoute::Vulkan) const {
        Entry e{};
        const auto offset=frame_import_native::creation_dispatch_offset(route);
        e.sp=begin.sp-offset;
        e.x[29]=route==frame_import_native::CreationRoute::Facet?e.sp:e.sp+96;
        e.x[30]=bias+(route==frame_import_native::CreationRoute::Facet?0x3076a0:0x2c75ac);
        e.x[0]=0xabc0;return e;
    }
};
enum class Mode {Normal,Empty,Foreign,ReenterPrepare,CancelPrepare,RetirePrepare,
    ChangePrepare,ChangeMarkerPrepare,RetireRead,ChangeRead,ChangeMarkerRead,ReenterRead};
struct Control {
    Gate* gate=nullptr;Fixture* f=nullptr;Mode mode=Mode::Normal;
    Stack foreign{};unsigned prepares=0,reads=0;bool reader_fail=false;
    std::size_t partial=0,actual_copied=0;
};
Stack prepare(void* owner) noexcept;
bool read(std::uintptr_t at,void* output,std::size_t count,void* owner) noexcept {
    auto& c=*static_cast<Control*>(owner);++c.reads;
    check(at==c.f->begin.sp && count==kParentCopyBytes);
    const auto copied=c.reader_fail?c.partial:count;
    check(copied<=count);if(copied)std::memcpy(output,reinterpret_cast<const void*>(at),copied);
    c.actual_copied+=copied;
    if(c.mode==Mode::RetireRead)c.gate->retire();
    if(c.mode==Mode::ChangeRead)c.f->begin.x[0]^=8;
    if(c.mode==Mode::ChangeMarkerRead)marker^=kMarkerBit;
    if(c.mode==Mode::ReenterRead) {
        check(c.gate->before_first_begin(c.f->begin,marker,prepare,&c).reason==Reason::Preparing);
    }
    errno=EIO;return !c.reader_fail;
}
Stack prepare(void* owner) noexcept {
    auto& c=*static_cast<Control*>(owner);++c.prepares;
    if(c.mode==Mode::ReenterPrepare)
        check(c.gate->before_first_begin(c.f->begin,marker,prepare,&c).reason==Reason::Preparing);
    if(c.mode==Mode::CancelPrepare)c.gate->cancel();
    if(c.mode==Mode::RetirePrepare)c.gate->retire();
    if(c.mode==Mode::ChangePrepare)c.f->begin.x[0]^=8;
    if(c.mode==Mode::ChangeMarkerPrepare)marker^=kMarkerBit;
    errno=EDOM;
    if(c.mode==Mode::Empty)return {};
    if(c.mode==Mode::Foreign)return c.foreign;
    return actual_stack();
}
Outcome before(Gate& g,Control& c){return g.before_first_begin(c.f->begin,marker,prepare,&c);}
Outcome after(Gate& g,Control& c,int result=0){return g.after_first_begin(c.f->begin,result,marker,read,&c);}
void unavailable(Gate& g) {
    check(!g.current(marker));check(!g.begin_frame_current(marker));
    const auto r=g.region_current(marker);check(!r.begin && !r.end);
    check(!g.window_current(marker,1,1));
}
template<class Work>void child(Work work){std::thread t(work);t.join();}
struct Concurrent {
    std::mutex mutex;std::condition_variable changed;bool entered=false,resume=false;
    std::atomic<unsigned> prepares{0},reads{0};
};
Stack blocked_prepare(void* owner) noexcept {
    auto& c=*static_cast<Concurrent*>(owner);c.prepares.fetch_add(1);
    {std::unique_lock<std::mutex> l(c.mutex);c.entered=true;c.changed.notify_one();
     c.changed.wait(l,[&]{return c.resume;});}
    return actual_stack();
}
}
int main() {
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        check(g.arm_before_compositor_threads());marker=7;errno=E2BIG;
        for(unsigned i=0;i<32;++i) {
            auto e=f.begin;e.x[30]+=4;
            check(g.before_first_begin(e,marker,prepare,&c).kind==Kind::None);
            check(g.after_first_begin(e,0,marker,read,&c).kind==Kind::None);
        }
        check(!c.prepares && !c.reads && marker==7);
        const auto b=before(g,c);check(b.kind==Kind::BeginStaged && b.reason==Reason::None);
        check(b.entry.sp==f.begin.sp && b.entry.x==f.begin.x && !b.bytes_requested && !b.bytes_confirmed);
        check(errno==E2BIG && marker==15 && g.pending_current(marker));unavailable(g);
        check(c.prepares==1 && !c.reads && !g.bytes_requested());
        unsigned char copy=marker;check(!g.pending_current(copy));
        child([&]{check(!g.pending_current(marker));});
        const auto a=after(g,c);check(a.kind==Kind::BeginAdmitted && a.reason==Reason::None);
        check(a.entry.sp==f.begin.sp && a.entry.x==f.begin.x && a.bytes_requested==16 && a.bytes_confirmed==16);
        check(c.prepares==1 && c.reads==1 && c.actual_copied==16 && errno==E2BIG);
        check(g.bytes_requested()==16 && g.bytes_confirmed()==16 && g.current(marker));
        check(!g.pending_current(marker) && g.begin_frame_current(marker)==f.begin.sp);
        check(g.window_current(marker,f.begin.sp,320));
        check(!g.window_current(marker,std::numeric_limits<std::uintptr_t>::max()-8,16));
        check(!g.current(copy));child([&]{check(!g.current(marker));check(!g.current(copy));});
        check(g.current(marker));check(after(g,c).reason==Reason::RepeatedBegin);unavailable(g);
        check(c.prepares==1 && c.reads==1 && !g.arm_before_compositor_threads());
        check(before(g,c).kind==Kind::Refused && after(g,c).kind==Kind::Refused);
    });
    for(unsigned i=0;i<10;++i)child([i] {
        auto p=image();
        switch(i) {
        case 0:p.file_identity[0]^=1;break;case 1:p.bias+=16;break;case 2:p.bias=0;break;
        case 3:p.bias=std::numeric_limits<std::uintptr_t>::max()-15;break;
        case 4:p.code_begin=bias+0x153f74;break;case 5:p.code_end=bias+kDispatchReturn;break;
        case 6:p.code_begin=1;break;case 7:p.code_end=1ULL<<48;break;
        }
        Fixture f;Gate g(p,i==8?nullptr:current_marker,i==9?nullptr:&marker_owner);Control c{&g,&f};
        check(!g.arm_before_compositor_threads());check(g.reason()==(i<8?Reason::UnpreparedImage:Reason::MissingCurrentMarker));
        unavailable(g);check(before(g,c).kind!=Kind::BeginStaged && !c.prepares && !c.reads);
    });
    for(unsigned i=0;i<10;++i)child([i] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        switch(i) {
        case 0:f.begin.x[0]+=8;break;case 1:f.begin.x[19]+=16;break;case 2:f.begin.x[0]=f.begin.x[19]=0;break;
        case 3:f.begin.x[0]=f.begin.x[19]=1ULL<<48;break;case 4:f.begin.x[29]+=16;break;
        case 5:f.begin.sp+=8;break;
        case 6:f.begin.sp=std::numeric_limits<std::uintptr_t>::max()-15;f.begin.x[29]=f.begin.sp;break;
        case 7:f.begin.sp=f.begin.x[29]=0;break;case 8:marker=kMarkerBit;break;
        }
        unsigned char foreign=0;
        const auto b=g.before_first_begin(f.begin,i==9?foreign:marker,prepare,&c);
        check(b.kind==Kind::Refused && b.reason==(i==8?Reason::MarkerAlreadyUsed:i==9?Reason::WrongThread:Reason::Entry));
        check(!c.prepares && !c.reads && !g.bytes_requested());unavailable(g);
        check(before(g,c).kind==Kind::Refused && !g.arm_before_compositor_threads());
    });
    for(unsigned i=0;i<6;++i)child([i] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        if(i)check(g.arm_before_compositor_threads());
        Outcome a;
        if(i==0)a=before(g,c);
        else if(i==1)a=after(g,c);
        else if(i==2 || i==3)a=g.before_first_begin(f.begin,marker,i==2?nullptr:prepare,i==3?nullptr:&c);
        else {check(before(g,c).kind==Kind::BeginStaged);
              a=g.after_first_begin(f.begin,0,marker,i==4?nullptr:read,i==5?nullptr:&c);}
        check(a.reason==(i==0?Reason::NotArmed:i==1?Reason::MissingBefore:i<4?Reason::MissingPrepare:Reason::MissingReader));
        check(c.prepares==(i>=4?1U:0U) && !c.reads);unavailable(g);
    });
    for(unsigned i=0;i<31;++i)child([i] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        check(before(g,c).kind==Kind::BeginStaged);f.begin.x[i]^=16;
        // An altered LR is unrelated and leaves the pending scope for explicit
        // unwind cancellation; no artificial After pairing can restore it.
        const auto a=after(g,c);check(a.kind==(i==30?Kind::None:Kind::Refused));
        if(i!=30)check(a.reason==Reason::ChangedEntry);else g.cancel();
        check(c.prepares==1 && !c.reads);unavailable(g);
    });
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        check(before(g,c).kind==Kind::BeginStaged);f.begin.sp+=16;
        check(after(g,c).reason==Reason::ChangedEntry && c.prepares==1 && !c.reads);
    });
    for(const int failure:{1,EINVAL,EBUSY,-1})child([failure] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        check(before(g,c).kind==Kind::BeginStaged);errno=E2BIG;
        check(after(g,c,failure).reason==Reason::NativeFailure && errno==E2BIG);
        check(c.prepares==1 && !c.reads && !g.bytes_requested());check(after(g,c).kind==Kind::Refused);
    });
    for(const auto mode:{Mode::Empty,Mode::ReenterPrepare,Mode::CancelPrepare,Mode::RetirePrepare,
                         Mode::ChangePrepare,Mode::ChangeMarkerPrepare})child([mode] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f,mode};check(g.arm_before_compositor_threads());
        errno=E2BIG;
        const auto a=before(g,c);check(a.kind==Kind::Refused && c.prepares==1 && !c.reads);
        check(a.reason==(mode==Mode::Empty?Reason::Stack:mode==Mode::ReenterPrepare?Reason::Preparing:
              mode==Mode::CancelPrepare?Reason::Cancelled:mode==Mode::RetirePrepare?Reason::Retired:Reason::ChangedEntry));
        check(!g.bytes_requested() && errno==E2BIG);unavailable(g);
        check(after(g,c).kind==Kind::Refused && c.prepares==1 && !g.arm_before_compositor_threads());
    });
    const auto foreign=actual_stack();
    child([&] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f,Mode::Foreign,foreign};
        check(g.arm_before_compositor_threads());check(before(g,c).reason==Reason::Stack && !c.reads);
    });
    for(unsigned partial=0;partial<=16;++partial)child([partial] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        c.reader_fail=true;c.partial=partial;check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);
        const auto a=after(g,c);check(a.reason==Reason::Reader && a.bytes_requested==16 && !a.bytes_confirmed);
        check(c.prepares==1 && c.reads==1 && c.actual_copied==partial && g.bytes_requested()==16 && !g.bytes_confirmed());
        unavailable(g);check(after(g,c).kind==Kind::Refused && c.reads==1);
    });
    for(unsigned byte=0;byte<16;++byte)child([byte] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        f.bytes[byte]^=0x80;check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);
        const auto a=after(g,c);check(a.reason==Reason::SourceLink && a.bytes_requested==16 && a.bytes_confirmed==16);
        check(c.prepares==1 && c.reads==1 && c.actual_copied==16);unavailable(g);
    });
    for(const auto mode:{Mode::RetireRead,Mode::ChangeRead,Mode::ChangeMarkerRead,Mode::ReenterRead})child([mode] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f,mode};
        check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);errno=E2BIG;
        const auto a=after(g,c);check(a.kind==Kind::Refused && a.bytes_requested==16 && a.bytes_confirmed==16);
        check(a.reason==(mode==Mode::RetireRead?Reason::Retired:mode==Mode::ReenterRead?Reason::Preparing:Reason::ChangedEntry));
        check(c.actual_copied==16 && g.bytes_confirmed()==16 && errno==E2BIG);unavailable(g);
    });
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);
        auto& owner_marker=marker;
        child([&]{check(g.after_first_begin(f.begin,0,owner_marker,read,&c).reason==Reason::WrongThread);});
        check(c.prepares==1 && !c.reads);unavailable(g);
    });
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);
        check(before(g,c).reason==Reason::Preparing && c.prepares==1 && !c.reads);unavailable(g);
    });
    for(unsigned phase=0;phase<3;++phase)child([phase] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        if(phase)check(before(g,c).kind==Kind::BeginStaged);
        if(phase==2)check(after(g,c).kind==Kind::BeginAdmitted);
        g.cancel();check(g.reason()==Reason::Cancelled);unavailable(g);
        check(before(g,c).kind==Kind::Refused && after(g,c).kind==Kind::Refused && !g.arm_before_compositor_threads());
        check(c.prepares==(phase?1U:0U) && c.reads==(phase==2?1U:0U));
    });
    {
        Gate g(image(),current_marker,&marker_owner);check(g.arm_before_compositor_threads());Concurrent c;
        std::thread first([&]{Fixture f;
            const auto a=g.before_first_begin(f.begin,marker,blocked_prepare,&c);
            check(a.reason==Reason::Preparing);unavailable(g);});
        {std::unique_lock<std::mutex> l(c.mutex);c.changed.wait(l,[&]{return c.entered;});}
        Fixture other;check(g.before_first_begin(other.begin,marker,blocked_prepare,&c).reason==Reason::Preparing);
        {std::lock_guard<std::mutex> l(c.mutex);c.resume=true;c.changed.notify_one();}
        first.join();check(c.prepares==1 && !c.reads && !g.bytes_requested());
    }
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        check(!g.arm_before_compositor_threads() && g.reason()==Reason::LateArm);
        check(before(g,c).kind==Kind::Refused && !c.prepares && !c.reads);
    });
    child([] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        check(g.arm_before_compositor_threads());check(before(g,c).kind==Kind::BeginStaged);
        struct Unwind {Gate& g;~Unwind(){g.cancel();}};
        bool caught=false;
        try {Unwind cleanup{g};throw 1;} catch(int) {caught=true;}
        check(caught && g.reason()==Reason::Cancelled && c.prepares==1 && !c.reads);
        unavailable(g);check(after(g,c).kind==Kind::Refused && !g.arm_before_compositor_threads());
    });
    for(unsigned phase=0;phase<3;++phase)child([phase] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        check(g.arm_before_compositor_threads());
        if(phase)check(before(g,c).kind==Kind::BeginStaged);
        if(phase==2)check(after(g,c).kind==Kind::BeginAdmitted);
        g.retire();check(g.reason()==Reason::Retired);unavailable(g);
        check(before(g,c).kind==Kind::Refused && after(g,c).kind==Kind::Refused && !g.arm_before_compositor_threads());
        check(c.prepares==(phase?1U:0U) && c.reads==(phase==2?1U:0U));
    });
    for(const auto route:{frame_import_native::CreationRoute::Vulkan,frame_import_native::CreationRoute::Facet})child([route] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        const auto stack=actual_stack();const auto current=f.current_image(route);errno=E2BIG;
        const auto out=g.admit_staged_image(f.begin,current,stack,marker,read,&c);
        check(out.kind==Kind::BeginAdmitted && out.reason==Reason::None && out.bytes_requested==16 && out.bytes_confirmed==16);
        check(!c.prepares && c.reads==1 && c.actual_copied==16 && errno==E2BIG);
        check(out.entry.sp==current.sp && out.entry.x==current.x && g.current(marker));
        check(g.begin_frame_current(marker)==f.begin.sp && g.window_current(marker,f.begin.sp,320));
        check(g.admit_staged_image(f.begin,current,stack,marker,read,&c).kind==Kind::Refused && c.reads==1);
    });
    for(unsigned problem=0;problem<10;++problem)child([problem] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        if(problem!=0)check(g.arm_before_compositor_threads());
        auto stack=actual_stack();auto current=f.current_image();unsigned char foreign=0;
        if(problem==1)f.begin.x[19]+=16;
        if(problem==2)current.x[30]+=4;
        if(problem==3)current.sp+=16;
        if(problem==4)stack={};
        if(problem==5)marker=kMarkerBit;
        if(problem==8)g.cancel();
        if(problem==9)g.retire();
        const auto out=g.admit_staged_image(f.begin,current,stack,problem==6?foreign:marker,problem==7?nullptr:read,&c);
        check(out.kind==Kind::Refused && !c.prepares && !c.reads && !g.bytes_requested());unavailable(g);
    });
    for(unsigned partial=0;partial<=16;++partial)child([partial] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        c.reader_fail=true;c.partial=partial;check(g.arm_before_compositor_threads());
        const auto out=g.admit_staged_image(f.begin,f.current_image(),actual_stack(),marker,read,&c);
        check(out.kind==Kind::Refused && out.reason==Reason::Reader && out.bytes_requested==16 && !out.bytes_confirmed);
        check(c.reads==1 && c.actual_copied==partial && !c.prepares);unavailable(g);
    });
    for(unsigned byte=0;byte<16;++byte)child([byte] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};
        f.bytes[byte]^=0x80;check(g.arm_before_compositor_threads());
        const auto out=g.admit_staged_image(f.begin,f.current_image(),actual_stack(),marker,read,&c);
        check(out.kind==Kind::Refused && out.reason==Reason::SourceLink && out.bytes_requested==16 && out.bytes_confirmed==16);
        check(c.reads==1 && c.actual_copied==16 && !c.prepares);unavailable(g);
    });
    for(const auto mode:{Mode::RetireRead,Mode::ChangeRead,Mode::ChangeMarkerRead,Mode::ReenterRead})child([mode] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f,mode};
        check(g.arm_before_compositor_threads());errno=E2BIG;
        const auto stack=actual_stack();errno=E2BIG;
        const auto out=g.admit_staged_image(f.begin,f.current_image(),stack,marker,read,&c);
        check(out.kind==Kind::Refused && out.bytes_requested==16 && out.bytes_confirmed==16);
        check(c.reads==1 && !c.prepares && c.actual_copied==16 && errno==E2BIG);unavailable(g);
    });
    child([&] {
        Fixture f;Gate g(image(),current_marker,&marker_owner);Control c{&g,&f};check(g.arm_before_compositor_threads());
        check(g.admit_staged_image(f.begin,f.current_image(),foreign,marker,read,&c).reason==Reason::Stack);
        check(!c.prepares && !c.reads);unavailable(g);
    });
    std::printf("first-import thread gate: %u checks passed\n",checks.load());
}
