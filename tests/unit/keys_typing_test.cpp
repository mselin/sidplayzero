// SPDX-License-Identifier: GPL-3.0-or-later
// The field that text is typed into (SPEC-099): the characters, Backspace and Delete, the cursor, and the
// most a field holds.
#include <string>

#include "doctest.h"
#include "keys_support.h"
#include "spz/text_field.h"

namespace {

void type(spz::TextField* field, const std::string& text) {
    for (const char c : text) field->apply("type", static_cast<char32_t>(c));
}

}  // namespace

TEST_CASE("SPEC-099 typed characters go in at the cursor, and the cursor moves on with them") {
    spz::TextField field(spz::kSearchFieldLimit);
    CHECK(field.text() == "");
    CHECK(field.cursor() == 0);
    type(&field, "hub 1985");
    CHECK(field.text() == "hub 1985");
    CHECK(field.length() == 8);
    CHECK(field.cursor() == 8);
    CHECK(field.apply("cursor_first"));
    CHECK(field.cursor() == 0);
    CHECK(field.apply("type", U'x'));
    CHECK(field.text() == "xhub 1985");
    CHECK(field.cursor() == 1);
    CHECK(field.apply("cursor_right"));
    CHECK(field.apply("cursor_right"));
    CHECK(field.apply("type", U'~'));
    CHECK(field.text() == "xhu~b 1985");
    CHECK(field.cursor() == 4);
}

TEST_CASE("SPEC-099 Backspace removes the character before the cursor and Delete the one after it") {
    spz::TextField field(spz::kSearchFieldLimit, "commando");
    CHECK(field.cursor() == 8);                          // a field that opens with a value has the cursor at its end
    CHECK(field.apply("delete_before"));
    CHECK(field.text() == "command");
    CHECK(field.cursor() == 7);
    CHECK_FALSE(field.apply("delete_after"));            // nothing after the cursor
    CHECK(field.text() == "command");
    CHECK(field.apply("cursor_first"));
    CHECK_FALSE(field.apply("delete_before"));           // nothing before it
    CHECK(field.text() == "command");
    CHECK(field.apply("delete_after"));
    CHECK(field.text() == "ommand");
    CHECK(field.cursor() == 0);
    CHECK(field.apply("cursor_right"));
    CHECK(field.apply("cursor_right"));
    CHECK(field.apply("delete_before"));
    CHECK(field.text() == "omand");
    CHECK(field.cursor() == 1);
    CHECK(field.apply("delete_after"));
    CHECK(field.text() == "oand");
    CHECK(field.cursor() == 1);
}

TEST_CASE("SPEC-099 Left, Right, Home and End move the cursor, and it stops at both ends") {
    spz::TextField field(spz::kSearchFieldLimit, "abc");
    CHECK_FALSE(field.apply("cursor_right"));            // at the end already
    CHECK_FALSE(field.apply("cursor_last"));
    CHECK(field.cursor() == 3);
    CHECK(field.apply("cursor_left"));
    CHECK(field.cursor() == 2);
    CHECK(field.apply("cursor_left"));
    CHECK(field.apply("cursor_left"));
    CHECK(field.cursor() == 0);
    CHECK_FALSE(field.apply("cursor_left"));             // at the start
    CHECK_FALSE(field.apply("cursor_first"));
    CHECK(field.cursor() == 0);
    CHECK(field.apply("cursor_last"));
    CHECK(field.cursor() == 3);
    CHECK(field.apply("cursor_first"));
    CHECK(field.cursor() == 0);
    CHECK(field.text() == "abc");                        // moving changes no text
}

TEST_CASE("SPEC-099 the Search field holds 64 characters, and the 65th is not added") {
    spz::TextField field(spz::kSearchFieldLimit);
    const std::string full(64, 'a');
    type(&field, full);
    CHECK(field.text() == full);
    CHECK_FALSE(field.apply("type", U'b'));
    CHECK(field.text() == full);
    CHECK(field.length() == 64);
    CHECK(field.cursor() == 64);
    CHECK(field.apply("cursor_first"));
    CHECK_FALSE(field.apply("type", U'b'));              // nor anywhere else in a full field
    CHECK(field.text() == full);
    CHECK(field.cursor() == 0);
    CHECK(field.apply("delete_after"));                  // with one removed there is room for one again
    CHECK(field.apply("type", U'b'));
    CHECK(field.text() == "b" + std::string(63, 'a'));
    CHECK_FALSE(field.apply("type", U'c'));
}

