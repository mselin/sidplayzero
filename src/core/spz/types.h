// SPDX-License-Identifier: GPL-3.0-or-later
// Small types that the parts of `core` share.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spz {

using Bytes = std::vector<std::uint8_t>;
using Millis = std::int64_t;

// An entry of a queue or a playlist (the specification's section 1): a tune, with or without a subtune
// number. Subtunes count from 1; 0 means the tune's default subtune, decided when its file is there.
struct Entry {
    std::string path;      // a collection path such as "/MUSICIANS/H/Hubbard_Rob/Commando.sid", or "file://..."
    int subtune = 0;
    std::string md5;       // of the file, 32 hexadecimal digits, when known (SPEC-170)

    bool operator==(const Entry& other) const { return path == other.path && subtune == other.subtune; }
    bool operator!=(const Entry& other) const { return !(*this == other); }
};

}  // namespace spz
