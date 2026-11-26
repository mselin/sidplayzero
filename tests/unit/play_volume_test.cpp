// SPDX-License-Identifier: GPL-3.0-or-later
// The volume keys (SPEC-076) against the fake SystemVolume, which holds a level and records what it is
// asked: "get", "set 45", "toggle_mute".
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/volume.h"

using spz::VolumeKey;

namespace {

// The levels the fake was asked to set, in order.
std::vector<int> sets(const spz::fake::SystemVolume& volume) {
    std::vector<int> levels;
    for (const std::string& call : volume.log) {
        if (call.compare(0, 4, "set ") == 0) levels.push_back(std::stoi(call.substr(4)));
    }
    return levels;
}

int count(const spz::fake::SystemVolume& volume, const std::string& what) {
    int found = 0;
    for (const std::string& call : volume.log) found += call == what ? 1 : 0;
    return found;
}

spz::fake::SystemVolume at(int level) {
    spz::fake::SystemVolume volume;
    volume.level = level;
    return volume;
}

}  // namespace

TEST_CASE("SPEC-076 from 42 %, volume up asks for 45 % and volume down for 40 %") {
    spz::fake::SystemVolume up = at(42);
    const spz::VolumeOutcome raised = spz::press_volume_key(&up, VolumeKey::Up);
    REQUIRE_FALSE(up.log.empty());
    CHECK(up.log.front() == "get");                         // it reads the level through the seam first
    CHECK(sets(up) == std::vector<int>{45});
    CHECK(raised.notice == "MSG_VOLUME");
    CHECK(raised.level == 45);
    CHECK_FALSE(raised.muted);
    CHECK(up.calls >= 2);

    spz::fake::SystemVolume down = at(42);
    const spz::VolumeOutcome lowered = spz::press_volume_key(&down, VolumeKey::Down);
    CHECK(sets(down) == std::vector<int>{40});
    CHECK(lowered.notice == "MSG_VOLUME");
    CHECK(lowered.level == 40);
    CHECK(count(down, "toggle_mute") == 0);
}

TEST_CASE("SPEC-076 the level is set to the nearest multiple of 5 % above or below, from any level") {
    struct Case {
        int from, up, down;
    };
    const Case cases[] = {{42, 45, 40}, {45, 50, 40}, {40, 45, 35}, {1, 5, 0}, {4, 5, 0}, {5, 10, 0},
                          {96, 100, 95}, {99, 100, 95}, {95, 100, 90}, {50, 55, 45}};
    for (const Case& c : cases) {
        CAPTURE(c.from);
        spz::fake::SystemVolume up = at(c.from);
        CHECK(spz::press_volume_key(&up, VolumeKey::Up).level == c.up);
        CHECK(sets(up) == std::vector<int>{c.up});
        CHECK(up.level == c.up);
        spz::fake::SystemVolume down = at(c.from);
        CHECK(spz::press_volume_key(&down, VolumeKey::Down).level == c.down);
        CHECK(sets(down) == std::vector<int>{c.down});
        CHECK(down.level == c.down);
    }
}

TEST_CASE("SPEC-076 at 100 % volume up asks for nothing, and at 0 % volume down asks for nothing: never above 100 % or below 0 %") {
    spz::fake::SystemVolume full = at(100);
    spz::press_volume_key(&full, VolumeKey::Up);
    CHECK(sets(full).empty());
    CHECK(full.level == 100);
    CHECK(count(full, "get") >= 1);                         // it did look

    spz::fake::SystemVolume silent = at(0);
    spz::press_volume_key(&silent, VolumeKey::Down);
    CHECK(sets(silent).empty());
    CHECK(silent.level == 0);

    spz::fake::SystemVolume over = at(100);                 // and from the ends the other way it moves
    spz::press_volume_key(&over, VolumeKey::Down);
    CHECK(sets(over) == std::vector<int>{95});
    spz::fake::SystemVolume under = at(0);
    spz::press_volume_key(&under, VolumeKey::Up);
    CHECK(sets(under) == std::vector<int>{5});
}

TEST_CASE("SPEC-076 mute toggles the system's mute, and the notice then says Muted, or the level again") {
    spz::fake::SystemVolume volume = at(42);
    const spz::VolumeOutcome muted = spz::press_volume_key(&volume, VolumeKey::Mute);
    CHECK(count(volume, "toggle_mute") == 1);
    CHECK(sets(volume).empty());                            // the level is left alone
    CHECK(volume.muted);
    CHECK(muted.notice == "MSG_VOLUME");
    CHECK(muted.muted);

    const spz::VolumeOutcome unmuted = spz::press_volume_key(&volume, VolumeKey::Mute);
    CHECK(count(volume, "toggle_mute") == 2);
    CHECK_FALSE(volume.muted);
    CHECK(unmuted.notice == "MSG_VOLUME");
    CHECK_FALSE(unmuted.muted);
    CHECK(unmuted.level == 42);
}

TEST_CASE("SPEC-076 if the system's volume cannot be read or changed, the notice is MSG_VOLUME_FAILED") {
    for (const VolumeKey key : {VolumeKey::Up, VolumeKey::Down, VolumeKey::Mute}) {
        spz::fake::SystemVolume broken = at(42);
        broken.works = false;                               // as when pactl is missing or fails
        const spz::VolumeOutcome outcome = spz::press_volume_key(&broken, key);
        CHECK(outcome.notice == "MSG_VOLUME_FAILED");
        CHECK(broken.calls >= 1);                           // it tried
        CHECK(broken.level == 42);
        CHECK_FALSE(broken.muted);
    }
}

TEST_CASE("SPEC-076 five presses of volume up from 42 % ask for 45, 50, 55, 60 and 65 %") {
    spz::fake::SystemVolume volume = at(42);
    spz::VolumeOutcome last;
    for (int press = 0; press < 5; ++press) last = spz::press_volume_key(&volume, VolumeKey::Up);
    CHECK(sets(volume) == std::vector<int>{45, 50, 55, 60, 65});
    CHECK(last.level == 65);                                // the last level is the one the notice ends with (SPEC-113)
}
