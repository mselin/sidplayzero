// SPDX-License-Identifier: GPL-3.0-or-later
// When an entry cannot be played (SPEC-069), row by row of the item's table, and the ROMs a tune may
// need (SPEC-068). Against the fake SoundBackend and Clock and the stand-in for fetching.
//
// "Attended" is an entry a key chose; "unattended" one that a song's end reached. Here a song ends when
// the test moves the clock on by its two seconds and tells the fake backend to say so.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "play_support.h"
#include "spz/player.h"
#include "spz/player_roms.h"
#include "support.h"

using spz::PlayerState;
using namespace spz::test;

namespace {

// Moves the clock to one millisecond before the unattended wait is over, checks that the player is still
// Failed on the same entry, and then lets the wait end.
void wait_out_the_failure(PlayerFixture& f) {
    const std::string failed_on = f.current();
    REQUIRE(f.state() == PlayerState::Failed);
    f.clock.advance(spz::kFailedWaitMs - 1);
    f.settle();
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.current() == failed_on);
    f.clock.advance(1);
    f.settle();
}

// An absent entry that playback has reached by itself, with no fetch-ahead out for it, is requested: at
// once, or after the 700 ms that a step waits. The specification does not say which; either is taken.
void until_requested(PlayerFixture& f) {
    if (f.state() == PlayerState::Waiting) {
        f.clock.advance(spz::kStepWaitMs);
        f.settle();
    }
    REQUIRE(f.state() == PlayerState::Fetching);
}

}  // namespace

// ---- The oracle's three queues -----------------------------------------------------------------------------------

TEST_CASE("SPEC-069 a queue of a good tune, five truncated ones and another good one plays the first and ends Idle with MSG_STOPPED_BAD_FILES after the fifth bad file") {
    PlayerFixture f;
    f.play({kGoodA, kBad[0], kBad[1], kBad[2], kBad[3], kBad[4], kGoodG}, 0);
    REQUIRE(f.state() == PlayerState::Playing);
    CHECK(f.backend.length == 96000);                       // the two-second length
    f.run_to_end();
    for (int bad = 0; bad < 5; ++bad) {
        CAPTURE(bad);
        CHECK(f.state() == PlayerState::Failed);
        CHECK(f.current() == kBad[bad]);
        CHECK(f.status().message == "MSG_INVALID_SID");
        CHECK(f.notices.ids.empty());
        wait_out_the_failure(f);
    }
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.notices.ids == Calls{"MSG_STOPPED_BAD_FILES"});
    CHECK(f.backend.count("load") == 1);                    // the first tune, and never the last
    CHECK(f.current() == kBad[4]);
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.backend.count("load") == 1);
}

TEST_CASE("SPEC-069 a queue of good, bad, good plays both good ones, the bad one shown for 2 s in between") {
    PlayerFixture f;
    f.play({kGoodA, kBad[0], kGoodG}, 0);
    f.run_to_end();
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_INVALID_SID");
    wait_out_the_failure(f);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kGoodG);
    f.run_to_end();
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.backend.count("start") == 2);
    CHECK(f.notices.ids.empty());
}

TEST_CASE("SPEC-069 a bad file chosen with Enter leaves the player Failed and does not advance") {
    PlayerFixture f;
    f.play({kGoodA, kBad[0], kGoodG}, 1);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_INVALID_SID");
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.current() == kBad[0]);
    CHECK(f.backend.count("load") + f.backend.count("start") == 0);   // nothing was given to the backend
    CHECK(f.notices.ids.empty());
}

TEST_CASE("SPEC-069 an entry that plays resets the count of bad files: four, a good one and four more do not stop playback") {
    PlayerFixture f;
    f.play({kGoodA, kBad[0], kBad[1], kBad[2], kBad[3], kGoodG, kBad[0], kBad[1], kBad[2], kBad[3], kGoodA}, 0);
    for (int round = 0; round < 2; ++round) {
        CAPTURE(round);
        REQUIRE(f.state() == PlayerState::Playing);
        f.run_to_end();
        for (int bad = 0; bad < 4; ++bad) wait_out_the_failure(f);
    }
    CHECK(f.state() == PlayerState::Playing);               // the third good tune
    CHECK(f.player->place() == 10);
    CHECK(f.backend.count("start") == 3);
    CHECK(f.notices.ids.empty());
}

TEST_CASE("SPEC-069 SPEC-065 after a failed entry at the end of the queue there is no next entry: the player is Idle, without a notice") {
    PlayerFixture f;
    f.play({kGoodA, kBad[0]}, 0);
    f.run_to_end();
    wait_out_the_failure(f);
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.notices.ids.empty());
}

