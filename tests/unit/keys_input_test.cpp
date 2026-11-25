// SPDX-License-Identifier: GPL-3.0-or-later
// The one function where a key becomes an action (SPEC-111): press and release events in, with the key
// codes of the specification's tables written out here; key names, key-map actions and typed characters
// out. With it: typing (SPEC-099), the letters that are arrows (SPEC-093), the app's own key repeat
// (SPEC-045), ESC and its hold (SPEC-106), and Right and its hold (SPEC-096).
#include <string>
#include <vector>

#include "doctest.h"
#include "keys_support.h"
#include "spz/key_input.h"

using spz::test::Keyboard;

namespace {

struct NamedKey {
    const char* name;
    int code;
};

// SPEC-111's table.
const std::vector<NamedKey> kKeys = {
    {"Esc", 1}, {"Tab", 15}, {"Enter", 28}, {"Backspace", 14}, {"Space", 57},
    {"Q", 16}, {"W", 17}, {"E", 18}, {"R", 19}, {"T", 20}, {"Y", 21}, {"U", 22}, {"I", 23}, {"O", 24}, {"P", 25},
    {"A", 30}, {"S", 31}, {"D", 32}, {"F", 33}, {"G", 34}, {"H", 35}, {"J", 36}, {"K", 37}, {"L", 38},
    {"Z", 44}, {"X", 45}, {"C", 46}, {"V", 47}, {"B", 48}, {"N", 49}, {"M", 50},
    {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6}, {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10}, {"0", 11},
    {"Shift", 42},
    {"Up", 103}, {"Down", 108}, {"Left", 105}, {"Right", 106},
    {"Home", 102}, {"End", 107}, {"PageUp", 104}, {"PageDown", 109},
    {"Delete", 111},
    {"PlayPause", 164}, {"Previous", 165}, {"Next", 163},
    {"Mute", 113}, {"VolumeDown", 114}, {"VolumeUp", 115},
    {"BrightnessDown", 224}, {"BrightnessUp", 225},
    {"Help", 138}, {"PrintScreen", 99},
};

struct Sign {
    int code;
    char character;
};

// SPEC-099: the `sym` layer arrives as its own codes.
const std::vector<Sign> kSigns = {
    {26, '!'}, {27, '@'}, {39, '#'}, {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'}, {52, '*'}, {53, '('}, {94, ')'},
    {55, '~'}, {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'}, {73, '='}, {74, '['}, {75, ']'}, {76, '{'}, {77, '}'},
    {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'}, {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'}, {90, ','},
    {91, '.'}, {92, '/'}, {93, '?'},
};

const int kShift = 42, kEsc = 1, kEnter = 28, kTab = 15, kSpace = 57, kBackspace = 14, kDelete = 111;
const int kUp = 103, kDown = 108, kLeft = 105, kRight = 106, kHome = 102, kEnd = 107, kPageUp = 104, kPageDown = 109;
const int kLetterF = 33, kLetterX = 45, kLetterZ = 44, kLetterC = 46, kLetterP = 25, kLetterL = 38, kLetterA = 30;

int code_of(const std::string& name) {
    for (const NamedKey& key : kKeys) {
        if (name == key.name) return key.code;
    }
    return 0;
}

// Where no text is typed, four letters are the arrows (SPEC-093).
std::string name_outside_text(const std::string& name) {
    if (name == "F") return "Up";
    if (name == "X") return "Down";
    if (name == "Z") return "Left";
    if (name == "C") return "Right";
    return name;
}

}  // namespace

TEST_CASE("SPEC-111 each key code of the specification's table gives its key's name, once for each press") {
    const std::vector<spz::KeyMapRow> file = spz::test::keymap_file();
    REQUIRE(kKeys.size() == 61);
    for (const NamedKey& key : kKeys) {
        CAPTURE(key.name);
        Keyboard keys("browser");
        keys.tap(key.code);
        REQUIRE(keys.seen.events.size() == 1);
        const spz::KeyEvent& event = keys.seen.events[0];
        const std::string name = name_outside_text(key.name);
        CHECK(event.key == name);
        CHECK(event.code == key.code);
        CHECK(event.action == spz::test::action_by_the_file(file, "browser", name));
        CHECK(event.character == 0);
        CHECK_FALSE(event.repeat);
        CHECK(keys.seen.hold_notices.empty());
    }
}

TEST_CASE("SPEC-111 a key code that the tables do not name is ignored: Ctrl, Alt, sym, fn, Insert, F1 to F12") {
    for (const char* place : {"browser", "prompt", "settings", "help"}) {
        Keyboard keys(place);
        for (int code : {29, 56, 383, 389, 110, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 87, 88, 0, 255, 700}) {
            CAPTURE(place);
            CAPTURE(code);
            keys.tap(code);
            CHECK(keys.seen.events.empty());
        }
        keys.tap(kEnter);                                      // and the key after them is not disturbed
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].key == "Enter");
    }
}

