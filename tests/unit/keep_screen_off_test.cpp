// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-207, the part `screen-off`: the app turns the backlight off while a tune plays and nobody presses a
// key, and always puts it back.
#include <functional>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/screen_off.h"
#include "spz/state_file.h"
#include "support.h"

#if SPZ_PART_SCREEN_OFF

using spz::test::TempFolder;
using spz::test::has_line;
using spz::test::text_on_disk;

namespace {

// The fake backlight, telling the test each time it is set, before it is.
struct WatchedBacklight : spz::fake::Backlight {
    std::function<void(int)> on_set;
    bool set(int percent) override {
        if (on_set) on_set(percent);
        return spz::fake::Backlight::set(percent);
    }
    int sets() const {
        int count = 0;
        for (const std::string& entry : log) count += entry.compare(0, 4, "set ") == 0 ? 1 : 0;
        return count;
    }
};

// The app's pieces around the screen: a state file on a card of the test's own, the fake clock, the quick
// worker, and the fake backlight at 70 %.
struct Rig {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;
    WatchedBacklight backlight;
    std::string path = folder.file("state.ini");
    spz::StateFile state{&files, &clock, &quick, path};
    spz::ScreenOff screen{&clock, &quick, &backlight, &state};

    // The start of the app, as far as the screen is concerned (SPEC-200, step 2).
    void start() {
        state.load();
        screen.at_start();
    }
    // Time moves on, and then the quick worker does whatever it was handed.
    void pass(std::int64_t ms) {
        clock.advance(ms);
        quick.run_all();
    }
    // The start of a tune by a key, with the setting at `seconds`.
    void play_with(int seconds) {
        start();
        screen.set_after_s(seconds);
        CHECK_FALSE(screen.key());
        screen.set_playing(true);
        quick.run_all();
    }
    std::string card() const { return text_on_disk(path); }
};

}  // namespace

TEST_CASE("SPEC-207 with the backlight at 70 % and the setting at 10, after 10 s of playing without a key the backlight is at 0 and state.ini holds 70 [parts: screen-off]") {
    Rig rig;
    REQUIRE(rig.backlight.level == 70);
    bool card_had_it_first = false;
    rig.backlight.on_set = [&](int percent) {
        if (percent == 0) card_had_it_first = has_line(rig.card(), "backlight_saved = 70");
    };
    rig.play_with(10);
    CHECK(rig.screen.available());

    rig.pass(9999);
    CHECK(rig.backlight.level == 70);
    CHECK(rig.backlight.sets() == 0);
    CHECK_FALSE(rig.screen.is_off());

    rig.pass(1);
    CHECK(rig.backlight.level == 0);
    CHECK(rig.screen.is_off());
    CHECK(has_line(rig.card(), "backlight_saved = 70"));     // at once: not two seconds later
    CHECK(card_had_it_first);                                // written before the backlight went off
    CHECK(rig.state.state().backlight_saved == 70);
    CHECK(rig.backlight.sets() == 1);
}

TEST_CASE("SPEC-207 SPEC-022 turning the screen off is the quick worker's work: the timer itself touches neither the card nor the backlight [parts: screen-off]") {
    Rig rig;
    rig.play_with(10);
    const int file_calls = rig.files.calls;
    const int backlight_calls = rig.backlight.calls;
    rig.clock.advance(10000);
    CHECK(rig.files.calls == file_calls);
    CHECK(rig.backlight.calls == backlight_calls);
    CHECK(rig.quick.waiting() >= 1);
    rig.quick.run_all();
    CHECK(rig.backlight.level == 0);
}

TEST_CASE("SPEC-207 any key turns the screen back on and does nothing else [parts: screen-off]") {
    Rig rig;
    rig.play_with(10);
    rig.pass(10000);
    REQUIRE(rig.backlight.level == 0);

    CHECK(rig.screen.key());              // true: the key is used up, whatever key it was
    rig.quick.run_all();
    CHECK(rig.backlight.level == 70);
    CHECK_FALSE(rig.screen.is_off());
    CHECK(rig.state.state().backlight_saved == -1);

    CHECK_FALSE(rig.screen.key());        // with the screen on, a key is an ordinary key again
    rig.quick.run_all();
    CHECK(rig.backlight.level == 70);

    rig.pass(9999);                       // and ten seconds after it the screen goes off again
    CHECK(rig.backlight.level == 70);
    rig.pass(1);
    CHECK(rig.backlight.level == 0);
    CHECK(has_line(rig.card(), "backlight_saved = 70"));
}

TEST_CASE("SPEC-207 the seconds are counted from the last key, and only while a tune plays and the setting is above 0 [parts: screen-off]") {
    {
        Rig rig;
        rig.play_with(10);
        rig.pass(6000);
        CHECK_FALSE(rig.screen.key());    // a key after six seconds
        rig.pass(9999);                   // 15.999 s after the start, 9.999 s after the key
        CHECK(rig.backlight.level == 70);
        rig.pass(1);
        CHECK(rig.backlight.level == 0);
    }
    {
        Rig rig;                          // nothing plays
        rig.start();
        rig.screen.set_after_s(10);
        rig.pass(60000);
        CHECK(rig.backlight.level == 70);
        CHECK(rig.backlight.sets() == 0);
        CHECK_FALSE(rig.screen.is_off());
    }
    {
        Rig rig;                          // the setting is 0: never
        rig.play_with(0);
        rig.pass(3600 * 1000 + 1000);
        CHECK(rig.backlight.level == 70);
        CHECK(rig.backlight.sets() == 0);
    }
    {
        Rig rig;                          // the other end of the setting's range
        rig.play_with(3600);
        rig.pass(3600 * 1000 - 1);
        CHECK(rig.backlight.level == 70);
        rig.pass(1);
        CHECK(rig.backlight.level == 0);
    }
}

