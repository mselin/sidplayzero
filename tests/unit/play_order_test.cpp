// SPDX-License-Identifier: GPL-3.0-or-later
// What the player plays, and in what order: subtunes (SPEC-061), the queue (SPEC-062), and what follows
// the end of a song (SPEC-065). Against the fake SoundBackend, with the fixture collection's two-second
// lengths and the fake clock.
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/player.h"
#include "support.h"

using spz::Entry;
using spz::PlayerState;
using namespace spz::test;

namespace {

using Played = std::vector<std::pair<std::string, int>>;    // each a tune's path and the subtune that played

// Lets the queue play by itself from where it is: each song runs to its end, until the player is no
// longer Playing or `most` songs have been heard. Returns what played, the song now playing first.
Played play_out(PlayerFixture& f, std::size_t most = 50) {
    Played played;
    while (f.state() == PlayerState::Playing && played.size() < most) {
        played.push_back({f.current(), f.status().subtune});
        f.run_to_end();
    }
    return played;
}

// Ten tunes on the device, numbered as SPEC-065's oracle numbers its entries: entry n is "/TEN/n.sid".
// They are copies of one fixture tune, so the listing gives each of them its two seconds.
std::vector<Entry> ten_tunes(PlayerFixture& f) {
    std::vector<Entry> queue;
    for (int n = 1; n <= 10; ++n) {
        const std::string path = "/TEN/" + std::string(n < 10 ? "0" : "") + std::to_string(n) + ".sid";
        f.put(path, f.tune(kOne));
        queue.push_back(entry(path));
    }
    return queue;
}

int number(const std::string& path) { return std::stoi(path.substr(5, 2)); }

std::vector<int> numbers(const Played& played) {
    std::vector<int> found;
    for (const auto& song : played) found.push_back(number(song.first));
    return found;
}

std::vector<int> numbers(const std::vector<Entry>& entries) {
    std::vector<int> found;
    for (const Entry& e : entries) found.push_back(number(e.path));
    return found;
}

}  // namespace

// ---- SPEC-061 ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-061 the three-subtune fixture with default 2: play starts at 2, next gives 3, next again starts the following entry") {
    PlayerFixture f;
    f.play({kOne, kThree, kTwoChips}, 1);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.status().subtune == 2);
    CHECK(f.status().subtunes == 3);
    CHECK(f.backend.loaded_subtune == 2);

    std::size_t mark = f.mark();
    f.player->next_subtune();
    f.settle();
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 3);
    CHECK(f.calls_since(mark) == Calls{"stop", "load 3", "start"});

    mark = f.mark();
    f.player->next_subtune();
    f.settle();
    CHECK(f.current() == kTwoChips);                        // next from the last subtune does "next tune"
    CHECK(f.status().subtune == 1);
    CHECK(f.player->place() == 2);
    CHECK(f.calls_since(mark) == Calls{"stop", "load 1", "start"});
}

TEST_CASE("SPEC-061 from the default subtune 2, previous gives 1, and previous again starts the preceding entry") {
    PlayerFixture f;
    f.play({kOne, kThree, kTwoChips}, 1);
    f.player->previous_subtune();
    f.settle();
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 1);
    CHECK(f.backend.loaded_subtune == 1);
    f.player->previous_subtune();
    f.settle();
    CHECK(f.current() == kOne);                             // previous from the first subtune does "previous tune"
    CHECK(f.player->place() == 0);
    CHECK(f.state() == PlayerState::Playing);
}

TEST_CASE("SPEC-061 an entry with a subtune number plays that subtune, and a number the tune does not have is treated as the default") {
    PlayerFixture f;
    f.player->play({entry(kThree, 3)}, 0);
    f.settle();
    CHECK(f.status().subtune == 3);
    CHECK(f.backend.loaded_subtune == 3);
    f.player->play({entry(kThree, 1)}, 0);
    f.settle();
    CHECK(f.backend.loaded_subtune == 1);
    for (const int absurd : {4, 99, -1}) {
        CAPTURE(absurd);
        f.player->play({entry(kThree, absurd)}, 0);
        f.settle();
        CHECK(f.state() == PlayerState::Playing);
        CHECK(f.status().subtune == 2);
        CHECK(f.backend.loaded_subtune == 2);
    }
}

