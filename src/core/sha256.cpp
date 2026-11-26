// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-256 (FIPS 180-4). See spz/sha256.h.
#include "spz/sha256.h"

#include <cstdint>
#include <cstdio>

namespace spz {

namespace {

const std::uint32_t kRounds[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

class Sha256 {
public:
    void add(const std::uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            block_[used_++] = data[i];
            if (used_ == 64) {
                compress();
                used_ = 0;
            }
        }
        bytes_ += size;
    }

    // The padding and the length in bits, then the digest. Once only.
    std::string hex() {
        const std::uint64_t bits = bytes_ * 8;
        const std::uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (used_ != 56) add(&zero, 1);
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        add(length, 8);
        std::string text;
        char digits[9];
        for (const std::uint32_t word : h_) {
            std::snprintf(digits, sizeof digits, "%08x", word);
            text += digits;
        }
        return text;
    }

private:
    static std::uint32_t rotate(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress() {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = static_cast<std::uint32_t>(block_[4 * i]) << 24 | static_cast<std::uint32_t>(block_[4 * i + 1]) << 16 |
                   static_cast<std::uint32_t>(block_[4 * i + 2]) << 8 | static_cast<std::uint32_t>(block_[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + choose + kRounds[i] + w[i];
            const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    std::uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t block_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
};

}  // namespace

std::string sha256_hex(const Bytes& data) {
    Sha256 sha;
    sha.add(data.data(), data.size());
    return sha.hex();
}

}  // namespace spz
