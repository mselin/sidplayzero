// SPDX-License-Identifier: GPL-3.0-or-later
// The stack of screens (SPEC-090) and the rows of the Settings screen (SPEC-101), with what a build without
// a part leaves out of them (SPEC-115).
#include <map>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "keys_support.h"
#include "spz/screen_settings.h"
#include "spz/screen_stack.h"

namespace {

using Stack = std::vector<std::string>;

struct TableRow {
    const char* name;
    const char* key;
    const char* part;        // "" for Always
    bool choice;
};

// SPEC-101's table, with the three ROM files as three rows.
const std::vector<TableRow> kSettingsTable = {
    {"Collection folder", "collection", "", false},
    {"Engine", "engine", "", true},
    {"When a song ends", "when_song_ends", "modes", true},
    {"Default length", "default_length", "", false},
    {"Repeat all", "repeat_all", "modes", true},
    {"Shuffle", "shuffle", "shuffle", true},
    {"Sample rate", "sample_rate", "rate", true},
    {"SID model", "sid_model", "force", true},
    {"Clock", "clock", "force", true},
    {"KERNAL ROM", "rom_kernal", "roms", false},
    {"BASIC ROM", "rom_basic", "roms", false},
    {"Character ROM", "rom_chargen", "roms", false},
    {"Resume on start", "resume_on_start", "resume", true},
    {"Screen off while playing", "screen_off_after", "screen-off", false},
    {"Address for single files", "url_files", "", false},
    {"Address for the complete archive", "url_archive", "download-all", false},
};

// Which screen an action of the key map opens (SPEC-090's table, and the keys that show a tune in the
// browser or play one).
const std::map<std::string, std::string> kOpens = {
    {"open_playing", "playing"}, {"open_search", "search"}, {"open_playlists", "playlists"},
    {"open_playlist", "playlist"}, {"open_settings", "settings"}, {"open_hvsc", "hvsc"}, {"open_about", "about"},
    {"browse_here", "browser"}, {"open_or_play", "playing"}, {"play_result", "playing"}, {"play_entry", "playing"},
};

// The places of the key map a screen has.
std::vector<std::string> places_of(const std::string& screen) {
    if (screen == "search") return {"search-field", "search-results"};
    return {screen};
}

}  // namespace

TEST_CASE("SPEC-090 the stack starts as the browser alone, a key that opens a screen puts it on top, and ESC closes the top one") {
    spz::ScreenStack stack;
    CHECK(stack.screens() == Stack{"browser"});
    CHECK(stack.top() == "browser");
    stack.open("search");
    CHECK(stack.screens() == Stack{"browser", "search"});
    stack.open("playing");
    stack.open("settings");
    stack.open("hvsc");
    CHECK(stack.screens() == Stack{"browser", "search", "playing", "settings", "hvsc"});
    CHECK(stack.top() == "hvsc");
    // Four short presses of ESC reach the browser.
    CHECK(stack.close_top());
    CHECK(stack.screens() == Stack{"browser", "search", "playing", "settings"});
    CHECK(stack.close_top());
    CHECK(stack.close_top());
    CHECK(stack.close_top());
    CHECK(stack.screens() == Stack{"browser"});
    // On the browser ESC is the "parent folder" key: the stack closes nothing.
    CHECK_FALSE(stack.close_top());
    CHECK(stack.screens() == Stack{"browser"});
}

TEST_CASE("SPEC-090 opening a screen that is in the stack returns to it and closes those above it") {
    spz::ScreenStack stack;
    stack.open("playing");
    stack.open("settings");
    stack.open("hvsc");
    stack.open("settings");                              // TAB in HVSC: the stack that Settings had
    CHECK(stack.screens() == Stack{"browser", "playing", "settings"});
    stack.open("about");
    stack.open("settings");                              // TAB in About
    CHECK(stack.screens() == Stack{"browser", "playing", "settings"});
    stack.open("settings");                              // already on top: nothing changes
    CHECK(stack.screens() == Stack{"browser", "playing", "settings"});
    stack.open("hvsc");
    stack.open("playing");                               // P from HVSC
    CHECK(stack.screens() == Stack{"browser", "playing"});
    stack.open("settings");
    stack.open("browser");                               // "Browse": back to the bottom
    CHECK(stack.screens() == Stack{"browser"});
}

TEST_CASE("SPEC-090 SPEC-048 the bottom of the stack is the browser or, in its place, the empty screen") {
    spz::ScreenStack stack;
    stack.set_bottom("empty");
    CHECK(stack.screens() == Stack{"empty"});
    stack.open("hvsc");                                  // "Get HVSC" on the empty screen
    CHECK(stack.screens() == Stack{"empty", "hvsc"});
    stack.set_bottom("browser");                         // a collection appeared: the screens above stay
    CHECK(stack.screens() == Stack{"browser", "hvsc"});
    CHECK(stack.close_top());
    CHECK(stack.screens() == Stack{"browser"});
    CHECK_FALSE(stack.close_top());
}

