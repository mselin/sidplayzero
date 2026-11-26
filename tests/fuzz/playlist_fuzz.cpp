// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 SPEC-170 fuzz: reading a playlist's file, and writing it again
//
// The playlist reader of SPEC-170 takes any bytes as a playlist's file; what it read is written again,
// which must be valid UTF-8 (SPEC-009) and read back with as many entries.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "spz/importers.h"
#include "spz/playlist.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const spz::Bytes file(data, data + size);
    const spz::PlaylistRead read = spz::read_playlist(file);
    if (read.playlist.entries.size() > spz::kPlaylistEntryLimit) std::abort();
    const spz::Bytes written = spz::write_playlist(read.playlist);
    if (!spz::lists_text::is_valid_utf8(std::string(written.begin(), written.end()))) std::abort();
    if (read.message.empty() && spz::read_playlist(written).playlist.entries.size() != read.playlist.entries.size()) {
        std::abort();
    }
    return 0;
}