TEST_CASE("SPEC-061 SPEC-062 next from the last subtune of the last entry, and next tune there, stop playback") {
    for (const bool by_subtune : {true, false}) {
        CAPTURE(by_subtune);
        PlayerFixture f;
        f.play({kOne, kThree}, 1);
        f.player->next_subtune();                           // subtune 3, the tune's last
        f.settle();
        REQUIRE(f.status().subtune == 3);
        const std::size_t mark = f.mark();
        if (by_subtune) f.player->next_subtune();
        else f.player->next_tune();
        f.settle();
        CHECK(f.state() == PlayerState::Idle);
        CHECK(f.current() == kThree);                       // the current entry stays
        CHECK(f.calls_since(mark) == Calls{"stop"});
    }
}

// ---- SPEC-062 ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-062 in a three-tune folder, next from the second plays the third, and next again leaves the player Idle") {
    PlayerFixture f;
    f.play({kOne, kTwoChips, kLong}, 1);
    f.player->next_tune();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kLong);
    const std::size_t mark = f.mark();
    f.player->next_tune();
    f.settle();
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.current() == kLong);
    CHECK(f.calls_since(mark) == Calls{"stop"});
}

TEST_CASE("SPEC-062 previous at the first entry plays it again, from its start") {
    PlayerFixture f;
    f.play({kOne, kTwoChips}, 1);
    f.player->previous_tune();
    f.settle();
    CHECK(f.current() == kOne);
    f.clock.advance(1000);
    REQUIRE(f.status().elapsed_ms == 1000);
    const std::size_t mark = f.mark();
    f.player->previous_tune();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kOne);
    CHECK(f.player->place() == 0);
    CHECK(f.calls_since(mark) == Calls{"stop", "load 1", "start"});
    CHECK(f.status().elapsed_ms == 0);
}

TEST_CASE("SPEC-062 the queue is a copy: a new search made while the results of an earlier one play leaves the queue as it was") {
    PlayerFixture f;
    std::vector<Entry> results = f.entries({kOne, kTwoChips, kLong});       // what a search found
    f.player->play(results, 0);
    f.settle();
    results = f.entries({kPresentA, kPresentE});            // a new search replaces what the screen holds
    CHECK(f.player->queue() == f.entries({kOne, kTwoChips, kLong}));
    f.player->next_tune();
    f.settle();
    CHECK(f.current() == kTwoChips);
    f.player->next_tune();
    f.settle();
    CHECK(f.current() == kLong);
    CHECK(f.player->queue().size() == 3);
}

TEST_CASE("SPEC-062 a queue played from the browser or from search results has no subtune numbers: each tune plays its default subtune") {
    PlayerFixture f;
    f.play({kThreeLengths, kThree, kOne}, 0);
    CHECK(f.status().subtune == 1);                         // Three Lengths' default
    f.player->next_tune();
    f.settle();
    CHECK(f.status().subtune == 2);                         // Three's default
    f.player->next_tune();
    f.settle();
    CHECK(f.status().subtune == 1);
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("SPEC-062 started from a playlist whose order differs from the folder's, next follows the playlist, with the subtune numbers it has; deleting the playlist leaves the queue as it was [parts: playlists]") {
    PlayerFixture f;
    {
        // The playlist's entries: not in the folder's order, and one of them with a subtune number.
        std::vector<Entry> playlist = {entry(kTwoChips), entry(kThree, 3), entry(kOne)};
        f.player->play(playlist, 0);
        f.settle();
        playlist.clear();                                   // the playlist is deleted while it plays
    }
    CHECK(f.current() == kTwoChips);
    CHECK(f.player->queue().size() == 3);
    f.player->next_tune();
    f.settle();
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 3);                         // as the playlist has it, not the tune's default
    f.player->next_tune();
    f.settle();
    CHECK(f.current() == kOne);
    f.player->previous_tune();
    f.settle();
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 3);
}
#endif

#if SPZ_PART_MODES
TEST_CASE("SPEC-062 SPEC-065 with repeat-all, next tune at the last entry goes to the first [parts: modes]") {
    PlayerFixture f;
    f.settings.repeat_all = true;
    f.play({kOne, kTwoChips}, 1);
    const std::size_t mark = f.mark();
    f.player->next_tune();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kOne);
    CHECK(f.player->place() == 0);
    CHECK(f.calls_since(mark) == Calls{"stop", "load 1", "start"});
}
#endif

// ---- SPEC-065 ----------------------------------------------------------------------------------------------------

