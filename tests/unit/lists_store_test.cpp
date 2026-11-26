// SPDX-License-Identifier: GPL-3.0-or-later
// Making and changing playlists (SPEC-172) and Favourites (SPEC-171): what each action leaves in the
// list and, byte for byte, in the file (SPEC-170), and that every change is saved at once and safely
// (SPEC-132).
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/favourites.h"
#include "spz/playlist.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

std::string expected(const std::string& name) { return text(fixture("lists/expected/" + name)); }

// A world with the playlist "Loading" holding the three tunes of SPEC-172's oracle, in the order added.
void add_three(ListsWorld* world) {
    world->playlists->load();
    REQUIRE(world->playlists->create("Loading").ok);
    for (const char* path : {kClockDemo, kEitherChip, kHuelsbeck}) {
        REQUIRE(world->playlists->add("Loading", spz::Entry{path, 0, ""}).ok);
    }
}

std::vector<std::string> names(const spz::Playlists& playlists) {
    std::vector<std::string> out;
    for (const spz::PlaylistSummary& row : playlists.list()) out.push_back(row.name + " (" + std::to_string(row.entries) + ")");
    return out;
}

}  // namespace

TEST_CASE("SPEC-171 Favourites always exists, is listed first, and cannot be renamed or deleted [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();                                   // an empty folder: nothing was ever saved
    std::vector<spz::PlaylistSummary> list = world.playlists->list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "Favourites");
    CHECK(list[0].favourites);
    CHECK(list[0].entries == 0);

    REQUIRE(world.playlists->create("Aardvark").ok);           // before it by the alphabet, after it in the list
    list = world.playlists->list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Favourites");
    CHECK(list[1].name == "Aardvark");
    CHECK_FALSE(list[1].favourites);

    CHECK_FALSE(world.playlists->rename("Favourites", "Best").ok);
    CHECK_FALSE(world.playlists->remove("Favourites").ok);
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nAardvark (0)\n");
}

TEST_CASE("SPEC-172 SPEC-170 a new playlist is listed with no entries, and its file is there at once [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    const spz::ListsResult made = world.playlists->create("Loading");
    CHECK(made.ok);
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nLoading (0)\n");
    CHECK(world.playlists->file_of("Loading") == world.playlists_folder + "/Loading.m3u8");
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == expected("empty.m3u8"));
    REQUIRE(world.playlists->find("Loading") != nullptr);
    CHECK(world.playlists->find("Loading")->entries.empty());
    CHECK(world.playlists->find("Nobody's") == nullptr);
    CHECK(world.files.count("write") >= 1);                    // through the seam (SPEC-021)
}

TEST_CASE("SPEC-172 a name is 1 to 40 characters with no control characters [parts: playlists]") {
    CHECK(spz::is_playlist_name("A"));
    CHECK(spz::is_playlist_name(std::string(40, 'a')));
    CHECK_FALSE(spz::is_playlist_name(std::string(41, 'a')));
    CHECK_FALSE(spz::is_playlist_name(""));
    std::string accents;                                       // 40 characters in 80 bytes
    for (int i = 0; i < 40; ++i) accents += "\xC3\xA9";
    CHECK(spz::is_playlist_name(accents));
    CHECK_FALSE(spz::is_playlist_name(accents + "\xC3\xA9"));
    CHECK_FALSE(spz::is_playlist_name("tab\there"));
    CHECK_FALSE(spz::is_playlist_name("a bell\x07"));
    CHECK_FALSE(spz::is_playlist_name("two\nlines"));
    CHECK(spz::is_playlist_name("Rock/Pop: 80's!"));           // anything else may be typed

    ListsWorld world;
    world.playlists->load();
    CHECK_FALSE(world.playlists->create("").ok);
    CHECK_FALSE(world.playlists->create(std::string(41, 'a')).ok);
    CHECK_FALSE(world.playlists->create("tab\there").ok);
    CHECK(world.playlists->create(std::string(40, 'a')).ok);
    CHECK(world.playlists->list().size() == 2);                // Favourites and the one that was allowed
}