TEST_CASE("SPEC-111 the action of a key is the key map's for the place the keys go to at that moment") {
    Keyboard keys("browser");
    keys.tap(kEnter);
    keys.place = "confirm";
    keys.tap(kEnter);
    keys.place = "settings";
    keys.tap(code_of("5"));
    keys.place = "about";
    keys.tap(code_of("5"));
    keys.tap(code_of("9"));
    CHECK(keys.seen.actions() == "open_or_play yes open_hvsc show_notice -");
}

TEST_CASE("SPEC-045 SPEC-111 a second press of a key that is still down is the keyboard's own repeat, and is ignored") {
    Keyboard keys("browser");
    keys.press(kEnter);
    keys.press(kEnter);
    keys.press(kEnter);
    CHECK(keys.seen.count("open_or_play") == 1);
    keys.release(kEnter);
    keys.press(kEnter);
    CHECK(keys.seen.count("open_or_play") == 2);
}

TEST_CASE("SPEC-099 where text is typed, a letter key gives its lower-case letter, and its capital while Shift is down") {
    for (const char* place : {"prompt", "search-field"}) {
        for (const NamedKey& key : kKeys) {
            const std::string name = key.name;
            if (name.size() != 1 || name[0] < 'A' || name[0] > 'Z') continue;
            CAPTURE(place);
            CAPTURE(name);
            Keyboard keys(place);
            keys.tap(key.code);
            keys.press(kShift);
            keys.tap(key.code);
            keys.release(kShift);
            keys.tap(key.code);
            const std::string lower(1, static_cast<char>(name[0] - 'A' + 'a'));
            CHECK(keys.seen.typed() == lower + name + lower);
            CHECK(keys.seen.count("type") == 3);
            for (const spz::KeyEvent& event : keys.seen.events) {
                if (event.action == "type") CHECK(event.key == "Char");
            }
        }
    }
}

TEST_CASE("SPEC-099 digits and Space give themselves, with Shift or without, and each sym code gives its sign") {
    Keyboard keys("prompt");
    for (const char* digit : {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}) keys.tap(code_of(digit));
    keys.tap(kSpace);
    keys.press(kShift);
    keys.tap(code_of("7"));
    keys.tap(kSpace);
    keys.release(kShift);
    CHECK(keys.seen.typed() == "1234567890 7 ");

    REQUIRE(kSigns.size() == 32);
    for (const Sign& sign : kSigns) {
        CAPTURE(sign.code);
        Keyboard field("search-field");
        field.tap(sign.code);
        REQUIRE(field.seen.events.size() == 1);
        CHECK(field.seen.events[0].key == "Char");
        CHECK(field.seen.events[0].action == "type");
        CHECK(field.seen.events[0].character == static_cast<char32_t>(sign.character));
        CHECK(field.seen.events[0].code == sign.code);
        field.press(kShift);
        field.tap(sign.code);
        field.release(kShift);
        CHECK(field.seen.typed() == std::string(2, sign.character));     // Shift does not change a sign
    }
}

