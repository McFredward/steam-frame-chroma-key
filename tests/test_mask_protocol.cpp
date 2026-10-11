#include "../mask/mask_protocol.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

using namespace mr::mask;
namespace {

unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

FrameIdentity identity(std::uint64_t frame = 1, std::uint8_t eye = 0,
                       std::uint16_t width = 80, std::uint16_t height = 50) {
    FrameIdentity id;
    for (unsigned i = 0; i < id.session.size(); ++i) id.session[i] = i + 1;
    id.frame = frame; id.eye = eye; id.width = width; id.height = height;
    return id;
}

std::vector<std::uint8_t> pattern(const FrameIdentity& id) {
    std::vector<std::uint8_t> v(std::size_t(id.width) * id.height);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::uint8_t>((i * 97 + id.eye * 63) & 255);
    return v;
}

void put(std::uint8_t* p, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
        p[size - i - 1] = static_cast<std::uint8_t>(value); value >>= 8;
    }
}

// Re-sign altered packets to test validation beyond accidental packet damage.
void sign(std::vector<std::uint8_t>& p, bool entireFrame = false) {
    if (entireFrame)
        put(p.data() + 48, crc32(p.data() + kHeaderBytes,
                                p.size() - kHeaderBytes), 4);
    std::vector<std::uint8_t> checksum(p.begin(), p.begin() + 52);
    checksum.insert(checksum.end(), p.begin() + kHeaderBytes, p.end());
    put(p.data() + 52, crc32(checksum.data(), checksum.size()), 4);
}

void ingestAll(Receiver& r, const std::vector<std::vector<std::uint8_t>>& packets,
               std::uint64_t now = 0) {
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto result = r.ingest(packets[i], now);
        require(result == (i + 1 == packets.size() ? IngestResult::Completed :
                                                    IngestResult::Accepted),
                "clean complete frame must assemble");
    }
}

void wireAndReorder() {
    const auto* sample = reinterpret_cast<const std::uint8_t*>("123456789");
    require(crc32(sample, 9) == 0xcbf43926u, "standard CRC known vector");
    const auto tiny = identity(0x0102030405060708ull, 1, 2, 2);
    auto tinyPackets = encode(tiny, {0, 64, 128, 255}, Encoding::Raw);
    const std::vector<std::uint8_t> expected = {
        77,82,65,49,1,0,1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        1,2,3,4,5,6,7,8,0,2,0,2,0,0,0,4,0,0,0,0,0,4,0,0,
        0x47,0x5f,0x25,0x1a,0x2f,0x92,0x00,0xe4,0,64,128,255};
    // CRC bytes were calculated independently with Python's zlib.crc32.
    require(tinyPackets[0] == expected, "complete independently fixed wire vector");
    const auto id = identity(); const auto bytes = pattern(id);
    auto packets = encode(id, bytes, Encoding::Raw);
    require(packets.size() == 4, "raw mask must be fragmented");
    for (const auto& p : packets) require(p.size() <= 1200, "UDP MTU cap");
    Receiver receiver; receiver.beginSession(id.session);
    require(!receiver.takeExact(id, 0), "color before mask must remain opaque");
    require(receiver.ingest(packets[2], 1) == IngestResult::Accepted, "reorder accepted");
    require(receiver.ingest(packets[2], 2) == IngestResult::Duplicate, "duplicate ignored");
    require(receiver.ingest(packets[0], 3) == IngestResult::Accepted, "first accepted late");
    require(receiver.ingest(packets[3], 4) == IngestResult::Accepted, "last before middle");
    require(!receiver.takeExact(id, 5), "packet loss may not reveal partial mask");
    require(receiver.ingest(packets[1], 6) == IngestResult::Completed, "reorder completes");
    auto wrong = id; ++wrong.frame;
    require(!receiver.takeExact(wrong, 7), "newer color cannot use older mask");
    wrong.frame -= 2;
    require(!receiver.takeExact(wrong, 8), "older color cannot use newer mask");
    wrong = id; ++wrong.width;
    require(!receiver.takeExact(wrong, 9), "resolution must match");
    auto got = receiver.takeExact(id, 10);
    require(got && *got == bytes, "exact coverage survives fragmentation");
    require(receiver.bufferedBytes() == 0, "consumption releases buffer");
    require(receiver.stats().droppedFrames == 0, "consumption is not a loss statistic");
    require(receiver.ingest(packets[0], 11) == IngestResult::Blocked,
            "consumed frame cannot be recreated by retransmission");
}