TEST_CASE("SPEC-172 a second playlist with the same name in other case is refused with MSG_NAME_TAKEN, and so is Favourites [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    REQUIRE(world.playlists->create("Loading").ok);
    for (const char* name : {"Loading", "LOADING", "loading", "Favourites", "FAVOURITES", "favourites"}) {
        CAPTURE(name);
        const spz::ListsResult result = world.playlists->create(name);
        CHECK_FALSE(result.ok);
        CHECK(result.message == "MSG_NAME_TAKEN");
    }
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nLoading (0)\n");
    CHECK_FALSE(exists(world.playlists_folder + "/LOADING.m3u8"));

    // Renaming obeys the same rule, and a playlist may take its own name in other case.
    REQUIRE(world.playlists->create("Other").ok);
    const spz::ListsResult taken = world.playlists->rename("Other", "LOADING");
    CHECK_FALSE(taken.ok);
    CHECK(taken.message == "MSG_NAME_TAKEN");
    CHECK(world.playlists->rename("Other", "favourites").message == "MSG_NAME_TAKEN");
    CHECK(world.playlists->rename("Other", "OTHER").ok);
    CHECK(world.playlists->find("OTHER") != nullptr);
}

TEST_CASE("SPEC-170 two playlists whose names give the same file name get two files, the second with a number added [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    REQUIRE(world.playlists->create("A/B").ok);
    REQUIRE(world.playlists->create("A:B").ok);
    const std::string first = std::filesystem::path(world.playlists->file_of("A/B")).filename().string();
    const std::string second = std::filesystem::path(world.playlists->file_of("A:B")).filename().string();
    CHECK(first == "A_B.m3u8");
    CHECK(second != first);
    REQUIRE(second.size() > 9);
    CHECK(second.compare(0, 3, "A_B") == 0);
    CHECK(second.compare(second.size() - 5, 5, ".m3u8") == 0);
    bool has_number = false;
    for (std::size_t i = 3; i + 5 < second.size(); ++i) has_number = has_number || std::isdigit(static_cast<unsigned char>(second[i]));
    CHECK(has_number);
    CHECK(exists(world.playlists_folder + "/" + first));
    CHECK(exists(world.playlists_folder + "/" + second));

    world.restart();                                           // each is shown by the name the user typed
    CHECK(world.playlists->find("A/B") != nullptr);
    CHECK(world.playlists->find("A:B") != nullptr);
    CHECK(world.playlists->list().size() == 3);
}

TEST_CASE("SPEC-172 SPEC-170 Add puts a tune at the end with MSG_ADDED, and the file holds what is known of each tune [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    REQUIRE(world.playlists->create("Loading").ok);

    const spz::ListsResult first = world.playlists->add("Loading", spz::Entry{kClockDemo, 0, ""});
    CHECK(first.ok);
    CHECK(first.message == "MSG_ADDED");
    REQUIRE(first.about.count("name") == 1);
    CHECK(first.about.at("name") == "Loading");
    // Saved at once: the file has the entry before anything else happens.
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8").find(std::string(kClockDemo) + "\n") != std::string::npos);

    CHECK(world.playlists->add("Loading", spz::Entry{kEitherChip, 0, ""}).message == "MSG_ADDED");
    CHECK(world.playlists->add("Loading", spz::Entry{kHuelsbeck, 0, ""}).message == "MSG_ADDED");
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nLoading (3)\n");
    CHECK(join(world.entries_of("Loading")) == std::string(kClockDemo) + "\n" + kEitherChip + "\n" + kHuelsbeck + "\n");
    // The length in whole seconds, "author - title", and the file's MD5, for each: SPEC-170's lines.
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == expected("three.m3u8"));
}

