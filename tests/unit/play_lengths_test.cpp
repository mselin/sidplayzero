// SPDX-License-Identifier: GPL-3.0-or-later
// How long a song is, and how the player tells the backend: song lengths from the listing (SPEC-063), the
// length as a number of samples and the elapsed time (SPEC-064), and starting again (SPEC-066). Against
// the fake SoundBackend, whose position moves with the fake clock.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/player.h"
#include "support.h"

using spz::PlayerState;
using namespace spz::test;

namespace {

const spz::Millis kDefaultMs = 210000;      // 3:30, where `default_length` starts (SPEC-063)

}  // namespace

// ---- SPEC-063 ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-063 each fixture subtune reports the length the database gives, to the millisecond") {
    PlayerFixture f;
    f.play({kThreeLengths}, 0);                             // 0:01.001 0:02.345 1:01.288
    CHECK(f.status().subtune == 1);
    CHECK(f.status().length_ms == 1001);
    f.player->next_subtune();
    f.settle();
    CHECK(f.status().length_ms == 2345);
    f.player->next_subtune();
    f.settle();
    CHECK(f.status().length_ms == 61288);

    f.play({kOdd}, 0);
    CHECK(f.status().length_ms == 2001);
    f.play({kLong}, 0);
    CHECK(f.status().length_ms == 30000);
    f.play({kThree}, 0);
    CHECK(f.status().length_ms == 2000);
}

TEST_CASE("SPEC-063 a tune copied to another path is still found by its MD5") {
    PlayerFixture f;
    f.put("/ELSEWHERE/A Copy.sid", f.tune(kThreeLengths)); // a path the database does not have
    f.play({"/ELSEWHERE/A Copy.sid"}, 0);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().length_ms == 1001);
    f.player->next_subtune();
    f.settle();
    CHECK(f.status().length_ms == 2345);
}

TEST_CASE("SPEC-063 a different tune put at a listed path reports the default, not that path's lengths") {
    PlayerFixture f;
    f.put(kLong, f.tune(kUnlisted));                        // the database lists the path with 0:30, for other bytes
    f.play({kLong}, 0);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().length_ms == kDefaultMs);
}

TEST_CASE("SPEC-063 a tune the database leaves out reports the default length, 3:30") {
    PlayerFixture f;
    f.play({kUnlisted}, 0);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().length_ms == kDefaultMs);
    CHECK(f.backend.length == 210u * 48000u);
}

TEST_CASE("SPEC-063 SPEC-047 with no listing every tune has the default length") {
    PlayerFixture f(false);
    for (const char* path : {kOne, kLong, kThreeLengths}) {
        CAPTURE(std::string(path));
        f.play({path}, 0);
        CHECK(f.state() == PlayerState::Playing);
        CHECK(f.status().length_ms == kDefaultMs);
    }
}

TEST_CASE("SPEC-063 the default length is the setting `default_length`, from 0:05 to 59:59") {
    PlayerFixture f;
    f.settings.default_length_s = 5;
    f.play({kUnlisted}, 0);
    CHECK(f.status().length_ms == 5000);
    CHECK(f.backend.length == 5u * 48000u);
    f.settings.default_length_s = 3599;
    f.play({kUnlisted}, 0);
    CHECK(f.status().length_ms == 3599000);
    CHECK(f.backend.length == 3599u * 48000u);
    f.play({kOne}, 0);                                      // a listed tune keeps the database's length
    CHECK(f.status().length_ms == 2000);
}

TEST_CASE("SPEC-063 SPEC-025 a default length of 0 means until told otherwise: the song has no known length and the backend is told never to end it") {
    PlayerFixture f;
    f.settings.default_length_s = 0;
    f.play({kUnlisted}, 0);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().length_ms == 0);
    CHECK(f.backend.length == 0);                           // set_length(0): never
    f.clock.advance(3600 * 1000);
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().elapsed_ms == 3600 * 1000);
}

TEST_CASE("SPEC-063 an absent tune's length is looked up by its path: the number of subtunes and the first length are known before its file is") {
    PlayerFixture f;
    f.play({kAbsentB}, 0);                                  // the database: 0:05 0:06 0:07
    REQUIRE(f.state() == PlayerState::Fetching);
    CHECK(f.status().subtunes == 3);
    CHECK(f.status().length_ms == 5000);
    CHECK_FALSE(f.status().tune_known);                     // no header yet
    REQUIRE(f.fetcher.deliver(kAbsentB));
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().tune_known);
    CHECK(f.status().subtune == 2);                         // its header's default, now that there is a header
    CHECK(f.status().length_ms == 6000);
}

// ---- SPEC-064 ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-064 the player gives the backend the length in samples: milliseconds times the rate, divided by 1,000") {
    PlayerFixture f;
    f.play({kThree}, 0);
    CHECK(f.backend.length == 96000);                       // 2.000 s at 48,000 Hz
    CHECK(f.backend.count("set_length") == 1);
    CHECK(f.backend.loaded_settings.sample_rate == 48000);
    f.play({kOdd}, 0);
    CHECK(f.backend.length == 96048);                       // 2.001 s
    f.play({kThreeLengths}, 0);
    CHECK(f.backend.length == 48048);                       // 1.001 s
    f.player->next_subtune();
    f.settle();
    CHECK(f.backend.length == 112560);                      // 2.345 s
    f.player->next_subtune();
    f.settle();
    CHECK(f.backend.length == 2941824);                     // 61.288 s
}