TEST_CASE("SPEC-099 a field holds 64 characters in Search, 40 for a playlist's name, 255 for a path or an address, 5 for a length") {
    CHECK(spz::kSearchFieldLimit == 64);
    CHECK(spz::kPlaylistNameLimit == 40);
    CHECK(spz::kPathFieldLimit == 255);
    CHECK(spz::kLengthFieldLimit == 5);
    for (const std::size_t limit : {spz::kSearchFieldLimit, spz::kPlaylistNameLimit, spz::kPathFieldLimit,
                                    spz::kLengthFieldLimit}) {
        CAPTURE(limit);
        spz::TextField field(limit);
        CHECK(field.limit() == limit);
        type(&field, std::string(limit + 10, '7'));
        CHECK(field.text() == std::string(limit, '7'));
        CHECK(field.length() == limit);
    }
    spz::TextField length(spz::kLengthFieldLimit);
    type(&length, "59:591");
    CHECK(length.text() == "59:59");
}

TEST_CASE("SPEC-099 a field that opens holding a value keeps it, and counts characters, not bytes") {
    spz::TextField field(spz::kPathFieldLimit, "~/My Rips/Cool S\xC3\xB6ng");          // "Söng"
    CHECK(field.text() == "~/My Rips/Cool S\xC3\xB6ng");
    CHECK(field.length() == 19);
    CHECK(field.cursor() == 19);
    CHECK(field.apply("delete_before"));
    CHECK(field.apply("delete_before"));
    CHECK(field.text() == "~/My Rips/Cool S\xC3\xB6");
    CHECK(field.apply("delete_before"));                 // the whole letter goes, both of its bytes
    CHECK(field.text() == "~/My Rips/Cool S");
    CHECK(field.cursor() == 16);

    spz::TextField other(spz::kPathFieldLimit, "S\xC3\xB6ng");
    CHECK(other.apply("cursor_first"));
    CHECK(other.apply("cursor_right"));
    CHECK(other.apply("cursor_right"));
    CHECK(other.cursor() == 2);                          // past "S" and "ö"
    CHECK(other.apply("type", U'_'));
    CHECK(other.text() == "S\xC3\xB6_ng");
    CHECK(other.apply("cursor_left"));
    CHECK(other.apply("cursor_left"));
    CHECK(other.apply("delete_after"));
    CHECK(other.text() == "S_ng");

    spz::TextField full(spz::kLengthFieldLimit, "\xC3\xB6\xC3\xB6\xC3\xB6\xC3\xB6");    // four letters, eight bytes
    CHECK(full.apply("type", U'1'));                     // the fifth character fits
    CHECK_FALSE(full.apply("type", U'2'));               // the sixth does not
    CHECK(full.length() == 5);
}

TEST_CASE("SPEC-099 an action that is not a field's changes nothing") {
    spz::TextField field(spz::kSearchFieldLimit, "abc");
    for (const char* action : {"up", "down", "accept", "back", "run_search", "to_results", "nothing", ""}) {
        CAPTURE(action);
        CHECK_FALSE(field.apply(action));
    }
    CHECK(field.text() == "abc");
    CHECK(field.cursor() == 3);
}

TEST_CASE("SPEC-099 the keys of a text prompt, through the key map, edit a field as the item says") {
    // The codes for "~/C64_Music.2", then two Backspaces, Home, Delete, End and "x": what the user sees.
    spz::test::Keyboard keys("prompt");
    for (int code : {55, 92}) keys.tap(code);
    keys.press(42); keys.tap(46); keys.release(42);
    for (int code : {7, 5, 70}) keys.tap(code);
    keys.press(42); keys.tap(50); keys.release(42);
    for (int code : {22, 31, 23, 46, 91, 3}) keys.tap(code);
    for (int code : {14, 14, 102, 111, 107, 45}) keys.tap(code);
    spz::TextField field(spz::kPathFieldLimit);
    for (const spz::KeyEvent& event : keys.seen.events) field.apply(event.action, event.character);
    CHECK(field.text() == "/C64_Musicx");
    CHECK(field.cursor() == 11);
}