TEST_CASE("SPEC-172 adding an entry that is already there adds nothing and gives MSG_ALREADY_THERE [parts: playlists]") {
    ListsWorld world;
    add_three(&world);
    const std::string before = file_text(world.playlists_folder + "/Loading.m3u8");

    const spz::ListsResult again = world.playlists->add("Loading", spz::Entry{kEitherChip, 0, ""});
    CHECK_FALSE(again.ok);
    CHECK(again.message == "MSG_ALREADY_THERE");
    CHECK(world.playlists->find("Loading")->entries.size() == 3);
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == before);

    // The same tune with a subtune number is another entry (SPEC-171 has both in Favourites).
    CHECK(world.playlists->add("Loading", spz::Entry{kThreeSongs, 0, ""}).message == "MSG_ADDED");
    CHECK(world.playlists->add("Loading", spz::Entry{kThreeSongs, 3, ""}).message == "MSG_ADDED");
    CHECK(world.playlists->add("Loading", spz::Entry{kThreeSongs, 3, ""}).message == "MSG_ALREADY_THERE");
    CHECK(world.playlists->find("Loading")->entries.size() == 5);
}

TEST_CASE("SPEC-172 SPEC-062 Up moves the third entry to the top in two steps, the file follows, and the queue is in the new order [parts: playlists]") {
    ListsWorld world;
    add_three(&world);
    CHECK(world.playlists->move_entry("Loading", 2, -1).ok);
    CHECK(join(world.entries_of("Loading")) == std::string(kClockDemo) + "\n" + kHuelsbeck + "\n" + kEitherChip + "\n");
    CHECK(world.playlists->move_entry("Loading", 1, -1).ok);
    CHECK(join(world.entries_of("Loading")) == std::string(kHuelsbeck) + "\n" + kClockDemo + "\n" + kEitherChip + "\n");
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == expected("three_moved.m3u8"));

    const std::vector<spz::Entry> queue = world.playlists->queue("Loading");
    REQUIRE(queue.size() == 3);
    CHECK(queue[0] == spz::Entry{kHuelsbeck, 0, ""});
    CHECK(queue[1] == spz::Entry{kClockDemo, 0, ""});
    CHECK(queue[2] == spz::Entry{kEitherChip, 0, ""});

    // At either end an entry stays where it is; Down undoes Up.
    world.playlists->move_entry("Loading", 0, -1);
    world.playlists->move_entry("Loading", 2, +1);
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == expected("three_moved.m3u8"));
    CHECK(world.playlists->move_entry("Loading", 0, +1).ok);
    CHECK(world.playlists->move_entry("Loading", 1, +1).ok);
    CHECK(file_text(world.playlists_folder + "/Loading.m3u8") == expected("three.m3u8"));
}

TEST_CASE("SPEC-172 Remove takes one entry out, and the file follows [parts: playlists]") {
    ListsWorld world;
    add_three(&world);
    CHECK(world.playlists->remove_entry("Loading", 1).ok);
    CHECK(join(world.entries_of("Loading")) == std::string(kClockDemo) + "\n" + kHuelsbeck + "\n");
    const std::string saved = file_text(world.playlists_folder + "/Loading.m3u8");
    CHECK(saved.find(kEitherChip) == std::string::npos);
    CHECK(saved.find(kClockDemo) != std::string::npos);
    CHECK(saved.find(kHuelsbeck) != std::string::npos);
    CHECK_FALSE(world.playlists->remove_entry("Loading", 2).ok);       // there is no third entry now
    CHECK(world.playlists->find("Loading")->entries.size() == 2);
}

TEST_CASE("SPEC-172 SPEC-170 Rename gives the playlist and its file the new name, and Delete removes both [parts: playlists]") {
    ListsWorld world;
    add_three(&world);
    REQUIRE(world.playlists->move_entry("Loading", 2, -1).ok);
    REQUIRE(world.playlists->move_entry("Loading", 1, -1).ok);

    CHECK(world.playlists->rename("Loading", "Turbo").ok);
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nTurbo (3)\n");
    CHECK(world.playlists->find("Loading") == nullptr);
    CHECK_FALSE(exists(world.playlists_folder + "/Loading.m3u8"));
    CHECK(file_text(world.playlists_folder + "/Turbo.m3u8") == expected("three_renamed.m3u8"));

    world.restart();                                           // what is on the card is what was shown
    CHECK(join(names(*world.playlists)) == "Favourites (0)\nTurbo (3)\n");

    CHECK(world.playlists->remove("Turbo").ok);
    CHECK(join(names(*world.playlists)) == "Favourites (0)\n");
    CHECK_FALSE(exists(world.playlists_folder + "/Turbo.m3u8"));
    CHECK_FALSE(world.playlists->remove("Turbo").ok);          // it is gone
    world.restart();
    CHECK(join(names(*world.playlists)) == "Favourites (0)\n");
}

