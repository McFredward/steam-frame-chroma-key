#include "mask_protocol.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>

namespace mr::mask {
namespace {

constexpr std::array<std::uint32_t, 256> crcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto value = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
        table[i] = value;
    }
    return table;
}
constexpr auto kCrcTable = crcTable();

std::uint32_t crcUpdate(std::uint32_t crc, const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        crc = (crc >> 8) ^ kCrcTable[(crc ^ p[i]) & 0xffu];
    return crc;
}

std::uint32_t packetCrc(const std::uint8_t* p, std::size_t n) {
    auto crc = crcUpdate(0xffffffffu, p, 52);
    return crcUpdate(crc, p + kHeaderBytes, n - kHeaderBytes) ^ 0xffffffffu;
}

void put(std::uint8_t* p, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        p[bytes - i - 1] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
}

std::uint64_t get(const std::uint8_t* p, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value = (value << 8) | p[i];
    return value;
}

using Key = std::pair<std::uint64_t, std::uint8_t>;
Key key(const FrameIdentity& id) { return {id.frame, id.eye}; }

bool sameIdentity(const FrameIdentity& a, const FrameIdentity& b) {
    return a.session == b.session && a.frame == b.frame && a.eye == b.eye &&
           a.width == b.width && a.height == b.height;
}

bool elapsed(std::uint64_t now, std::uint64_t then, std::uint64_t duration) {
    return now >= then && now - then >= duration;
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    return crcUpdate(0xffffffffu, data, size) ^ 0xffffffffu;
}

std::vector<std::vector<std::uint8_t>> encode(
    const FrameIdentity& id, const std::vector<std::uint8_t>& coverage,
    Encoding encoding) {
    const auto pixels = std::size_t(id.width) * id.height;
    if (id.eye > 1 || id.width == 0 || id.height == 0 || id.width > 4096 ||
        id.height > 4096 || pixels > kMaxMaskBytes || pixels != coverage.size() ||
        (encoding != Encoding::Raw && encoding != Encoding::Rle &&
         encoding != Encoding::Auto))
        throw std::invalid_argument("Invalid alpha mask identity or payload");

    std::vector<std::uint8_t> encoded;
    if (encoding != Encoding::Raw) {
        encoded.reserve(coverage.size());
        for (std::size_t i = 0; i < coverage.size();) {
            std::size_t count = 1;
            while (count < 255 && i + count < coverage.size() &&
                   coverage[i + count] == coverage[i]) ++count;
            encoded.push_back(static_cast<std::uint8_t>(count));
            encoded.push_back(coverage[i]);
            i += count;
        }
        if (encoding == Encoding::Auto && encoded.size() >= coverage.size())
            encoded.clear();
    }
    const auto& bytes = encoded.empty() ? coverage : encoded;
    const auto actualEncoding = encoded.empty() ? Encoding::Raw : Encoding::Rle;
    const auto checksum = crc32(bytes.data(), bytes.size());
    std::vector<std::vector<std::uint8_t>> packets;
    packets.reserve((bytes.size() + kFragmentBytes - 1) / kFragmentBytes);
    for (std::size_t offset = 0; offset < bytes.size(); offset += kFragmentBytes) {
        const auto n = std::min(kFragmentBytes, bytes.size() - offset);
        std::vector<std::uint8_t> p(kHeaderBytes + n, 0);
        std::memcpy(p.data(), "MRA1", 4);
        p[4] = 1;
        p[5] = static_cast<std::uint8_t>(actualEncoding);
        p[6] = id.eye;
        std::copy(id.session.begin(), id.session.end(), p.begin() + 8);
        put(p.data() + 24, id.frame, 8);
        put(p.data() + 32, id.width, 2);
        put(p.data() + 34, id.height, 2);
        put(p.data() + 36, bytes.size(), 4);
        put(p.data() + 40, offset, 4);
        put(p.data() + 44, n, 2);
        put(p.data() + 48, checksum, 4);
        std::copy(bytes.begin() + offset, bytes.begin() + offset + n,
                  p.begin() + kHeaderBytes);
        put(p.data() + 52, packetCrc(p.data(), p.size()), 4);
        packets.push_back(std::move(p));
    }
    return packets;
}

