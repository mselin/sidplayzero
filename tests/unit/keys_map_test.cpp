// SPDX-License-Identifier: GPL-3.0-or-later
// The key map compiled into `core` (SPEC-094) against docs/keymap.tsv as this test reads it: the rows, the
// three groups (SPEC-092), the labels of the action bar (SPEC-091, SPEC-093), and parts (SPEC-115).
#include <array>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "keys_support.h"
#include "spz/keymap.h"

using spz::test::action_by_the_file;
using spz::test::built;
using spz::test::keymap_file;

TEST_CASE("SPEC-094 the key map compiled into core has exactly the rows of docs/keymap.tsv, in its order") {
    const std::vector<spz::KeyMapRow> file = keymap_file();
    const std::vector<spz::KeyMapRow>& compiled = spz::key_map();
    REQUIRE(file.size() == 109);          // the rows the file has today; a row added there is added here
    REQUIRE(compiled.size() == file.size());
    for (std::size_t i = 0; i < file.size(); ++i) {
        CAPTURE(i);
        CAPTURE(file[i].where);
        CAPTURE(file[i].key);
        CHECK(compiled[i].where == file[i].where);
        CHECK(compiled[i].key == file[i].key);
        CHECK(compiled[i].action == file[i].action);
        CHECK(compiled[i].label == file[i].label);
        CHECK(compiled[i].part == file[i].part);
        CHECK(compiled[i].help == file[i].help);
    }
}

TEST_CASE("SPEC-094 SPEC-092 what a key does, in every place and for every key name, is what the file's rows and groups give") {
    const std::vector<spz::KeyMapRow> file = keymap_file();
    for (const std::string& place : spz::test::all_places()) {
        for (const std::string& key : spz::test::all_key_names()) {
            CAPTURE(place);
            CAPTURE(key);
            CHECK(spz::key_action(place, key) == action_by_the_file(file, place, key));
        }
    }
}

TEST_CASE("SPEC-092 each place is a screen, a place where text is typed, a panel, or the key step") {
    for (const std::string& place : spz::test::screen_places()) {
        CAPTURE(place);
        CHECK(spz::place_kind(place) == spz::PlaceKind::Screen);
    }
    CHECK(spz::place_kind("search-field") == spz::PlaceKind::Typing);
    CHECK(spz::place_kind("prompt") == spz::PlaceKind::Typing);
    for (const std::string& place : spz::test::panel_places()) {
        CAPTURE(place);
        CHECK(spz::place_kind(place) == spz::PlaceKind::Panel);
    }
    CHECK(spz::place_kind("key-step") == spz::PlaceKind::Alone);
    CHECK(spz::place_kind("always") == spz::PlaceKind::Unknown);      // a group is not a place
    CHECK(spz::place_kind("cellar") == spz::PlaceKind::Unknown);
}

TEST_CASE("SPEC-092 the keys of `always` act in every place, also where text is typed and in a panel") {
    for (const std::string place : {"browser", "empty", "playing", "search-results", "settings", "hvsc", "about",
                                     "search-field", "prompt", "confirm", "text", "notice-panel", "help"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "PlayPause") == "play_pause");
        CHECK(spz::key_action(place, "Previous") == "previous_tune");
        CHECK(spz::key_action(place, "Next") == "next_tune");
        CHECK(spz::key_action(place, "Mute") == "volume_mute");
        CHECK(spz::key_action(place, "VolumeDown") == "volume_down");
        CHECK(spz::key_action(place, "VolumeUp") == "volume_up");
        CHECK(spz::key_action(place, "Help") == "toggle_help");
        CHECK(spz::key_action(place, "Esc") == "back");
        CHECK(spz::key_action(place, "EscHold") == "leave");
    }
}

