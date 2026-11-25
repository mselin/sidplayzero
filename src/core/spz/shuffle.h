// SPDX-License-Identifier: GPL-3.0-or-later
// The random order of SPEC-065 (the part `shuffle`): a generator and a loop, made in exactly the way the
// specification gives, so that a test can know the order a seed leads to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spz {

// xorshift64* (SPEC-065). Its state x is a 64-bit number, started from the seed, or from
// 0x9E3779B97F4A7C15 when the seed is 0.
class ShuffleGenerator {
public:
    explicit ShuffleGenerator(std::uint64_t seed = 0);

    // The next number: x ^= x >> 12; x ^= x << 25; x ^= x >> 27, keeping 64 bits; the number is x times
    // 0x2545F4914F6CDD1D, again keeping 64 bits. With seed 1 the first number is 0x47E4CE4B896CDD1D.
    std::uint64_t next();

private:
    std::uint64_t x_ = 0;
};

// Permutes a list of m entries in place (SPEC-065): going from i = m - 1 down to 1, it takes the
// generator's next number modulo i + 1 as j and swaps a[i] with a[j]. The entries here are places, such
// as 0 to m - 1, standing for whatever is being put in a random order. A list of one or none takes no
// number from the generator.
void shuffle_places(std::vector<std::size_t>* places, ShuffleGenerator* generator);

}  // namespace spz
