// SPDX-License-Identifier: GPL-3.0-or-later
// The random order of SPEC-065: xorshift64* and the loop that permutes a list with it.
#include "spz/shuffle.h"

#include <utility>

namespace spz {

namespace {

const std::uint64_t kZeroSeedState = 0x9E3779B97F4A7C15ULL;
const std::uint64_t kMultiplier = 0x2545F4914F6CDD1DULL;

}  // namespace

ShuffleGenerator::ShuffleGenerator(std::uint64_t seed) : x_(seed == 0 ? kZeroSeedState : seed) {}

std::uint64_t ShuffleGenerator::next() {
    x_ ^= x_ >> 12;
    x_ ^= x_ << 25;
    x_ ^= x_ >> 27;
    return x_ * kMultiplier;
}

void shuffle_places(std::vector<std::size_t>* places, ShuffleGenerator* generator) {
    if (places == nullptr || generator == nullptr || places->size() < 2) return;
    for (std::size_t i = places->size() - 1; i >= 1; --i) {
        const std::size_t j = static_cast<std::size_t>(generator->next() % (static_cast<std::uint64_t>(i) + 1));
        std::swap((*places)[i], (*places)[j]);
    }
}

}  // namespace spz
