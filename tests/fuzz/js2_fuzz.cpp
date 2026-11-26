// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 SPEC-176 fuzz: importing JSidplay2's .js2 file
//
// The .js2 importer of SPEC-176 takes any bytes as JSidplay2's list of tunes.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "spz/importers.h"
#include "spz/playlist.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const spz::Bytes file(data, data + size);
    const spz::ImportRead read = spz::read_js2(file);
    // Whatever the bytes: no more entries than SPEC-204 allows, and every one of them valid UTF-8 text.
    if (read.lines.size() > spz::kPlaylistEntryLimit) std::abort();
    for (const spz::ImportLine& line : read.lines) {
        if (line.text.empty() || !spz::lists_text::is_valid_utf8(line.text)) std::abort();
        if (line.subtune < 0 || line.subtune > 256) std::abort();
    }
    return 0;
}