TEST_CASE("SPEC-099 the key codes for hub 1985 type exactly that text, and those for ~/C64_Music.2, with Shift and sym codes, that one") {
    Keyboard search("search-field");
    for (int code : {35, 22, 48, 57, 2, 10, 9, 6}) search.tap(code);         // h u b space 1 9 8 5
    CHECK(search.seen.typed() == "hub 1985");

    Keyboard prompt("prompt");
    prompt.tap(55);                                                          // ~
    prompt.tap(92);                                                          // /
    prompt.press(kShift); prompt.tap(46); prompt.release(kShift);            // C
    prompt.tap(7);                                                           // 6
    prompt.tap(5);                                                           // 4
    prompt.tap(70);                                                          // _
    prompt.press(kShift); prompt.tap(50); prompt.release(kShift);            // M
    for (int code : {22, 31, 23, 46}) prompt.tap(code);                      // u s i c
    prompt.tap(91);                                                          // .
    prompt.tap(3);                                                           // 2
    CHECK(prompt.seen.typed() == "~/C64_Music.2");
}

TEST_CASE("SPEC-099 SPEC-093 the letters F, X, Z and C are typed in a text field, and are Up, Down, Left and Right elsewhere") {
    Keyboard field("search-field");
    for (int code : {kLetterF, kLetterX, kLetterZ, kLetterC}) field.tap(code);
    CHECK(field.seen.typed() == "fxzc");
    CHECK(field.seen.actions() == "type type type type");

    Keyboard browser("browser");
    for (int code : {kLetterF, kLetterX, kLetterZ, kLetterC}) browser.tap(code);
    REQUIRE(browser.seen.events.size() == 4);
    CHECK(browser.seen.events[0].key == "Up");
    CHECK(browser.seen.events[1].key == "Down");
    CHECK(browser.seen.events[2].key == "Left");
    CHECK(browser.seen.events[3].key == "Right");
    CHECK(browser.seen.actions() == "up down parent open_or_play");
    CHECK(browser.seen.events[0].code == kLetterF);              // the code that arrived stays the letter's

    Keyboard arrows("browser");
    for (int code : {kUp, kDown, kLeft, kRight}) arrows.tap(code);
    CHECK(arrows.seen.actions() == browser.seen.actions());      // exactly what the arrow codes do

    // In a panel and in help too, and on a screen whose rows name the arrows.
    for (const char* place : {"confirm", "help", "text", "settings", "hvsc"}) {
        CAPTURE(place);
        Keyboard letters(place), codes(place);
        for (int code : {kLetterF, kLetterX, kLetterZ, kLetterC}) letters.tap(code);
        for (int code : {kUp, kDown, kLeft, kRight}) codes.tap(code);
        CHECK(letters.seen.actions() == codes.seen.actions());
        CHECK(letters.seen.count("type") == 0);
    }
    Keyboard playing("playing");
    playing.tap(kLetterF);
    playing.tap(kLetterX);
    playing.tap(kLetterZ);
    CHECK(playing.seen.actions() == "previous_tune next_tune previous_subtune");
}

TEST_CASE("SPEC-099 in a text field the keys that edit and move arrive as their own key codes, and Enter and ESC end it") {
    Keyboard prompt("prompt");
    for (int code : {kBackspace, kDelete, kLeft, kRight, kHome, kEnd, kEnter}) prompt.tap(code);
    CHECK(prompt.seen.actions() == "delete_before delete_after cursor_left cursor_right cursor_first cursor_last accept");
    prompt.tap(kEsc);
    CHECK(prompt.seen.events.back().key == "Esc");
    CHECK(prompt.seen.events.back().action == "back");
    CHECK(prompt.seen.count("type") == 0);

    Keyboard search("search-field");
    for (int code : {kBackspace, kDelete, kLeft, kRight, kHome, kEnd, kDown, kEnter}) search.tap(code);
    CHECK(search.seen.actions() ==
          "delete_before delete_after cursor_left cursor_right cursor_first cursor_last to_results run_search");
}