TEST_CASE("SPEC-090 by the keys of the key map no screen is ever in the stack twice, and the stack is never deeper than six") {
    // Every stack that the keys can reach from the browser: from the top screen, each key of its places
    // whose action opens a screen (the key map's rows and groups, read here), and ESC.
    const std::vector<spz::KeyMapRow> file = spz::test::keymap_file();
    std::set<Stack> seen;
    std::vector<Stack> todo = {Stack{"browser"}};
    std::size_t deepest = 0;
    while (!todo.empty()) {
        const Stack now = todo.back();
        todo.pop_back();
        if (!seen.insert(now).second) continue;
        CAPTURE(now);
        CHECK(std::set<std::string>(now.begin(), now.end()).size() == now.size());
        CHECK(now.size() <= 6);
        REQUIRE(now.size() <= 9);
        deepest = std::max(deepest, now.size());
        for (const std::string& place : places_of(now.back())) {
            for (const std::string& key : spz::test::all_key_names()) {
                const std::string action = spz::test::action_by_the_file(file, place, key);
                const auto opens = kOpens.find(action);
                spz::ScreenStack stack;
                for (std::size_t i = 1; i < now.size(); ++i) stack.open(now[i]);
                if (opens != kOpens.end()) stack.open(opens->second);
                else if (action == "back") stack.close_top();
                else continue;
                todo.push_back(stack.screens());
            }
        }
    }
    CHECK(seen.count(Stack{"browser", "search", "playing", "settings", "hvsc"}) == 1);
    CHECK(seen.count(Stack{"browser", "settings", "about"}) == 1);
    if (SPZ_PART_PLAYLISTS) {
        CHECK(deepest == 6);
        CHECK(seen.count(Stack{"browser", "playlists", "playlist", "playing", "settings", "hvsc"}) == 1);
    } else {
        CHECK(deepest == 5);
    }
}

TEST_CASE("SPEC-101 the rows of the Settings table are the specification's, in its order, each with its setting and its part") {
    const std::vector<spz::SettingsRow>& table = spz::settings_table();
    REQUIRE(table.size() == kSettingsTable.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        CAPTURE(kSettingsTable[i].name);
        CHECK(table[i].name == kSettingsTable[i].name);
        CHECK(table[i].key == kSettingsTable[i].key);
        CHECK(table[i].part == kSettingsTable[i].part);
        CHECK((table[i].kind == spz::SettingKind::Choice) == kSettingsTable[i].choice);
    }
}

TEST_CASE("SPEC-101 SPEC-115 the Settings screen has the rows that are always built and those whose part is, and no others") {
    std::vector<std::string> expected;
    for (const TableRow& row : kSettingsTable) {
        if (spz::test::built(row.part)) expected.push_back(row.name);
    }
    std::vector<std::string> actual;
    for (const spz::SettingsRow& row : spz::settings_rows()) actual.push_back(row.name);
    CHECK(actual == expected);
    // Whatever is built, these four are there, in this order.
    std::vector<std::string> always;
    for (const std::string& name : actual) {
        if (name == "Collection folder" || name == "Engine" || name == "Default length" ||
            name == "Address for single files") {
            always.push_back(name);
        }
    }
    CHECK(always == std::vector<std::string>{"Collection folder", "Engine", "Default length",
                                             "Address for single files"});
    REQUIRE(actual.size() >= 2);
    CHECK(actual[0] == "Collection folder");
    CHECK(actual[1] == "Engine");
}

TEST_CASE("SPEC-101 SPEC-130 a setting with a fixed set of values has exactly those values, in order; the others open a prompt") {
    const std::map<std::string, std::vector<std::string>> values = {
        {"engine", {"auto", "accurate", "light"}},
        {"sample_rate", {"44100", "48000"}},
        {"when_song_ends", {"subtunes", "next_tune", "repeat", "stop"}},
        {"repeat_all", {"on", "off"}},
        {"shuffle", {"on", "off"}},
        {"sid_model", {"auto", "6581", "8580"}},
        {"clock", {"auto", "pal", "ntsc"}},
        {"resume_on_start", {"on", "off"}},
    };
    const std::map<std::string, spz::SettingKind> prompts = {
        {"collection", spz::SettingKind::Path}, {"rom_kernal", spz::SettingKind::Path},
        {"rom_basic", spz::SettingKind::Path}, {"rom_chargen", spz::SettingKind::Path},
        {"url_files", spz::SettingKind::Address}, {"url_archive", spz::SettingKind::Address},
        {"default_length", spz::SettingKind::Time},
    };
    for (const spz::SettingsRow& row : spz::settings_table()) {
        CAPTURE(row.key);
        const auto fixed = values.find(row.key);
        if (fixed != values.end()) {
            CHECK(row.kind == spz::SettingKind::Choice);
            CHECK(row.values == fixed->second);
            continue;
        }
        CHECK(row.kind != spz::SettingKind::Choice);
        CHECK(row.values.empty());
        const auto prompt = prompts.find(row.key);
        if (prompt != prompts.end()) CHECK(row.kind == prompt->second);
        // SPEC-099: 255 characters for a path or an address, 5 for a length.
        if (row.kind == spz::SettingKind::Path || row.kind == spz::SettingKind::Address) CHECK(row.limit == 255);
        if (row.key == "default_length") CHECK(row.limit == 5);
    }
}