struct Receiver::Impl {
    struct Assembly {
        FrameIdentity id;
        Encoding encoding;
        std::uint32_t checksum;
        std::uint64_t firstMs;
        std::vector<std::uint8_t> bytes;
        std::vector<bool> fragments;
        std::size_t received = 0;
    };
    struct Cached {
        FrameIdentity id;
        Encoding encoding;
        std::uint32_t checksum;
        std::size_t encodedSize;
        std::uint64_t createdMs;
        std::vector<std::uint8_t> bytes;
    };
    Limits limits;
    Stats stats;
    SessionId session{};
    bool bound = false;
    bool clockSet = false;
    std::uint64_t lastMs = 0;
    std::size_t buffered = 0;
    std::map<Key, Assembly> assemblies;
    std::map<Key, Cached> cache;
    std::map<Key, std::uint64_t> blocked;
    std::array<std::uint64_t, 2> retiredThrough{};
    std::array<bool, 2> retirementSet{};

    explicit Impl(Limits lim) : limits(lim) {
        if (!lim.maxWidth || !lim.maxHeight || !lim.maxMaskBytes ||
            lim.maxMaskBytes > kMaxMaskBytes || !lim.maxEncodedBytes ||
            lim.maxEncodedBytes > 2 * kMaxMaskBytes || !lim.maxBufferedBytes ||
            !lim.maxAssemblies || lim.maxAssemblies > 1024 ||
            !lim.maxCachedFrames || lim.maxCachedFrames > 1024 ||
            !lim.maxBlockedFrames || lim.maxBlockedFrames > 4096 ||
            !lim.assemblyExpiryMs || !lim.cacheExpiryMs)
            throw std::invalid_argument("Invalid alpha mask receiver limits");
    }

    void clear() {
        assemblies.clear(); cache.clear(); blocked.clear(); buffered = 0;
        retirementSet.fill(false);
        retiredThrough.fill(0);
    }

    bool retired(Key k) const {
        return retirementSet[k.second] && k.first <= retiredThrough[k.second];
    }

    void retire(Key k) {
        if (!retirementSet[k.second] || k.first > retiredThrough[k.second])
            retiredThrough[k.second] = k.first;
        retirementSet[k.second] = true;
    }

    void rememberBlock(Key k, std::uint64_t now) {
        if (blocked.size() >= limits.maxBlockedFrames && blocked.count(k) == 0) {
            auto oldest = std::min_element(blocked.begin(), blocked.end(),
                [](const auto& x, const auto& y) { return x.second < y.second; });
            retire(oldest->first);
            blocked.erase(oldest);
        }
        blocked[k] = now;
    }

    void tick(std::uint64_t now) {
        if (clockSet && now < lastMs) clear();
        clockSet = true;
        lastMs = now;
        for (auto it = assemblies.begin(); it != assemblies.end();) {
            if (!retired(it->first) &&
                !elapsed(now, it->second.firstMs, limits.assemblyExpiryMs)) {
                ++it; continue;
            }
            const auto k = it->first;
            buffered -= it->second.bytes.size();
            it = assemblies.erase(it);
            rememberBlock(k, now);
            ++stats.expiredFrames;
        }
        for (auto it = cache.begin(); it != cache.end();) {
            if (!retired(it->first) &&
                !elapsed(now, it->second.createdMs, limits.cacheExpiryMs)) {
                ++it; continue;
            }
            const auto k = it->first;
            buffered -= it->second.bytes.size();
            it = cache.erase(it);
            rememberBlock(k, now);
            ++stats.expiredFrames;
        }
        for (auto it = blocked.begin(); it != blocked.end();) {
            if (elapsed(now, it->second, std::max(limits.assemblyExpiryMs,
                                                limits.cacheExpiryMs))) {
                retire(it->first);
                it = blocked.erase(it);
            } else ++it;
        }
    }

    void block(Key k, std::uint64_t now, bool dropped = true) {
        auto a = assemblies.find(k);
        if (a != assemblies.end()) {
            buffered -= a->second.bytes.size(); assemblies.erase(a);
        }
        auto c = cache.find(k);
        if (c != cache.end()) {
            buffered -= c->second.bytes.size(); cache.erase(c);
        }
        rememberBlock(k, now);
        if (dropped) ++stats.droppedFrames;
    }