// ---- The table's rows: the message, and what happens unattended ----------------------------------------------------

TEST_CASE("SPEC-069 the engine refuses a tune: MSG_ENGINE_REFUSED; attended it stays Failed, unattended the next entry plays after 2 s") {
    PlayerFixture f;
    f.backend.refuse = true;                                // load() fails, with MSG_ENGINE_REFUSED as its reason
    f.play({kOne, kTwoChips}, 0);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_ENGINE_REFUSED");
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.current() == kOne);

    PlayerFixture g;
    g.play({kGoodA, kOne, kGoodG}, 0);
    g.backend.refuse = true;
    g.run_to_end();
    CHECK(g.state() == PlayerState::Failed);
    CHECK(g.current() == kOne);
    CHECK(g.status().message == "MSG_ENGINE_REFUSED");
    g.backend.refuse = false;
    wait_out_the_failure(g);
    CHECK(g.state() == PlayerState::Playing);
    CHECK(g.current() == kGoodG);
}

TEST_CASE("SPEC-069 SPEC-068 a tune that needs BASIC with no ROM set: MSG_NEEDS_BASIC, and it is not given to the backend; unattended the next entry plays after 2 s") {
    PlayerFixture f;
    f.play({kNeedsBasic, kOne}, 0);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_NEEDS_BASIC");
    CHECK(f.backend.count("load") == 0);
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Failed);

    PlayerFixture g;
    g.play({kGoodA, kNeedsBasic, kGoodG}, 0);
    g.run_to_end();
    CHECK(g.state() == PlayerState::Failed);
    CHECK(g.status().message == "MSG_NEEDS_BASIC");
    wait_out_the_failure(g);
    CHECK(g.state() == PlayerState::Playing);
    CHECK(g.current() == kGoodG);
    CHECK(g.backend.count("load") == 2);
}

TEST_CASE("SPEC-069 each kind of failed fetch has its message, and a tune chosen with Enter stays Failed with it") {
    for (const char* message : {"MSG_FETCH_NOT_FOUND", "MSG_FETCH_MISMATCH", "MSG_FETCH_NETWORK", "MSG_FETCH_TOO_LARGE",
                                "MSG_CARD_FULL", "MSG_CANNOT_WRITE"}) {
        CAPTURE(std::string(message));
        PlayerFixture f;
        f.play({kPresentA, kAbsentC, kPresentE}, 1);
        REQUIRE(f.state() == PlayerState::Fetching);
        REQUIRE(f.fetcher.fail(kAbsentC, message));
        f.settle();
        CHECK(f.state() == PlayerState::Failed);
        CHECK(f.status().message == message);
        CHECK(f.current() == kAbsentC);
        f.clock.advance(60000);
        f.settle();
        CHECK(f.state() == PlayerState::Failed);            // attended: it stays there
        CHECK(f.fetcher.requests.size() == 1);
        CHECK(f.backend.count("load") + f.backend.count("start") == 0);   // nothing was given to the backend
    }
}

TEST_CASE("SPEC-069 unattended, a fetch that fails because the file is not there or does not match counts as a bad file: the next entry follows after 2 s, absent or not") {
    for (const char* message : {"MSG_FETCH_NOT_FOUND", "MSG_FETCH_MISMATCH"}) {
        CAPTURE(std::string(message));
        PlayerFixture f;
        f.play({kPresentA, kAbsentC, kAbsentD, kPresentE}, 0);
        f.run_to_end();                                     // the fetch-ahead for the second entry is still out
        REQUIRE(f.state() == PlayerState::Fetching);
        REQUIRE(f.fetcher.fail(kAbsentC, message));
        f.settle();
        CHECK(f.state() == PlayerState::Failed);
        CHECK(f.status().message == message);
        wait_out_the_failure(f);
        CHECK(f.current() == kAbsentD);                     // the next entry, which is requested in its turn
        until_requested(f);
        REQUIRE(f.fetcher.requests.size() == 2);
        CHECK(f.fetcher.requests[1].path == kAbsentD);
        CHECK_FALSE(f.fetcher.requests[1].attended);
    }
}

