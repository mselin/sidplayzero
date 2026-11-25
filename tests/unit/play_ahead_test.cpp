// SPDX-License-Identifier: GPL-3.0-or-later
// The player's side of the fetch-ahead (SPEC-141): while a song plays to its end, the entry that will
// play next, if it is absent, is asked for when the song has 10 s or less to go. The asking is the
// player's; the fetching is behind the TuneFetcher, for which these tests have a stand-in that keeps
// every request until the test answers it. What becomes of the answer is in play_states_test.cpp.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/player.h"
#include "support.h"

using spz::PlayerState;
using namespace spz::test;

TEST_CASE("SPEC-141 SPEC-060 with a 30-second present tune followed by an absent one, the fetch-ahead goes out when 10 s remain and not before") {
    PlayerFixture f;
    f.play({kLong, kAbsentC, kPresentE}, 0);
    REQUIRE(f.state() == PlayerState::Playing);
    REQUIRE(f.status().length_ms == 30000);
    f.clock.advance(19999);
    f.settle();
    CHECK(f.fetcher.requests.empty());                      // 10.001 s to go
    f.clock.advance(1);
    f.settle();
    REQUIRE(f.fetcher.requests.size() == 1);
    CHECK(f.fetcher.requests[0].path == kAbsentC);          // the next entry, and nothing further ahead
    CHECK(f.fetcher.requests[0].ahead);
    CHECK_FALSE(f.fetcher.requests[0].attended);
    CHECK(f.fetcher.asked >= 1);                            // it asked whether it may
    CHECK(f.state() == PlayerState::Playing);               // "it does not touch the player's state"
    CHECK(f.current() == kLong);
    CHECK(f.calls_since(0) == Calls{"load 1", "start"});
    f.clock.advance(9000);
    f.settle();
    CHECK(f.fetcher.requests.size() == 1);                  // once
}

TEST_CASE("SPEC-141 a song shorter than 10 s has its absent next entry fetched ahead from its start") {
    PlayerFixture f;
    f.play({kPresentA, kAbsentC}, 0);                       // two seconds long
    f.clock.advance(100);
    f.settle();
    REQUIRE(f.fetcher.requests.size() == 1);
    CHECK(f.fetcher.requests[0].path == kAbsentC);
    CHECK(f.fetcher.requests[0].ahead);
}

TEST_CASE("SPEC-141 nothing is fetched ahead when the next entry is on the device, or when there is none") {
    PlayerFixture f;
    f.play({kPresentA, kPresentE, kAbsentC}, 0);            // the absent one is two entries ahead
    f.clock.advance(1999);
    f.settle();
    CHECK(f.current() == kPresentA);
    CHECK(f.fetcher.requests.empty());                      // "and nothing further ahead"

    PlayerFixture g;
    g.play({kPresentA}, 0);
    g.run_to_end();
    CHECK(g.state() == PlayerState::Idle);
    CHECK(g.fetcher.requests.empty());
}

TEST_CASE("SPEC-141 SPEC-065 the fetch-ahead is for what will play next: while a tune still has a subtune to play, the entry after it is not fetched") {
    PlayerFixture f;
    f.play({kThree, kAbsentC}, 0);                          // subtune 2 of three, each two seconds
    f.clock.advance(1900);
    f.settle();
    CHECK(f.fetcher.requests.empty());                      // what follows subtune 2 is subtune 3
    f.run_to_end();
    REQUIRE(f.status().subtune == 3);
    f.clock.advance(100);
    f.settle();
    REQUIRE(f.fetcher.requests.size() == 1);                // what follows subtune 3 is the next entry
    CHECK(f.fetcher.requests[0].path == kAbsentC);
}

TEST_CASE("SPEC-141 the 10 s are of the song, not of the clock: a pause puts the fetch-ahead off") {
    PlayerFixture f;
    f.play({kLong, kAbsentC}, 0);
    f.clock.advance(15000);
    f.player->play_pause();
    f.settle();
    f.clock.advance(60000);                                 // paused with 15 s to go
    f.settle();
    CHECK(f.fetcher.requests.empty());
    f.player->play_pause();
    f.settle();
    f.clock.advance(4999);
    f.settle();
    CHECK(f.fetcher.requests.empty());
    f.clock.advance(1);
    f.settle();
    CHECK(f.fetcher.requests.size() == 1);
}