    bool makeRoom(std::size_t bytes, std::optional<Key> protectedKey = {}) {
        if (bytes > limits.maxBufferedBytes) return false;
        while (buffered > limits.maxBufferedBytes - bytes) {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if ((!protectedKey || it->first != *protectedKey) &&
                    (oldest == cache.end() ||
                     it->second.createdMs < oldest->second.createdMs)) oldest = it;
            if (oldest != cache.end()) {
                const auto k = oldest->first;
                buffered -= oldest->second.bytes.size(); cache.erase(oldest);
                rememberBlock(k, lastMs);
                ++stats.droppedFrames;
                continue;
            }
            auto oldAssembly = assemblies.end();
            for (auto it = assemblies.begin(); it != assemblies.end(); ++it)
                if ((!protectedKey || it->first != *protectedKey) &&
                    (oldAssembly == assemblies.end() ||
                     it->second.firstMs < oldAssembly->second.firstMs))
                    oldAssembly = it;
            if (oldAssembly == assemblies.end()) return false;
            const auto k = oldAssembly->first;
            buffered -= oldAssembly->second.bytes.size();
            assemblies.erase(oldAssembly);
            rememberBlock(k, lastMs);
            ++stats.droppedFrames;
        }
        return true;
    }
};

Receiver::Receiver(Limits limits) : impl_(new Impl(limits)) {}
Receiver::~Receiver() = default;
Receiver::Receiver(Receiver&&) noexcept = default;
Receiver& Receiver::operator=(Receiver&&) noexcept = default;

void Receiver::beginSession(const SessionId& session) {
    impl_->clear(); impl_->session = session; impl_->bound = true;
    impl_->clockSet = false;
}

void Receiver::expire(std::uint64_t nowMs) { impl_->tick(nowMs); }