TEST_CASE("SPEC-069 SPEC-143 unattended, a fetch that fails for the network, the size, the card or the folder is not a bad file: after 2 s the next present entry plays, and no absent one is requested") {
    for (const char* message : {"MSG_FETCH_NETWORK", "MSG_FETCH_TOO_LARGE", "MSG_CARD_FULL", "MSG_CANNOT_WRITE"}) {
        CAPTURE(std::string(message));
        PlayerFixture f;
        f.play({kPresentA, kAbsentC, kAbsentD, kPresentE}, 0);
        f.run_to_end();
        REQUIRE(f.state() == PlayerState::Fetching);
        REQUIRE(f.fetcher.fail(kAbsentC, message));
        f.settle();
        CHECK(f.state() == PlayerState::Failed);
        CHECK(f.status().message == message);
        CHECK(f.current() == kAbsentC);
        wait_out_the_failure(f);
        CHECK(f.state() == PlayerState::Playing);
        CHECK(f.current() == kPresentE);
        CHECK(f.fetcher.requests.size() == 1);              // the one that failed
        CHECK(f.notices.ids.empty());
    }
}

TEST_CASE("SPEC-069 SPEC-143 when no present entry is left after such a failure, playback ends: Idle with the notice MSG_REST_ABSENT") {
    PlayerFixture f;
    f.play({kPresentA, kAbsentC, kAbsentD}, 0);
    f.run_to_end();
    REQUIRE(f.fetcher.fail(kAbsentC, "MSG_FETCH_NETWORK"));
    f.settle();
    wait_out_the_failure(f);
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.notices.ids == Calls{"MSG_REST_ABSENT"});
    CHECK(f.fetcher.requests.size() == 1);
}

TEST_CASE("SPEC-069 SPEC-143 an absent entry that may not be requested is passed over at once, unattended: no request, no message, no wait") {
    PlayerFixture f;
    f.fetcher.leave = spz::UnattendedFetch::NotNow;         // within the 60 s after a failed fetch
    f.play({kPresentA, kAbsentC, kAbsentD, kPresentE}, 0);
    f.clock.advance(100);
    f.settle();
    CHECK(f.fetcher.requests.empty());                      // and no fetch-ahead is started meanwhile (SPEC-141)
    f.run_to_end();
    CHECK(f.state() == PlayerState::Playing);               // at once: the clock has not moved since the song ended
    CHECK(f.current() == kPresentE);
    CHECK(f.fetcher.requests.empty());
    CHECK(f.notices.ids.empty());
    CHECK(f.status().message == "");
}

TEST_CASE("SPEC-069 SPEC-152 with HVSC's notice not accepted, an absent entry is passed over at once, unattended, and the notice MSG_NOTICE_NEEDED is raised once") {
    PlayerFixture f;
    f.fetcher.leave = spz::UnattendedFetch::NoticeNeeded;
    f.play({kPresentA, kAbsentC, kAbsentD, kPresentE}, 0);
    f.clock.advance(100);
    f.settle();
    CHECK(f.fetcher.requests.empty());
    f.run_to_end();
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.current() == kPresentE);
    CHECK(f.fetcher.requests.empty());
    CHECK(f.notices.ids == Calls{"MSG_NOTICE_NEEDED"});     // once, for the two entries passed over
}

TEST_CASE("SPEC-069 sound cannot be opened: MSG_NO_SOUND, and the player stays Failed, unattended too") {
    PlayerFixture f;
    f.backend.refuse = true;
    f.backend.refuse_why = "MSG_NO_SOUND";
    f.play({kOne, kTwoChips}, 0);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_NO_SOUND");

    PlayerFixture g;
    g.play({kGoodA, kOne, kGoodG}, 0);
    g.backend.refuse = true;
    g.backend.refuse_why = "MSG_NO_SOUND";
    g.run_to_end();
    CHECK(g.state() == PlayerState::Failed);
    CHECK(g.current() == kOne);
    CHECK(g.status().message == "MSG_NO_SOUND");
    g.backend.refuse = false;
    g.clock.advance(60000);
    g.settle();
    CHECK(g.state() == PlayerState::Failed);                // "Stays Failed"
    CHECK(g.current() == kOne);
    CHECK(g.notices.ids.empty());
    g.player->play_pause();                                 // "the next attempt to play tries again" (SPEC-078)
    g.settle();
    CHECK(g.state() == PlayerState::Playing);
    CHECK(g.current() == kOne);
}