TEST_CASE("SPEC-092 the keys of `lists` act wherever no text is typed, panels and help included, and not where it is") {
    const std::vector<std::pair<std::string, std::string>> keys = {
        {"PageUp", "page_up"}, {"PageDown", "page_down"}, {"Home", "first"}, {"End", "last"}};
    for (const std::string place : {"browser", "empty", "playing", "search-results", "settings", "hvsc", "about",
                                     "confirm", "text", "notice-panel", "help"}) {
        CAPTURE(place);
        for (const auto& key : keys) CHECK(spz::key_action(place, key.first) == key.second);
        if (std::string(place) != "playing") {
            CHECK(spz::key_action(place, "Up") == "up");
            CHECK(spz::key_action(place, "Down") == "down");
        }
    }
    // SPEC-094: on the now-playing screen Up and Down step through tunes instead.
    CHECK(spz::key_action("playing", "Up") == "previous_tune");
    CHECK(spz::key_action("playing", "Down") == "next_tune");
    // SPEC-099: in a prompt Up and Down do nothing; in the Search field Down moves to the results.
    CHECK(spz::key_action("prompt", "Up") == "");
    CHECK(spz::key_action("prompt", "Down") == "");
    CHECK(spz::key_action("prompt", "PageDown") == "");
    CHECK(spz::key_action("search-field", "Up") == "");
    CHECK(spz::key_action("search-field", "Down") == "to_results");
    // SPEC-099: there Home and End move the cursor.
    CHECK(spz::key_action("prompt", "Home") == "cursor_first");
    CHECK(spz::key_action("search-field", "End") == "cursor_last");
}

TEST_CASE("SPEC-092 the keys of `screens` act on a screen only, and a place can switch one off") {
    for (const std::string place : {"browser", "empty", "search-results", "hvsc", "about"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "Tab") == "open_settings");
        CHECK(spz::key_action(place, "Space") == "play_pause");
        CHECK(spz::key_action(place, "P") == "open_playing");
    }
    CHECK(spz::key_action("settings", "Tab") == "");               // switched off with "nothing"
    CHECK(spz::key_action("settings", "P") == "open_playing");
    CHECK(spz::key_action("playing", "P") == "");                  // switched off with "nothing"
    CHECK(spz::key_action("playing", "Tab") == "open_settings");
    for (const std::string place : {"confirm", "text", "notice-panel", "help", "prompt", "search-field"}) {
        CAPTURE(place);                                            // a panel takes the keys; so does typing
        CHECK(spz::key_action(place, "Tab") == "");
        CHECK(spz::key_action(place, "Space") == "");
        CHECK(spz::key_action(place, "P") == "");
        CHECK(spz::key_action(place, "L") == "");
    }
}

TEST_CASE("SPEC-093 a letter or digit the key map does not name for a place does nothing there") {
    CHECK(spz::key_action("browser", "9") == "");
    CHECK(spz::key_action("browser", "Q") == "");
    CHECK(spz::key_action("settings", "6") == "");
    CHECK(spz::key_action("settings", "7") == "");
    CHECK(spz::key_action("about", "8") == "");
    CHECK(spz::key_action("empty", "4") == "");
    CHECK(spz::key_action("browser", "Enter") == "open_or_play");
    CHECK(spz::key_action("browser", "Right") == "open_or_play");
    CHECK(spz::key_action("browser", "Left") == "parent");
    CHECK(spz::key_action("empty", "Enter") == "get_hvsc");
    CHECK(spz::key_action("confirm", "Enter") == "yes");
    CHECK(spz::key_action("cellar", "Enter") == "");               // not a place at all
}

TEST_CASE("SPEC-099 where text is typed, the place's own rows are for a character and for the keys that edit") {
    for (const std::string place : {"search-field", "prompt"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "Char") == "type");
        CHECK(spz::key_action(place, "Backspace") == "delete_before");
        CHECK(spz::key_action(place, "Delete") == "delete_after");
        CHECK(spz::key_action(place, "Left") == "cursor_left");
        CHECK(spz::key_action(place, "Right") == "cursor_right");
        CHECK(spz::key_action(place, "Home") == "cursor_first");
        CHECK(spz::key_action(place, "End") == "cursor_last");
        CHECK(spz::key_action(place, "Tab") == "");
    }
    CHECK(spz::key_action("prompt", "Enter") == "accept");
    CHECK(spz::key_action("search-field", "Enter") == "run_search");
    CHECK(spz::key_action("browser", "Char") == "");
}