TEST_CASE("SPEC-172 SPEC-173 the Playlist screen's rows: the title or else the file's name, the subtune number, and the mark for absent and missing [parts: playlists]") {
    ListsWorld world;
    world.take_away(kClockDemo);                               // HVSC lists it, and it is not on the device
    world.put_playlist("Rows.m3u8", std::string("#EXTM3U\n#PLAYLIST:Rows\n") +
                                        "#EXTINF:3,Test Author - Commando Test\n" + kCommando + "\n" +
                                        "#SIDTUNE:subtune=3\n" + kClockDemo + "\n" +
                                        kNothing + "\n");
    world.playlists->load();
    const std::vector<spz::PlaylistRow> rows = world.playlists->rows("Rows");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].text == "Commando Test");
    CHECK(rows[0].subtune == 0);
    CHECK(rows[0].found == spz::Found::Present);
    CHECK(rows[1].text == "Clock_Demo.sid");                   // no title is known: the file's name
    CHECK(rows[1].subtune == 3);
    CHECK(rows[1].found == spz::Found::Absent);
    CHECK(rows[2].text == "Nothing.sid");
    CHECK(rows[2].found == spz::Found::Missing);
}

TEST_CASE("SPEC-172 SPEC-132 a change is written to a new file beside the playlist, flushed, renamed over it, and the folder flushed [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    REQUIRE(world.playlists->create("Loading").ok);
    world.files.log.clear();
    REQUIRE(world.playlists->add("Loading", spz::Entry{kClockDemo, 0, ""}).ok);

    const std::string target = world.playlists_folder + "/Loading.m3u8";
    const std::string temporary = world.playlists_folder + "/.Loading.m3u8.tmp-sidplayzero";
    int wrote = -1, flushed = -1, renamed = -1, folder_flushed = -1;
    for (int i = 0; i < static_cast<int>(world.files.log.size()); ++i) {
        const spz::fake::Files::Operation& op = world.files.log[static_cast<std::size_t>(i)];
        if (op.what == "write" && op.path == temporary) wrote = i;
        if (op.what == "sync" && op.path == temporary && renamed < 0) flushed = i;
        if (op.what == "rename" && op.path == temporary && op.to == target) renamed = i;
        if (op.what == "sync" && op.path == world.playlists_folder && renamed >= 0) folder_flushed = i;
        if (op.what == "write") CHECK(op.path == temporary);    // the playlist's own file is never written in place
    }
    CHECK(wrote >= 0);
    CHECK(flushed > wrote);
    CHECK(renamed > flushed);
    CHECK(folder_flushed > renamed);
    CHECK_FALSE(exists(temporary));
}

