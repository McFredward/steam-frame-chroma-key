#include "import/client_resource_census.h"
#include "import/client_thread_marker.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <pthread.h>
#include <thread>

namespace r=frame_client_resource;
using Entry=frame_import_native::Entry;
using Operation=frame_import_forwarding::Operation;
using Phase=frame_import_forwarding::Phase;
namespace {
unsigned checks=0;
void check_at(bool okay,unsigned line) {
    ++checks;if(!okay){std::fprintf(stderr,"resource census check %u at line %u\n",checks,line);std::abort();}
}
#define check(v) check_at((v),__LINE__)
constexpr std::uintptr_t bias=0x500000,sp=0x100000,output=0x900020;
thread_local unsigned char test_marker=0;
frame_client_connect::Target target() {
    frame_client_connect::Target t;
    t.module={bias,bias,bias+0x60699a,72,{}};t.module.code_identity[0]=0xb5;
    t.object={0x123400,bias+0x649ff0,1010182,1,
              frame_import_pipe::Domain::Client045,frame_import_pipe::Reason::None};
    return t;
}
Entry entry(unsigned which) {
    Entry e;
    for(unsigned i=0;i<31;++i)e.x[i]=0x8000+16*i;
    // Independently literal audited source sites; these scalar fixtures cannot
    // authenticate production modules, stack windows or native resources.
    constexpr std::uintptr_t sites[]{0x3623c0,0x3624c4,0x362d58};
    e.x[30]=bias+sites[which];e.sp=e.x[29]=sp+(which==2?112:0);
    e.x[0]=bias+0x649ff0;
    if(which==0){e.x[21]=e.x[0];e.x[20]=output;}
    if(which==1){e.x[21]=0xdeadbeef;e.x[20]=output;e.x[24]=1;}
    if(which==2){e.x[19]=e.x[0];e.x[20]=bias+0x649f78;e.x[24]=0x123456789abcdef0;}
    return e;
}
Operation operation(unsigned which){return which==1?Operation::Unlock:Operation::Lock;}
struct Fixture {
    r::Gate gate;
    r::Scope scope=r::Scope::prepare_current(true);
    explicit Fixture(frame_client_connect::Target t=target()):gate(t){}
    r::Outcome send(unsigned which,Phase phase,Entry e,std::uint64_t bits=0,int error=0) {
        return gate.observe({operation(which),phase,e,bits,error},scope);
    }
    void pair(unsigned which) {
        auto before=send(which,Phase::Before,entry(which));
        check(before.kind==r::Kind::Boundary && !before.native_result_available);
        auto after=send(which,Phase::After,entry(which),0,EAGAIN);
        check(after.kind==r::Kind::Boundary && after.native_result_available && after.native_result==0);
    }
    void prior(unsigned which){for(unsigned i=0;i<which;++i)pair(i);}
};
}
int main() {
    {
        frame_import_startup::OwnedThreadStack cached;
        std::thread original([&]{
            pthread_attr_t attr;check(pthread_getattr_np(pthread_self(),&attr)==0);
            void* base=nullptr;std::size_t size=0;
            check(pthread_attr_getstack(&attr,&base,&size)==0);
            check(pthread_attr_destroy(&attr)==0);
            const auto low=reinterpret_cast<std::uintptr_t>(base);
            cached=frame_import_startup::OwnedThreadStack::prepare_current(low,low+size);
            check(cached.current());
            frame_client_thread::claim(test_marker,frame_client_thread::kStartup);
            frame_client_thread::claim(test_marker,frame_client_thread::kConnection);
            frame_client_thread::claim(test_marker,frame_client_thread::kResource);
            check(frame_client_thread::startup_stack_owned(test_marker,cached.current()));
            check(frame_client_thread::claimed(test_marker,frame_client_thread::kConnection));
        });original.join();
        unsigned recycled=0;
        for(unsigned i=0;i<8;++i) {
            std::thread replacement([&]{
                check(test_marker==0);
                if(cached.current())++recycled; // Actual old marker address reused.
                check(!frame_client_thread::startup_stack_owned(test_marker,cached.current()));
                frame_client_thread::claim(test_marker,frame_client_thread::kResource);
                check(!frame_client_thread::startup_stack_owned(test_marker,cached.current()));
                check(!frame_client_thread::claimed(test_marker,frame_client_thread::kConnection));
            });replacement.join();
        }
        std::printf("%u actual recycled startup-marker addresses rejected by fresh-thread cookie\n",recycled);
    }
    {
        Fixture f;errno=E2BIG;
        for(unsigned i=0;i<3;++i){f.pair(i);check(f.scope.complete()==(i==2));}
        check(errno==E2BIG && !f.scope.expired());
        check(f.send(0,Phase::Before,entry(0)).kind==r::Kind::None);
        check(f.scope.complete()); // One terminal census, not repeated sampling.
    }
    {
        Fixture f;f.scope=r::Scope::prepare_current(false);
        const auto out=f.send(0,Phase::Before,entry(0));
        check(out.kind==r::Kind::Boundary && !out.startup_thread_stack_owned);
        // Registers can be copied without authority to read this thread's stack.
    }
    for(unsigned field=0;field<10;++field) {
        auto t=target();
        switch(field){
        case 0:t.module.load_bias+=16;break;
        case 1:t.module.code_begin=bias+0x3623c4;break;
        case 2:t.module.code_end=bias+0x362d58;break;
        case 3:t.module.module_lifetime_label=0;break;
        case 4:t.module.code_identity.fill(0);break;
        case 5:t.object.pipe+=4096;break;
        case 6:t.object.registry_identity=0;break;
        case 7:t.object.object_generation=0;break;
        case 8:t.object.process_generation=0;break;
        case 9:t.object.domain=frame_import_pipe::Domain::Compositor045;break;
        }
        Fixture f(t);const auto out=f.send(0,Phase::Before,entry(0));
        check(out.kind==r::Kind::Rejected && out.reason==r::Reason::Target && f.scope.expired());
    }
    {
        Fixture f;r::Scope empty;
        const auto out=f.gate.observe({Operation::Lock,Phase::Before,entry(0),0,0},empty);
        check(out.kind==r::Kind::Rejected && out.reason==r::Reason::Thread);
    }
    {
        Fixture f;r::Outcome out;
        std::thread other([&]{out=f.send(0,Phase::Before,entry(0));});other.join();
        check(out.kind==r::Kind::Rejected && out.reason==r::Reason::Thread && !f.scope.expired());
    }
    {
        Fixture f;r::Gate other(target());
        check(f.send(0,Phase::Before,entry(0)).kind==r::Kind::Boundary);
        const auto out=other.observe({Operation::Lock,Phase::After,entry(0),0,0},f.scope);
        check(out.kind==r::Kind::Rejected && out.reason==r::Reason::Target);
    }
    for(unsigned boundary=0;boundary<3;++boundary) {
        for(unsigned field=0;field<32;++field) {
            Fixture f;f.prior(boundary);
            check(f.send(boundary,Phase::Before,entry(boundary)).kind==r::Kind::Boundary);
            auto changed=entry(boundary);
            if(field==31)changed.sp+=16;else changed.x[field]^=1;
            check(f.send(boundary,Phase::After,changed).kind==r::Kind::Rejected);
            check(f.scope.expired() && !f.scope.complete());
        }
        for(const auto bits:{std::uint64_t{1},std::uint64_t{0xffffffff},std::uint64_t{0x100000000}}) {
            Fixture f;f.prior(boundary);
            f.send(boundary,Phase::Before,entry(boundary));
            const auto out=f.send(boundary,Phase::After,entry(boundary),bits,EIO);
            check(out.kind==r::Kind::Rejected && out.reason==r::Reason::NativeResult);
            check(out.native_result_available==(bits<=0xffffffff) && out.preserved_errno==EIO);
            check(!f.scope.complete());
        }
        {
            Fixture f;f.prior(boundary);
            check(f.send(boundary,Phase::After,entry(boundary)).kind==r::Kind::Rejected);
        }
        {
            Fixture f;f.prior(boundary);f.send(boundary,Phase::Before,entry(boundary));
            check(!f.scope.complete()); // Missing native After cannot complete.
            f.gate.expire(f.scope);check(f.scope.expired());
        }
    }
    for(unsigned which=0;which<3;++which) {
        for(unsigned fault=0;fault<6;++fault) {
            Fixture f;f.prior(which);auto e=entry(which);
            switch(fault){
            case 0:e.x[0]+=16;break;
            case 1:e.sp+=16;break;
            case 2:e.x[29]+=16;break;
            case 3:if(which==0)e.x[21]+=16;else if(which==1)e.x[20]+=16;else e.x[19]+=16;break;
            case 4:if(which==0)e.x[20]|=1;else if(which==1)e.x[24]=2;else e.x[24]=0;break;
            case 5:if(which==0)e.x[20]=0;else if(which==1)e.x[24]=~std::uint64_t{0};else e.x[20]+=16;break;
            }
            check(f.send(which,Phase::Before,e).kind==r::Kind::Rejected);
        }
    }
    {
        Fixture f;auto unrelated=entry(0);unrelated.x[30]=bias+0x11111;
        check(f.send(0,Phase::Before,unrelated).kind==r::Kind::None);
        f.pair(0);check(f.send(1,Phase::Before,unrelated).kind==r::Kind::None);
        check(f.send(2,Phase::Before,entry(2)).kind==r::Kind::Rejected);
    }
    std::printf("%u register-only native resource census controls passed; no production Ready, private read, accepted resource or frame proof\n",checks);
}