TEST_CASE("SPEC-099 TAB, Up and Down do nothing in a prompt, and Up nothing in the Search field") {
    Keyboard prompt("prompt");
    for (int code : {kTab, kUp, kDown, kPageUp, kPageDown}) prompt.tap(code);
    CHECK(prompt.seen.actions() == "- - - - -");
    Keyboard search("search-field");
    search.tap(kTab);
    search.tap(kUp);
    CHECK(search.seen.actions() == "- -");
}

TEST_CASE("SPEC-099 where no text is typed, a sym code types nothing and does nothing") {
    for (const char* place : {"browser", "settings", "playing", "confirm", "help"}) {
        CAPTURE(place);
        Keyboard keys(place);
        for (const Sign& sign : kSigns) keys.tap(sign.code);
        CHECK(keys.seen.typed().empty());
        for (const spz::KeyEvent& event : keys.seen.events) CHECK(event.action.empty());
        keys.tap(kEnter);                                       // and the key after them acts as ever
        CHECK(keys.seen.events.back().key == "Enter");
        CHECK_FALSE(keys.seen.events.back().action.empty());
    }
}

TEST_CASE("SPEC-092 while text is typed the keys of `always` still act, and P, L and Space are typed and do nothing else") {
    for (const char* place : {"prompt", "search-field"}) {
        CAPTURE(place);
        Keyboard keys(place);
        for (const char* name : {"PlayPause", "Previous", "Next", "Mute", "VolumeDown", "VolumeUp", "Help"}) {
            keys.tap(code_of(name));
        }
        CHECK(keys.seen.actions() == "play_pause previous_tune next_tune volume_mute volume_down volume_up toggle_help");
        CHECK(keys.seen.typed().empty());

        Keyboard typing(place);
        typing.tap(kLetterP);
        typing.tap(kLetterL);
        typing.tap(kSpace);
        CHECK(typing.seen.typed() == "pl ");
        CHECK(typing.seen.actions() == "type type type");
    }
}

TEST_CASE("SPEC-092 in a panel and in help, TAB, Space, P and L give no action, and the `lists` keys do") {
    for (const char* place : {"confirm", "text", "notice-panel", "help"}) {
        CAPTURE(place);
        Keyboard keys(place);
        for (int code : {kTab, kSpace, kLetterP, kLetterL}) keys.tap(code);
        CHECK(keys.seen.actions() == "- - - -");
        Keyboard lists(place);
        for (int code : {kUp, kDown, kPageUp, kPageDown, kHome, kEnd}) lists.tap(code);
        CHECK(lists.seen.actions() == "up down page_up page_down first last");
    }
}

TEST_CASE("SPEC-045 a key whose action repeats, held for one second, acts 16 times: once, and then, after 400 ms, 25 times a second") {
    Keyboard keys("browser");
    keys.press(kDown);
    REQUIRE(keys.seen.events.size() == 1);
    CHECK(keys.seen.events[0].action == "down");
    CHECK_FALSE(keys.seen.events[0].repeat);
    keys.clock.advance(399);
    CHECK(keys.seen.events.size() == 1);                  // nothing more before 400 ms
    keys.clock.advance(601);                              // one second since the press
    REQUIRE(keys.seen.events.size() == 16);
    for (std::size_t i = 1; i < keys.seen.events.size(); ++i) {
        CHECK(keys.seen.events[i].repeat);
        CHECK(keys.seen.events[i].action == "down");
        CHECK(keys.seen.events[i].key == "Down");
        CHECK(keys.seen.events[i].code == kDown);
    }
    keys.clock.advance(1000);
    CHECK(keys.seen.events.size() == 41);                 // 25 in each second after that
    keys.clock.advance(1000);
    CHECK(keys.seen.events.size() == 66);
    keys.release(kDown);
    keys.clock.advance(5000);
    CHECK(keys.seen.events.size() == 66);                 // and not after the key is let go
    CHECK(keys.clock.calls > 0);                          // the time came from the Clock seam
}

