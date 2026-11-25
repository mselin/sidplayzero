// SPDX-License-Identifier: GPL-3.0-or-later
// Finding an entry's tune (SPEC-173): present, absent or missing, and where a tune that has moved is now.
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/playlist_resolve.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

const char* const kOldName = "/MUSICIANS/T/Tester_Tom/Old_Name.sid";   // where release 84 had the tune that 85 calls Renamed.sid

void replace_all(std::string* text, const std::string& from, const std::string& to) {
    for (std::size_t at = text->find(from); at != std::string::npos; at = text->find(from, at + to.size())) {
        text->replace(at, from.size(), to);
    }
}

}  // namespace

TEST_CASE("SPEC-173 an entry is found in this order: a file at its path; a tune with its MD5 at another path; absent if HVSC lists the path; else missing [parts: playlists]") {
    ListsWorld world;
    world.take_away(kClockDemo);

    spz::EntryFacts facts = world.resolver->resolve(spz::Entry{kCommando, 0, ""});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.path == kCommando);
    CHECK(world.files.calls > 0);                              // it looked through the seam (SPEC-021)

    // A file at the entry's path is taken whatever MD5 the entry holds.
    facts = world.resolver->resolve(spz::Entry{kCommando, 0, kMd5ThreeSongs});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.path == kCommando);

    // No file there, and the entry's MD5 is a tune's at another path in the listing: that tune.
    facts = world.resolver->resolve(spz::Entry{kOldName, 2, kMd5Renamed});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.path == kRenamed);

    // The listing has the path, and the file is not on the device: absent.
    facts = world.resolver->resolve(spz::Entry{kClockDemo, 0, ""});
    CHECK(facts.found == spz::Found::Absent);
    CHECK(facts.path == kClockDemo);
    facts = world.resolver->resolve(spz::Entry{kClockDemo, 0, kMd5ClockDemo});      // the usual absent entry, with its MD5
    CHECK(facts.found == spz::Found::Absent);
    CHECK(facts.path == kClockDemo);

    // Neither on the device nor listed: missing, with and without an MD5 that nothing has.
    CHECK(world.resolver->resolve(spz::Entry{kNothing, 0, ""}).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(spz::Entry{kOldName, 0, ""}).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(spz::Entry{kNothing, 0, "00000000000000000000000000000000"}).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(spz::Entry{kNothing, 0, ""}).path == kNothing);
}

TEST_CASE("SPEC-173 an entry whose MD5 is a listed tune's at another path, not on the device, is absent at that path and so is fetched from there [parts: playlists]") {
    ListsWorld world;
    world.take_away(kRenamed);
    const spz::EntryFacts facts = world.resolver->resolve(spz::Entry{kOldName, 0, kMd5Renamed});
    CHECK(facts.found == spz::Found::Absent);
    CHECK(facts.path == kRenamed);
}

TEST_CASE("SPEC-173 SPEC-154 a tune outside the collection is present while its file is there, and a tune of the user's own is found at its path [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    const std::string outside = world.temp.write("elsewhere/a tune.sid", fixture("tunes/own_tune.sid"));

    CHECK(world.resolver->resolve(spz::Entry{"file://" + outside, 0, ""}).found == spz::Found::Present);
    CHECK(world.resolver->resolve(spz::Entry{"file://" + outside, 0, ""}).path == "file://" + outside);
    CHECK(world.resolver->resolve(spz::Entry{"file://" + world.temp.file("elsewhere/gone.sid"), 0, ""}).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(spz::Entry{kOwnTune, 0, ""}).found == spz::Found::Present);
    CHECK(join(world.changes_outside(world.playlists_folder)) == "");    // tunes are only read
}

TEST_CASE("SPEC-173 in a collection that is not HVSC's, an entry is present or missing: nothing is absent [parts: playlists]") {
    ListsWorld world("C64Music", false);
    CHECK(world.resolver->resolve(spz::Entry{kCommando, 0, kMd5Commando}).found == spz::Found::Present);
    world.take_away(kClockDemo);
    CHECK(world.resolver->resolve(spz::Entry{kClockDemo, 0, kMd5ClockDemo}).found == spz::Found::Missing);
}