TEST_CASE("SPEC-172 SPEC-132 a save that fails at any of its operations leaves the old playlist or the new one, whole [parts: playlists]") {
    // The change is "Remove" on the first of two entries: nothing of a tune has to be read for it.
    std::string old_text, new_text;
    {
        ListsWorld world;
        world.playlists->load();
        REQUIRE(world.playlists->create("Loading").ok);
        REQUIRE(world.playlists->add("Loading", spz::Entry{kClockDemo, 0, ""}).ok);
        REQUIRE(world.playlists->add("Loading", spz::Entry{kEitherChip, 0, ""}).ok);
        old_text = file_text(world.playlists_folder + "/Loading.m3u8");
        REQUIRE(world.playlists->remove_entry("Loading", 0).ok);
        new_text = file_text(world.playlists_folder + "/Loading.m3u8");
    }
    REQUIRE(old_text != new_text);
    REQUIRE(old_text.find(kClockDemo) != std::string::npos);
    REQUIRE(new_text.find(kClockDemo) == std::string::npos);

    int failures_tried = 0;
    for (int n = 1; n <= 60; ++n) {
        CAPTURE(n);
        ListsWorld world;
        world.playlists->load();
        REQUIRE(world.playlists->create("Loading").ok);
        REQUIRE(world.playlists->add("Loading", spz::Entry{kClockDemo, 0, ""}).ok);
        REQUIRE(world.playlists->add("Loading", spz::Entry{kEitherChip, 0, ""}).ok);
        const std::size_t before = world.files.log.size();
        world.files.fail_at(n);                                // the n-th operation from here fails
        world.playlists->remove_entry("Loading", 0);
        const bool failure_fell_in_the_save = world.files.log.size() - before >= static_cast<std::size_t>(n);
        world.files.fail_at(0);                                // no failure is left waiting

        const std::string on_card = file_text(world.playlists_folder + "/Loading.m3u8");
        CHECK((on_card == old_text || on_card == new_text));
        world.restart();                                       // and the next start reads a whole playlist
        const spz::Playlist* list = world.playlists->find("Loading");
        REQUIRE(list != nullptr);
        CHECK((list->entries.size() == 1 || list->entries.size() == 2));
        if (!failure_fell_in_the_save) break;                  // the save has fewer operations than n: all were tried
        ++failures_tried;
    }
    CHECK(failures_tried >= 4);                                // at least: write, flush, rename, flush
}

// --- Favourites (SPEC-171) ---------------------------------------------------------------------------

TEST_CASE("SPEC-171 marking a tune twice adds then removes one entry, with MSG_FAV_ADDED and MSG_FAV_REMOVED [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    spz::Favourites favourites(world.playlists.get());

    const spz::ListsResult added = favourites.toggle(spz::Entry{kCommando, 0, ""});     // "Fav" in the browser
    CHECK(added.ok);
    CHECK(added.message == "MSG_FAV_ADDED");
    CHECK(world.playlists->list()[0].entries == 1);
    CHECK(favourites.has_tune(kCommando));
    CHECK(file_text(world.playlists_folder + "/Favourites.m3u8") == expected("favourites_one.m3u8"));

    const spz::ListsResult removed = favourites.toggle(spz::Entry{kCommando, 0, ""});
    CHECK(removed.ok);
    CHECK(removed.message == "MSG_FAV_REMOVED");
    CHECK(world.playlists->list()[0].entries == 0);
    CHECK_FALSE(favourites.has_tune(kCommando));
    CHECK(file_text(world.playlists_folder + "/Favourites.m3u8").find(kCommando) == std::string::npos);
}

TEST_CASE("SPEC-171 marking subtune 3 while it plays adds an entry with subtune=3, the browser's star shows, Fav in the browser adds a second entry without a number, and a restart keeps both [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    spz::Favourites favourites(world.playlists.get());

    CHECK(favourites.toggle(spz::Entry{kThreeSongs, 3, ""}).message == "MSG_FAV_ADDED");    // on the now-playing screen
    const std::string one = file_text(world.playlists_folder + "/Favourites.m3u8");
    CHECK(one.find(std::string("#SIDTUNE:subtune=3 md5=") + kMd5ThreeSongs + "\n" + kThreeSongs + "\n") != std::string::npos);
    CHECK(favourites.has_tune(kThreeSongs));                   // the star in the browser: any entry for the tune
    CHECK_FALSE(favourites.has_tune(kCommando));

    CHECK(favourites.toggle(spz::Entry{kThreeSongs, 0, ""}).message == "MSG_FAV_ADDED");    // in the browser
    CHECK(world.playlists->list()[0].entries == 2);
    CHECK(join(world.entries_of("Favourites")) == std::string(kThreeSongs) + "#3\n" + kThreeSongs + "\n");
    const std::string two = file_text(world.playlists_folder + "/Favourites.m3u8");
    CHECK(two == expected("favourites_two.m3u8"));

    world.restart();
    spz::Favourites after(world.playlists.get());
    CHECK(after.has_tune(kThreeSongs));
    CHECK_FALSE(after.has_tune(kCommando));
    CHECK(world.playlists->list()[0].name == "Favourites");
    CHECK(world.playlists->list()[0].entries == 2);
    CHECK(file_text(world.playlists_folder + "/Favourites.m3u8") == two);
}

