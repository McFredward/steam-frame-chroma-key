#include "import/client_resource_thread.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <pthread.h>
#include <sys/mman.h>
#include <thread>

namespace {
using namespace frame_client_resource_thread;
using Stack=frame_import_startup::OwnedThreadStack;
using Region=frame_native_client_birth::StackRegion;
std::atomic<unsigned> checks{0};
thread_local unsigned char fixture_marker=0; // Models root's existing byte; production core adds no TLS.
void check_at(bool okay,unsigned line) {
    const auto count=checks.fetch_add(1)+1;
    if (!okay) { std::fprintf(stderr,"resource thread check %u failed at line %u\n",count,line);std::abort(); }
}
#define check(value) check_at((value),__LINE__)
constexpr std::uintptr_t bias=0x500000;
frame_import_startup::PreparedImage image() {
    frame_import_startup::PreparedImage out{bias,bias+0x10000,bias+0x90000,{}};
    constexpr char hex[]="31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153";
    const auto nibble=[](char c){return static_cast<unsigned>(c>='a'?c-'a'+10:c-'0');};
    for (unsigned i=0;i<32;++i)out.file_identity[i]=static_cast<std::uint8_t>((nibble(hex[i*2])<<4)|nibble(hex[i*2+1]));
    return out;
}
Region actual_region() {
    pthread_attr_t attrs;check(::pthread_getattr_np(::pthread_self(),&attrs)==0);
    void* base=nullptr;std::size_t size=0;
    check(::pthread_attr_getstack(&attrs,&base,&size)==0);check(::pthread_attr_destroy(&attrs)==0);
    const auto begin=reinterpret_cast<std::uintptr_t>(base);
    check(size<=std::numeric_limits<std::uintptr_t>::max()-begin);
    return {begin,begin+size};
}
Stack actual_stack() {
    const auto region=actual_region();
    const auto stack=Stack::prepare_current(region.begin,region.end);check(stack.current());return stack;
}
Entry entry(void* frame) {
    Entry out;for(unsigned i=0;i<31;++i)out.x[i]=0x1020304050607080ULL+i*16;
    out.sp=reinterpret_cast<std::uintptr_t>(frame);out.x[29]=out.sp;
    out.x[30]=bias+0x328d0;out.x[1]=out.x[23]=out.sp+120;
    out.x[19]=0x123480;out.x[0]=0xaabbccdd0000000bULL;
    out.x[2]=0xeeddccbb00000001ULL;out.x[3]=0x12345678ffffffffULL;
    return out;
}
enum class Mode { Real, Empty, OtherOwner, Short, EntryDerived, Reenter, Retire, ChangeEntry };
struct Control {
    Gate* gate=nullptr;Entry input{};Mode mode=Mode::Real;
    Stack returned{};unsigned calls=0;
    Entry* live_entry=nullptr;
};
Stack prepare(void* context) noexcept {
    auto& control=*static_cast<Control*>(context);++control.calls;
    if (control.mode==Mode::Reenter) {
        const auto recursive=control.gate->before_first_wait(control.input,fixture_marker,prepare,context);
        check(recursive.kind==Kind::Refused && recursive.reason==Reason::Preparing);
    }
    if (control.mode==Mode::Retire)control.gate->retire();
    if (control.mode==Mode::ChangeEntry) {check(control.live_entry!=nullptr);control.live_entry->x[1]+=8;}
    if (control.mode==Mode::Empty)return {};
    if (control.mode==Mode::OtherOwner)return control.returned;
    if (control.mode==Mode::EntryDerived) {
        control.returned=Stack::prepare_current(control.input.sp,control.input.sp+336);
    } else {
        const auto region=actual_region();
        control.returned=Stack::prepare_current(region.begin,
            control.mode==Mode::Short?control.input.sp+320:region.end);
        check(control.returned.current());
    }
    errno=ENOTTY;return control.returned;
}
void exact(const Outcome& result,const Entry& original) {
    check(result.entry.sp==original.sp && result.entry.x==original.x);
}
void no_region(Gate& gate) {
    check(!gate.current(fixture_marker));
    const auto region=gate.region_current(fixture_marker);check(!region.begin && !region.end);
}
template<class Work>void child(Work work) { std::thread thread(work);thread.join(); }
struct ReuseControl {
    Gate* gate=nullptr;std::uintptr_t first_address=0,second_address=0;
    Stack first_stack{};unsigned calls=0;bool second=false;
};
struct ConcurrentControl {
    std::mutex mutex;std::condition_variable changed;
    bool entered=false,resume=false;std::atomic<unsigned> calls{0};
};
Stack prepare_blocked(void* context) noexcept {
    auto& control=*static_cast<ConcurrentControl*>(context);control.calls.fetch_add(1);
    {
        std::unique_lock<std::mutex> lock(control.mutex);control.entered=true;control.changed.notify_one();
        control.changed.wait(lock,[&]{return control.resume;});
    }
    return actual_stack();
}
void* reused_thread(void* context) {
    auto& control=*static_cast<ReuseControl*>(context);
    alignas(16)std::array<unsigned char,512> frame{};
    const auto input=entry(frame.data());
    if (!control.second) {
        check(fixture_marker==0);control.first_address=reinterpret_cast<std::uintptr_t>(&fixture_marker);
        Control prepare_control{control.gate,input};
        const auto result=control.gate->before_first_wait(input,fixture_marker,prepare,&prepare_control);
        check(result.kind==Kind::Prepared);control.calls=prepare_control.calls;
        control.first_stack=prepare_control.returned;check(control.gate->current(fixture_marker));
    } else {
        control.second_address=reinterpret_cast<std::uintptr_t>(&fixture_marker);check(fixture_marker==0);
        check(control.first_stack.current()); // Exact TLS/stack ownership anchor reused by real pthread.
        no_region(*control.gate);
        Control prepare_control{control.gate,input};
        const auto result=control.gate->before_first_wait(input,fixture_marker,prepare,&prepare_control);
        check(result.kind==Kind::Refused && result.reason==Reason::WrongThread);
        check(prepare_control.calls==0 && fixture_marker==0);no_region(*control.gate);
    }
    return nullptr;
}
} // namespace

