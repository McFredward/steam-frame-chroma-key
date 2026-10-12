#include "import/client_carrier_core.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>
#include <vector>
using namespace frame_client_carrier;
namespace {
unsigned checks = 0;
void check_at(bool v, unsigned line) { ++checks; if (!v) { std::fprintf(stderr,"carrier check %u failed at line %u\n",checks,line); std::abort(); } }
#define check(v) check_at((v), __LINE__)
constexpr std::uint64_t kToken = 0x8877665544332211ULL;
constexpr std::array<std::uint8_t,12> kRequest{0x34,2,0,0,0x40,0x30,0x20,0x10,9,0,0,0};
constexpr std::array<std::uint8_t,12> kReply{0x35,2,0,0,0x40,0x30,0x20,0x10,9,0,0,0};
constexpr std::array<std::uint8_t,9> kPayload{9,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
struct Fixture {
    Observer o; Scope s; Route r{};
    std::array<std::uint8_t,12> request=kRequest,header=kReply;
    std::array<std::uint8_t,9> payload=kPayload;
    Fixture() {
        r.process_generation=1;r.object_generation=2;r.connection_generation=3;
        r.import_frame=0x10000;r.receiver_frame=r.import_frame+352;r.pipe=0x20000;r.record=0x30000;r.fd=7;
        r.exact_native_route=r.active_invocation_owned=r.continuous_connection_owned=r.immutable_lifetime_lease=true;o.armed=true;
    }
    Event event(std::uint64_t site) const {return {r,site};}
    Span span(std::uint64_t site,std::size_t total,std::size_t offset) const {
        const auto base=site==kWrite?0x40000:site==kHeaderRead?0x50000:0x60000;
        return {event(site),static_cast<std::uint64_t>(base),static_cast<std::uintptr_t>(base),base+offset,total,total-offset,true,true};
    }
    void begin() {check(o.begin({r,kBegin,0},s));}
    void write(std::size_t count) {
        const auto offset=s.record.request_bytes;
        std::array<std::uint8_t,12> owned{};std::size_t n=0;
        if(offset<12) {n=count<12-offset?count:12-offset;for(std::size_t i=0;i<n;++i)owned[i]=request[offset+i];}
        o.request_written(span(kWrite,21,offset),owned,n,count,s);
    }
    void readHeader(std::size_t count) {
        std::array<std::uint8_t,12> owned{};const auto offset=s.record.reply_header_bytes;
        for(std::size_t i=0;i<count;++i)owned[i]=header[offset+i];
        o.reply_header_read(span(kHeaderRead,12,offset),owned,count,count,s);
    }
    void readPayload(std::size_t count) {
        std::array<std::uint8_t,9> owned{};const auto offset=s.record.reply_payload_bytes;
        for(std::size_t i=0;i<count;++i)owned[i]=payload[offset+i];
        o.reply_payload_read(span(kPayloadRead,9,offset),owned,count,count,s);
    }
    void wire() {write(21);readHeader(12);readPayload(9);}
    PendingProof pending() const {return {event(kPending),0,true,true,true,kToken};}
    ReferenceProof reference() const {return {event(kReference),r.receiver_frame-240,kToken,kToken,0};}
    void end() {o.pending(pending(),s);if(s.active)o.reference(reference(),s);}
    Record take() {Record v;check(o.take(v));check(!o.take(v));return v;}
};
void denied(const Record& r) {
    check(!r.reference_succeeded);check(!r.native_cross_endpoint_associated);check(!r.native_namespace_associated);
    check(!r.source_frame_associated);check(!r.uv_owned);check(!r.accepted_submit);check(!r.gpu_consumed);
}
void reject(Fixture& f,std::uint64_t reason) {f.end();const auto r=f.take();check(!r.output_published&&(r.reason&reason));denied(r);}
}
int main() {
    {Fixture f;f.begin();check(!f.s.record.sequence_available);f.wire();check(f.s.record.sequence_available&&f.s.record.sequence==0x10203040);
    f.o.pending(f.pending(),f.s);check(f.s.active&&f.s.record.native_pending&&!f.s.record.output_published);
    Record v;check(!f.o.take(v));f.o.reference(f.reference(),f.s);v=f.take();check(v.output_published&&v.reference_invoked&&!v.reason&&v.reply_token==kToken);denied(v);}
    for(unsigned a=1;a<21;++a)for(unsigned b=1;b<12;++b)for(unsigned c=1;c<9;++c) {
        Fixture f;f.begin();f.write(a);f.write(21-a);f.readHeader(b);f.readHeader(12-b);f.readPayload(c);f.readPayload(9-c);f.end();check(f.take().output_published);
    }
    for(unsigned which=0;which<9;++which) {
        Fixture f;f.begin();f.wire();
        switch(which){case 0:++f.r.process_generation;break;case 1:++f.r.object_generation;break;case 2:++f.r.connection_generation;break;
        case 3:f.r.import_frame+=16;f.r.receiver_frame+=16;break;case 4:++f.r.pipe;break;case 5:f.r.record+=8;break;case 6:++f.r.fd;break;
        case 7:f.r.active_invocation_owned=false;break;default:f.r.immutable_lifetime_lease=false;break;}
        reject(f,which<7?ChangedRoute:InvalidRoute);
    }
    for(unsigned byte=0;byte<12;++byte) {Fixture f;f.begin();f.header[byte]^=1;f.wire();reject(f,BadReply|ParserContamination);}
    for(unsigned byte: {0U,1U,2U,3U,8U,9U,10U,11U}) {Fixture f;f.begin();f.request[byte]^=1;f.wire();reject(f,BadRequest);}
    {Fixture f;f.begin();f.payload[0]=0x0a;f.wire();reject(f,BadReply|ParserContamination);}
    {Fixture f;f.begin();for(unsigned i=1;i<9;++i)f.payload[i]=0;f.wire();reject(f,BadReply);}
    {Fixture f;f.begin();f.write(21);f.header[4]^=1;f.readHeader(12);
    // Native parser can retain a mismatched payload and later return true for a
    // matching packet. The observer permanently refuses this contaminated path.
    f.o.reply_header_read(f.span(kHeaderRead,12,0),kReply,12,12,f.s);
    f.o.reply_payload_read(f.span(kPayloadRead,9,0),kPayload,9,9,f.s);reject(f,ParserContamination);}
    for(unsigned which=0;which<9;++which) {
        Fixture f;f.begin();f.wire();auto p=f.pending();
        switch(which){case 0:p.native_unlock_result=1;break;case 1:p.copied_while_native_lock_owned=false;break;case 2:p.native_wait_success=false;break;
        case 3:p.parsed_token_present=false;break;case 4:p.parsed_token=0;break;case 5:++p.parsed_token;break;
        case 6:p.event.site=kPending+4;break;case 7:p.event.route.continuous_connection_owned=false;break;default:p.event.route.connection_generation++;break;}
        f.o.pending(p,f.s);check(!f.s.active);const auto r=f.take();check(!r.output_published&&r.reason);denied(r);
    }
    for(unsigned which=0;which<7;++which) {
        Fixture f;f.begin();f.wire();f.o.pending(f.pending(),f.s);auto p=f.reference();
        switch(which){case 0:p.ref_frame+=16;break;case 1:p.live_token=0;break;case 2:++p.saved_token;break;case 3:++p.live_token;break;
        case 4:p.native_lock_result=1;break;case 5:p.event.site+=4;break;default:p.event.route.record+=8;break;}
        f.o.reference(p,f.s);const auto r=f.take();check(!r.output_published&&(r.reason&InvalidReference));denied(r);
    }
    for(unsigned stage=0;stage<3;++stage)for(unsigned which=0;which<10;++which) {
        Fixture f;f.begin();if(stage>0)f.write(21);if(stage>1)f.readHeader(12);
        const auto site=stage==0?kWrite:stage==1?kHeaderRead:kPayloadRead;const auto total=stage==2?9U:stage==1?12U:21U;
        auto p=f.span(site,total,0);std::int64_t count=stage==0?21:total;std::size_t copied=stage==2?9:12;
        switch(which){case 0:p.base=0;break;case 1:p.buffer_identity=0;break;case 2:p.current++;break;case 3:p.remaining--;break;case 4:p.total++;break;
        case 5:p.owned_successful_prefix=false;break;case 6:p.immutable_native_span=false;break;case 7:count=0;copied=0;break;case 8:count=-1;copied=0;break;default:copied--;break;}
        if(stage==0)f.o.request_written(p,kRequest,copied,count,f.s);else if(stage==1)f.o.reply_header_read(p,kReply,copied,count,f.s);else f.o.reply_payload_read(p,kPayload,copied,count,f.s);
        reject(f,stage==0?(which==7||which==8?WriteFailed:InvalidSpan|BadRequest):(which>=7?ReadFailed:InvalidSpan));
    }
    for(unsigned stage=0;stage<3;++stage) {
        Fixture f;f.begin();if(stage>0)f.write(21);if(stage>1)f.readHeader(12);
        if(stage==0)f.write(1);
        if(stage==1)f.readHeader(1);
        if(stage==2)f.readPayload(1);
        auto p=f.span(stage==0?kWrite:stage==1?kHeaderRead:kPayloadRead,stage==0?21:stage==1?12:9,1);++p.buffer_identity;
        if(stage==0)f.o.request_written(p,{},11,20,f.s);
        if(stage==1)f.o.reply_header_read(p,{},11,11,f.s);
        if(stage==2)f.o.reply_payload_read(p,{},8,8,f.s);
        reject(f,ChangedSpan);
    }
    {Fixture f;f.begin();f.write(21);f.o.request_written(f.span(kWrite,21,0),kRequest,12,21,f.s);reject(f,RepeatedTransfer);}
    {Fixture f;f.begin();f.readHeader(1);reject(f,MissingRequest);}
    {Fixture f;f.begin();f.write(21);f.readPayload(1);reject(f,MissingReply);}
    {Fixture f;f.begin();f.wire();f.o.pending(f.pending(),f.s);f.o.pending(f.pending(),f.s);check(f.take().reason&RepeatedTransfer);}
    {Fixture f;f.begin();f.wire();f.o.reference(f.reference(),f.s);check(f.take().reason&MissingPending);}
    {Fixture f;f.begin();f.wire();f.o.invalidate(f.s);reject(f,LifetimeInvalidated);}
    {Fixture f;f.begin();check(!f.o.begin({f.r,kBegin,0},f.s));reject(f,NestedImport);}
    {Fixture f;f.begin();f.wire();f.o.armed=false;std::atomic<std::uint64_t> callbacks{0};check(!f.o.quiescent(callbacks));f.end();check(f.take().output_published&&f.o.quiescent(callbacks));}
    {Fixture f;f.begin();f.o.abandon(MissingReply,f.s);check(f.take().reason&Abandoned);}
    {Fixture f;f.o.armed=false;check(!f.o.begin({f.r,kBegin,0},f.s));f.o.armed=true;check(!f.o.begin({f.r,kBegin+4,0},f.s));check(!f.o.counters.claimed);}
    {Fixture f;for(unsigned i=0;i<32;++i){f.begin();f.wire();f.end();}check(!f.o.begin({f.r,kBegin,0},f.s));f.o.pending(f.pending(),f.s);
    check(f.o.counters.claimed==32&&f.o.counters.cap_skipped==1);Record r;unsigned n=0;while(f.o.take(r)){check(r.output_published);++n;}check(n==32);}
    {Observer o;Record r;for(unsigned i=0;i<32;++i)check(o.publish(r));check(!o.publish(r)&&o.counters.queue_loss==1);}
    {Observer o;o.armed=true;std::vector<std::thread> producers;
    for(unsigned i=0;i<4;++i)producers.emplace_back([&,i]{for(unsigned j=0;j<8;++j){Fixture f;f.r.record+=i*8;f.o.armed=false;
    if(!o.begin({f.r,kBegin,0},f.s))std::abort();
    f.o.armed=true;
    // Actual shared observer, with native scalar fixtures independently copied.
    o.request_written(f.span(kWrite,21,0),kRequest,12,21,f.s);o.reply_header_read(f.span(kHeaderRead,12,0),kReply,12,12,f.s);
    o.reply_payload_read(f.span(kPayloadRead,9,0),kPayload,9,9,f.s);o.pending(f.pending(),f.s);o.reference(f.reference(),f.s);}});
    for(auto& t:producers)t.join();
    Record r;std::set<std::uint64_t> seen;while(o.take(r)){check(r.output_published);seen.insert(r.observation);}check(seen.size()==32&&!o.counters.queue_loss&&!o.counters.active);}
    // Large request bodies are only counted, never copied. Sequence zero and
    // maximum remain valid actual wire values without a monotonic heuristic.
    for (const auto sequence : {0U,0xffffffffU}) {
        Fixture f;f.begin();auto request=kRequest,reply=kReply;
        for(unsigned i=0;i<4;++i){request[4+i]=reply[4+i]=static_cast<std::uint8_t>(sequence>>(8*i));}
        request[8]=0xf4;request[9]=1; // 500-byte body, total512.
        f.o.request_written(f.span(kWrite,512,0),request,12,512,f.s);
        f.o.reply_header_read(f.span(kHeaderRead,12,0),reply,12,12,f.s);
        f.o.reply_payload_read(f.span(kPayloadRead,9,0),kPayload,9,9,f.s);f.end();const auto r=f.take();
        check(r.output_published&&r.sequence==sequence&&r.request_bytes==512);
    }
    {Fixture f;f.begin();Observer foreign;foreign.armed=true;check(!foreign.begin({f.r,kBegin,0},f.s));
    f.o.abandon(None,f.s);check(f.take().reason&OwnerMismatch);}
    std::printf("%u client carrier checks passed; copied local wire/publication proof only\n",checks);
}