void sessionsStereoAndExpiry() {
    const auto left = identity(); const auto right = identity(1, 1);
    const auto l = pattern(left); const auto r = pattern(right);
    Receiver receiver; receiver.beginSession(left.session);
    ingestAll(receiver, encode(right, r), 10);
    require(!receiver.takeExact(left, 11), "stereo eyes never cross-fallback");
    ingestAll(receiver, encode(left, l), 12);
    require(*receiver.takeExact(right, 13) == r, "right eye independent");
    require(*receiver.takeExact(left, 14) == l, "left eye independent");
    auto oldPackets = encode(identity(2), l);
    require(receiver.ingest(oldPackets[0], 20) == IngestResult::Accepted,
            "new frame begins");
    receiver.expire(770);
    require(receiver.assemblyCount() == 0 && receiver.bufferedBytes() == 0,
            "dropped partial frame expires");
    require(receiver.ingest(oldPackets[0], 771) == IngestResult::Blocked,
            "late packet cannot resurrect expired assembly");
    require(receiver.ingest(oldPackets[0], 1800) == IngestResult::Blocked,
            "expired retirement history retains replay floor");
    ingestAll(receiver, encode(identity(3), l), 1801);
    require(!receiver.takeExact(identity(3), 2801), "completed mask cache expires");
    auto next = identity(); next.session[0] = 99;
    ingestAll(receiver, encode(identity(4), l), 2810);
    receiver.beginSession(next.session);
    require(receiver.bufferedBytes() == 0, "new session invalidates all cache");
    require(receiver.ingest(oldPackets[0], 2811) == IngestResult::WrongSession,
            "late previous-session packet refused");
    require(!receiver.takeExact(identity(4), 2812), "old-session color cannot match");
    ingestAll(receiver, encode(next, l), 2813);
    require(*receiver.takeExact(next, 2814) == l, "restart same frame number safe");
    auto current = identity(5); current.session = next.session;
    ingestAll(receiver, encode(current, l), 3000);
    require(*receiver.takeExact(current, 3001) == l, "active session stays usable");
}

void malformedAndConflicts() {
    const auto id = identity(); auto packets = encode(id, pattern(id), Encoding::Raw);
    Receiver r; r.beginSession(id.session);
    require(r.ingest(nullptr, 100, 0) == IngestResult::Invalid, "null rejected");
    require(r.ingest(packets[0].data(), 55, 0) == IngestResult::Invalid, "short header");
    auto p = packets[0]; p.resize(1201);
    require(r.ingest(p, 0) == IngestResult::Invalid, "oversized packet");
    p = packets[0]; p.back() ^= 3;
    require(r.ingest(p, 0) == IngestResult::Invalid, "payload CRC catches corruption");
    p = packets[0]; p[24] ^= 3;
    require(r.ingest(p, 0) == IngestResult::Invalid, "metadata CRC catches corruption");
    p = packets[0]; p[7] = 1; sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "unknown flags rejected");
    p = packets[0]; p[46] = 1; sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "reserved bytes rejected");
    p = packets[0]; put(p.data() + 40, 1, 4); sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "overlapping offset rejected");
    p = packets[0]; put(p.data() + 44, 1, 2); sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "false fragment size rejected");
    p = packets[0]; put(p.data() + 36, 0xffffffffu, 4); sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "overflow total rejected");
    p = packets[0]; p[6] = 2; sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "unsupported eye rejected");
    p = packets[0]; p[5] = 2; sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "Auto is never a wire codec");
    p = packets[0]; p[4] = 2; sign(p);
    require(r.ingest(p, 0) == IngestResult::Invalid, "future protocol version rejected");
    require(r.ingest(packets[0], 0) == IngestResult::Accepted, "valid beginning");
    p = packets[1]; put(p.data() + 32, 100, 2); put(p.data() + 34, 40, 2); sign(p);
    require(r.ingest(p, 1) == IngestResult::Invalid, "conflicting resolution poisoned");
    require(r.assemblyCount() == 0 && r.bufferedBytes() == 0,
            "conflict releases partial storage");
    require(r.ingest(packets[1], 2) == IngestResult::Blocked, "poison cannot be revived");
    r.beginSession(id.session);
    require(r.ingest(packets[0], 0) == IngestResult::Accepted, "duplicate setup");
    p = packets[0]; p.back() ^= 1; sign(p);
    require(r.ingest(p, 1) == IngestResult::Invalid, "conflicting duplicate rejected");
    require(!r.takeExact(id, 2), "conflicting duplicate never presented");
    r.beginSession(id.session);
    auto badFrame = packets;
    for (auto& part : badFrame) { part[48] ^= 1; sign(part); }
    for (std::size_t i = 0; i < badFrame.size(); ++i)
        require(r.ingest(badFrame[i], 0) == (i + 1 == badFrame.size() ?
            IngestResult::Invalid : IngestResult::Accepted), "whole mask CRC verified");
}