TEST_CASE("SPEC-173 after a folder is renamed in the collection and in the listing, every entry that pointed into it resolves and is queued at its new path, and the next save corrects the file [parts: playlists]") {
    ListsWorld world;
    world.put_playlist("Tom.m3u8", std::string("#EXTM3U\n#PLAYLIST:Tom\n") +
                                       "#SIDTUNE:subtune=3 md5=" + kMd5ThreeSongs + "\n" + kThreeSongs + "\n" +
                                       "#SIDTUNE:md5=" + kMd5NewChip + "\n" + kNewChip + "\n" +
                                       "#SIDTUNE:md5=" + kMd5TwoChips + "\n" + kTwoChips + "\n" +
                                       kCommando + "\n");
    world.playlists->load();

    // The folder gets a new name on the device, and HVSC's database names it so too.
    std::filesystem::rename(world.root + "/MUSICIANS/T/Tester_Tom", world.root + "/MUSICIANS/T/Tester_Thomas");
    std::string database = file_text(world.root + "/DOCUMENTS/Songlengths.md5");
    replace_all(&database, "/Tester_Tom/", "/Tester_Thomas/");
    world.temp.write(std::filesystem::path(world.root).filename().string() + "/DOCUMENTS/Songlengths.md5", database);
    world.build_listing();

    const std::string moved = "/MUSICIANS/T/Tester_Thomas/";
    const std::vector<spz::Entry> queue = world.playlists->queue("Tom");
    REQUIRE(queue.size() == 4);
    CHECK(queue[0] == spz::Entry{moved + "Three_Songs.sid", 3, ""});       // it plays from where it is now,
    CHECK(queue[1] == spz::Entry{moved + "New_Chip.sid", 0, ""});          // with the subtune the playlist gives
    CHECK(queue[2] == spz::Entry{moved + "Two_Chips_2SID.sid", 0, ""});
    CHECK(queue[3] == spz::Entry{kCommando, 0, ""});
    for (const spz::Entry& entry : queue) {
        CAPTURE(entry.path);
        CHECK(world.resolver->resolve(entry).found == spz::Found::Present);
    }
    const std::vector<spz::PlaylistRow> rows = world.playlists->rows("Tom");
    REQUIRE(rows.size() == 4);
    for (const spz::PlaylistRow& row : rows) CHECK(row.found == spz::Found::Present);

    // The entry's path is corrected the next time the playlist is saved.
    REQUIRE(world.playlists->move_entry("Tom", 3, -1).ok);
    const std::string saved = file_text(world.playlists->file_of("Tom"));
    CHECK(saved.find("/Tester_Tom/") == std::string::npos);
    CHECK(saved.find("#SIDTUNE:subtune=3 md5=" + std::string(kMd5ThreeSongs) + "\n" + moved + "Three_Songs.sid\n") != std::string::npos);
    CHECK(saved.find(moved + "New_Chip.sid\n") != std::string::npos);
    CHECK(saved.find(moved + "Two_Chips_2SID.sid\n") != std::string::npos);
    CHECK(saved.find(std::string(kCommando) + "\n") != std::string::npos);
}

TEST_CASE("SPEC-173 one of the user's own tunes, moved, is found by its MD5 through the index [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    world.put_playlist("Mine.m3u8", std::string("#EXTM3U\n#PLAYLIST:Mine\n#SIDTUNE:md5=") + kMd5OwnTune + "\n" + kOwnTune + "\n");
    world.playlists->load();
    REQUIRE(world.resolver->resolve(spz::Entry{kOwnTune, 0, kMd5OwnTune}).found == spz::Found::Present);

    const std::string new_path = "/Rips 2/Cool S\xC3\xB6ng.sid";
    std::filesystem::create_directories(world.root + "/Rips 2");
    std::filesystem::rename(world.root + kOwnTune, world.root + new_path);
    // HVSC does not list it, so until the index has seen the move the entry is missing.
    CHECK(world.resolver->resolve(spz::Entry{kOwnTune, 0, kMd5OwnTune}).found == spz::Found::Missing);

    world.own.by_md5[kMd5OwnTune] = new_path;                  // the index has found the file where it is now
    const int asked_before = world.own.calls;
    const spz::EntryFacts facts = world.resolver->resolve(spz::Entry{kOwnTune, 0, kMd5OwnTune});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.path == new_path);
    CHECK(world.own.calls > asked_before);                     // the index was asked, through its interface

    const std::vector<spz::Entry> queue = world.playlists->queue("Mine");
    REQUIRE(queue.size() == 1);
    CHECK(queue[0].path == new_path);
    // An entry without an MD5 cannot be found this way.
    CHECK(world.resolver->resolve(spz::Entry{kOwnTune, 0, ""}).found == spz::Found::Missing);
}

