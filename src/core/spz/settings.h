// SPDX-License-Identifier: GPL-3.0-or-later
// The settings and their defaults (SPEC-130). The file they are kept in is spz/settings_file.h.
#pragma once

#include <string>

#include "spz/engine.h"

namespace spz {

enum class EngineChoice { Auto, Accurate, Light };                  // SPEC-004
enum class WhenSongEnds { Subtunes, NextTune, Repeat, Stop };       // SPEC-065

struct Settings {
    std::string collection = "~/C64Music";
    EngineChoice engine = EngineChoice::Auto;
    int sample_rate = 48000;                                        // 44100 or 48000
    WhenSongEnds when_song_ends = WhenSongEnds::Subtunes;
    bool repeat_all = false;
    bool shuffle = false;
    int default_length_s = 210;                                     // 0: until told otherwise; else 5 to 3599
    SidModel sid_model = SidModel::FromTune;
    C64Clock clock = C64Clock::FromTune;
    std::string rom_kernal, rom_basic, rom_chargen;
    bool resume_on_start = false;
    int screen_off_after_s = 0;                                     // 0: never; else 10 to 3600
    std::string url_files = "https://hvsc.c64.org/download/C64Music";
    std::string url_archive = "https://hvsc.brona.dk/HVSC/HVSC_{release}-all-of-them.7z";
};

}  // namespace spz