TEST_CASE("SPEC-207 the brightness is put back when playback pauses or stops, and backlight_saved is cleared [parts: screen-off]") {
    Rig rig;
    rig.play_with(10);
    rig.pass(10000);
    REQUIRE(rig.backlight.level == 0);

    rig.screen.set_playing(false);        // the player paused, or stopped, by itself
    rig.quick.run_all();
    CHECK(rig.backlight.level == 70);
    CHECK_FALSE(rig.screen.is_off());
    CHECK(rig.state.state().backlight_saved == -1);

    rig.pass(60000);                      // paused: the screen stays on
    CHECK(rig.backlight.level == 70);
    CHECK_FALSE(has_line(rig.card(), "backlight_saved = 70"));    // and the card has forgotten it by now
}

TEST_CASE("SPEC-207 SPEC-201 the brightness is put back when the app exits, and backlight_saved is cleared [parts: screen-off]") {
    Rig rig;
    rig.play_with(10);
    rig.pass(10000);
    REQUIRE(rig.backlight.level == 0);
    REQUIRE(has_line(rig.card(), "backlight_saved = 70"));

    rig.screen.at_exit();
    CHECK(rig.backlight.level == 70);
    CHECK(rig.state.state().backlight_saved == -1);
    REQUIRE(rig.state.save_now() == spz::FileStatus::Ok);     // SPEC-201 writes state.ini next
    CHECK_FALSE(has_line(rig.card(), "backlight_saved = 70"));

    Rig on;                               // with the screen on, the exit leaves the backlight alone
    on.play_with(10);
    on.pass(5000);
    on.screen.at_exit();
    CHECK(on.backlight.sets() == 0);
}

TEST_CASE("SPEC-207 SPEC-200 a start with backlight_saved = 70 sets the backlight to 70 and clears the key [parts: screen-off]") {
    Rig rig;
    rig.folder.write("state.ini", "browser_folder = /MUSICIANS\nbacklight_saved = 70\n");
    rig.backlight.level = 0;              // as the killed run left the screen
    rig.start();
    CHECK(rig.backlight.level == 70);
    CHECK_FALSE(rig.screen.is_off());
    CHECK(rig.state.state().backlight_saved == -1);
    CHECK(rig.state.state().browser_folder == "/MUSICIANS");
    rig.pass(2000);                       // the change is saved like any other
    CHECK_FALSE(has_line(rig.card(), "backlight_saved = 70"));
    CHECK(has_line(rig.card(), "browser_folder = /MUSICIANS"));

    Rig nothing_saved;                    // without the key, a start does not touch the backlight
    nothing_saved.backlight.level = 35;
    nothing_saved.start();
    CHECK(nothing_saved.backlight.level == 35);
    CHECK(nothing_saved.backlight.sets() == 0);
}

TEST_CASE("SPEC-207 killed while the screen was off, the next start finds backlight_saved and puts the brightness back [parts: screen-off]") {
    TempFolder card;
    const std::string path = card.file("state.ini");
    {
        spz::fake::Files files;
        spz::fake::Clock clock;
        spz::fake::Executor quick;
        spz::fake::Backlight backlight;
        backlight.level = 40;
        spz::StateFile state(&files, &clock, &quick, path);
        spz::ScreenOff screen(&clock, &quick, &backlight, &state);
        state.load();
        screen.at_start();
        screen.set_after_s(10);
        screen.set_playing(true);
        clock.advance(10000);
        quick.run_all();
        REQUIRE(backlight.level == 0);
        // Killed here: nothing more is written, nothing is put back.
    }
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;
    spz::fake::Backlight backlight;
    backlight.level = 0;
    spz::StateFile state(&files, &clock, &quick, path);
    spz::ScreenOff screen(&clock, &quick, &backlight, &state);
    state.load();
    CHECK(state.state().backlight_saved == 40);
    screen.at_start();
    CHECK(backlight.level == 40);
    CHECK(state.state().backlight_saved == -1);
}

TEST_CASE("SPEC-207 with a backlight that cannot be changed the setting is not available, and nothing is ever turned off [parts: screen-off]") {
    Rig rig;
    rig.backlight.writable = false;
    rig.start();
    CHECK_FALSE(rig.screen.available());  // the Settings row reads "Not available on this device"
    rig.screen.set_after_s(10);
    CHECK_FALSE(rig.screen.key());
    rig.screen.set_playing(true);
    rig.pass(60000);
    CHECK(rig.backlight.level == 70);
    CHECK(rig.backlight.sets() == 0);
    CHECK_FALSE(rig.screen.is_off());
    CHECK(rig.state.state().backlight_saved == -1);
    CHECK_FALSE(rig.screen.key());
    CHECK_FALSE(has_line(rig.card(), "backlight_saved = 70"));
}

#endif  // SPZ_PART_SCREEN_OFF
