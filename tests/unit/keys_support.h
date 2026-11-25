// SPDX-License-Identifier: GPL-3.0-or-later
// What the unit tests of keys share: docs/keymap.tsv as the test itself reads it, the rules of SPEC-092 and
// SPEC-107 worked out from that file, which parts this build has, and a listener that records what
// KeyInput gives.
#pragma once

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fakes.h"
#include "spz/key_input.h"
#include "spz/keymap.h"

namespace spz::test {

// docs/keymap.tsv, read here and not through `core`: every row under the header, "-" as "".
inline std::vector<spz::KeyMapRow> keymap_file() {
    const std::string path = std::string(SPZ_FIXTURES) + "/../../docs/keymap.tsv";
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<spz::KeyMapRow> rows;
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::string cell;
        std::istringstream split(line);
        while (std::getline(split, cell, '\t')) cells.push_back(cell == "-" ? "" : cell);
        if (!header) { header = true; continue; }
        if (cells.size() != 6) throw std::runtime_error("docs/keymap.tsv: a row without six columns: " + line);
        rows.push_back({cells[0], cells[1], cells[2], cells[3], cells[4], cells[5]});
    }
    return rows;
}

// SPEC-115: whether one part is in this build, from the build's own switches.
inline bool one_part_built(const std::string& part) {
    static const std::vector<std::pair<std::string, bool>> parts = {
        {"detail", SPZ_PART_DETAIL}, {"mini", SPZ_PART_MINI}, {"modes", SPZ_PART_MODES},
        {"shuffle", SPZ_PART_SHUFFLE}, {"restart", SPZ_PART_RESTART}, {"skip", SPZ_PART_SKIP},
        {"voices", SPZ_PART_VOICES}, {"bars", SPZ_PART_BARS}, {"notes", SPZ_PART_NOTES},
        {"rate", SPZ_PART_RATE}, {"force", SPZ_PART_FORCE}, {"roms", SPZ_PART_ROMS},
        {"too-slow", SPZ_PART_TOO_SLOW}, {"brightness", SPZ_PART_BRIGHTNESS},
        {"screenshot", SPZ_PART_SCREENSHOT}, {"playlists", SPZ_PART_PLAYLISTS},
        {"import-more", SPZ_PART_IMPORT_MORE}, {"export", SPZ_PART_EXPORT}, {"filters", SPZ_PART_FILTERS},
        {"resume", SPZ_PART_RESUME}, {"screen-off", SPZ_PART_SCREEN_OFF}, {"releases", SPZ_PART_RELEASES},
        {"download-all", SPZ_PART_DOWNLOAD_ALL}, {"usbsid", SPZ_PART_USBSID},
    };
    for (const auto& entry : parts) {
        if (entry.first == part) return entry.second;
    }
    return false;
}

// The names of SPEC-115's table, in its order.
inline std::vector<std::string> all_parts() {
    return {"detail", "mini", "modes", "shuffle", "restart", "skip", "voices", "bars", "notes", "rate", "force",
            "roms", "too-slow", "brightness", "screenshot", "playlists", "import-more", "export", "filters",
            "resume", "screen-off", "releases", "download-all", "usbsid"};
}

// The part column of a key-map row: "" is always built, "a|b" is built with any of them.
inline bool built(const std::string& part) {
    if (part.empty()) return true;
    std::string name;
    std::istringstream split(part);
    while (std::getline(split, name, '|')) {
        if (one_part_built(name)) return true;
    }
    return false;
}