void rleBounds() {
    const auto id = identity();
    const std::vector<std::uint8_t> flat(std::size_t(id.width) * id.height, 113);
    auto packets = encode(id, flat);
    require(packets.size() == 1 && packets[0][5] == 1, "Auto chooses smaller RLE");
    Receiver r; r.beginSession(id.session); ingestAll(r, packets);
    require(*r.takeExact(id, 1) == flat, "RLE preserves nonbinary coverage");
    require(encode(id, pattern(id))[0][5] == 0, "Auto avoids RLE expansion");
    r.beginSession(id.session);
    auto p = packets[0]; p[kHeaderBytes] = 0; sign(p, true);
    require(r.ingest(p, 0) == IngestResult::Invalid, "zero run forbidden");
    r.beginSession(id.session);
    p = packets[0]; p[p.size() - 2] = 255; sign(p, true);
    require(r.ingest(p, 0) == IngestResult::Invalid, "decompressed overflow rejected");
    r.beginSession(id.session);
    p = packets[0]; p[p.size() - 2] = 1; sign(p, true);
    require(r.ingest(p, 0) == IngestResult::Invalid, "decompressed underflow rejected");
    const auto noisy = pattern(id);
    r.beginSession(id.session); ingestAll(r, encode(id, noisy, Encoding::Rle));
    require(*r.takeExact(id, 1) == noisy, "explicit expanding RLE bounded and supported");
}

void resourceLimitsAndClock() {
    auto id = identity(); const auto bytes = pattern(id);
    Limits limits; limits.maxBufferedBytes = 4500;
    limits.maxAssemblies = 2; limits.maxCachedFrames = 1;
    Receiver r(limits); r.beginSession(id.session);
    const auto one = encode(id, bytes, Encoding::Raw);
    require(r.ingest(one[0], 0) == IngestResult::Accepted, "first buffer fits");
    auto two = encode(identity(2), bytes, Encoding::Raw);
    require(r.ingest(two[0], 1) == IngestResult::Accepted, "old partial evicted for cap");
    require(r.bufferedBytes() <= limits.maxBufferedBytes && r.assemblyCount() == 1,
            "bounded partial storage");
    for (std::size_t i = 1; i < two.size(); ++i) r.ingest(two[i], 2);
    require(!r.takeExact(id, 3), "evicted frame does not fallback");
    require(*r.takeExact(identity(2), 4) == bytes, "retained exact frame survives");
    limits.maxBufferedBytes = 10;
    Receiver tiny(limits); tiny.beginSession(id.session);
    require(tiny.ingest(one[0], 0) == IngestResult::LimitExceeded, "large allocation refused");
    require(tiny.bufferedBytes() == 0, "refused allocation owns no buffer");
    limits.maxBufferedBytes = 4010;
    Receiver compressed(limits); compressed.beginSession(id.session);
    auto flat = encode(id, std::vector<std::uint8_t>(bytes.size(), 1));
    require(compressed.ingest(flat[0], 0) == IngestResult::LimitExceeded,
            "temporary decompression buffer also counted");
    require(compressed.bufferedBytes() == 0, "decompression refusal frees encoded data");
    Receiver clock; clock.beginSession(id.session); ingestAll(clock, one, 100);
    require(!clock.takeExact(id, 99) && clock.bufferedBytes() == 0,
            "clock regression discards possible stale masks");
    for (const auto& packet : one)
        require(clock.ingest(packet, 100) == IngestResult::WrongSession,
                "clock regression cannot resurrect buffered frames by replay");
    require(!clock.takeExact(id, 101), "replayed discarded frame stays opaque");
    auto fresh = id; fresh.session[0] ^= 0x80;
    clock.beginSession(fresh.session);
    ingestAll(clock, encode(fresh, bytes), 102);
    require(*clock.takeExact(fresh, 103) == bytes,
            "fresh explicit handshake recovers after clock regression");
    clock.expire(102);
    for (const auto& packet : encode(fresh, bytes))
        require(clock.ingest(packet, 103) == IngestResult::WrongSession,
                "clock regression cannot resurrect consumed frames by replay");
    require(!clock.takeExact(fresh, 104), "replayed consumed frame stays opaque");
    limits.maxBufferedBytes = 50000; limits.maxAssemblies = 1;
    Receiver count(limits); count.beginSession(id.session);
    count.ingest(one[0], 0); count.ingest(two[0], 1);
    require(count.assemblyCount() == 1 && count.stats().droppedFrames == 1,
            "assembly count is bounded separately from bytes");
    limits.maxCachedFrames = 1;
    Receiver cached(limits); cached.beginSession(id.session);
    ingestAll(cached, one, 0); ingestAll(cached, two, 1);
    require(cached.cachedFrameCount() == 1 && !cached.takeExact(id, 2),
            "cache count bounded separately from bytes");
    require(*cached.takeExact(identity(2), 3) == bytes, "new cache retained");
    limits.maxBlockedFrames = 1; limits.maxCachedFrames = 8;
    Receiver history(limits); history.beginSession(id.session);
    ingestAll(history, encode(identity(10), bytes), 0);
    ingestAll(history, encode(identity(9), bytes), 1);
    require(*history.takeExact(identity(9), 2) == bytes,
            "frame reorder retains independent complete cache");
    require(*history.takeExact(identity(10), 3) == bytes,
            "later exact color retains its own complete cache");
    require(history.ingest(encode(identity(9), bytes)[0], 4) == IngestResult::Blocked,
            "retirement eviction cannot permit replay of consumed frame");
    require(!history.takeExact(identity(1, 200), 4), "invalid color eye cannot index state");
}