TEST_CASE("SPEC-045 moving in a list, moving the cursor, deleting in a field and stepping a value repeat; with letters as arrows too") {
    struct Held {
        const char* place;
        int code;
        const char* action;
    };
    const std::vector<Held> repeating = {
        {"browser", kUp, "up"}, {"browser", kDown, "down"}, {"browser", kPageUp, "page_up"},
        {"browser", kPageDown, "page_down"}, {"browser", kLetterX, "down"}, {"browser", kLetterF, "up"},
        {"settings", kDown, "down"}, {"help", kDown, "down"}, {"text", kUp, "up"},
        {"settings", kLeft, "previous_value"}, {"settings", kRight, "next_value"},
        {"settings", kLetterC, "next_value"},
        {"prompt", kLeft, "cursor_left"}, {"prompt", kRight, "cursor_right"},
        {"prompt", kBackspace, "delete_before"}, {"prompt", kDelete, "delete_after"},
        {"search-field", kLeft, "cursor_left"}, {"search-field", kBackspace, "delete_before"},
    };
    for (const Held& held : repeating) {
        CAPTURE(held.place);
        CAPTURE(held.action);
        Keyboard keys(held.place);
        keys.hold(held.code, 1000);
        CHECK(keys.seen.count(held.action) == 16);
        CHECK(keys.seen.events.size() == 16);
    }
}

TEST_CASE("SPEC-045 no other action repeats: a key held for one second acts once, Up and Down on now playing among them") {
    struct Held {
        const char* place;
        int code;
        const char* action;
    };
    const std::vector<Held> once = {
        {"browser", kEnter, "open_or_play"}, {"browser", kHome, "first"}, {"browser", kEnd, "last"},
        {"browser", kLeft, "parent"}, {"browser", 6, "open_search"}, {"browser", kTab, "open_settings"},
        {"browser", kSpace, "play_pause"}, {"browser", 164, "play_pause"}, {"browser", 163, "next_tune"},
        {"browser", 115, "volume_up"}, {"browser", 113, "volume_mute"}, {"browser", 138, "toggle_help"},
        {"playing", kUp, "previous_tune"}, {"playing", kDown, "next_tune"}, {"playing", kLetterX, "next_tune"},
        {"playing", kLeft, "previous_subtune"}, {"playing", kEnter, "play_pause"},
        {"settings", kEnter, "change"}, {"confirm", kEnter, "yes"},
        {"prompt", kLetterA, "type"}, {"prompt", kEnter, "accept"}, {"search-field", kDown, "to_results"},
    };
    for (const Held& held : once) {
        CAPTURE(held.place);
        CAPTURE(held.action);
        Keyboard keys(held.place);
        keys.hold(held.code, 1000);
        keys.clock.advance(1000);
        CHECK(keys.seen.count(held.action) == 1);
        CHECK(keys.seen.events.size() == 1);
    }
}

TEST_CASE("SPEC-045 which actions of the key map repeat is told by the action alone") {
    const std::vector<std::string> repeating = {"up", "down", "page_up", "page_down", "cursor_left", "cursor_right",
                                                "delete_before", "delete_after", "previous_value", "next_value"};
    for (const spz::KeyMapRow& row : spz::test::keymap_file()) {
        // Whether the two that put the cursor at an end "move the cursor" in SPEC-045's sense cannot be seen,
        // since doing them twice is doing them once: they are left out here.
        if (row.action == "cursor_first" || row.action == "cursor_last") continue;
        CAPTURE(row.action);
        CHECK(spz::action_repeats(row.action) == spz::test::one_of(repeating, row.action));
    }
    CHECK_FALSE(spz::action_repeats(""));
    CHECK_FALSE(spz::action_repeats("nothing"));
}