// The places of the key map, by which groups apply there (SPEC-092, SPEC-114, SPEC-260).
inline std::vector<std::string> screen_places() {
    return {"browser", "empty", "playing", "search-results", "playlists", "playlist", "settings", "hvsc", "about",
            "selftest"};
}
inline std::vector<std::string> typing_places() { return {"search-field", "prompt"}; }
inline std::vector<std::string> panel_places() {
    return {"confirm", "text", "notice-panel", "more-playing", "pick-playlist", "more-playlist", "import", "filter",
            "help"};
}
inline std::vector<std::string> all_places() {
    std::vector<std::string> places = screen_places();
    for (const std::string& place : typing_places()) places.push_back(place);
    for (const std::string& place : panel_places()) places.push_back(place);
    places.push_back("key-step");
    return places;
}
inline bool one_of(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// The groups that apply in a place, in the order help lists them (SPEC-107).
inline std::vector<std::string> groups_of(const std::string& place) {
    if (one_of(screen_places(), place)) return {"screens", "lists", "always"};
    if (one_of(panel_places(), place)) return {"lists", "always"};
    if (one_of(typing_places(), place)) return {"always"};
    return {};
}

inline const spz::KeyMapRow* row_of(const std::vector<spz::KeyMapRow>& rows, const std::string& where,
                                    const std::string& key) {
    for (const spz::KeyMapRow& row : rows) {
        if (row.where == where && row.key == key) return &row;
    }
    return nullptr;
}

// SPEC-092, SPEC-115, from the file: what a key does in a place, "" for nothing.
inline std::string action_by_the_file(const std::vector<spz::KeyMapRow>& rows, const std::string& place,
                                      const std::string& key) {
    const spz::KeyMapRow* row = row_of(rows, place, key);
    if (row == nullptr && place == "key-step") row = row_of(rows, place, "Any");
    for (const std::string& group : groups_of(place)) {
        if (row == nullptr) row = row_of(rows, group, key);
    }
    if (row == nullptr || row->action == "nothing" || !built(row->part)) return "";
    return row->action;
}

// Every key name the key map may use (SPEC-111).
inline std::vector<std::string> all_key_names() {
    std::vector<std::string> names = {"Esc", "Tab", "Enter", "Backspace", "Space", "Shift", "Up", "Down", "Left",
                                      "Right", "Home", "End", "PageUp", "PageDown", "Delete", "PlayPause",
                                      "Previous", "Next", "Mute", "VolumeDown", "VolumeUp", "BrightnessDown",
                                      "BrightnessUp", "Help", "PrintScreen", "EscHold", "RightHold", "Char", "Any"};
    for (char c = 'A'; c <= 'Z'; ++c) names.push_back(std::string(1, c));
    for (char c = '0'; c <= '9'; ++c) names.push_back(std::string(1, c));
    return names;
}

// Records what KeyInput gives.
struct KeyRecorder : spz::KeyListener {
    std::vector<spz::KeyEvent> events;
    std::vector<bool> hold_notices;
    void on_key(const spz::KeyEvent& event) override { events.push_back(event); }
    void on_hold_notice(bool shown) override { hold_notices.push_back(shown); }

    // The actions of the events so far, joined by spaces, with "-" for an event without one.
    std::string actions() const {
        std::string all;
        for (const spz::KeyEvent& event : events) {
            if (!all.empty()) all += " ";
            all += event.action.empty() ? "-" : event.action;
        }
        return all;
    }
    // How many events had this action.
    int count(const std::string& action) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(),
                                              [&](const spz::KeyEvent& event) { return event.action == action; }));
    }
    // The characters typed so far, as text (they are all ASCII: SPEC-099).
    std::string typed() const {
        std::string text;
        for (const spz::KeyEvent& event : events) {
            if (event.action == "type") text += static_cast<char>(event.character);
        }
        return text;
    }
};

// A KeyInput on the fake clock, in a place the test names.
struct Keyboard {
    spz::fake::Clock clock;
    std::string place;
    KeyRecorder seen;
    spz::KeyInput input;

    explicit Keyboard(const std::string& where)
        : place(where), input(&clock, [this] { return place; }, &seen) {}

    void press(int code) { input.key(code, true); }
    void release(int code) { input.key(code, false); }
    void tap(int code) { press(code); release(code); }
    // Presses, lets `ms` of the fake clock pass, and releases.
    void hold(int code, std::int64_t ms) { press(code); clock.advance(ms); release(code); }
};

}  // namespace spz::test
