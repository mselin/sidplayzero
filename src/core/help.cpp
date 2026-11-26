// SPDX-License-Identifier: GPL-3.0-or-later
// Help (SPEC-107), made from the key map, and the names of the keys on the device (SPEC-111).
#include "spz/help.h"

#include "spz/keymap.h"

namespace spz {

namespace {

struct DeviceName {
    const char* key;
    const char* name;
};

// SPEC-111's last column, for the keys that are not their own keys.
const DeviceName kDeviceNames[] = {
    {"Up", "fn+F"},           {"Down", "fn+X"},          {"Left", "fn+Z"},         {"Right", "fn+C"},
    {"Home", "fn+K"},         {"End", "fn+N"},           {"PageUp", "fn+L"},       {"PageDown", "fn+M"},
    {"Delete", "fn+Backspace"},
    {"PlayPause", "fn+Q"},    {"Previous", "fn+W"},      {"Next", "fn+E"},
    {"Mute", "fn+A"},         {"VolumeDown", "fn+S"},    {"VolumeUp", "fn+D"},
    {"BrightnessDown", "fn+U"}, {"BrightnessUp", "fn+I"},
    {"Help", "fn+H"},         {"PrintScreen", "fn+J"},
    {"Shift", "shift"},
    // No one key: the specification gives these no name, so help says what they stand for.
    {"Char", "letters"},      {"Any", "any key"},
};

bool has_own_row(const std::string& place, const std::string& key) {
    for (const KeyMapRow& row : key_map()) {
        if (row.where == place && row.key == key) return true;
    }
    return false;
}

void add(const KeyMapRow& row, std::vector<HelpLine>* lines) {
    if (row.action == "nothing" || !part_built(row.part)) return;
    lines->push_back(HelpLine{key_device_name(row.key), row.help});
}

}  // namespace

std::string key_device_name(const std::string& key) {
    if (key == "EscHold") return key_device_name("Esc");
    if (key == "RightHold") return key_device_name("Right");
    for (const DeviceName& name : kDeviceNames) {
        if (key == name.key) return name.name;
    }
    return key;
}

std::vector<HelpLine> help_lines(const std::string& place) {
    std::vector<HelpLine> lines;
    std::vector<std::string> groups;
    switch (place_kind(place)) {
        case PlaceKind::Screen: groups = {"screens", "lists", "always"}; break;
        case PlaceKind::Panel: groups = {"lists", "always"}; break;
        case PlaceKind::Typing: groups = {"always"}; break;
        case PlaceKind::Alone: break;
        case PlaceKind::Unknown: return lines;
    }
    for (const KeyMapRow& row : key_map()) {
        if (row.where == place) add(row, &lines);
    }
    // A group's row for a key the place has a row of its own for is not what that key does there: the
    // place's row is (a `screens` key switched off, Up and Down on the now-playing screen).
    for (const std::string& group : groups) {
        for (const KeyMapRow& row : key_map()) {
            if (row.where == group && !has_own_row(place, row.key)) add(row, &lines);
        }
    }
    return lines;
}

}  // namespace spz
