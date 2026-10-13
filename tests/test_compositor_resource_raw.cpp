#include "import/compositor_resource_raw.h"
#include "import/compositor_resource_sparse.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {
unsigned checks = 0;
thread_local bool forbid_allocations = false;
std::atomic<unsigned> allocations{0};
void check_at(bool ok, unsigned line) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "compositor raw check %u failed at line %u\n", checks, line); std::abort(); }
}
#define check(value) check_at((value), __LINE__)
}
void* operator new(std::size_t size) {
    if (forbid_allocations) allocations.fetch_add(1);
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace frame_compositor_raw;
constexpr std::uintptr_t base = 0x100000, bias = 0x500000, pipe = 0x700000;
constexpr std::uintptr_t dispatch = base + 1232, marker = 0x900000;
constexpr std::uint32_t sequence = 0xffffffff;
constexpr std::uint64_t journal_device = 7, native_device = 0xabc0;
constexpr std::uint64_t token = 0x8877665544332211ULL;
constexpr Boundary stages[]{Boundary::Begin,Boundary::EarlyUnlock,Boundary::Image,Boundary::Bind,
    Boundary::View,Boundary::SendLock,Boundary::Write,Boundary::Finish};
struct Fixture {
    Observer observer{};
    Scope scope{};
    std::array<std::uint8_t,1552> stack{};
    std::array<std::uint8_t,21> packet{};
    template<class T> void put(std::uintptr_t at, T value) {
        check(at >= base && at - base <= stack.size() - sizeof(T));
        std::memcpy(stack.data() + at - base, &value, sizeof(T));
    }
    void link(std::uintptr_t fp, std::uintptr_t parent, std::uint64_t rva) {
        put(fp,std::uint64_t{parent}); put(fp+8,std::uint64_t{bias+rva});
    }
    Fixture() {
        put(dispatch+108,sequence);
        for (unsigned b=0;b<4;++b) {
            packet[b]=0x235U>>(8*b); packet[b+4]=sequence>>(8*b); packet[b+8]=9U>>(8*b);
        }
        packet[12]=9;
        for (unsigned b=0;b<8;++b) packet[b+13]=token>>(8*b);
    }
    Entry entry(Boundary b, unsigned offset=0) {
        Entry e{};
        constexpr std::uint64_t sites[]{0x153fc0,0x1540b0,0x2c75ac,0x2c7684,0x2c7804,0x172ea0,0x172c00,0x172fbc};
        e.x[30]=bias+sites[static_cast<unsigned>(b)];
        if (b==Boundary::Begin || b==Boundary::EarlyUnlock) {
            e.sp=e.x[29]=dispatch; e.x[0]=e.x[19]=pipe; e.x[25]=1;
            put(dispatch+128,std::uint64_t{pipe});
        } else if (b==Boundary::Image || b==Boundary::Bind || b==Boundary::View) {
            e.sp=base; e.x[29]=base+96; e.x[0]=native_device;
            link(base+96,base+752,0x70bc8); link(base+752,base+912,0x13bbdc);
            link(base+912,base+1008,0x13beb0); link(base+1008,dispatch,0x154194);
            put(base+152,std::uint64_t{0xdeadbeef00000000}); put(base+1024,std::uint64_t{pipe});
            if (b==Boundary::Bind) { e.x[1]=10; e.x[2]=20; e.x[3]=16; }
        } else {
            const auto send=dispatch-80;
            e.sp=e.x[29]=send; link(send,dispatch,0x1542b0); put(send+64,std::uint64_t{pipe});
            if (b==Boundary::SendLock) {
                e.x[0]=e.x[19]=pipe; e.x[20]=9; e.x[21]=0x235; e.x[22]=sequence;
            } else if (b==Boundary::Write) {
                e.sp=e.x[29]=send-112; link(send-112,send,0x172f34);
                e.x[0]=7; e.x[19]=offset; e.x[20]=21; e.x[22]=pipe;
                e.x[23]=send-32; e.x[1]=send-32+offset; e.x[2]=21-offset;
            } else { e.sp=send-32; e.x[0]=pipe; e.x[20]=1; }
        }
        return e;
    }
    Event bare(Boundary b, unsigned offset=0) {
        Event e{}; e.boundary=b; e.entry=entry(b,offset); e.thread_identity=marker; e.load_bias=bias;
        e.current_thread_owned=e.native_module_current=e.native_provider_current=e.source_current=true;
        return e;
    }
    Event decoded(Event e, std::size_t count) {
        e.scalars=frame_import_native::decode(e.boundary,e.entry,{stack.data(),base,stack.size()},bias,scope.expected());
        e.decoded_available=true; e.scalar_bytes_copied=count; return e;
    }
    static std::size_t budget(Boundary b) {
        switch(b) {
        case Boundary::Begin:case Boundary::EarlyUnlock:return 12;
        case Boundary::Image:case Boundary::Bind:case Boundary::View:return 80;
        case Boundary::SendLock:case Boundary::Finish:return 28;
        case Boundary::Write:return 44;
        default:return 0;
        }
    }
    static bool before_copy(Boundary b) {
        return b==Boundary::EarlyUnlock || b==Boundary::Image || b==Boundary::Bind || b==Boundary::View || b==Boundary::Finish;
    }
    Event before(Boundary b, unsigned offset=0) {
        auto e=bare(b,offset);
        check(observer.can_acquire(e,scope)==before_copy(b));
        return before_copy(b)?decoded(e,budget(b)):e;
    }
    Event after(Event e, std::int64_t result) {
        e.phase=Phase::After; e.native_result_available=true; e.native_result=result; e.preserved_errno=EBUSY;
        e.scalar_bytes_copied=0;
        if (!before_copy(e.boundary) && result>=0 && (e.boundary!=Boundary::Write || result>0)) {
            e=decoded(e,budget(e.boundary));
            if(e.boundary==Boundary::Write && result<=static_cast<std::int64_t>(e.entry.x[2])) {
                e.prefix.count=result; e.prefix.successful_prefix_owned=true;
                std::memcpy(e.prefix.bytes.data(),packet.data()+e.entry.x[19],e.prefix.count);
            }
        }
        if(e.boundary==Boundary::Image) {
            e.public_result_available=true; e.image={journal_device,10,123,37,1152,4608,1,1,static_cast<int>(result),true,native_device};
        } else if(e.boundary==Boundary::Bind) {
            e.public_result_available=true; e.bind={journal_device,10,20,16,static_cast<int>(result),native_device};
        } else if(e.boundary==Boundary::View) {
            e.public_result_available=true; e.view={journal_device,10,30,37,static_cast<int>(result),native_device};
        }
        return e;
    }
    Outcome pair(Boundary b, unsigned offset=0, std::int64_t result=0) {
        auto e=before(b,offset);
        check(observer.observe(e,scope).kind==Kind::None);
        auto probe=e; probe.phase=Phase::After; probe.native_result=result; probe.native_result_available=true;
        probe.scalar_bytes_copied=0; probe.decoded_available=false;
        const bool acquisition=!before_copy(b) && (result==0 || (b==Boundary::Write && result>0));
        check(observer.can_acquire(probe,scope)==acquisition);
        return observer.observe(after(e,result),scope);
    }
    void through(Boundary last) {
        for(auto b:stages) {
            if(b==Boundary::Write || b==Boundary::Finish) break;
            check(pair(b).kind==Kind::None);
            if(b==last) break;
        }
    }
    void writes(unsigned chunk=21) {
        for(unsigned at=0;at<21;) { const auto n=std::min(chunk,21-at); check(pair(Boundary::Write,at,n).kind==Kind::None);at+=n; }
    }
    Outcome end() { return pair(Boundary::Finish); }
};
void no_proof(const RawRecord& r) {
    check(r.raw_only && !r.request_header_observed && !r.continuous_connection_owned && !r.immutable_lifetime_lease);
    check(!r.native_cross_endpoint_associated && !r.native_namespace_associated && !r.source_frame_associated);
    check(!r.uv_owned && !r.accepted_submit && !r.gpu_consumed);
}
void matched(Fixture& f, const Outcome& result) {
    check(result.kind==Kind::Matched && result.record.matched && result.record.terminal && result.record.reason==0);
    check(f.scope.finished() && !f.scope.active());no_proof(result.record);
    const auto& r=result.record;
    check(r.invocation.dispatch_frame==dispatch && r.invocation.pipe==pipe && r.invocation.sequence==sequence);
    check(r.client_available && r.client==0 && r.fd==7 && r.reply_token==token && r.reply==f.packet);
    check(r.begin_lock_succeeded && r.early_unlock_succeeded && r.native_payload_read);
    check(r.image_succeeded && r.bind_succeeded && r.view_succeeded && r.send_lock_succeeded);
    check(r.finish_unlock_succeeded && r.native_send_success && r.reply_complete && r.reply_bytes==21);
    check(r.scalar_bytes_copied==320+44*r.writes && r.scalar_bytes_copied<=672 && r.wire_bytes_copied==21);
    check(r.image.device==journal_device && r.image.native_device==native_device && journal_device!=native_device);
    check(f.observer.abandon(f.scope).kind==Kind::None && f.observer.observer_loss(f.scope).kind==Kind::None);
    check(f.observer.observe(f.bare(Boundary::Begin),f.scope).kind==Kind::None);
}
void refused(Fixture& f, const Outcome& out, std::uint64_t expected=0) {
    check(out.kind==Kind::Refused && out.record.terminal && !out.record.matched);
    check(!expected || (out.record.reason&expected)==expected);no_proof(out.record);
    check(f.scope.finished() && f.observer.abandon(f.scope).kind==Kind::None);
}
void complete_partitions(unsigned left, unsigned at, unsigned count, std::array<unsigned,8>& parts, unsigned& total) {
    if(count==8) return;
    for(unsigned n=1;n<=left;++n) {
        parts[count]=n;
        if(n!=left) { complete_partitions(left-n,at+n,count+1,parts,total);continue; }
        Fixture f;f.through(Boundary::SendLock);unsigned offset=0;
        for(unsigned i=0;i<=count;++i) {check(f.pair(Boundary::Write,offset,parts[i]).kind==Kind::None);offset+=parts[i];}
        matched(f,f.end());++total;
    }
}
void admission_controls() {
    for(auto b:stages) {
        Fixture f;
        if(b!=Boundary::Begin)f.through(stages[static_cast<unsigned>(b)-1]);
        if(b==Boundary::Finish)f.writes();
        auto e=f.bare(b);
        const auto copies_before=Fixture::before_copy(b);
        if(!copies_before) {
            check(f.observer.observe(e,f.scope).kind==Kind::None);
            e.phase=Phase::After;e.native_result_available=true;e.native_result=b==Boundary::Write?21:0;
        }
        check(f.observer.can_acquire(e,f.scope));
        for(unsigned mutation=0;mutation<14;++mutation) {
            auto bad=e;
            switch(mutation) {
            case 0:bad.current_thread_owned=false;break;
            case 1:bad.thread_identity+=16;break;
            case 2:bad.thread_reason=9;break;
            case 3:bad.native_module_current=false;break;
            case 4:bad.native_provider_current=false;break;
            case 5:bad.source_current=false;break;
            case 6:bad.load_bias+=4096;break;
            case 7:bad.entry.x[30]+=4;break;
            case 8:bad.entry.x[29]+=16;break;
            case 9:bad.entry.sp+=16;break;
            case 10:bad.scalar_bytes_copied=1;break;
            case 11:bad.decoded_available=true;break;
            case 12:bad.prefix.count=1;break;
            case 13:bad.public_result_available=true;break;
            }
            check(!f.observer.can_acquire(bad,f.scope));
        }
    }
}
void lifecycle_controls() {
    for(unsigned m=0;m<7;++m) {
        Fixture f; f.through(Boundary::Begin);auto e=f.before(Boundary::EarlyUnlock);
        switch(m) {
        case 0:e.current_thread_owned=false;break;
        case 1:e.native_module_current=false;break;
        case 2:e.native_provider_current=false;break;
        case 3:e.source_current=false;break;
        case 4:e.thread_identity+=16;break;
        case 5:e.load_bias+=4096;break;
        case 6:e.thread_reason=9;break;
        }
        refused(f,f.observer.observe(e,f.scope));check(f.scope.record().scalar_bytes_copied==24);
    }
    {Fixture f;f.through(Boundary::Begin);Observer other;refused(f,other.observe(f.before(Boundary::EarlyUnlock),f.scope),OwnerMismatch);}
    {Fixture f;f.through(Boundary::Begin);refused(f,f.observer.observe(f.bare(Boundary::Begin),f.scope),NestedImport);}
    {Fixture f;f.through(Boundary::Begin);refused(f,f.observer.observer_loss(f.scope),ObserverLoss);}
    {Fixture f;f.through(Boundary::Begin);refused(f,f.observer.expire(9,f.scope),ThreadUnavailable);check(f.scope.record().thread_reason==9);}
    {Fixture f;refused(f,f.observer.abandon(f.scope),Abandoned);}
    {Fixture f;auto e=f.bare(Boundary::Begin);e.boundary=Boundary::AcceptedInit;refused(f,f.observer.observe(e,f.scope),UnsupportedBoundary);}
    {Fixture f;auto e=f.bare(Boundary::Begin);e.phase=static_cast<Phase>(44);refused(f,f.observer.observe(e,f.scope),Order);}
    {Fixture f;refused(f,f.observer.observe(f.bare(Boundary::Write),f.scope));}
}
void paired_controls() {
    for(auto b:stages) {
        Fixture f;if(b!=Boundary::Begin)f.through(stages[static_cast<unsigned>(b)-1]);if(b==Boundary::Finish)f.writes();
        auto e=f.before(b);check(f.observer.observe(e,f.scope).kind==Kind::None);
        auto after=f.after(e,b==Boundary::Write?21:0);after.entry.x[17]^=1;
        refused(f,f.observer.observe(after,f.scope),ChangedEntry);
    }
    for(auto b:stages) {
        Fixture f;if(b!=Boundary::Begin)f.through(stages[static_cast<unsigned>(b)-1]);if(b==Boundary::Finish)f.writes();
        auto e=f.before(b);check(f.observer.observe(e,f.scope).kind==Kind::None);
        auto after=f.after(e,-1);after.scalar_bytes_copied=0;after.prefix={};
        refused(f,f.observer.observe(after,f.scope),NativeFailure);
    }
    {Fixture f;f.through(Boundary::Begin);auto e=f.before(Boundary::EarlyUnlock);check(f.observer.observe(e,f.scope).kind==Kind::None);
      auto after=f.after(e,0);after.scalars.native_payload_read=false;refused(f,f.observer.observe(after,f.scope),ChangedSnapshot);}
    {Fixture f;f.through(Boundary::SendLock);f.writes();auto e=f.before(Boundary::Finish);e.scalars.native_send_success=false;
      check(f.observer.observe(e,f.scope).kind==Kind::None);refused(f,f.observer.observe(f.after(e,0),f.scope),NativeFailure);}
    {Fixture f;f.through(Boundary::Begin);auto e=f.bare(Boundary::EarlyUnlock);e.entry.x[25]=0;e=f.decoded(e,12);
      check(f.observer.observe(e,f.scope).kind==Kind::None);refused(f,f.observer.observe(f.after(e,0),f.scope),MissingPayload);}
}
void metadata_controls() {
    for(auto b:{Boundary::Image,Boundary::Bind,Boundary::View})for(unsigned m=0;m<9;++m) {
        Fixture f;f.through(stages[static_cast<unsigned>(b)-1]);auto e=f.before(b);check(f.observer.observe(e,f.scope).kind==Kind::None);
        auto a=f.after(e,0);
        if(m==0)a.public_result_available=false;
        else if(b==Boundary::Image) {
            switch(m){case 1:a.image.native_device++;break;case 2:a.image.device=0;break;case 3:a.image.image=0;break;
            case 4:a.image.format=0;break;case 5:a.image.width=0;break;case 6:a.image.height=0;break;case 7:a.image.layers=0;break;case 8:a.image.result=-1;break;}
        }else if(b==Boundary::Bind){
            switch(m){case 1:a.bind.native_device++;break;case 2:a.bind.device++;break;case 3:a.bind.image++;break;
            case 4:a.bind.memory++;break;case 5:a.bind.offset++;break;case 6:a.bind.memory=0;break;case 7:a.bind.image=0;break;case 8:a.bind.result=-1;break;}
        }else{
            switch(m){case 1:a.view.native_device++;break;case 2:a.view.device++;break;case 3:a.view.image++;break;
            case 4:a.view.format++;break;case 5:a.view.view=0;break;case 6:a.view.image=0;break;case 7:a.view.native_device=0;break;case 8:a.view.result=-1;break;}
        }
        refused(f,f.observer.observe(a,f.scope),PublicMetadata);
    }
    for(auto b:{Boundary::Bind,Boundary::View}) {
        Fixture f;f.through(stages[static_cast<unsigned>(b)-1]);auto e=f.before(b);e.scalars.client++;
        refused(f,f.observer.observe(e,f.scope),ChangedClient);
    }
}
void write_controls() {
    for(unsigned m=0;m<15;++m) {
        Fixture f;f.through(Boundary::SendLock);auto e=f.before(Boundary::Write);check(f.observer.observe(e,f.scope).kind==Kind::None);
        auto a=f.after(e,21);
        switch(m){
        case 0:a.scalars.invocation.sequence--;break;case 1:a.scalars.invocation.pipe+=16;break;
        case 2:a.scalars.invocation.dispatch_frame+=16;break;case 3:a.scalars.fd++;break;
        case 4:a.scalars.packet_base+=16;break;case 5:a.scalars.packet_offset++;break;
        case 6:a.scalars.remaining--;break;case 7:a.prefix.successful_prefix_owned=false;break;
        case 8:a.prefix.count--;break;case 9:a.native_result=22;break;
        case 10:a.scalar_bytes_copied=43;break;case 11:a.decoded_available=false;break;
        case 12:a.scalars.refusal=frame_import_native::Refusal::Arguments;break;
        case 13:a.native_result_available=false;break;case 14:a.public_result_available=true;break;
        }
        refused(f,f.observer.observe(a,f.scope));
    }
    for(unsigned p:{0U,4U,8U,12U,13U}) {
        Fixture f;f.through(Boundary::SendLock);if(p==13)std::fill(f.packet.begin()+13,f.packet.end(),0);else f.packet[p]^=1;
        refused(f,f.pair(Boundary::Write,0,21),BadReply);
    }
    {Fixture f;f.through(Boundary::SendLock);check(f.pair(Boundary::Write,0,1).kind==Kind::None);
      auto e=f.bare(Boundary::Write,1);e.entry.x[0]++;check(!f.observer.can_acquire(e,f.scope));refused(f,f.observer.observe(e,f.scope),InvalidEntry);}
    {Fixture f;f.through(Boundary::SendLock);for(unsigned n=0;n<8;++n)check(f.pair(Boundary::Write,n,1).kind==Kind::None);
      auto e=f.bare(Boundary::Write,8);check(!f.observer.can_acquire(e,f.scope));refused(f,f.observer.observe(e,f.scope),TransferLimit);}
    {Fixture f;f.through(Boundary::SendLock);check(f.pair(Boundary::Write,0,20).kind==Kind::None);
      auto e=f.bare(Boundary::Finish);check(!f.observer.can_acquire(e,f.scope));refused(f,f.observer.observe(e,f.scope),Incomplete);}
    {Fixture f;f.through(Boundary::SendLock);auto e=f.before(Boundary::Write);check(f.observer.observe(e,f.scope).kind==Kind::None);
      auto a=f.after(e,21);a.scalar_bytes_copied=9;a.decoded_available=false;a.prefix.count=3;
      const auto prior=f.scope.record().scalar_bytes_copied;refused(f,f.observer.observe(a,f.scope));
      check(f.scope.record().scalar_bytes_copied==prior+9 && f.scope.record().wire_bytes_copied==3);}
    {Fixture f;f.through(Boundary::SendLock);auto e=f.before(Boundary::Write);check(f.observer.observe(e,f.scope).kind==Kind::None);
      auto a=f.after(e,21);a.scalar_bytes_copied=std::numeric_limits<std::size_t>::max();
      refused(f,f.observer.observe(a,f.scope),AcquisitionBudget);check(f.scope.record().scalar_bytes_copied==std::numeric_limits<std::size_t>::max());}
}

struct SparseReader {
    Fixture& fixture;
    std::size_t actual_bytes = 0;
    unsigned calls = 0, fail_at = 0;
    bool admitted = false, fail_after_copy = false;
    static bool read(std::uintptr_t at, void* output, std::size_t size, void* opaque) noexcept {
        auto& self = *static_cast<SparseReader*>(opaque);
        check(self.admitted && at>=base && at-base<=self.fixture.stack.size() &&
            size<=self.fixture.stack.size()-(at-base));
        ++self.calls;
        if(self.calls==self.fail_at && !self.fail_after_copy)return false;
        std::memcpy(output,self.fixture.stack.data()+at-base,size);self.actual_bytes+=size;
        return self.calls!=self.fail_at;
    }
    Event copy(Event e) {
        admitted=fixture.observer.can_acquire(e,fixture.scope);check(admitted);
        const auto before=actual_bytes;
        const auto result=frame_compositor_sparse::decode(e.boundary,e.entry,e.load_bias,
            fixture.scope.expected(),read,this);
        e.scalar_bytes_copied=actual_bytes-before;e.decoded_available=result.valid();e.scalars=result.scalars;
        if(result.valid())check(result.bytes_requested==result.bytes_confirmed && result.bytes_confirmed==e.scalar_bytes_copied);
        admitted=false;return e;
    }
};
void sparse_adapter_controls() {
    for(unsigned chunk:{21U,3U}) {
        Fixture f;SparseReader reader{f};
        auto run=[&](Boundary b,unsigned offset=0,std::int64_t result=0) {
            auto before=f.bare(b,offset);
            if(Fixture::before_copy(b))before=reader.copy(before);
            check(f.observer.observe(before,f.scope).kind==Kind::None);
            auto after=f.after(before,result);
            after.scalar_bytes_copied=0;after.prefix={};
            if(!Fixture::before_copy(b)) {
                after.decoded_available=false;after.public_result_available=false;after=reader.copy(after);
                if(b==Boundary::Write) {
                    check(reader.admitted==false && result>0 && result<=static_cast<std::int64_t>(before.entry.x[2]));
                    after.prefix.count=result;after.prefix.successful_prefix_owned=true;
                    std::memcpy(after.prefix.bytes.data(),f.packet.data()+offset,result);
                }
            } else {
                check(!f.observer.can_acquire(after,f.scope));
            }
            return f.observer.observe(after,f.scope);
        };
        for(auto b:stages){if(b==Boundary::Write)break;check(run(b).kind==Kind::None);}
        for(unsigned at=0;at<21;){auto n=std::min(chunk,21-at);check(run(Boundary::Write,at,n).kind==Kind::None);at+=n;}
        auto out=run(Boundary::Finish);matched(f,out);
        check(reader.actual_bytes==out.record.scalar_bytes_copied && reader.actual_bytes==320+44*out.record.writes);
    }
    // A reader that actually copies before its ownership postcheck fails must
    // report that real count; bytes_confirmed alone would silently undercount.
    for(bool postcheck:{false,true}) {
        Fixture f;SparseReader reader{f};auto e=f.bare(Boundary::Begin);check(f.observer.observe(e,f.scope).kind==Kind::None);
        e.phase=Phase::After;e.native_result_available=true;e.native_result=0;
        reader.fail_at=2;reader.fail_after_copy=postcheck;e=reader.copy(e);
        check(!e.decoded_available && reader.actual_bytes==(postcheck?12U:4U));
        refused(f,f.observer.observe(e,f.scope));check(f.scope.record().scalar_bytes_copied==reader.actual_bytes);
    }
    {Fixture f;SparseReader reader{f};auto e=f.bare(Boundary::Begin);check(f.observer.observe(e,f.scope).kind==Kind::None);
      e.phase=Phase::After;e.native_result_available=true;e.native_result=0;e.native_provider_current=false;
      check(!f.observer.can_acquire(e,f.scope));refused(f,f.observer.observe(e,f.scope),ProviderUnavailable);
      check(reader.calls==0 && reader.actual_bytes==0);}
}

}
int main() {
    forbid_allocations=true;
    {Fixture f;f.through(Boundary::SendLock);f.writes();matched(f,f.end());}
    admission_controls();lifecycle_controls();paired_controls();metadata_controls();write_controls();sparse_adapter_controls();
    unsigned partition_count=0;std::array<unsigned,8> parts{};complete_partitions(21,0,0,parts,partition_count);
    check(partition_count==137980);check(allocations.load()==0);
    forbid_allocations=false;
    std::printf("%u raw compositor checks passed; %u reply partitions, no allocations or namespace/frame/GPU proof\n",checks,partition_count);
}
