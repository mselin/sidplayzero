// SPDX-License-Identifier: GPL-3.0-or-later
// What the app reads from a tune's file itself, before the engine sees it: the check of SPEC-203 and the
// header's fields (SPEC-046, SPEC-096, SPEC-098).
#pragma once

#include <string>

#include "spz/engine.h"
#include "spz/types.h"

namespace spz {

// The limit of SPEC-203 and SPEC-150: a tune's file is at most this long.
const std::size_t kTuneSizeLimit = 1024 * 1024;

// SPEC-203: whether a file may be given to the engine. Returns "" if it may, else the message identifier
// "MSG_INVALID_SID".
std::string check_tune(const Bytes& file);

struct TuneInfo {
    std::string title, author, released;   // as UTF-8, converted from Windows-1252 (SPEC-098)
    int songs = 0;
    int default_song = 0;                  // counted from 1; a header's 0 is read as 1
    int chips = 1;                         // 1 to 3 (SPEC-075)
    SidModel model = SidModel::FromTune;   // FromTune when the header says "unknown" or "any"
    C64Clock clock = C64Clock::FromTune;
    bool needs_basic = false;              // SPEC-068
    int year = 0;                          // the first four-digit number in the released line, else 0 (SPEC-160)
};

// Reads the header of a file that passes check_tune(). False if it does not pass.
bool read_tune_info(const Bytes& file, TuneInfo* out);

// Text from a tune's header to UTF-8 (SPEC-098): Windows-1252, with a byte that Windows-1252 leaves
// undefined turned into "?", and the field cut at its first zero byte.
std::string cp1252_to_utf8(const std::string& bytes);

}  // namespace spz
