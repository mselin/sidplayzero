// SPDX-License-Identifier: GPL-3.0-or-later
// Help (SPEC-107): the lines for each place are exactly what the rule gives from docs/keymap.tsv, which
// this test reads itself; and the names of the keys on the device (SPEC-111).
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "keys_support.h"
#include "spz/help.h"

namespace {

using Lines = std::vector<std::pair<std::string, std::string>>;   // a key's name in the key map, and the words

// SPEC-107's rule, from the file: the place's rows, then `screens` (a screen only, without a key the place
// has switched off), then `lists` (where no text is typed), then `always`; no row whose action is
// "nothing" or whose part is not built. With `replaced_too`, a row of a group is also left out when the
// place has a row of its own for that key (Up and Down on the now-playing screen: see the test below).
Lines lines_by_the_file(const std::vector<spz::KeyMapRow>& rows, const std::string& place, bool replaced_too) {
    Lines lines;
    auto add = [&](const spz::KeyMapRow& row) {
        if (row.action != "nothing" && spz::test::built(row.part)) lines.push_back({row.key, row.help});
    };
    for (const spz::KeyMapRow& row : rows) {
        if (row.where == place) add(row);
    }
    for (const std::string& group : spz::test::groups_of(place)) {
        for (const spz::KeyMapRow& row : rows) {
            if (row.where != group) continue;
            const spz::KeyMapRow* own = spz::test::row_of(rows, place, row.key);
            if (own != nullptr && (group == "screens" || replaced_too)) continue;
            add(row);
        }
    }
    return lines;
}

Lines lines_of_core(const std::string& place) {
    Lines lines;
    for (const spz::HelpLine& line : spz::help_lines(place)) lines.push_back({line.key, line.words});
    return lines;
}

Lines with_device_names(Lines lines) {
    for (auto& line : lines) line.first = spz::key_device_name(line.first);
    return lines;
}

bool has_key(const Lines& lines, const std::string& key) {
    for (const auto& line : lines) {
        if (line.first == key) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("SPEC-111 SPEC-107 the name of each key on the device is the one in the specification's table") {
    const std::vector<std::pair<std::string, std::string>> names = {
        {"Up", "fn+F"}, {"Down", "fn+X"}, {"Left", "fn+Z"}, {"Right", "fn+C"},
        {"Home", "fn+K"}, {"End", "fn+N"}, {"PageUp", "fn+L"}, {"PageDown", "fn+M"},
        {"Delete", "fn+Backspace"},
        {"PlayPause", "fn+Q"}, {"Previous", "fn+W"}, {"Next", "fn+E"},
        {"Mute", "fn+A"}, {"VolumeDown", "fn+S"}, {"VolumeUp", "fn+D"},
        {"BrightnessDown", "fn+U"}, {"BrightnessUp", "fn+I"},
        {"Help", "fn+H"}, {"PrintScreen", "fn+J"},
        {"Shift", "shift"},
    };
    for (const auto& name : names) {
        CAPTURE(name.first);
        CHECK(spz::key_device_name(name.first) == name.second);
    }
    // "Their own keys": a key that is its own is called by its name, whatever the case of its letters.
    for (const std::string own : {"Esc", "Tab", "Enter", "Backspace", "Space", "A", "P", "Z", "0", "4", "9"}) {
        CAPTURE(own);
        std::string given = spz::key_device_name(own), wanted = own;
        for (char& c : given) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        CHECK(given == wanted);
    }
    // A held key is called as the key that is held.
    CHECK(spz::key_device_name("EscHold") == spz::key_device_name("Esc"));
    CHECK(spz::key_device_name("RightHold") == spz::key_device_name("Right"));
}

TEST_CASE("SPEC-107 for every place, the lines of help are exactly those the rule gives from docs/keymap.tsv, with the same words") {
    const std::vector<spz::KeyMapRow> file = spz::test::keymap_file();
    for (const std::string& place : spz::test::all_places()) {
        if (place == "playing") continue;                // the test after this one
        CAPTURE(place);
        const Lines expected = with_device_names(lines_by_the_file(file, place, false));
        const Lines actual = lines_of_core(place);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CAPTURE(i);
            CHECK(actual[i].first == expected[i].first);
            CHECK(actual[i].second == expected[i].second);
        }
    }
}

TEST_CASE("SPEC-107 help for the now-playing screen: its own rows first, then the groups, with or without the two `lists` rows it replaces") {
    // The now-playing screen is the one place with rows of its own for two keys of `lists`, Up and Down
    // (SPEC-094). SPEC-107 says which `screens` rows a place's own rows take out of help and does not say it
    // of `lists` rows; so "Up" and "Down" of `lists` may be listed after "Previous tune" and "Next tune",
    // or left out. Everything else is fixed.
    const std::vector<spz::KeyMapRow> file = spz::test::keymap_file();
    const Lines actual = lines_of_core("playing");
    const Lines as_written = with_device_names(lines_by_the_file(file, "playing", false));
    const Lines without_replaced = with_device_names(lines_by_the_file(file, "playing", true));
    REQUIRE(as_written.size() == without_replaced.size() + 2);
    CHECK((actual == as_written || actual == without_replaced));
    REQUIRE(actual.size() >= 6);
    CHECK(actual[0].second == "Previous subtune");
    CHECK(actual[1].second == "Next subtune");
    CHECK(actual.back().second == "Hold to leave SidPlayZero");
    CHECK_FALSE(has_key(actual, spz::key_device_name("P")));     // switched off there with "nothing"
}

TEST_CASE("SPEC-107 help in Settings has no line for TAB, and help over a confirm panel none for TAB, Space, P or L") {
    const Lines settings = lines_of_core("settings");
    CHECK_FALSE(has_key(settings, spz::key_device_name("Tab")));
    CHECK(has_key(settings, spz::key_device_name("Space")));
    CHECK(has_key(settings, spz::key_device_name("P")));
    const Lines confirm = lines_of_core("confirm");
    for (const char* key : {"Tab", "Space", "P", "L"}) {
        CAPTURE(key);
        CHECK_FALSE(has_key(confirm, spz::key_device_name(key)));
    }
    REQUIRE(!confirm.empty());
    CHECK(confirm[0].first == spz::key_device_name("Enter"));
    CHECK(confirm[0].second == "Yes");
    CHECK(has_key(confirm, "fn+X"));                     // the `lists` rows are there: no text is typed
    CHECK(has_key(confirm, "fn+H"));                     // and the `always` rows
}

TEST_CASE("SPEC-107 where text is typed, help has the place's rows and the `always` rows and no others") {
    for (const char* place : {"prompt", "search-field"}) {
        CAPTURE(place);
        const Lines lines = lines_of_core(place);
        REQUIRE(lines.size() >= 8);
        CHECK(lines[0].second == "Type");
        CHECK(lines[1].first == spz::key_device_name("Backspace"));
        CHECK(lines[1].second == "Delete the character before");
        CHECK_FALSE(has_key(lines, "fn+L"));             // Page Up: a `lists` key
        CHECK_FALSE(has_key(lines, spz::key_device_name("Tab")));
        CHECK(has_key(lines, "fn+Q"));                   // Play or pause: an `always` key
        CHECK(lines.back().second == "Hold to leave SidPlayZero");
    }
}

TEST_CASE("SPEC-107 the groups come in the order screens, lists, always, each in the file's order") {
    const Lines about = lines_of_core("about");
    std::vector<std::string> words;
    for (const auto& line : about) words.push_back(line.second);
    const std::vector<std::string> before_parts = {"Back", "HVSC's notice", "Settings", "Play or pause", "Now playing"};
    REQUIRE(words.size() > before_parts.size());
    for (std::size_t i = 0; i < before_parts.size(); ++i) CHECK(words[i] == before_parts[i]);
    auto place_of = [&](const std::string& text) {
        return std::find(words.begin(), words.end(), text) - words.begin();
    };
    CHECK(place_of("Now playing") < place_of("Up"));
    CHECK(place_of("Up") < place_of("Down"));
    CHECK(place_of("Down") < place_of("A screenful up"));
    CHECK(place_of("Last entry") < place_of("Previous tune"));
    CHECK(place_of("Previous tune") < place_of("Mute"));
    CHECK(place_of("Show or hide help") < place_of("Back, or cancel"));
    CHECK(words.back() == "Hold to leave SidPlayZero");
}

TEST_CASE("SPEC-115 SPEC-107 help has a line for a key of a part exactly when that part is built") {
    auto has_words = [](const Lines& lines, const std::string& words) {
        for (const auto& line : lines) {
            if (line.second == words) return true;
        }
        return false;
    };
    const Lines browser = lines_of_core("browser");
    CHECK(has_words(browser, "Screen brighter") == static_cast<bool>(SPZ_PART_BRIGHTNESS));
    CHECK(has_words(browser, "Screen darker") == static_cast<bool>(SPZ_PART_BRIGHTNESS));
    CHECK(has_words(browser, "Save a picture of the screen") == static_cast<bool>(SPZ_PART_SCREENSHOT));
    CHECK(has_words(browser, "Use the light engine, when offered") == static_cast<bool>(SPZ_PART_TOO_SLOW));
    CHECK(has_words(browser, "Playlists") == static_cast<bool>(SPZ_PART_PLAYLISTS));
    CHECK(has_words(browser, "Add to a playlist") == static_cast<bool>(SPZ_PART_PLAYLISTS));
    CHECK(has_words(browser, "Search"));
    const Lines playing = lines_of_core("playing");
    CHECK(has_words(playing, "Hold to skip ahead") == static_cast<bool>(SPZ_PART_SKIP));
    CHECK(has_words(playing, "Start the subtune again") == static_cast<bool>(SPZ_PART_RESTART));
    CHECK(has_words(playing, "Voice 2 on or off") == static_cast<bool>(SPZ_PART_VOICES));
    CHECK(has_words(playing, "What happens when a song ends") == static_cast<bool>(SPZ_PART_MODES));
    CHECK(has_words(playing, "More") ==
          static_cast<bool>(SPZ_PART_PLAYLISTS || SPZ_PART_MODES || SPZ_PART_SHUFFLE || SPZ_PART_NOTES));
    CHECK(has_words(playing, "Stop"));
}