TEST_CASE("SPEC-065 `subtunes` on a folder queue of the three-subtune fixture and a one-subtune tune plays subtunes 2 and 3, then the second tune, and ends in Idle") {
    PlayerFixture f;
    REQUIRE(f.settings.when_song_ends == spz::WhenSongEnds::Subtunes);     // the default
    f.play({kThree, kOne}, 0);
    CHECK(f.backend.length == 96000);                       // the two-second test length, at 48,000 Hz
    CHECK(play_out(f) == Played{{kThree, 2}, {kThree, 3}, {kOne, 1}});
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.current() == kOne);
    CHECK(f.backend.count("load") == 3);
    CHECK(f.backend.count("start") == 3);
    CHECK(f.clock.now_ms() == 6000);                        // three songs of two seconds, with nothing between them
    CHECK(f.notices.ids.empty());
}

TEST_CASE("SPEC-065 a subtune reached with the subtune keys is followed, at its end, by the tune's next subtune all the same") {
    PlayerFixture f;
    f.play({kThree, kOne}, 0);
    f.player->previous_subtune();                           // subtune 1
    f.settle();
    CHECK(play_out(f) == Played{{kThree, 1}, {kThree, 2}, {kThree, 3}, {kOne, 1}});
    CHECK(f.state() == PlayerState::Idle);
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("SPEC-065 `subtunes` on a playlist whose entry names subtune 1 plays that subtune and then the next entry [parts: playlists]") {
    PlayerFixture f;
    f.player->play({entry(kThree, 1), entry(kOne)}, 0);
    f.settle();
    CHECK(play_out(f) == Played{{kThree, 1}, {kOne, 1}});
    CHECK(f.state() == PlayerState::Idle);
}
#endif

#if SPZ_PART_MODES
TEST_CASE("SPEC-065 `next_tune` plays the next entry after each song: one subtune of each tune [parts: modes]") {
    PlayerFixture f;
    f.settings.when_song_ends = spz::WhenSongEnds::NextTune;
    f.play({kThree, kOne}, 0);
    CHECK(play_out(f) == Played{{kThree, 2}, {kOne, 1}});
    CHECK(f.state() == PlayerState::Idle);
}

TEST_CASE("SPEC-065 `repeat` plays the same subtune again, from its beginning, as often as it ends [parts: modes]") {
    PlayerFixture f;
    f.settings.when_song_ends = spz::WhenSongEnds::Repeat;
    f.play({kThree, kOne}, 0);
    CHECK(play_out(f, 4) == Played{{kThree, 2}, {kThree, 2}, {kThree, 2}, {kThree, 2}});
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kThree);
    CHECK(f.status().subtune == 2);
    CHECK(f.status().elapsed_ms == 0);
    CHECK(f.backend.count("start") == 5);
}

TEST_CASE("SPEC-065 `stop` plays nothing after the song: the player is Idle [parts: modes]") {
    PlayerFixture f;
    f.settings.when_song_ends = spz::WhenSongEnds::Stop;
    f.play({kThree, kOne}, 0);
    CHECK(play_out(f) == Played{{kThree, 2}});
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.current() == kThree);
    CHECK(f.backend.count("load") == 1);
}

TEST_CASE("SPEC-065 with `repeat_all` the last entry is followed by the first, in `subtunes` and in `next_tune` [parts: modes]") {
    PlayerFixture f;
    f.settings.repeat_all = true;
    f.play({kThree, kOne}, 0);
    CHECK(play_out(f, 7) == Played{{kThree, 2}, {kThree, 3}, {kOne, 1}, {kThree, 2}, {kThree, 3}, {kOne, 1}, {kThree, 2}});
    CHECK(f.state() == PlayerState::Playing);

    PlayerFixture g;
    g.settings.repeat_all = true;
    g.settings.when_song_ends = spz::WhenSongEnds::NextTune;
    g.play({kThree, kOne}, 0);
    CHECK(play_out(g, 5) == Played{{kThree, 2}, {kOne, 1}, {kThree, 2}, {kOne, 1}, {kThree, 2}});
}

TEST_CASE("SPEC-065 `repeat_all` does not change `stop` or `repeat`: they never reach the next entry [parts: modes]") {
    PlayerFixture f;
    f.settings.repeat_all = true;
    f.settings.when_song_ends = spz::WhenSongEnds::Stop;
    f.play({kOne}, 0);
    CHECK(play_out(f) == Played{{kOne, 1}});
    CHECK(f.state() == PlayerState::Idle);
}

TEST_CASE("SPEC-065 a change of `when_song_ends` while a song plays decides what follows that song [parts: modes]") {
    PlayerFixture f;
    f.play({kThree, kOne}, 0);
    f.clock.advance(1000);
    f.settings.when_song_ends = spz::WhenSongEnds::NextTune;       // the "Mode" key, mid-song
    f.player->settings_changed();
    CHECK(play_out(f) == Played{{kThree, 2}, {kOne, 1}});
}
#endif