void randomRoundTripsAndInvalidInputs() {
    std::mt19937 random(0x4d524131u);
    for (unsigned trial = 0; trial < 100; ++trial) {
        const auto id = identity(20 + trial, trial % 2,
            static_cast<std::uint16_t>(1 + random() % 140),
            static_cast<std::uint16_t>(1 + random() % 80));
        auto bytes = pattern(id);
        if (trial % 3 == 0)
            for (auto& byte : bytes) byte = static_cast<std::uint8_t>(random() % 256);
        if (trial % 3 == 1) std::fill(bytes.begin(), bytes.end(), trial);
        auto packets = encode(id, bytes, static_cast<Encoding>(trial % 3));
        std::shuffle(packets.begin(), packets.end(), random);
        Receiver r; r.beginSession(id.session); ingestAll(r, packets);
        require(*r.takeExact(id, 1) == bytes, "random shuffled codec roundtrip");
    }
    bool threw = false;
    try { encode(identity(1, 2), pattern(identity())); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "sender invalid eye rejected");
    threw = false;
    try { encode(identity(), {1, 2}); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "sender invalid size rejected");
    threw = false;
    try { Limits l; l.maxAssemblies = 0; Receiver invalid(l); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "invalid receiver limits rejected");
}

void hostileTrafficBudgets() {
    Limits limits;
    limits.maxBufferedBytes = 7000;
    limits.maxAssemblies = 2; limits.maxCachedFrames = 2;
    limits.maxBlockedFrames = 2;
    limits.assemblyExpiryMs = 17; limits.cacheExpiryMs = 23;
    const auto base = identity(1, 0, 64, 32);
    Receiver unbound(limits);
    require(unbound.ingest(encode(base, pattern(base))[0], 0) ==
            IngestResult::WrongSession, "packet cannot auto-bind a session");
    Receiver receiver(limits); receiver.beginSession(base.session);
    std::mt19937 random(0xa1fa1234u);
    std::uint64_t now = 0;
    for (unsigned round = 0; round < 4000; ++round) {
        const auto id = identity(random() % 1000, random() % 2, 64, 32);
        auto packets = encode(id, pattern(id),
            round % 2 ? Encoding::Rle : Encoding::Raw);
        auto p = packets[random() % packets.size()];
        if (round % 3 == 0) {
            p[random() % p.size()] ^= static_cast<std::uint8_t>(random() % 255 + 1);
            if (round % 2 == 0) sign(p);
        }
        receiver.ingest(p, now);
        if (round % 7 == 0) receiver.takeExact(id, now);
        now += random() % 3;
        receiver.expire(now);
        require(receiver.bufferedBytes() <= limits.maxBufferedBytes,
                "hostile traffic cannot exceed byte budget");
        require(receiver.assemblyCount() <= limits.maxAssemblies &&
                receiver.cachedFrameCount() <= limits.maxCachedFrames,
                "hostile traffic cannot exceed frame count budgets");
    }
    receiver.expire(now + 100);
    require(receiver.bufferedBytes() == 0, "hostile traffic buffers eventually expire");
}

} // namespace

int main() {
    try {
        wireAndReorder(); sessionsStereoAndExpiry(); malformedAndConflicts();
        rleBounds(); resourceLimitsAndClock(); randomRoundTripsAndInvalidInputs();
        hostileTrafficBudgets();
        std::cout << "mask protocol: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mask protocol check failed: " << e.what() << '\n';
        return 1;
    }
}
