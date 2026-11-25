// SPDX-License-Identifier: GPL-3.0-or-later
// The C64 ROM files a user may name in the settings (SPEC-068). The app ships none.
#pragma once

#include <string>

#include "spz/files.h"
#include "spz/settings.h"
#include "spz/types.h"

namespace spz {

const std::size_t kKernalRomSize = 8192;
const std::size_t kBasicRomSize = 8192;
const std::size_t kChargenRomSize = 4096;

struct Roms {
    Bytes kernal, basic, chargen;   // each empty when it is not set, or was ignored
};

struct RomsRead {
    Roms roms;
    bool ignored = false;           // a setting named a file that is not there or has the wrong size:
                                    // the app shows the notice MSG_ROM_IGNORED at start
};

// Reads the files that `rom_kernal`, `rom_basic` and `rom_chargen` name. Each is used if it exists and
// has the right size (8,192, 8,192 and 4,096 bytes), and is otherwise ignored. A setting that is empty
// names no file, and nothing is ignored for it. Called on the quick worker, at start.
RomsRead read_roms(Files* files, const Settings& settings);

}  // namespace spz
