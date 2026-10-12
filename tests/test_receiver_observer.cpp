#include "receiver/receiver_observer_core.h"
#include "receiver/sha256.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace frame_receiver;
namespace {
unsigned checks=0;
void check(bool result) { ++checks; if (!result) { std::fprintf(stderr,"receiver check %u failed\n",checks); std::abort(); } }
struct Native {
    alignas(8) std::array<unsigned char,128> selected{};
    alignas(8) std::array<unsigned char,0x4400> metadata{};
    std::uintptr_t m() { return reinterpret_cast<std::uintptr_t>(selected.data()+0x38); }
    std::uintptr_t n() { return reinterpret_cast<std::uintptr_t>(metadata.data()+0x4248); }
    template<typename T> void put(bool meta,std::size_t offset,T value) {
        std::memcpy((meta?metadata.data():selected.data())+offset,&value,sizeof(value));
    }
    Native() {
        put(false,0x20,std::uint64_t{0x9988776655443322}); put(false,0x28,std::uint32_t{65535});
        put(false,0x30,std::uint64_t{0x100000001});
        put(true,8,std::uint32_t{65535}); put(true,12,std::uint8_t{1});
        put(true,0x4278,std::uint32_t{0xfedcba98});
    }
};
Record take(Observer& o) { Record r; check(o.take(r)); check(!o.take(r)); return r; }
void select(Observer& o,Scope& s,Native& n) { o.after_lock(kSelectedLock,n.m(),0,s); }
void metadata(Observer& o,Scope& s,Native& n) {
    o.after_lock(kMetadataLock,n.n(),0,s); o.before_unlock(kMetadataUnlock,n.n(),s);
}
void end(Observer& o,Scope& s,Native& n) { o.before_unlock(kSelectedUnlock,n.m(),s); }
std::string hash(const char* value,std::size_t length) {
    Sha256 sha; sha.update(value,length); const auto result=sha.finish(); std::string text;
    for (auto b:result) { constexpr char digits[]="0123456789abcdef"; text+=digits[b>>4]; text+=digits[b&15]; }
    return text;
}
} // namespace
int main() {
    check(hash("",0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check(hash("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",56)==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    check(!Observer::selected_address(0x10)); check(!Observer::selected_address(0x19));
    check(!Observer::selected_address(UINTPTR_MAX)); check(!Observer::metadata_address(0x4240-8));
    check(!Observer::metadata_address((std::uintptr_t{1}<<48)-8));
    {
        Observer o; o.armed=true; Scope s; Record r;
        // Historical return addresses must not read any record, including an
        // invalid pointer, after the current receiver changes its native sites.
        for (const auto old : {0x34a50ULL, 0x34d74ULL, 0x5200cULL, 0x5203cULL}) {
            o.after_lock(old, UINTPTR_MAX, 0, s);
            o.before_unlock(old, UINTPTR_MAX, s);
        }
        check(!s.active && !o.counters.claimed.load() && !o.counters.unmatched.load());
        check(!o.take(r));
    }
    {
        Observer o; Scope s; Native n;
        select(o,s,n); check(!s.active); check(!o.counters.claimed.load());
        o.armed=true;
        o.after_lock(0,UINTPTR_MAX,0,s); o.before_unlock(0,UINTPTR_MAX,s); // Never dereference unrelated calls.
        metadata(o,s,n); check(!o.counters.unmatched.load()); // Ordinary metadata is passthrough.
        select(o,s,n); metadata(o,s,n); end(o,s,n);
        const auto r=take(o); check(r.metadata_valid && !r.reason && r.metadata_pairs==1);
        check(r.selected.key==65535 && r.selected.content_generation==0x100000001 && r.metadata_word==0xfedcba98);
        check(!s.active && o.counters.valid==1 && o.counters.active==0);
    }
    for (unsigned which=0;which<10;++which) {
        Observer o; o.armed=true; Scope s; Native n; select(o,s,n);
        std::uint32_t reason=0;
        switch(which) {
        case 0: reason=MissingMetadata; break;
        case 1: n.put(true,12,std::uint8_t{0}); metadata(o,s,n); reason=InvalidMetadata; break;
        case 2: n.put(true,8,std::uint32_t{1}); metadata(o,s,n); reason=KeyMismatch; break;
        case 3: metadata(o,s,n); n.put(false,0x30,std::uint64_t{0x100000002}); reason=ChangedTuple; break;
        case 4: metadata(o,s,n); metadata(o,s,n); reason=MultipleMetadata; break;
        case 5: o.after_lock(kMetadataLock,n.n(),0,s); reason=IncompleteMetadata; break;
        case 6: o.before_unlock(kMetadataUnlock,n.n(),s); reason=UnmatchedUnlock; break;
        case 7: o.after_lock(kMetadataLock,n.n(),99,s); reason=LockFailure; break;
        case 8: o.after_lock(kMetadataLock,n.n(),0,s); o.before_unlock(kMetadataUnlock,n.n()+8,s); reason=UnexpectedMutex; break;
        default: o.after_lock(kMetadataLock,UINTPTR_MAX,0,s); o.before_unlock(kMetadataUnlock,UINTPTR_MAX,s); reason=InvalidPointer; break;
        }
        end(o,s,n); const auto r=take(o); check((r.reason&reason)!=0 && !r.metadata_valid);
        check(o.counters.invalid==1 && !o.counters.active.load());
    }
    {
        Observer o; o.armed=true; Scope s; Native n;
        o.after_lock(kSelectedLock,UINTPTR_MAX,35,s); check(!s.active && o.counters.lock_failures==1);
        select(o,s,n); select(o,s,n); metadata(o,s,n); end(o,s,n); check(s.active); end(o,s,n);
        check(take(o).reason&NestedSelection);
    }
    {
        Observer o; o.armed=true; Scope s; Native n; select(o,s,n);
        o.after_lock(kMetadataLock,n.n(),0,s); o.after_lock(kMetadataLock,n.n(),0,s);
        o.before_unlock(kMetadataUnlock,n.n(),s); o.before_unlock(kMetadataUnlock,n.n(),s); end(o,s,n);
        check(take(o).reason&NestedMetadata);
    }
    {
        Observer o; o.armed=true; Scope s; Native n; select(o,s,n); o.armed=false;
        std::atomic<std::uint64_t> callbacks{0};
        const bool latched=o.quiescent(callbacks); check(!latched);
        metadata(o,s,n); end(o,s,n); check(take(o).metadata_valid); // In-flight scopes close after admission ends.
        check(!latched && o.quiescent(callbacks)); // Completion cannot retroactively un-latch a timed-out closure.
        callbacks=1; check(!o.quiescent(callbacks)); callbacks=0; check(o.quiescent(callbacks));
    }
    {
        Observer o; o.armed=true; Scope s; Native n;
        for (unsigned i=0;i<33;++i) { select(o,s,n); metadata(o,s,n); end(o,s,n); }
        check(o.counters.completed==32 && o.counters.cap_skipped==1 && !o.counters.unmatched.load());
        unsigned count=0; Record r; while(o.take(r)) {check(r.metadata_valid); ++count;} check(count==32);
    }
    {
        Observer o; Record r;
        for (unsigned i=0;i<32;++i) {r.observation=i; check(o.publish(r));}
        check(!o.publish(r) && o.counters.queue_loss==1);
        std::set<std::uint64_t> seen; while(o.take(r)) seen.insert(r.observation); check(seen.size()==32);
        check(o.publish(r) && o.take(r));
    }
    {
        Observer o; o.armed=true;
        std::vector<std::thread> producers;
        for (unsigned i=0;i<4;++i) producers.emplace_back([&] {
            Native n; Scope s;
            for (unsigned j=0;j<8;++j) {select(o,s,n); metadata(o,s,n); end(o,s,n);}
        });
        for (auto& t:producers) t.join();
        std::set<std::uint64_t> seen; Record r; while(o.take(r)) {check(r.metadata_valid); seen.insert(r.observation);}
        check(seen.size()==32 && !o.counters.queue_loss.load() && o.counters.completed==32);
    }
    std::printf("receiver observer: %u checks passed; offline scope/publication evidence only\n",checks);
}
