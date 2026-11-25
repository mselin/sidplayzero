// SPDX-License-Identifier: GPL-3.0-or-later
// Help (SPEC-107): the lines the help panel shows for a place, made from the key map (spz/keymap.h).
#pragma once

#include <string>
#include <vector>

namespace spz {

// SPEC-111: what a key is called on the device, from the key's name in the key map: "fn+F" for "Up",
// "fn+Backspace" for "Delete", "shift" for "Shift", and for a key that is its own key, its name. "EscHold"
// and "RightHold" are called as the key that is held. "Char" and "Any" are no one key, and the
// specification does not say what help calls them.
std::string key_device_name(const std::string& key);

struct HelpLine {
    std::string key;      // key_device_name() of the row's key
    std::string words;    // the row's help words
};

// SPEC-107: the lines of help for a place, in this order: the place's own rows of the key map; then the
// `screens` rows, if the place is a screen, without a key the place has switched off; then the `lists`
// rows, if no text is typed there; then the `always` rows. Each group in the file's order. A row whose
// action is "nothing", and a row whose part is not built (SPEC-115), is left out. In "key-step", where
// no group applies, only the place's own rows.
std::vector<HelpLine> help_lines(const std::string& place);

}  // namespace spz
