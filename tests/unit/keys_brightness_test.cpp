// SPDX-License-Identifier: GPL-3.0-or-later
// The brightness keys (SPEC-104), the part `brightness`, against the fake Backlight.
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/brightness.h"

#if SPZ_PART_BRIGHTNESS

namespace {

// What the fake was asked to set, as "set 60 set 40"; "" if nothing.
std::string asked(const spz::fake::Backlight& backlight) {
    std::string all;
    for (const std::string& entry : backlight.log) {
        if (entry.compare(0, 4, "set ") == 0) all += (all.empty() ? "" : " ") + entry;
    }
    return all;
}

}  // namespace

TEST_CASE("SPEC-104 with the screen at 50 %, brighter asks the backlight for 60 % and darker for 40 % [parts: brightness]") {
    spz::fake::Backlight backlight;
    backlight.level = 50;
    const spz::BrightnessStep up = spz::step_brightness(&backlight, true);
    CHECK(asked(backlight) == "set 60");
    CHECK(up.notice == "MSG_BRIGHTNESS");
    CHECK(up.percent == 60);
    CHECK(std::count(backlight.log.begin(), backlight.log.end(), "get") >= 1);   // it read the level through the seam
    CHECK(backlight.calls > 0);

    spz::fake::Backlight other;
    other.level = 50;
    const spz::BrightnessStep down = spz::step_brightness(&other, false);
    CHECK(asked(other) == "set 40");
    CHECK(down.notice == "MSG_BRIGHTNESS");
    CHECK(down.percent == 40);
}

TEST_CASE("SPEC-104 a level between two tens goes to the nearest multiple of 10 % above or below [parts: brightness]") {
    struct Case {
        int level;
        bool up;
        int wanted;
    };
    for (const Case& c : std::vector<Case>{{55, true, 60}, {55, false, 50}, {51, true, 60}, {51, false, 50},
                                           {59, true, 60}, {59, false, 50}, {70, true, 80}, {70, false, 60},
                                           {90, true, 100}, {20, false, 10}, {11, false, 10}, {99, true, 100},
                                           {5, true, 10}}) {
        CAPTURE(c.level);
        CAPTURE(c.up);
        spz::fake::Backlight backlight;
        backlight.level = c.level;
        const spz::BrightnessStep step = spz::step_brightness(&backlight, c.up);
        CHECK(asked(backlight) == "set " + std::to_string(c.wanted));
        CHECK(step.notice == "MSG_BRIGHTNESS");
        CHECK(step.percent == c.wanted);
    }
}

TEST_CASE("SPEC-104 at 10 % darker asks for nothing, and at 100 % brighter asks for nothing; the notice shows the level [parts: brightness]") {
    spz::fake::Backlight low;
    low.level = 10;
    const spz::BrightnessStep down = spz::step_brightness(&low, false);
    CHECK(asked(low) == "");
    CHECK(low.level == 10);
    CHECK(down.notice == "MSG_BRIGHTNESS");
    CHECK(down.percent == 10);

    spz::fake::Backlight high;
    high.level = 100;
    const spz::BrightnessStep up = spz::step_brightness(&high, true);
    CHECK(asked(high) == "");
    CHECK(high.level == 100);
    CHECK(up.notice == "MSG_BRIGHTNESS");
    CHECK(up.percent == 100);
}

TEST_CASE("SPEC-104 several presses walk the level in tens and stop at the ends [parts: brightness]") {
    spz::fake::Backlight backlight;
    backlight.level = 70;
    std::string levels;
    for (int i = 0; i < 5; ++i) levels += std::to_string(spz::step_brightness(&backlight, true).percent) + " ";
    CHECK(levels == "80 90 100 100 100 ");
    CHECK(asked(backlight) == "set 80 set 90 set 100");
    levels.clear();
    for (int i = 0; i < 11; ++i) levels += std::to_string(spz::step_brightness(&backlight, false).percent) + " ";
    CHECK(levels == "90 80 70 60 50 40 30 20 10 10 10 ");
    CHECK(backlight.level == 10);
}

TEST_CASE("SPEC-104 with a backlight that cannot be changed, the notice is MSG_BRIGHTNESS_UNAVAILABLE and nothing is set [parts: brightness]") {
    for (const bool up : {true, false}) {
        CAPTURE(up);
        spz::fake::Backlight backlight;
        backlight.level = 50;
        backlight.writable = false;
        const spz::BrightnessStep step = spz::step_brightness(&backlight, up);
        CHECK(step.notice == "MSG_BRIGHTNESS_UNAVAILABLE");
        CHECK(asked(backlight) == "");
        CHECK(backlight.level == 50);
        CHECK(backlight.calls > 0);                      // it asked the seam whether it can
    }
}

#endif  // SPZ_PART_BRIGHTNESS