TEST_CASE("SPEC-106 a press of ESC let go within 500 ms is the key Esc, which goes back, and it acts when it is let go") {
    for (std::int64_t held : {0, 200, 490}) {
        CAPTURE(held);
        Keyboard keys("settings");
        keys.press(kEsc);
        keys.clock.advance(held);
        CHECK(keys.seen.events.empty());                  // nothing yet: it could still become a hold
        keys.release(kEsc);
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].key == "Esc");
        CHECK(keys.seen.events[0].action == "back");
        CHECK(keys.seen.events[0].code == kEsc);
        CHECK(keys.seen.hold_notices.empty());
        keys.clock.advance(2000);
        CHECK(keys.seen.events.size() == 1);              // and no hold follows a key that is up
        CHECK(keys.seen.hold_notices.empty());
    }
}

TEST_CASE("SPEC-106 ESC held for 500 ms raises the notice; let go before 1,000 ms it takes the notice away and does nothing else") {
    Keyboard keys("settings");
    keys.press(kEsc);
    keys.clock.advance(499);
    CHECK(keys.seen.hold_notices.empty());
    keys.clock.advance(1);
    REQUIRE(keys.seen.hold_notices.size() == 1);
    CHECK(keys.seen.hold_notices[0] == true);
    keys.clock.advance(200);                              // 700 ms in all
    keys.release(kEsc);
    REQUIRE(keys.seen.hold_notices.size() == 2);
    CHECK(keys.seen.hold_notices[1] == false);
    CHECK(keys.seen.events.empty());                      // not "back", and not "leave"
    keys.clock.advance(2000);
    CHECK(keys.seen.events.empty());
    CHECK(keys.seen.hold_notices.size() == 2);
}

TEST_CASE("SPEC-106 ESC held for 1,000 ms is the key EscHold, which leaves, once, and letting go then does nothing") {
    Keyboard keys("settings");
    keys.press(kEsc);
    keys.clock.advance(999);
    CHECK(keys.seen.events.empty());
    keys.clock.advance(1);
    REQUIRE(keys.seen.events.size() == 1);
    CHECK(keys.seen.events[0].key == "EscHold");
    CHECK(keys.seen.events[0].action == "leave");
    REQUIRE(!keys.seen.hold_notices.empty());
    CHECK(keys.seen.hold_notices[0] == true);             // the notice came first, at 500 ms
    keys.clock.advance(3000);
    keys.release(kEsc);
    CHECK(keys.seen.events.size() == 1);
    CHECK(keys.seen.count("back") == 0);
}

TEST_CASE("SPEC-106 SPEC-092 ESC and its hold are the same in every place: a panel, help, a text field") {
    for (const char* place : {"browser", "empty", "playing", "search-results", "hvsc", "about", "confirm", "text",
                              "notice-panel", "help", "prompt", "search-field"}) {
        CAPTURE(place);
        Keyboard keys(place);
        keys.hold(kEsc, 200);
        CHECK(keys.seen.actions() == "back");
        keys.hold(kEsc, 700);
        CHECK(keys.seen.actions() == "back");
        CHECK(keys.seen.hold_notices == std::vector<bool>{true, false});
        keys.press(kEsc);
        keys.clock.advance(1000);
        CHECK(keys.seen.actions() == "back leave");
    }
}

