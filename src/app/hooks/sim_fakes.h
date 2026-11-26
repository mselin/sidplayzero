// SPDX-License-Identifier: GPL-3.0-or-later
// The fakes a simulator run is given (SPEC-253): set up from the --fakes folder, changed by the script's
// `fake` command, and looked at by its `expect-...` commands.
#pragma once

#include <string>

#include "fakes.h"

namespace spz::hooks {

struct SimFakes {
    fake::HttpClient http;
    fake::SystemVolume volume;
    fake::Backlight backlight;
    fake::Memory memory;
    fake::Unpacker unpacker;
    fake::AudioSink sink;
    fake::Engine engine;
    fake::Files files;
    fake::SoundBackend backend;
    std::string folder;                  // the --fakes folder, "" if none
    std::int64_t list_delay_ms = 0;
    std::size_t samples_saved = 0;       // how much of sink.received `save-samples` has written out

    // Where a file a script names is: "fixtures/..." is under tests/fixtures (the SPZ_FIXTURES variable),
    // anything else under the --fakes folder.
    std::string resolve(const std::string& name) const;
    // Reads fakes.ini and http.tsv from the folder. False, with the reason, if one cannot be read.
    bool load(const std::string& folder, const std::string& home, std::string* why);
    // `fake NAME VALUE` for the values of fakes.ini. False if there is no such value.
    bool set(const std::string& name, const std::string& value, std::string* why);
    // The log `expect-log NAME` looks at: "volume", "backlight" or "unpacker". False for another name.
    bool log(const std::string& name, std::string* text) const;
};

}  // namespace spz::hooks