TEST_CASE("SPEC-173 a playlist of present, missing, present queues the first and third as present, and still holds three entries afterwards [parts: playlists]") {
    ListsWorld world;
    world.put_playlist("Gap.m3u8", text(fixture("lists/playlists/gap.m3u8")));
    world.playlists->load();

    const std::vector<spz::Entry> queue = world.playlists->queue("Gap");
    REQUIRE(queue.size() == 3);                                // the missing entry is in the queue: playback passes over it
    CHECK(world.resolver->resolve(queue[0]).found == spz::Found::Present);
    CHECK(world.resolver->resolve(queue[1]).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(queue[2]).found == spz::Found::Present);
    CHECK(queue[0].path == kClockDemo);
    CHECK(queue[2].path == kEitherChip);

    const std::vector<spz::PlaylistRow> rows = world.playlists->rows("Gap");
    REQUIRE(rows.size() == 3);
    CHECK(rows[1].found == spz::Found::Missing);               // shown with its mark

    // Afterwards, and after a save, the playlist holds its three entries, the missing one as it was.
    REQUIRE(world.playlists->move_entry("Gap", 0, +1).ok);
    REQUIRE(world.playlists->move_entry("Gap", 1, -1).ok);
    CHECK(world.playlists->list()[1].entries == 3);
    std::string tunes_in_file;
    for (const std::string& line : lines_of(file_text(world.playlists->file_of("Gap")))) {
        if (!line.empty() && line[0] != '#') tunes_in_file += line + "\n";
    }
    CHECK(tunes_in_file == std::string(kClockDemo) + "\n" + kNothing + "\n" + kEitherChip + "\n");
    world.restart();
    CHECK(join(world.entries_of("Gap")) == std::string(kClockDemo) + "\n" + kNothing + "\n" + kEitherChip + "\n");
}

TEST_CASE("SPEC-170 SPEC-173 what is known of a tune for its lines in the file: the file's MD5, the header's author and title, the listing's length [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    world.take_away(kClockDemo);

    spz::EntryFacts facts = world.resolver->facts(spz::Entry{kThreeSongs, 0, ""});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.md5 == kMd5ThreeSongs);
    CHECK(facts.author == "Test Author");
    CHECK(facts.title == "Three Subtunes");
    CHECK(facts.length_ms == 2500);                            // no number: the default subtune, 2
    CHECK(world.resolver->facts(spz::Entry{kThreeSongs, 1, ""}).length_ms == 2000);
    CHECK(world.resolver->facts(spz::Entry{kThreeSongs, 3, ""}).length_ms == 3000);
    CHECK(world.resolver->facts(spz::Entry{kCommando, 0, ""}).length_ms == 3500);
    CHECK(world.files.count("read") >= 1);

    // An absent tune has no file to read: the listing's MD5 and length, and no title.
    facts = world.resolver->facts(spz::Entry{kClockDemo, 0, ""});
    CHECK(facts.found == spz::Found::Absent);
    CHECK(facts.md5 == kMd5ClockDemo);
    CHECK(facts.title == "");
    CHECK(facts.author == "");
    CHECK(facts.length_ms == 2000);

    // A tune HVSC does not list has its own MD5 and header, and no length that is known.
    facts = world.resolver->facts(spz::Entry{kOwnTune, 0, ""});
    CHECK(facts.found == spz::Found::Present);
    CHECK(facts.md5 == kMd5OwnTune);
    CHECK(facts.author == "Somebody");
    CHECK(facts.title == "The User's Own");
    CHECK(facts.length_ms == -1);

    // Nothing is known of a missing entry.
    facts = world.resolver->facts(spz::Entry{kNothing, 0, ""});
    CHECK(facts.found == spz::Found::Missing);
    CHECK(facts.md5 == "");
    CHECK(facts.length_ms == -1);
}

#endif  // SPZ_PART_PLAYLISTS
