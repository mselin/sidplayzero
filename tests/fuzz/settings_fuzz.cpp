// SPDX-License-Identifier: GPL-3.0-or-later
// test: SPEC-205 fuzz: the settings and state reader (SPEC-133), with any bytes as settings.ini and state.ini
//
// Whatever the bytes, both readers must end, must not read outside them, and must give only settings that
// the file can hold again: each setting's text, set back, is allowed and gives the same text.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "spz/settings_file.h"
#include "spz/state_file.h"
#include "spz/types.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const spz::Bytes file(data, data + size);

    const spz::SettingsRead settings = spz::read_settings(file);
    if (!settings.used && !settings.fault) std::abort();   // a file that is not used is a fault
    for (const std::string& key : spz::setting_keys()) {
        const std::string text = spz::setting_text(settings.settings, key);
        spz::Settings again;
        if (!spz::set_setting(&again, key, text)) std::abort();
        if (spz::setting_text(again, key) != text) std::abort();
    }

    const spz::StateRead state = spz::read_state(file);
    if (!state.used && !state.fault) std::abort();
    if (!state.used && state.state != spz::State()) std::abort();
    return 0;
}