TEST_CASE("SPEC-171 the star on the now-playing screen: an entry for the playing subtune, or one without a number while the default subtune plays [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    spz::Favourites favourites(world.playlists.get());
    const int default_subtune = 2;                             // of the three-subtune fixture

    CHECK_FALSE(favourites.has_playing(kThreeSongs, 2, default_subtune));
    REQUIRE(favourites.toggle(spz::Entry{kThreeSongs, 3, ""}).ok);
    CHECK(favourites.has_playing(kThreeSongs, 3, default_subtune));
    CHECK_FALSE(favourites.has_playing(kThreeSongs, 2, default_subtune));
    CHECK_FALSE(favourites.has_playing(kThreeSongs, 1, default_subtune));
    CHECK_FALSE(favourites.has_playing(kCommando, 3, 1));

    REQUIRE(favourites.toggle(spz::Entry{kThreeSongs, 3, ""}).message == "MSG_FAV_REMOVED");
    REQUIRE(favourites.toggle(spz::Entry{kThreeSongs, 0, ""}).ok);       // "this tune, no subtune number"
    CHECK(favourites.has_playing(kThreeSongs, 2, default_subtune));      // the default subtune plays
    CHECK_FALSE(favourites.has_playing(kThreeSongs, 3, default_subtune));
    CHECK_FALSE(favourites.has_playing(kThreeSongs, 1, default_subtune));
    CHECK(favourites.has_tune(kThreeSongs));
}

TEST_CASE("SPEC-171 Fav on the now-playing screen acts on the playing subtune's entry and leaves the tune's other entry alone [parts: playlists]") {
    ListsWorld world;
    world.playlists->load();
    spz::Favourites favourites(world.playlists.get());
    REQUIRE(favourites.toggle(spz::Entry{kThreeSongs, 3, ""}).ok);
    REQUIRE(favourites.toggle(spz::Entry{kThreeSongs, 0, ""}).ok);

    CHECK(favourites.toggle(spz::Entry{kThreeSongs, 3, ""}).message == "MSG_FAV_REMOVED");
    CHECK(join(world.entries_of("Favourites")) == std::string(kThreeSongs) + "\n");
    CHECK(favourites.has_tune(kThreeSongs));
    CHECK(favourites.toggle(spz::Entry{kThreeSongs, 1, ""}).message == "MSG_FAV_ADDED");
    CHECK(join(world.entries_of("Favourites")) == std::string(kThreeSongs) + "\n" + kThreeSongs + "#1\n");
}

TEST_CASE("SPEC-154 SPEC-171 a tune of the user's own, with a space and an accent in its path, becomes a favourite and is still one after a restart [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    world.playlists->load();
    spz::Favourites favourites(world.playlists.get());

    CHECK(favourites.toggle(spz::Entry{kOwnTune, 0, ""}).message == "MSG_FAV_ADDED");
    CHECK(favourites.has_tune(kOwnTune));
    const std::string saved = file_text(world.playlists_folder + "/Favourites.m3u8");
    CHECK(saved.find(std::string("#SIDTUNE:md5=") + kMd5OwnTune + "\n" + kOwnTune + "\n") != std::string::npos);

    world.restart();
    spz::Favourites after(world.playlists.get());
    CHECK(after.has_tune(kOwnTune));
    CHECK(world.resolver->resolve(world.playlists->queue("Favourites").at(0)).found == spz::Found::Present);
    CHECK(join(world.changes_outside(world.playlists_folder)) == "");    // the tune itself is only ever read
}

#endif  // SPZ_PART_PLAYLISTS