#if SPZ_PART_RATE
TEST_CASE("SPEC-064 SPEC-073 at 44,100 Hz the length in samples is rounded down [parts: rate]") {
    PlayerFixture f;
    f.settings.sample_rate = 44100;
    f.play({kOdd}, 0);
    CHECK(f.backend.loaded_settings.sample_rate == 44100);
    CHECK(f.backend.length == 88244);                       // 2,001 ms x 44,100 / 1,000 = 88,244.1
    f.play({kThreeLengths}, 0);
    CHECK(f.backend.length == 44144);                       // 1,001 ms: 44,144.1
    f.player->next_subtune();
    f.settle();
    CHECK(f.backend.length == 103414);                      // 2,345 ms: 103,414.5
    f.player->next_subtune();
    f.settle();
    CHECK(f.backend.length == 2702800);                     // 61,288 ms: 2,702,800.8
    f.play({kThree}, 0);
    CHECK(f.backend.length == 88200);                       // 2,000 ms: exactly
}

TEST_CASE("SPEC-073 a change of the sample rate takes effect when the next tune is loaded [parts: rate]") {
    PlayerFixture f;
    f.play({kOne, kTwoChips}, 0);
    REQUIRE(f.backend.loaded_settings.sample_rate == 48000);
    const std::size_t mark = f.mark();
    f.settings.sample_rate = 44100;
    f.player->settings_changed();
    f.settle();
    CHECK(f.calls_since(mark).empty());                     // the tune that plays is left alone
    CHECK(f.status().sample_rate == 48000);
    f.player->next_tune();
    f.settle();
    CHECK(f.backend.loaded_settings.sample_rate == 44100);
    CHECK(f.status().sample_rate == 44100);
    CHECK(f.backend.length == 88200);
}
#endif

TEST_CASE("SPEC-064 SPEC-025 the elapsed time is the backend's position divided by the rate, and stands still while paused") {
    PlayerFixture f;
    f.play({kLong}, 0);
    CHECK(f.status().elapsed_ms == 0);
    f.clock.advance(1500);
    CHECK(f.status().elapsed_ms == 1500);
    CHECK(f.backend.position() == 72000);
    f.player->play_pause();
    f.settle();
    f.clock.advance(4000);
    CHECK(f.status().elapsed_ms == 1500);
    f.player->play_pause();
    f.settle();
    f.clock.advance(500);
    CHECK(f.status().elapsed_ms == 2000);
    f.player->stop();
    f.settle();
    CHECK(f.status().elapsed_ms == 0);                      // Idle with a current entry: 0:00 and the total (SPEC-096)
    CHECK(f.status().length_ms == 30000);
}

TEST_CASE("SPEC-025 the player makes sound only through the backend: a play, a pause, a resume and a stop are each one call of its, in that order") {
    PlayerFixture f;
    f.play({kOne}, 0);
    f.player->play_pause();
    f.player->play_pause();
    f.player->stop();
    f.settle();
    CHECK(f.calls_since(0) == Calls{"load 1", "start", "pause", "resume", "stop"});
    CHECK(f.backend.calls >= 6);                            // those, and set_length
    // Its events come back the same way: the fake reports the song's end only when told.
    f.play({kOne}, 0);
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Playing);               // long past its two seconds: nobody has said it ended
    f.backend.emit_song_ended();
    f.settle();
    CHECK(f.state() == PlayerState::Idle);
}

// ---- SPEC-066 ----------------------------------------------------------------------------------------------------

#if SPZ_PART_RESTART
TEST_CASE("SPEC-066 restart loads the current subtune again from its beginning: the same subtune, and position 0 [parts: restart]") {
    PlayerFixture f;
    f.play({kOne, kThree}, 1);
    f.player->next_subtune();
    f.settle();
    REQUIRE(f.status().subtune == 3);
    f.clock.advance(1200);
    REQUIRE(f.backend.position() == 57600);
    const std::size_t mark = f.mark();
    f.player->restart();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 3);                         // not the tune's default
    CHECK(tail(f.calls_since(mark, false), 2) == Calls{"load 3", "start"});
    CHECK(f.backend.position() == 0);
    CHECK(f.status().elapsed_ms == 0);
    CHECK(f.backend.length == 96000);                       // and its length is given again
    CHECK(f.player->place() == 1);
}
#endif

#if SPZ_PART_SKIP
TEST_CASE("SPEC-066 skipping ahead is on while its key is held: the player tells the backend when it goes down and when it comes up [parts: skip]") {
    PlayerFixture f;
    f.play({kLong}, 0);
    f.player->set_skip(true);
    CHECK(f.backend.log.back() == "set_skip on");
    f.clock.advance(1000);
    f.player->set_skip(false);
    CHECK(f.backend.log.back() == "set_skip off");
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().subtune == 1);
    CHECK(f.backend.count("set_skip") == 2);
}
#endif
