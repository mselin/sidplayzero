// SPDX-License-Identifier: GPL-3.0-or-later
// The key map (SPEC-094), the groups (SPEC-092) and the parts (SPEC-115). The rows of docs/keymap.tsv are
// in keymap_table.cpp, made from that file by keymap_table.cmake.
#include "spz/keymap.h"

#include <sstream>

namespace spz {

namespace {

// SPEC-115's table, in its order, with whether this build has each.
struct Part {
    const char* name;
    bool built;
};
const Part kParts[] = {
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

// The places of SPEC-090 and SPEC-114, by kind (SPEC-092).
const char* const kScreens[] = {"browser", "empty", "playing", "search-results", "playlists", "playlist",
                                "settings", "hvsc", "about", "selftest"};
const char* const kTyping[] = {"search-field", "prompt"};
const char* const kPanels[] = {"confirm", "text", "notice-panel", "more-playing", "pick-playlist",
                               "more-playlist", "import", "filter", "help"};

bool one_part_built(const std::string& name) {
    for (const Part& part : kParts) {
        if (name == part.name) return part.built;
    }
    return false;
}

const KeyMapRow* row_of(const std::string& where, const std::string& key) {
    for (const KeyMapRow& row : key_map()) {
        if (row.where == where && row.key == key) return &row;
    }
    return nullptr;
}

// The groups that apply in a place, in the order help lists them.
std::vector<std::string> groups_of(PlaceKind kind) {
    switch (kind) {
        case PlaceKind::Screen: return {"screens", "lists", "always"};
        case PlaceKind::Panel: return {"lists", "always"};
        case PlaceKind::Typing: return {"always"};
        default: return {};
    }
}

}  // namespace

bool part_built(const std::string& part) {
    if (part.empty()) return true;
    std::istringstream names(part);
    std::string name;
    while (std::getline(names, name, '|')) {
        if (one_part_built(name)) return true;
    }
    return false;
}

std::vector<std::string> parts_built() {
    std::vector<std::string> names;
    for (const Part& part : kParts) {
        if (part.built) names.push_back(part.name);
    }
    return names;
}

PlaceKind place_kind(const std::string& place) {
    for (const char* name : kScreens) {
        if (place == name) return PlaceKind::Screen;
    }
    for (const char* name : kTyping) {
        if (place == name) return PlaceKind::Typing;
    }
    for (const char* name : kPanels) {
        if (place == name) return PlaceKind::Panel;
    }
    if (place == "key-step") return PlaceKind::Alone;
    return PlaceKind::Unknown;
}

std::string key_action(const std::string& place, const std::string& key) {
    const PlaceKind kind = place_kind(place);
    if (kind == PlaceKind::Unknown) return "";
    const KeyMapRow* row = row_of(place, key);
    if (row == nullptr) row = row_of(place, "Any");
    for (const std::string& group : groups_of(kind)) {
        if (row == nullptr) row = row_of(group, key);
    }
    if (row == nullptr || row->action == "nothing" || !part_built(row->part)) return "";
    return row->action;
}

std::array<std::string, 5> action_labels(const std::string& place) {
    std::array<std::string, 5> labels;
    if (place_kind(place) == PlaceKind::Unknown) return labels;
    for (int i = 0; i < 5; ++i) {
        const KeyMapRow* row = row_of(place, std::string(1, static_cast<char>('4' + i)));
        if (row != nullptr && part_built(row->part)) labels[static_cast<std::size_t>(i)] = row->label;
    }
    return labels;
}

}  // namespace spz