TEST_CASE("SPEC-094 SPEC-111 in the key step no group applies: every key is recorded, and a held ESC leaves") {
    for (const std::string key : {"Enter", "Esc", "Tab", "Help", "PlayPause", "Up", "P", "4", "Char"}) {
        CAPTURE(key);
        CHECK(spz::key_action("key-step", key) == "record_key");
    }
    CHECK(spz::key_action("key-step", "EscHold") == "leave");
}

TEST_CASE("SPEC-091 SPEC-093 the labels over the keys 4 to 8 are the key map's, for every place") {
    const std::vector<spz::KeyMapRow> file = keymap_file();
    for (const std::string& place : spz::test::all_places()) {
        CAPTURE(place);
        std::array<std::string, 5> expected;
        for (int i = 0; i < 5; ++i) {
            const spz::KeyMapRow* row = spz::test::row_of(file, place, std::string(1, static_cast<char>('4' + i)));
            if (row != nullptr && built(row->part)) expected[i] = row->label;
        }
        CHECK(spz::action_labels(place) == expected);
    }
}

TEST_CASE("SPEC-101 SPEC-103 SPEC-093 Settings has Back, HVSC and About; About has Back and Notice; a panel has no labels") {
    using Labels = std::array<std::string, 5>;
    CHECK(spz::action_labels("settings") == Labels{"Back", "HVSC", "", "", "About"});
    CHECK(spz::action_labels("about") == Labels{"Back", "Notice", "", "", ""});
    CHECK(spz::action_labels("hvsc") == Labels{"Back", "Stop", "", "", ""});
    CHECK(spz::action_labels("empty") == Labels{"", "", "", "", ""});
    for (const std::string place : {"confirm", "prompt", "text", "notice-panel", "help", "search-field", "cellar"}) {
        CAPTURE(place);
        CHECK(spz::action_labels(place) == Labels{"", "", "", "", ""});
    }
    CHECK(spz::key_action("settings", "4") == "back");
    CHECK(spz::key_action("settings", "5") == "open_hvsc");
    CHECK(spz::key_action("settings", "8") == "open_about");
    CHECK(spz::key_action("about", "4") == "back");
    CHECK(spz::key_action("about", "5") == "show_notice");
}

TEST_CASE("SPEC-115 a part is built exactly when its build switch is on, and several joined by | when any of them is") {
    std::vector<std::string> expected;
    for (const std::string& part : spz::test::all_parts()) {
        CAPTURE(part);
        CHECK(spz::part_built(part) == spz::test::one_part_built(part));
        if (spz::test::one_part_built(part)) expected.push_back(part);
    }
    CHECK(spz::parts_built() == expected);
    CHECK(spz::part_built(""));                                    // a key that is always built
    CHECK_FALSE(spz::part_built("cellar"));                        // not a part
    CHECK(spz::part_built("usbsid|cellar") == spz::test::one_part_built("usbsid"));
    CHECK(spz::part_built("playlists|modes|shuffle|notes") ==
          (SPZ_PART_PLAYLISTS || SPZ_PART_MODES || SPZ_PART_SHUFFLE || SPZ_PART_NOTES));
    CHECK(spz::part_built("brightness|cellar") == static_cast<bool>(SPZ_PART_BRIGHTNESS));
}

