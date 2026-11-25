// SPDX-License-Identifier: GPL-3.0-or-later
// The key map (SPEC-094): docs/keymap.tsv, compiled into `core` when the app is built. Key handling, the
// action bar and help are all made from these rows, and nothing else in the source maps a key to an
// action. The groups are those of SPEC-092; which parts are built is SPEC-115.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace spz {

// One row of docs/keymap.tsv, as the file has it. A "-" in the file is "" here.
struct KeyMapRow {
    std::string where;    // a place ("browser", "prompt", "help" ...) or a group ("always", "lists", "screens")
    std::string key;      // a key name of SPEC-111, or "EscHold", "RightHold", "Char", "Any"
    std::string action;   // "open_or_play", "back", "nothing" ...
    std::string label;    // the word over a key 4 to 8 in the action bar, else ""
    std::string part;     // "" for a key that is always built; else a part, or several joined by "|"
    std::string help;     // the words shown in help, else ""
};

// SPEC-094: every row of docs/keymap.tsv, in the file's order, whatever parts are built.
const std::vector<KeyMapRow>& key_map();

// SPEC-115: whether a part is in this build. "" is always built; "a|b" is built when any of them is.
// False for a name that is not a part.
bool part_built(const std::string& part);

// SPEC-115: the names of the parts in this build, in the order of the specification's table.
std::vector<std::string> parts_built();

// What kind of place a name of the key map is, which decides the groups that apply there (SPEC-092).
enum class PlaceKind {
    Screen,    // a screen, or the result list of Search: its own rows, then `screens`, `lists` and `always`
    Typing,    // "search-field" and "prompt", where text is typed: its own rows, then `always`
    Panel,     // a panel where no text is typed, and help: its own rows, then `lists` and `always`
    Alone,     // "key-step": its own rows and no group (SPEC-260)
    Unknown,   // not a place
};
PlaceKind place_kind(const std::string& place);

// SPEC-092, SPEC-093, SPEC-115: what a key does in a place. `key` is a name as the key map has it: a key
// of SPEC-111, or "EscHold", "RightHold", "Char". The place's own row for the key decides; else the row of
// a group that applies there. In a place with a row for "Any", that row answers for every key the place
// has no row for. Returns the action, or "" when the key does nothing there: no row, the action "nothing",
// or a row whose part is not built.
std::string key_action(const std::string& place, const std::string& key);

// SPEC-091, SPEC-093, SPEC-115: the labels over the keys 4 to 8 in a place, as the key map gives them: ""
// where the place has no row for the key, or the row's part is not built. A screen takes a label away
// while its action cannot be used (SPEC-095); that is the screen's business, not the key map's.
std::array<std::string, 5> action_labels(const std::string& place);

}  // namespace spz
