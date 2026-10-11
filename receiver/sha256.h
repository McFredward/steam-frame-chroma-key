#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace frame_receiver {
// Streaming SHA-256 for the one startup executable identity check. Never used
// on a receiver callback. No OpenSSL initialization or loader callback recursion.
class Sha256 {
public:
    void update(const void* input, std::size_t length) noexcept {
        const auto* data = static_cast<const std::uint8_t*>(input);
        bytes_ += length;
        while (length) {
            const auto count = length < 64 - used_ ? length : 64 - used_;
            std::memcpy(block_.data() + used_, data, count);
            used_ += count; data += count; length -= count;
            if (used_ == 64) { compress(); used_ = 0; }
        }
    }
    std::array<std::uint8_t, 32> finish() noexcept {
        const auto bits = bytes_ * 8;
        block_[used_++] = 0x80;
        if (used_ > 56) {
            while (used_ < 64) block_[used_++] = 0;
            compress(); used_ = 0;
        }
        while (used_ < 56) block_[used_++] = 0;
        for (unsigned i = 0; i < 8; ++i) block_[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        compress();
        std::array<std::uint8_t, 32> digest{};
        for (unsigned i = 0; i < 32; ++i)
            digest[i] = static_cast<std::uint8_t>(state_[i / 4] >> (24 - 8 * (i % 4)));
        return digest;
    }
private:
    std::array<std::uint32_t, 8> state_{{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
    static std::uint32_t rotate(std::uint32_t x, unsigned n) noexcept { return (x >> n) | (x << (32 - n)); }
    void compress() noexcept {
        constexpr std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64]{};
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block_[4*i]) << 24) | (std::uint32_t(block_[4*i+1]) << 16) |
                   (std::uint32_t(block_[4*i+2]) << 8) | block_[4*i+3];
        for (unsigned i = 16; i < 64; ++i) {
            const auto s0 = rotate(w[i-15],7) ^ rotate(w[i-15],18) ^ (w[i-15] >> 3);
            const auto s1 = rotate(w[i-2],17) ^ rotate(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto a=state_[0], b=state_[1], c=state_[2], d=state_[3];
        auto e=state_[4], f=state_[5], g=state_[6], h=state_[7];
        for (unsigned i = 0; i < 64; ++i) {
            const auto s1 = rotate(e,6) ^ rotate(e,11) ^ rotate(e,25);
            const auto t1 = h + s1 + ((e&f) ^ (~e&g)) + k[i] + w[i];
            const auto s0 = rotate(a,2) ^ rotate(a,13) ^ rotate(a,22);
            const auto t2 = s0 + ((a&b) ^ (a&c) ^ (b&c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }
};
} // namespace frame_receiver
