// SPDX-License-Identifier: GPL-3.0-or-later
// The rows of the Settings screen (SPEC-101): which setting each shows, how it is changed, and the part
// it belongs to (SPEC-115). The values themselves, and the file they are kept in, are SPEC-130's.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace spz {

// How a row is changed.
enum class SettingKind {
    Choice,    // a fixed set of values: Left and Right step through them, Enter steps forward
    Path,      // Enter opens a text prompt holding the value; at most 255 characters
    Address,   // the same
    Time,      // the same; at most 5 characters
};

struct SettingsRow {
    std::string name;                  // the row's words, as SPEC-101's table has them: "Collection folder"
    std::string key;                   // the setting's key in settings.ini: "collection"
    SettingKind kind = SettingKind::Choice;
    std::string part;                  // "" for a row that is always built, else its part
    std::vector<std::string> values;   // for a Choice: its values as settings.ini writes them, in SPEC-130's order
    std::size_t limit = 0;             // for the others: how many characters the prompt's field holds (SPEC-099)
};

// SPEC-101: every row of the table, in its order, whatever parts are built. The three ROM files are three
// rows: "KERNAL ROM", "BASIC ROM" and "Character ROM".
const std::vector<SettingsRow>& settings_table();

// SPEC-101, SPEC-115: the rows the Settings screen has in this build, in order: those that are always
// built, and those whose part is.
std::vector<SettingsRow> settings_rows();

}  // namespace spz
