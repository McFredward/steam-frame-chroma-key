#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mr::mask {

using SessionId = std::array<std::uint8_t, 16>;

struct FrameIdentity {
    SessionId session{};
    std::uint64_t frame = 0;
    std::uint8_t eye = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

enum class Encoding : std::uint8_t { Raw = 0, Rle = 1, Auto = 2 };

// Protocol MRA1, version 1. Every integer is unsigned and in network byte order.
// Header (56 bytes): magic[4], version[1], encoding[1], eye[1], flags[1],
// session[16], frame[8], width[2], height[2], encoded size[4], offset[4],
// payload size[2], reserved[2], full encoded CRC[4], packet CRC[4].
// CRC is CRC-32/ISO-HDLC (polynomial 0xedb88320, initial/final XOR 0xffffffff).
// Packet CRC covers header bytes 0..51 followed by payload, excluding itself.
// Raw payload is row-major R8 coverage; RLE is (nonzero uint8 count, R8 value).
// Fragments use fixed 1144-byte boundaries, except the last fragment. This
// rejects overlaps and bounds the fragment bookkeeping without interval scans.
constexpr std::size_t kHeaderBytes = 56;
constexpr std::size_t kMaxPacketBytes = 1200;
constexpr std::size_t kFragmentBytes = kMaxPacketBytes - kHeaderBytes;
constexpr std::size_t kMaxMaskBytes = 16u * 1024u * 1024u;

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

// Throws std::invalid_argument for unsupported identities or payload lengths.
// Auto chooses RLE only when it is smaller. Explicit RLE may use twice the
// decoded size; sender and receiver limits must agree for such packets.
std::vector<std::vector<std::uint8_t>> encode(
    const FrameIdentity& identity, const std::vector<std::uint8_t>& coverage,
    Encoding encoding = Encoding::Auto);

struct Limits {
    std::uint16_t maxWidth = 4096;
    std::uint16_t maxHeight = 4096;
    std::size_t maxMaskBytes = kMaxMaskBytes;
    std::size_t maxEncodedBytes = 2 * kMaxMaskBytes;
    // Covers live encoded buffers and decoded cache. Decoding also reserves
    // the temporary output against this budget before allocating it.
    std::size_t maxBufferedBytes = 64u * 1024u * 1024u;
    std::size_t maxAssemblies = 8;
    std::size_t maxCachedFrames = 8;
    std::size_t maxBlockedFrames = 32;
    std::uint64_t assemblyExpiryMs = 750;
    std::uint64_t cacheExpiryMs = 1000;
};

enum class IngestResult {
    Accepted, Duplicate, Completed, WrongSession, Invalid, LimitExceeded, Blocked
};

struct Stats {
    std::uint64_t acceptedPackets = 0;
    std::uint64_t invalidPackets = 0;
    std::uint64_t wrongSessionPackets = 0;
    std::uint64_t completedFrames = 0;
    std::uint64_t expiredFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t exactMisses = 0;
};

// This class performs no networking and creates no threads. Call it serially,
// passing monotonic milliseconds. A clock regression clears buffered masks.
// Session binding is explicit: a packet cannot switch sessions. Transport
// discovery/authentication is the application's responsibility.
// Frame counters must increase within each session (start a new session before
// wrapping). Bounded reordering is supported; retired frames cannot be replayed
// after their expiry or after eviction from the bounded retirement history.
class Receiver {
public:
    explicit Receiver(Limits limits = {});
    ~Receiver();
    Receiver(Receiver&&) noexcept;
    Receiver& operator=(Receiver&&) noexcept;
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    void beginSession(const SessionId& session);
    IngestResult ingest(const std::uint8_t* packet, std::size_t size,
                        std::uint64_t nowMs);
    IngestResult ingest(const std::vector<std::uint8_t>& packet,
                        std::uint64_t nowMs) {
        return ingest(packet.data(), packet.size(), nowMs);
    }
    // Consume only an exact session/frame/eye/resolution match. No mask means
    // opaque presentation, never a previous/newer frame's mask. The caller must
    // obtain the identity from the actual color frame: packet arrival order,
    // timestamps and a Vulkan submission counter are not color frame IDs.
    std::optional<std::vector<std::uint8_t>> takeExact(
        const FrameIdentity& colorFrame, std::uint64_t nowMs);
    void expire(std::uint64_t nowMs);
    std::size_t bufferedBytes() const;
    std::size_t assemblyCount() const;
    std::size_t cachedFrameCount() const;
    const Stats& stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mr::mask