TEST_CASE("SPEC-115 a key whose part is not built does nothing and has no label; one whose part is built has both") {
    const std::vector<spz::KeyMapRow> file = keymap_file();
    int with_part = 0;
    for (const spz::KeyMapRow& row : file) {
        if (row.part.empty()) continue;
        ++with_part;
        CAPTURE(row.where);
        CAPTURE(row.key);
        CAPTURE(row.part);
        // A row of a group is looked at in a place where the group applies.
        const std::string place = spz::test::one_of({"always", "lists", "screens"}, row.where) ? "browser" : row.where;
        CHECK(spz::key_action(place, row.key) == (built(row.part) ? row.action : ""));
        if (!row.label.empty()) {
            const std::size_t index = static_cast<std::size_t>(row.key[0] - '4');
            REQUIRE(index < 5);
            CHECK(spz::action_labels(place)[index] == (built(row.part) ? row.label : ""));
        }
    }
    CHECK(with_part == 39);               // the rows of the file that name a part today
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("SPEC-115 SPEC-095 with playlists built, the browser's keys 6 to 8 are Lists, Fav and Add [parts: playlists]") {
    using Labels = std::array<std::string, 5>;
    CHECK(spz::action_labels("browser") == Labels{"Playing", "Search", "Lists", "Fav", "Add"});
    CHECK(spz::key_action("browser", "6") == "open_playlists");
    CHECK(spz::key_action("browser", "7") == "toggle_favourite");
    CHECK(spz::key_action("browser", "8") == "add_to_playlist");
    CHECK(spz::place_kind("playlists") == spz::PlaceKind::Screen);
    CHECK(spz::key_action("pick-playlist", "Enter") == "choose");
    CHECK(spz::key_action("pick-playlist", "Down") == "down");      // a list panel: the `lists` keys act
    CHECK(spz::key_action("pick-playlist", "Tab") == "");
}
#endif

#if SPZ_PART_BRIGHTNESS
TEST_CASE("SPEC-115 SPEC-104 with brightness built, its two keys act in every place [parts: brightness]") {
    for (const std::string place : {"browser", "prompt", "confirm", "help", "settings"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "BrightnessDown") == "brightness_down");
        CHECK(spz::key_action(place, "BrightnessUp") == "brightness_up");
    }
}
#endif

#if SPZ_PART_SCREENSHOT
TEST_CASE("SPEC-115 SPEC-108 with screenshot built, Print Screen acts in every place [parts: screenshot]") {
    for (const std::string place : {"browser", "prompt", "confirm", "help", "playing"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "PrintScreen") == "screenshot");
    }
}
#endif

#if SPZ_PART_TOO_SLOW
TEST_CASE("SPEC-115 SPEC-092 with too-slow built, L is a key of every screen and of no panel [parts: too-slow]") {
    for (const std::string place : {"browser", "playing", "settings", "hvsc", "about", "search-results"}) {
        CAPTURE(place);
        CHECK(spz::key_action(place, "L") == "use_light_engine");
    }
    CHECK(spz::key_action("confirm", "L") == "");
    CHECK(spz::key_action("prompt", "L") == "");
}
#endif

TEST_CASE("SPEC-115 SPEC-095 SPEC-096 the keys of the browser and of now playing are there exactly when their parts are") {
    using Labels = std::array<std::string, 5>;
    const Labels browser = spz::action_labels("browser");
    CHECK(browser[0] == "Playing");
    CHECK(browser[1] == "Search");
    CHECK(browser[2] == (SPZ_PART_PLAYLISTS ? "Lists" : ""));
    CHECK(browser[3] == (SPZ_PART_PLAYLISTS ? "Fav" : ""));
    CHECK(browser[4] == (SPZ_PART_PLAYLISTS ? "Add" : ""));
    const Labels playing = spz::action_labels("playing");
    CHECK(playing[0] == "Browse");
    CHECK(playing[1] == "Stop");
    CHECK(playing[2] == (SPZ_PART_MODES ? "Mode" : ""));
    CHECK(playing[3] == (SPZ_PART_PLAYLISTS ? "Fav" : ""));
    CHECK(playing[4] == ((SPZ_PART_PLAYLISTS || SPZ_PART_MODES || SPZ_PART_SHUFFLE || SPZ_PART_NOTES) ? "More" : ""));
    CHECK(spz::key_action("playing", "Backspace") == (SPZ_PART_RESTART ? "restart" : ""));
    CHECK(spz::key_action("playing", "1") == (SPZ_PART_VOICES ? "toggle_voice_1" : ""));
    CHECK(spz::key_action("playing", "RightHold") == (SPZ_PART_SKIP ? "skip_ahead" : ""));
    CHECK(spz::key_action("playing", "Right") == "next_subtune");
    CHECK(spz::key_action("browser", "BrightnessUp") == (SPZ_PART_BRIGHTNESS ? "brightness_up" : ""));
    CHECK(spz::key_action("browser", "PrintScreen") == (SPZ_PART_SCREENSHOT ? "screenshot" : ""));
    CHECK(spz::key_action("browser", "L") == (SPZ_PART_TOO_SLOW ? "use_light_engine" : ""));
}