TEST_CASE("SPEC-141 SPEC-063 no fetch-ahead is started when the song has no length") {
    PlayerFixture f;
    f.settings.default_length_s = 0;                        // "until told otherwise"
    f.play({kUnlisted, kAbsentC}, 0);
    REQUIRE(f.status().length_ms == 0);
    f.clock.advance(600000);
    f.settle();
    CHECK(f.fetcher.requests.empty());
    CHECK(f.state() == PlayerState::Playing);
}

TEST_CASE("SPEC-141 SPEC-143 no fetch-ahead is started while absent tunes may not be requested: after a failed fetch, or with HVSC's notice not accepted") {
    for (const spz::UnattendedFetch leave : {spz::UnattendedFetch::NotNow, spz::UnattendedFetch::NoticeNeeded}) {
        PlayerFixture f;
        f.fetcher.leave = leave;
        f.play({kLong, kAbsentC}, 0);
        f.clock.advance(25000);
        f.settle();
        CHECK(f.fetcher.requests.empty());
        CHECK(f.state() == PlayerState::Playing);
        CHECK(f.notices.ids.empty());                       // nothing is said while the song plays
    }
}

TEST_CASE("SPEC-141 a key that leaves the song gives up its fetch-ahead, unless it leads to that very tune") {
    PlayerFixture f;
    f.play({kPresentA, kAbsentC, kPresentE}, 0);
    f.clock.advance(100);
    f.settle();
    REQUIRE(f.fetcher.in_flight(kAbsentC));
    f.player->play(f.entries({kPresentE}), 0);              // Enter on a third tune meanwhile cancels it
    f.settle();
    CHECK_FALSE(f.fetcher.in_flight(kAbsentC));
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kPresentE);

    PlayerFixture g;
    g.play({kPresentA, kAbsentC, kPresentE}, 0);
    g.clock.advance(100);
    g.settle();
    REQUIRE(g.fetcher.in_flight(kAbsentC));
    g.player->play(g.entries({kPresentA, kAbsentC, kPresentE}), 1);     // Enter on the tune being fetched ahead
    g.settle();
    CHECK(g.state() == PlayerState::Fetching);
    CHECK(g.fetcher.in_flight(kAbsentC));
    CHECK(g.fetcher.requests.size() == 1);                  // its request is taken over: no second one
    REQUIRE(g.fetcher.deliver(kAbsentC));
    g.settle();
    CHECK(g.state() == PlayerState::Playing);
    CHECK(g.current() == kAbsentC);
}

#if SPZ_PART_MODES
TEST_CASE("SPEC-141 SPEC-065 with `repeat` or `stop` nothing follows the song from the queue, so nothing is fetched ahead; with `next_tune` the next entry is, though the tune has subtunes left [parts: modes]") {
    for (const spz::WhenSongEnds mode : {spz::WhenSongEnds::Repeat, spz::WhenSongEnds::Stop}) {
        PlayerFixture f;
        f.settings.when_song_ends = mode;
        f.play({kPresentA, kAbsentC}, 0);
        f.clock.advance(1900);
        f.settle();
        CHECK(f.fetcher.requests.empty());
    }
    PlayerFixture g;
    g.settings.when_song_ends = spz::WhenSongEnds::NextTune;
    g.play({kThree, kAbsentC}, 0);
    g.clock.advance(100);
    g.settle();
    REQUIRE(g.fetcher.requests.size() == 1);
    CHECK(g.fetcher.requests[0].path == kAbsentC);
}

TEST_CASE("SPEC-141 SPEC-065 with repeat-all the entry after the last is the first, and it is fetched ahead if it is absent [parts: modes]") {
    PlayerFixture f;
    f.settings.repeat_all = true;
    f.play({kAbsentC, kPresentA}, 1);
    f.clock.advance(100);
    f.settle();
    REQUIRE(f.fetcher.requests.size() == 1);
    CHECK(f.fetcher.requests[0].path == kAbsentC);
    CHECK(f.fetcher.requests[0].ahead);
}
#endif