TEST_CASE("SPEC-111 SPEC-260 in the key step every key is recorded with its code, the signs too, and a held ESC still leaves") {
    for (const NamedKey& key : kKeys) {
        CAPTURE(key.name);
        Keyboard keys("key-step");
        keys.tap(key.code);
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].action == "record_key");
        CHECK(keys.seen.events[0].code == key.code);
    }
    for (int code : {92, 91, 70, 80, 55, 71}) {             // / . _ : ~ -
        CAPTURE(code);
        Keyboard keys("key-step");
        keys.tap(code);
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].action == "record_key");
        CHECK(keys.seen.events[0].code == code);
    }
    Keyboard keys("key-step");
    keys.press(kEsc);
    keys.clock.advance(1000);
    REQUIRE(keys.seen.events.size() == 1);
    CHECK(keys.seen.events[0].key == "EscHold");
    CHECK(keys.seen.events[0].action == "leave");
}

TEST_CASE("SPEC-096 Right acts at once where it cannot be held: in the browser, in Settings, in a text field") {
    for (const char* place : {"browser", "settings", "prompt"}) {
        CAPTURE(place);
        Keyboard keys(place);
        keys.press(kRight);
        CHECK(keys.seen.events.size() == 1);
    }
}

#if SPZ_PART_SKIP
TEST_CASE("SPEC-096 on now playing, Right let go before 400 ms is the next subtune, when it is let go [parts: skip]") {
    for (int code : {kRight, kLetterC}) {
        CAPTURE(code);
        Keyboard keys("playing");
        keys.press(code);
        keys.clock.advance(390);
        CHECK(keys.seen.events.empty());
        keys.release(code);
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].key == "Right");
        CHECK(keys.seen.events[0].action == "next_subtune");
        keys.clock.advance(2000);
        CHECK(keys.seen.events.size() == 1);
    }
}

TEST_CASE("SPEC-096 Right held for 400 ms skips ahead until it is let go, and letting go does not change the subtune [parts: skip]") {
    for (int code : {kRight, kLetterC}) {
        CAPTURE(code);
        Keyboard keys("playing");
        keys.press(code);
        keys.clock.advance(399);
        CHECK(keys.seen.events.empty());
        keys.clock.advance(1);
        REQUIRE(keys.seen.events.size() == 1);
        CHECK(keys.seen.events[0].key == "RightHold");
        CHECK(keys.seen.events[0].action == "skip_ahead");
        CHECK_FALSE(keys.seen.events[0].ended);
        keys.clock.advance(3000);
        CHECK(keys.seen.events.size() == 1);              // skipping is one thing that lasts, not a repeat
        keys.release(code);
        REQUIRE(keys.seen.events.size() == 2);
        CHECK(keys.seen.events[1].key == "RightHold");
        CHECK(keys.seen.events[1].action == "skip_ahead");
        CHECK(keys.seen.events[1].ended);
        CHECK(keys.seen.count("next_subtune") == 0);
    }
}
#endif

TEST_CASE("SPEC-115 SPEC-096 Right held on now playing skips ahead where the part skip is built, and is the next subtune, once, where it is not") {
    Keyboard keys("playing");
    keys.hold(kRight, 1000);
    keys.clock.advance(1000);
    if (SPZ_PART_SKIP) {
        CHECK(keys.seen.actions() == "skip_ahead skip_ahead");    // it began, and it ended
    } else {
        CHECK(keys.seen.actions() == "next_subtune");
    }
}

TEST_CASE("SPEC-115 the keys of brightness, screenshot, too-slow and restart give their actions only when their parts are built") {
    Keyboard keys("playing");
    keys.tap(225);                                        // BrightnessUp
    keys.tap(224);                                        // BrightnessDown
    keys.tap(99);                                         // PrintScreen
    keys.tap(kLetterL);
    keys.tap(kBackspace);
    const std::string brightness = SPZ_PART_BRIGHTNESS ? "brightness_up brightness_down" : "- -";
    const std::string screenshot = SPZ_PART_SCREENSHOT ? "screenshot" : "-";
    const std::string light = SPZ_PART_TOO_SLOW ? "use_light_engine" : "-";
    const std::string restart = SPZ_PART_RESTART ? "restart" : "-";
    CHECK(keys.seen.actions() == brightness + " " + screenshot + " " + light + " " + restart);
}