int main() {
    child([] {
        alignas(16)std::array<unsigned char,512> frame{};
        const auto input=entry(frame.data());Gate gate(image());check(gate.arm_before_receiver_threads());
        Control control{&gate,input};fixture_marker=7;errno=E2BIG;
        auto unrelated=input;unrelated.x[30]+=4;
        check(gate.before_first_wait(unrelated,fixture_marker,prepare,&control).kind==Kind::None);
        check(control.calls==0 && fixture_marker==7);
        const auto result=gate.before_first_wait(input,fixture_marker,prepare,&control);
        check(result.kind==Kind::Prepared && result.reason==Reason::None);exact(result,input);
        check(errno==E2BIG && control.calls==1 && fixture_marker==15 && gate.current(fixture_marker));
        const auto region=gate.region_current(fixture_marker);
        check(region.begin==control.returned.region().begin && region.end==control.returned.region().end);
        unsigned char copy=fixture_marker;check(!gate.current(copy));
        for(unsigned i=0;i<32;++i) {
            const auto repeated=gate.before_first_wait(input,fixture_marker,nullptr,nullptr);
            check(repeated.kind==Kind::None && control.calls==1 && gate.current(fixture_marker));
        }
        fixture_marker=7;no_region(gate);fixture_marker=15;check(gate.current(fixture_marker));
        child([&] {check(!gate.current(fixture_marker));check(!gate.current(copy));});
        gate.retire();no_region(gate);check(!gate.arm_before_receiver_threads());
        check(gate.before_first_wait(input,fixture_marker,prepare,&control).kind==Kind::Refused);
        check(control.calls==1);
    });
    for(unsigned mutation=0;mutation<9;++mutation)child([mutation] {
        auto prepared=image();
        switch(mutation) {
        case 0:prepared.file_identity[0]^=1;break;
        case 1:prepared.bias+=16;break;
        case 2:prepared.code_begin=bias+0x3287c;break;
        case 3:prepared.code_end=bias+0x328d0;break;
        case 4:prepared.bias=0;break;
        case 5:prepared.bias=std::numeric_limits<std::uintptr_t>::max()-15;break;
        case 6:prepared.bias=1ULL<<48;break;
        case 7:prepared.code_begin=1;break;
        case 8:prepared.code_end=(1ULL<<48)+4096;break;
        }
        Gate gate(prepared);check(!gate.arm_before_receiver_threads());no_region(gate);
        check(gate.reason()==Reason::UnpreparedImage);
    });
    for(unsigned mutation=0;mutation<17;++mutation)child([mutation] {
        alignas(16)std::array<unsigned char,512> frame{};
        auto input=entry(frame.data());
        switch(mutation) {
        case 0:input.sp=0;break;
        case 1:input.sp+=8;break;
        case 2:input.x[29]+=16;break;
        case 3:input.x[1]+=8;break;
        case 4:input.x[23]+=8;break;
        case 5:input.x[19]=0;break;
        case 6:input.x[19]+=1;break;
        case 7:input.x[0]=0xabcd0000ffffffffULL;break;
        case 8:input.x[2]=0xabcd000000000002ULL;break;
        case 9:input.x[3]=0xabcd0000fffffffeULL;break;
        case 10:input.x[3]=0;break;
        case 11:input.sp=std::numeric_limits<std::uintptr_t>::max()-15;input.x[29]=input.sp;
            input.x[1]=input.x[23]=input.sp+120;break;
        case 12:input.x[19]=8;break;
        case 13:input.x[19]=(1ULL<<48)+8;break;
        case 14:input.sp=0xff0;input.x[29]=input.sp;input.x[1]=input.x[23]=input.sp+120;break;
        case 15:input.sp=(1ULL<<48)+4096;input.x[29]=input.sp;input.x[1]=input.x[23]=input.sp+120;break;
        case 16:input.sp=(1ULL<<48)-16;input.x[29]=input.sp;input.x[1]=input.x[23]=input.sp+120;break;
        }
        Gate gate(image());check(gate.arm_before_receiver_threads());Control control{&gate,input};
        const auto result=gate.before_first_wait(input,fixture_marker,prepare,&control);
        check(result.kind==Kind::Refused && result.reason==Reason::Entry);exact(result,input);
        check(control.calls==0 && fixture_marker==0);no_region(gate);
        check(!gate.arm_before_receiver_threads());
        check(gate.before_first_wait(entry(frame.data()),fixture_marker,prepare,&control).kind==Kind::Refused);
        check(control.calls==0);
    });
    for(unsigned mutation=0;mutation<5;++mutation)child([mutation] {
        alignas(16)std::array<unsigned char,512> frame{};
        auto input=entry(frame.data());Gate gate(image());Control control{&gate,input};
        if(mutation!=0)check(gate.arm_before_receiver_threads());
        if(mutation==1)fixture_marker=8;
        if(mutation==3)check(!gate.arm_before_receiver_threads());
        if(mutation==4)gate.retire();
        const auto result=gate.before_first_wait(input,fixture_marker,mutation==2?nullptr:prepare,&control);
        constexpr Reason reasons[]{Reason::NotArmed,Reason::MarkerAlreadyUsed,Reason::MissingPrepare,Reason::LateArm,Reason::Retired};
        check(result.kind==Kind::Refused && result.reason==reasons[mutation]);exact(result,input);
        check(control.calls==0);no_region(gate);check(!gate.arm_before_receiver_threads());
    });
    for(const auto mode:{Mode::Empty,Mode::Short,Mode::EntryDerived,Mode::Reenter,Mode::Retire})child([mode] {
        alignas(16)std::array<unsigned char,512> frame{};
        auto input=entry(frame.data());Gate gate(image());check(gate.arm_before_receiver_threads());
        Control control{&gate,input,mode};const auto result=gate.before_first_wait(input,fixture_marker,prepare,&control);
        check(result.kind==Kind::Refused);
        check(result.reason==(mode==Mode::Reenter?Reason::Preparing:mode==Mode::Retire?Reason::Retired:Reason::Stack));
        exact(result,input);check(control.calls==1 && fixture_marker==0);no_region(gate);
        check(gate.before_first_wait(input,fixture_marker,prepare,&control).kind==Kind::Refused && control.calls==1);
    });
    const auto main_stack=actual_stack();
    child([] {
        alignas(16)std::array<unsigned char,512> frame{};auto input=entry(frame.data());const auto original=input;
        Gate gate(image());check(gate.arm_before_receiver_threads());Control control{&gate,input,Mode::ChangeEntry};
        control.live_entry=&input;
        const auto result=gate.before_first_wait(input,fixture_marker,prepare,&control);
        check(result.kind==Kind::Refused && result.reason==Reason::Entry);exact(result,original);
        check(control.calls==1 && fixture_marker==0);no_region(gate);
    });
    child([&] {
        alignas(16)std::array<unsigned char,512> frame{};auto input=entry(frame.data());
        Gate gate(image());check(gate.arm_before_receiver_threads());Control control{&gate,input,Mode::OtherOwner,main_stack};
        check(gate.before_first_wait(input,fixture_marker,prepare,&control).reason==Reason::Stack);
        check(control.calls==1 && fixture_marker==0);no_region(gate);
    });
    for(unsigned placement=0;placement<2;++placement)child([placement] {
        const auto region=actual_region();const auto sp=placement?region.end:region.begin-16;
        auto input=entry(reinterpret_cast<void*>(sp));Gate gate(image());check(gate.arm_before_receiver_threads());
        Control control{&gate,input};
        check(gate.before_first_wait(input,fixture_marker,prepare,&control).reason==Reason::Stack);
        check(control.calls==1 && fixture_marker==0);no_region(gate);
    });
    {
        Gate gate(image());check(gate.arm_before_receiver_threads());ConcurrentControl control;
        std::thread first([&] {
            alignas(16)std::array<unsigned char,512> frame{};
            const auto result=gate.before_first_wait(entry(frame.data()),fixture_marker,prepare_blocked,&control);
            check(result.kind==Kind::Refused && result.reason==Reason::Preparing);
            check(fixture_marker==0);no_region(gate);
        });
        {
            std::unique_lock<std::mutex> lock(control.mutex);
            control.changed.wait(lock,[&]{return control.entered;});
        }
        child([&] {
            alignas(16)std::array<unsigned char,512> frame{};
            const auto result=gate.before_first_wait(entry(frame.data()),fixture_marker,prepare_blocked,&control);
            check(result.kind==Kind::Refused && result.reason==Reason::Preparing && fixture_marker==0);
        });
        {std::lock_guard<std::mutex> lock(control.mutex);control.resume=true;control.changed.notify_one();}
        first.join();check(control.calls.load()==1);check(!gate.arm_before_receiver_threads());
    }
    {
        constexpr std::size_t bytes=8*1024*1024;
        void* stack=::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);check(stack!=MAP_FAILED);
        pthread_attr_t attributes;check(::pthread_attr_init(&attributes)==0);
        check(::pthread_attr_setstack(&attributes,stack,bytes)==0);
        Gate gate(image());check(gate.arm_before_receiver_threads());ReuseControl control{&gate};
        pthread_t thread;check(::pthread_create(&thread,&attributes,reused_thread,&control)==0);check(::pthread_join(thread,nullptr)==0);
        control.second=true;check(::pthread_create(&thread,&attributes,reused_thread,&control)==0);check(::pthread_join(thread,nullptr)==0);
        check(control.first_address==control.second_address && control.calls==1);
        check(::pthread_attr_destroy(&attributes)==0);check(::munmap(stack,bytes)==0);
    }
    std::printf("resource thread cold preparation checks: %u\n",checks.load());
    return 0;
}
