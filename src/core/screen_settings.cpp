// SPDX-License-Identifier: GPL-3.0-or-later
// The rows of the Settings screen (SPEC-101), with the values of SPEC-130.
#include "spz/screen_settings.h"

#include <utility>

#include "spz/keymap.h"
#include "spz/text_field.h"

namespace spz {

namespace {

SettingsRow choice(const char* name, const char* key, const char* part, std::vector<std::string> values) {
    SettingsRow row;
    row.name = name;
    row.key = key;
    row.part = part;
    row.kind = SettingKind::Choice;
    row.values = std::move(values);
    return row;
}

SettingsRow prompt(const char* name, const char* key, const char* part, SettingKind kind, std::size_t limit) {
    SettingsRow row;
    row.name = name;
    row.key = key;
    row.part = part;
    row.kind = kind;
    row.limit = limit;
    return row;
}

}  // namespace

const std::vector<SettingsRow>& settings_table() {
    static const std::vector<SettingsRow> table = {
        prompt("Collection folder", "collection", "", SettingKind::Path, kPathFieldLimit),
        choice("Engine", "engine", "", {"auto", "accurate", "light"}),
        choice("When a song ends", "when_song_ends", "modes", {"subtunes", "next_tune", "repeat", "stop"}),
        prompt("Default length", "default_length", "", SettingKind::Time, kLengthFieldLimit),
        choice("Repeat all", "repeat_all", "modes", {"on", "off"}),
        choice("Shuffle", "shuffle", "shuffle", {"on", "off"}),
        choice("Sample rate", "sample_rate", "rate", {"44100", "48000"}),
        choice("SID model", "sid_model", "force", {"auto", "6581", "8580"}),
        choice("Clock", "clock", "force", {"auto", "pal", "ntsc"}),
        prompt("KERNAL ROM", "rom_kernal", "roms", SettingKind::Path, kPathFieldLimit),
        prompt("BASIC ROM", "rom_basic", "roms", SettingKind::Path, kPathFieldLimit),
        prompt("Character ROM", "rom_chargen", "roms", SettingKind::Path, kPathFieldLimit),
        choice("Resume on start", "resume_on_start", "resume", {"on", "off"}),
        // Seconds, 0 or 10 to 3600: a time, typed as a number in a field of a length's size.
        prompt("Screen off while playing", "screen_off_after", "screen-off", SettingKind::Time, kLengthFieldLimit),
        prompt("Address for single files", "url_files", "", SettingKind::Address, kPathFieldLimit),
        prompt("Address for the complete archive", "url_archive", "download-all", SettingKind::Address,
               kPathFieldLimit),
    };
    return table;
}

std::vector<SettingsRow> settings_rows() {
    std::vector<SettingsRow> rows;
    for (const SettingsRow& row : settings_table()) {
        if (part_built(row.part)) rows.push_back(row);
    }
    return rows;
}

}  // namespace spz
