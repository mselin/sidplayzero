// SPDX-License-Identifier: GPL-3.0-or-later
// TF-042, TF-043: the pieces of `core` that the simulator's script language and screen description are
// built on: key names and codes, and the JSON value. Tests of the test framework.
#include <string>

#include "doctest.h"
#include "spz/json.h"
#include "spz/keys.h"

TEST_CASE("TF-042 SPEC-111 key names and key codes are those of the specification's table") {
    CHECK(spz::key_code(spz::Key::Esc) == 1);
    CHECK(spz::key_code(spz::key_from_name("PlayPause")) == 164);
    CHECK(spz::key_code(spz::key_from_name("BrightnessUp")) == 225);
    CHECK(spz::key_code(spz::key_from_name("Q")) == 16);
    CHECK(spz::key_code(spz::key_from_name("M")) == 50);
    CHECK(spz::key_code(spz::key_from_name("0")) == 11);
    CHECK(spz::key_code(spz::key_from_name("4")) == 5);
    CHECK(std::string(spz::key_name(spz::key_from_code(109))) == "PageDown");
    CHECK(spz::key_from_name("F13") == spz::Key::None);
    CHECK(spz::key_from_code(29) == spz::Key::None);   // Ctrl: not named, so ignored
}

TEST_CASE("TF-042 SPEC-099 characters and the key codes that type them") {
    bool shift = true;
    CHECK(spz::code_for_character(U'h', &shift) == 35);
    CHECK_FALSE(shift);
    CHECK(spz::code_for_character(U'H', &shift) == 35);
    CHECK(shift);
    CHECK(spz::code_for_character(U'/', &shift) == 92);
    CHECK(spz::code_for_character(U'_', &shift) == 70);
    CHECK(spz::code_for_character(U' ', &shift) == 57);
    CHECK(spz::code_for_character(U'é', &shift) == 0);
    CHECK(spz::character_for_code(35, false) == U'h');
    CHECK(spz::character_for_code(35, true) == U'H');
    CHECK(spz::character_for_code(80, false) == U':');
    CHECK(spz::character_for_code(1, false) == 0);
}

TEST_CASE("TF-043 the JSON value keeps its keys in order, escapes text, and is found by path") {
    spz::Json root = spz::Json::object();
    root.set("screen", "browser");
    spz::Json rows = spz::Json::array();
    spz::Json row = spz::Json::object();
    row.set("text", "Hubbard \"Rob\"\n");
    row.set("highlighted", true);
    rows.push(row);
    root.set("rows", rows);
    root.set("position", nullptr);
    root.set("count", 3);

    CHECK(root.dump() == R"({"screen":"browser","rows":[{"text":"Hubbard \"Rob\"\n","highlighted":true}],"position":null,"count":3})");
    REQUIRE(root.find("rows[0].text") != nullptr);
    CHECK(root.find("rows[0].text")->scalar_text() == "Hubbard \"Rob\"\n");
    CHECK(root.find("rows[0].highlighted")->scalar_text() == "true");
    CHECK(root.find("count")->scalar_text() == "3");
    CHECK(root.find("position")->is_null());
    CHECK(root.find("rows[1].text") == nullptr);
    CHECK(root.find("nothing.here") == nullptr);
    CHECK(root.dump(2).find("\n  \"screen\": \"browser\"") != std::string::npos);
}