TEST_CASE("SPEC-069 five bad files in a row of the five kinds that count stop unattended playback: invalid, refused, needing BASIC, not found, not matching") {
    PlayerFixture f;
    f.play({kGoodA, kInvalid, kOne, kNeedsBasic, kAbsentC, kAbsentD, kGoodG}, 0);
    f.run_to_end();
    CHECK(f.status().message == "MSG_INVALID_SID");
    f.backend.refuse = true;                                // the next one is refused by the engine
    wait_out_the_failure(f);
    CHECK(f.status().message == "MSG_ENGINE_REFUSED");
    f.backend.refuse = false;
    wait_out_the_failure(f);
    CHECK(f.status().message == "MSG_NEEDS_BASIC");
    wait_out_the_failure(f);
    until_requested(f);
    REQUIRE(f.fetcher.fail(kAbsentC, "MSG_FETCH_NOT_FOUND"));
    f.settle();
    CHECK(f.status().message == "MSG_FETCH_NOT_FOUND");
    wait_out_the_failure(f);
    until_requested(f);
    REQUIRE(f.fetcher.fail(kAbsentD, "MSG_FETCH_MISMATCH"));
    f.settle();
    CHECK(f.status().message == "MSG_FETCH_MISMATCH");
    CHECK(f.notices.ids.empty());
    wait_out_the_failure(f);
    CHECK(f.state() == PlayerState::Idle);
    CHECK(f.notices.ids == Calls{"MSG_STOPPED_BAD_FILES"});
    CHECK(f.backend.count("start") == 1);
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("SPEC-069 a playlist entry that is missing is passed over at once, unattended, and its message is not shown [parts: playlists]") {
    PlayerFixture f;
    f.play({kGoodA, kNowhere, kGoodG}, 0);
    f.run_to_end();
    CHECK(f.state() == PlayerState::Playing);               // at once: no 2 s in Failed
    CHECK(f.current() == kGoodG);
    CHECK(f.status().message == "");
    CHECK(f.notices.ids.empty());
    CHECK(f.fetcher.requests.empty());                      // HVSC does not list it: nothing to request
}

TEST_CASE("SPEC-069 a missing entry chosen with Enter shows MSG_ENTRY_MISSING, and the player stays Failed [parts: playlists]") {
    PlayerFixture f;
    f.play({kGoodA, kNowhere, kGoodG}, 1);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_ENTRY_MISSING");
    CHECK(f.current() == kNowhere);
    f.clock.advance(60000);
    f.settle();
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.fetcher.requests.empty());
    CHECK(f.backend.count("load") + f.backend.count("start") == 0);   // nothing was given to the backend
}
#endif

// ---- SPEC-068 ----------------------------------------------------------------------------------------------------

namespace {

// A file of `size` bytes that is no ROM of anybody's: a counting pattern.
spz::Bytes pretend_rom(std::size_t size) {
    spz::Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(i * 7 + 1);
    return bytes;
}

}  // namespace

TEST_CASE("SPEC-068 the fixture tune marked as needing BASIC fails with MSG_NEEDS_BASIC when no ROM is set, and never reaches the backend") {
    PlayerFixture f;
    REQUIRE(f.settings.rom_basic.empty());                  // the default
    f.play({kNeedsBasic}, 0);
    CHECK(f.state() == PlayerState::Failed);
    CHECK(f.status().message == "MSG_NEEDS_BASIC");
    CHECK(f.backend.count("load") + f.backend.count("start") == 0);   // nothing was given to the backend
    f.play({kOne}, 0);                                      // a tune that does not need it plays as ever
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.backend.loaded_settings.basic.empty());
}

#if SPZ_PART_ROMS
TEST_CASE("SPEC-068 with no ROM named in the settings nothing is read and nothing is ignored [parts: roms]") {
    spz::fake::Files files;
    spz::Settings settings;
    const spz::RomsRead read = spz::read_roms(&files, settings);
    CHECK_FALSE(read.ignored);
    CHECK(read.roms.kernal.empty());
    CHECK(read.roms.basic.empty());
    CHECK(read.roms.chargen.empty());
}

TEST_CASE("SPEC-068 a tune that needs BASIC is handed to the backend when a BASIC ROM file of the right size is set [parts: roms]") {
    PlayerFixture f;
    const spz::Bytes basic = pretend_rom(8192);
    f.settings.rom_basic = f.home.write("roms/basic.bin", basic);
    const spz::RomsRead read = spz::read_roms(&f.files, f.settings);
    CHECK_FALSE(read.ignored);
    CHECK(read.roms.basic == basic);
    CHECK(f.files.count("read") >= 1);                      // through the Files seam
    f.player->set_roms(read.roms);
    f.play({kNeedsBasic}, 0);
    CHECK(f.state() == PlayerState::Playing);
    CHECK(f.backend.count("load") == 1);
    CHECK(f.backend.loaded_settings.basic == basic);
    CHECK(f.backend.loaded_settings.kernal.empty());
}