IngestResult Receiver::ingest(const std::uint8_t* p, std::size_t n,
                             std::uint64_t now) {
    auto& s = *impl_;
    s.tick(now);
    const auto invalid = [&s]() {
        ++s.stats.invalidPackets; return IngestResult::Invalid;
    };
    if (!p || n < kHeaderBytes || n > kMaxPacketBytes ||
        std::memcmp(p, "MRA1", 4) != 0 || p[4] != 1 || p[5] > 1 ||
        p[6] > 1 || p[7] != 0 || get(p + 46, 2) != 0 ||
        packetCrc(p, n) != get(p + 52, 4)) return invalid();
    FrameIdentity id;
    std::copy(p + 8, p + 24, id.session.begin());
    id.frame = get(p + 24, 8); id.eye = p[6];
    id.width = static_cast<std::uint16_t>(get(p + 32, 2));
    id.height = static_cast<std::uint16_t>(get(p + 34, 2));
    if (!s.bound || id.session != s.session) {
        ++s.stats.wrongSessionPackets; return IngestResult::WrongSession;
    }
    const auto pixels = std::size_t(id.width) * id.height;
    const auto total = static_cast<std::size_t>(get(p + 36, 4));
    const auto offset = static_cast<std::size_t>(get(p + 40, 4));
    const auto payload = static_cast<std::size_t>(get(p + 44, 2));
    const auto encoding = static_cast<Encoding>(p[5]);
    const auto checksum = static_cast<std::uint32_t>(get(p + 48, 4));
    if (!pixels || id.width > s.limits.maxWidth ||
        id.height > s.limits.maxHeight || pixels > s.limits.maxMaskBytes ||
        !total || total > s.limits.maxEncodedBytes ||
        (encoding == Encoding::Raw && total != pixels) ||
        (encoding == Encoding::Rle && (total % 2 || total > 2 * pixels)) ||
        offset >= total || offset % kFragmentBytes != 0 ||
        payload != std::min(kFragmentBytes, total - offset) ||
        n != kHeaderBytes + payload) return invalid();
    const auto k = key(id);
    if (s.retired(k) || s.blocked.count(k)) return IngestResult::Blocked;
    auto cached = s.cache.find(k);
    if (cached != s.cache.end()) {
        const auto& c = cached->second;
        if (sameIdentity(c.id, id) && c.encoding == encoding &&
            c.checksum == checksum && c.encodedSize == total)
            return IngestResult::Duplicate;
        s.block(k, now); return invalid();
    }
    auto it = s.assemblies.find(k);
    if (it == s.assemblies.end()) {
        if (s.assemblies.size() >= s.limits.maxAssemblies) {
            auto oldest = std::min_element(s.assemblies.begin(), s.assemblies.end(),
                [](const auto& a, const auto& b) {
                    return a.second.firstMs < b.second.firstMs;
                });
            const auto oldKey = oldest->first;
            s.buffered -= oldest->second.bytes.size();
            s.assemblies.erase(oldest); ++s.stats.droppedFrames;
            s.rememberBlock(oldKey, now);
        }
        if (!s.makeRoom(total)) return IngestResult::LimitExceeded;
        if (s.retired(k)) return IngestResult::Blocked;
        Impl::Assembly a{id, encoding, checksum, now,
            std::vector<std::uint8_t>(total),
            std::vector<bool>((total + kFragmentBytes - 1) / kFragmentBytes), 0};
        it = s.assemblies.emplace(k, std::move(a)).first;
        s.buffered += total;
    }
    auto& a = it->second;
    if (!sameIdentity(a.id, id) || a.encoding != encoding ||
        a.checksum != checksum || a.bytes.size() != total) {
        s.block(k, now); return invalid();
    }
    const auto fragment = offset / kFragmentBytes;
    if (a.fragments[fragment]) {
        if (std::equal(p + kHeaderBytes, p + n, a.bytes.begin() + offset))
            return IngestResult::Duplicate;
        s.block(k, now); return invalid();
    }
    std::copy(p + kHeaderBytes, p + n, a.bytes.begin() + offset);
    a.fragments[fragment] = true; ++a.received; ++s.stats.acceptedPackets;
    if (a.received != a.fragments.size()) return IngestResult::Accepted;
    if (crc32(a.bytes.data(), a.bytes.size()) != checksum) {
        s.block(k, now); return invalid();
    }
    std::vector<std::uint8_t> decoded;
    if (encoding == Encoding::Rle) {
        if (!s.makeRoom(pixels, k)) {
            s.block(k, now); return IngestResult::LimitExceeded;
        }
        if (s.retired(k)) {
            s.block(k, now); return IngestResult::Blocked;
        }
        decoded.reserve(pixels);
        for (std::size_t i = 0; i < a.bytes.size(); i += 2) {
            const auto count = a.bytes[i];
            if (!count || count > pixels - decoded.size()) {
                s.block(k, now); return invalid();
            }
            decoded.insert(decoded.end(), count, a.bytes[i + 1]);
        }
        if (decoded.size() != pixels) {
            s.block(k, now); return invalid();
        }
    } else decoded = std::move(a.bytes);
    s.buffered -= total;
    s.assemblies.erase(k);
    if (s.cache.size() >= s.limits.maxCachedFrames) {
        auto oldest = std::min_element(s.cache.begin(), s.cache.end(),
            [](const auto& x, const auto& y) {
                return x.second.createdMs < y.second.createdMs;
            });
        const auto oldKey = oldest->first;
        s.buffered -= oldest->second.bytes.size(); s.cache.erase(oldest);
        s.rememberBlock(oldKey, now);
        ++s.stats.droppedFrames;
    }
    s.buffered += decoded.size();
    s.cache.emplace(k, Impl::Cached{id, encoding, checksum, total, now,
                                   std::move(decoded)});
    ++s.stats.completedFrames;
    return IngestResult::Completed;
}

std::optional<std::vector<std::uint8_t>> Receiver::takeExact(
    const FrameIdentity& id, std::uint64_t now) {
    auto& s = *impl_;
    s.tick(now);
    if (s.bound && id.session == s.session && id.eye <= 1 && !s.retired(key(id))) {
        const auto it = s.cache.find(key(id));
        if (it != s.cache.end() && sameIdentity(it->second.id, id)) {
            auto bytes = std::move(it->second.bytes);
            s.buffered -= bytes.size(); s.cache.erase(it);
            // Retransmission cannot make a consumed frame appear twice.
            s.block(key(id), now, false);
            return bytes;
        }
    }
    ++s.stats.exactMisses;
    return std::nullopt;
}

std::size_t Receiver::bufferedBytes() const { return impl_->buffered; }
std::size_t Receiver::assemblyCount() const { return impl_->assemblies.size(); }
std::size_t Receiver::cachedFrameCount() const { return impl_->cache.size(); }
const Stats& Receiver::stats() const { return impl_->stats; }

} // namespace mr::mask