#if SPZ_PART_SHUFFLE
TEST_CASE("SPEC-065 shuffle with seed 1: ten entries numbered 1 to 10, started at entry 3, play in the order 3, 4, 8, 1, 10, 9, 5, 6, 7, 2 [parts: shuffle]") {
    PlayerFixture f;                                        // its player has the seed 1, as --seed 1 gives it
    f.settings.shuffle = true;
    const std::vector<Entry> queue = ten_tunes(f);
    f.player->play(queue, 2);
    f.settle();
    CHECK(numbers(f.player->queue()) == std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2});
    CHECK(f.player->place() == 0);
    CHECK(numbers(play_out(f)) == std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2});
    CHECK(f.state() == PlayerState::Idle);
}

TEST_CASE("SPEC-065 shuffle follows the keys too: next tune and previous tune move through the shuffled order [parts: shuffle]") {
    PlayerFixture f;
    f.settings.shuffle = true;
    f.player->play(ten_tunes(f), 2);
    f.settle();
    f.player->next_tune();
    f.settle();
    CHECK(number(f.current()) == 4);
    f.player->next_tune();
    f.settle();
    CHECK(number(f.current()) == 8);
    f.player->previous_tune();
    f.settle();
    CHECK(number(f.current()) == 4);
}

TEST_CASE("SPEC-065 switching shuffle on while a queue plays permutes the entries after the current one, and the song goes on [parts: shuffle]") {
    PlayerFixture f;
    f.player->play(ten_tunes(f), 2);
    f.settle();
    const std::size_t mark = f.mark();
    f.settings.shuffle = true;
    f.player->settings_changed();
    f.settle();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(number(f.current()) == 3);
    CHECK(f.player->place() == 2);
    CHECK(f.calls_since(mark).empty());
    // The seven entries after the third, permuted by the item's loop with the first numbers of seed 1.
    CHECK(numbers(f.player->queue()) == std::vector<int>{1, 2, 3, 4, 8, 6, 5, 7, 10, 9});
    CHECK(numbers(play_out(f)) == std::vector<int>{3, 4, 8, 6, 5, 7, 10, 9});
}

TEST_CASE("SPEC-065 switching shuffle off while a queue plays puts it back in its original order from the current entry on, and the song goes on [parts: shuffle]") {
    PlayerFixture f;
    f.settings.shuffle = true;
    f.player->play(ten_tunes(f), 2);
    f.settle();
    f.player->next_tune();
    f.settle();
    f.player->next_tune();
    f.settle();
    REQUIRE(number(f.current()) == 8);
    const std::size_t mark = f.mark();
    f.settings.shuffle = false;
    f.player->settings_changed();
    f.settle();
    CHECK(f.calls_since(mark).empty());                     // the song goes on
    CHECK(numbers(f.player->queue()) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK(f.player->place() == 7);
    CHECK(numbers(play_out(f)) == std::vector<int>{8, 9, 10});
}

TEST_CASE("SPEC-065 without --seed the seed is the clock's value at start: the order is that of a player given that value as its seed [parts: shuffle]") {
    std::vector<int> orders[2];
    for (int given = 0; given < 2; ++given) {
        PlayerFixture f;
        f.options.has_seed = given == 1;
        f.options.seed = given == 1 ? static_cast<std::uint64_t>(f.clock.wall_time_s()) : 0;
        f.make();
        f.settings.shuffle = true;
        f.player->play(ten_tunes(f), 2);
        f.settle();
        orders[given] = numbers(f.player->queue());
    }
    CHECK(orders[0] == orders[1]);
    CHECK(orders[0] != std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2});   // not seed 1's
    CHECK(orders[0].front() == 3);
}
#endif

#if SPZ_PART_SHUFFLE && SPZ_PART_MODES
TEST_CASE("SPEC-065 shuffle with seed 1 and repeat-all: after 3, 4, 8, 1, 10, 9, 5, 6, 7, 2 the second round is 9, 8, 1, 2, 4, 10, 3, 7, 6, 5 [parts: shuffle modes]") {
    PlayerFixture f;
    f.settings.shuffle = true;
    f.settings.repeat_all = true;
    f.player->play(ten_tunes(f), 2);
    f.settle();
    CHECK(numbers(play_out(f, 20)) ==
          std::vector<int>{3, 4, 8, 1, 10, 9, 5, 6, 7, 2, 9, 8, 1, 2, 4, 10, 3, 7, 6, 5});
    CHECK(f.state() == PlayerState::Playing);               // and a third round has begun
}
#endif
