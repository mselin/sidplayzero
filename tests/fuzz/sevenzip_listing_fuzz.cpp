// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: the reader of 7z's listing of SPEC-149, read_7z_listing(), on any text at all
//
// What `7z l -slt` prints names what is in an archive that came from the network. Whatever the text,
// reading it must end and must not read outside it; what it gives is then checked (SPEC-149) and
// measured (SPEC-146) as the complete download does, which must end too.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "spz/complete_download.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);
    std::vector<spz::ArchiveEntry> entries;
    if (spz::read_7z_listing(text, &entries)) {
        (void)spz::check_archive(entries);
        (void)spz::room_for_unpacking(size, entries, 4096);
    }
    return 0;
}