TEST_CASE("SPEC-068 each ROM is used if its file has the right size: 8,192 bytes for the KERNAL and BASIC, 4,096 for the characters, and all three reach the backend [parts: roms]") {
    PlayerFixture f;
    const spz::Bytes kernal = pretend_rom(8192), basic = pretend_rom(8192), chargen = pretend_rom(4096);
    f.settings.rom_kernal = f.home.write("roms/kernal.bin", kernal);
    f.settings.rom_basic = f.home.write("roms/basic.bin", basic);
    f.settings.rom_chargen = f.home.write("roms/chargen.bin", chargen);
    const spz::RomsRead read = spz::read_roms(&f.files, f.settings);
    CHECK_FALSE(read.ignored);
    CHECK(read.roms.kernal == kernal);
    CHECK(read.roms.basic == basic);
    CHECK(read.roms.chargen == chargen);
    f.player->set_roms(read.roms);
    f.play({kOne}, 0);
    CHECK(f.backend.loaded_settings.kernal == kernal);
    CHECK(f.backend.loaded_settings.basic == basic);
    CHECK(f.backend.loaded_settings.chargen == chargen);
}

TEST_CASE("SPEC-068 a ROM file of the wrong size, or one that is not there, is ignored, which is what the notice MSG_ROM_IGNORED tells at start [parts: roms]") {
    struct Case {
        const char* what;
        std::size_t kernal, basic, chargen;     // the sizes of the files; 0 for a file that is not there
    };
    const Case cases[] = {
        {"a BASIC ROM one byte short", 8192, 8191, 4096},
        {"a BASIC ROM one byte long", 8192, 8193, 4096},
        {"a KERNAL of the character ROM's size", 4096, 8192, 4096},
        {"a character ROM of the KERNAL's size", 8192, 8192, 8192},
        {"a BASIC ROM that is not there", 8192, 0, 4096},
    };
    for (const Case& c : cases) {
        CAPTURE(std::string(c.what));
        PlayerFixture f;
        const std::size_t sizes[3] = {c.kernal, c.basic, c.chargen};
        std::string* names[3] = {&f.settings.rom_kernal, &f.settings.rom_basic, &f.settings.rom_chargen};
        const char* files[3] = {"roms/kernal.bin", "roms/basic.bin", "roms/chargen.bin"};
        for (int i = 0; i < 3; ++i) {
            *names[i] = sizes[i] ? f.home.write(files[i], pretend_rom(sizes[i])) : f.home.file(files[i]);
        }
        const spz::RomsRead read = spz::read_roms(&f.files, f.settings);
        CHECK(read.ignored);
        CHECK(read.roms.kernal.size() == (c.kernal == 8192 ? 8192u : 0u));
        CHECK(read.roms.basic.size() == (c.basic == 8192 ? 8192u : 0u));
        CHECK(read.roms.chargen.size() == (c.chargen == 4096 ? 4096u : 0u));
        if (c.basic != 8192) {                              // and a tune that needs BASIC still says so
            f.player->set_roms(read.roms);
            f.play({kNeedsBasic}, 0);
            CHECK(f.state() == PlayerState::Failed);
            CHECK(f.status().message == "MSG_NEEDS_BASIC");
        }
    }
}
#endif

// ---- A tune outside the collection (SPEC-170): a file:// entry is read only at a full path ------------------------

TEST_CASE("SPEC-069 SPEC-170 a file:// entry is read only when it names a full path: a relative one, or one with a control character, fails with MSG_INVALID_SID and is not read") {
    {
        PlayerFixture f;   // the full path plays, as any tune does
        f.play({"file://" + f.root + kGoodA}, 0);
        CHECK(f.state() == PlayerState::Playing);
        CHECK(f.backend.count("load") == 1);
    }
    // The same file named from the folder the program happens to run in, or with a zero byte or a line feed
    // that the system would end its name at: none of these is a full path, and none is read.
    const std::vector<std::string> kinds = {"relative", "zero byte", "line feed", "empty"};
    for (const std::string& kind : kinds) {
        CAPTURE(kind);
        PlayerFixture f;
        const std::string good = f.root + kGoodA;
        std::string entry = "file://";
        if (kind == "relative") {
            const std::string relative = std::filesystem::relative(good, std::filesystem::current_path()).generic_string();
            REQUIRE_FALSE(relative.empty());
            REQUIRE(relative[0] != '/');
            entry += relative;
        } else if (kind == "zero byte") {
            entry += good + std::string(1, '\0') + "/../B_Bad_1.sid";
        } else if (kind == "line feed") {
            entry += good + "\n";
        }
        f.play({entry}, 0);
        CHECK(f.state() == PlayerState::Failed);
        CHECK(f.status().message == "MSG_INVALID_SID");
        CHECK(f.backend.count("load") == 0);
    }
}
