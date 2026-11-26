// SPDX-License-Identifier: GPL-3.0-or-later
// What happens at start with what was kept: where the browser opens and what the player gets back
// (SPEC-131), carrying on with the tune that was playing (SPEC-135), and the clean-up after a kill
// (SPEC-202, SPEC-132).
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/keep_start.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::is_there;
using spz::test::names_in;

namespace {

// The fixture collection for release 85, in a folder of the test's own.
struct Collection {
    TempFolder folder;
    spz::fake::Files files;
    std::string root = folder.file("C64Music");
    Collection() { spz::test::copy_fixture_tree("collections/85/C64Music", folder.path / "C64Music"); }

    void remove(const std::string& path_under_root) const { std::filesystem::remove_all(root + path_under_root); }
};

// The player, as far as start-up knows it: it records what it is asked.
struct FakePlayer : spz::PlayerAtStart {
    std::vector<std::string> log;       // "restore", "play"
    spz::Entry current;
    std::vector<spz::Entry> queue;
    int place = 0;
    void restore(const spz::Entry& entry, const std::vector<spz::Entry>& entries, int at) override {
        log.push_back("restore");
        current = entry;
        queue = entries;
        place = at;
    }
    void play_current() override { log.push_back("play"); }
};

// The collection and the playlists, as far as start-up knows them.
struct FakeSources : spz::QueueSources {
    std::vector<std::string> asked;
    bool folder_tunes(const std::string& folder, std::vector<spz::Entry>* out) override {
        asked.push_back("folder " + folder);
        if (folder != "/MUSICIANS/T/Tester_Tom") return false;
        *out = {{"/MUSICIANS/T/Tester_Tom/Changed.sid", 0, ""},
                {"/MUSICIANS/T/Tester_Tom/New_Chip.sid", 0, ""},
                {"/MUSICIANS/T/Tester_Tom/Three_Songs.sid", 0, ""}};
        return true;
    }
    bool playlist_entries(const std::string& name, std::vector<spz::Entry>* out) override {
        asked.push_back("playlist " + name);
        if (name != "Loading tunes") return false;
        *out = {{"/MUSICIANS/T/Tester_Tom/Three_Songs.sid", 3, ""},
                {"/MUSICIANS/H/Hubbard_Rob/Commando.sid", 1, ""}};
        return true;
    }
};

spz::State playing_from_the_folder() {
    spz::State state;
    state.browser_folder = "/MUSICIANS/T/Tester_Tom";
    state.browser_entry = "New_Chip.sid";
    state.current_path = "/MUSICIANS/T/Tester_Tom/Three_Songs.sid";
    state.current_subtune = 3;
    state.queue_kind = "folder";
    state.queue_source = "/MUSICIANS/T/Tester_Tom";
    state.queue_place = 3;
    return state;
}

}  // namespace

// ---- SPEC-131: where the browser opens -------------------------------------------------------------------

TEST_CASE("SPEC-131 at start the browser opens the remembered folder with the remembered entry highlighted") {
    Collection collection;
    spz::State state;
    state.browser_folder = "/MUSICIANS/T/Tester_Tom";
    state.browser_entry = "New_Chip.sid";
    const spz::BrowserPlace place = spz::browser_place_at_start(state, &collection.files, collection.root, nullptr);
    CHECK(place.folder == "/MUSICIANS/T/Tester_Tom");
    CHECK(place.entry == "New_Chip.sid");
    CHECK(collection.files.calls > 0);          // it asked the card, through the seam

    const spz::BrowserPlace nothing_kept = spz::browser_place_at_start(spz::State(), &collection.files, collection.root, nullptr);
    CHECK(nothing_kept.folder == "/");
}

TEST_CASE("SPEC-131 with the remembered folder removed, the nearest parent that is there opens, and the root if none is") {
    Collection collection;
    spz::State state;
    state.browser_folder = "/MUSICIANS/T/Tester_Tom";
    state.browser_entry = "New_Chip.sid";

    collection.remove("/MUSICIANS/T/Tester_Tom");
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, nullptr).folder == "/MUSICIANS/T");
    collection.remove("/MUSICIANS/T");
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, nullptr).folder == "/MUSICIANS");
    collection.remove("/MUSICIANS");
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, nullptr).folder == "/");

    // A file of that name is not a folder to open.
    state.browser_folder = "/DEMOS/A-F/Clock_Demo.sid";
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, nullptr).folder == "/DEMOS/A-F");
}

TEST_CASE("SPEC-131 a remembered folder that is only in the listing still opens; one that is in neither gives its parent") {
    Collection collection;
    TempFolder cache;
    const std::string database = collection.root + "/DOCUMENTS/Songlengths.md5";
    const std::string listing_file = cache.file("listing.bin");
    REQUIRE(spz::Listing::build(&collection.files, database, listing_file).ok);
    spz::Listing listing;
    REQUIRE(listing.open(&collection.files, listing_file, database));

    spz::State state;
    state.browser_folder = "/MUSICIANS/H/Hubbard_Rob";
    state.browser_entry = "Commando.sid";
    collection.remove("/MUSICIANS/H/Hubbard_Rob");       // no longer on the device; HVSC still lists it
    const spz::BrowserPlace listed = spz::browser_place_at_start(state, &collection.files, collection.root, &listing);
    CHECK(listed.folder == "/MUSICIANS/H/Hubbard_Rob");
    CHECK(listed.entry == "Commando.sid");
    collection.remove("/MUSICIANS");                     // nothing of it on the device at all
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, &listing).folder == "/MUSICIANS/H/Hubbard_Rob");

    state.browser_folder = "/MUSICIANS/H/Nobody_Known/Old";      // on the device never, and not in the listing
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, &listing).folder == "/MUSICIANS/H");
    state.browser_folder = "/My Rips/1988";
    CHECK(spz::browser_place_at_start(state, &collection.files, collection.root, &listing).folder == "/");
}

// ---- SPEC-131: what the player gets back -------------------------------------------------------------------

TEST_CASE("SPEC-131 the current entry is restored into an idle player, with the queue made again from its folder") {
    FakePlayer player;
    FakeSources sources;
    CHECK(spz::restore_player(playing_from_the_folder(), &sources, &player));
    CHECK(player.log == std::vector<std::string>{"restore"});      // restored, and nothing asked to play
    CHECK(player.current.path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.current.subtune == 3);
    REQUIRE(player.queue.size() == 3);
    CHECK(player.queue[0].path == "/MUSICIANS/T/Tester_Tom/Changed.sid");
    CHECK(player.queue[1].path == "/MUSICIANS/T/Tester_Tom/New_Chip.sid");
    CHECK(player.queue[2].path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.place == 3);
    CHECK(sources.asked == std::vector<std::string>{"folder /MUSICIANS/T/Tester_Tom"});
}

TEST_CASE("SPEC-131 a queue that came from a search is restored as the one current entry alone") {
    FakePlayer player;
    FakeSources sources;
    spz::State state = playing_from_the_folder();
    state.queue_kind = "search";
    state.queue_source = "three songs";
    state.queue_place = 2;
    CHECK(spz::restore_player(state, &sources, &player));
    CHECK(player.log == std::vector<std::string>{"restore"});
    CHECK(player.current.path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.current.subtune == 3);
    REQUIRE(player.queue.size() == 1);
    CHECK(player.queue[0] == player.current);
    CHECK(player.place == 1);
    CHECK(sources.asked.empty());               // the search is not made again
}

TEST_CASE("SPEC-131 with no current entry remembered, the player is left as it is") {
    FakePlayer player;
    FakeSources sources;
    spz::State state;
    state.browser_folder = "/MUSICIANS";
    CHECK_FALSE(spz::restore_player(state, &sources, &player));
    CHECK(player.log.empty());
}

#if SPZ_PART_PLAYLISTS
TEST_CASE("SPEC-131 a queue that came from a playlist is made again from the playlist's entries [parts: playlists]") {
    FakePlayer player;
    FakeSources sources;
    spz::State state = playing_from_the_folder();
    state.queue_kind = "playlist";
    state.queue_source = "Loading tunes";
    state.queue_place = 1;
    CHECK(spz::restore_player(state, &sources, &player));
    CHECK(player.log == std::vector<std::string>{"restore"});
    CHECK(player.current.path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.current.subtune == 3);
    REQUIRE(player.queue.size() == 2);
    CHECK(player.queue[0].path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.queue[0].subtune == 3);
    CHECK(player.queue[1].path == "/MUSICIANS/H/Hubbard_Rob/Commando.sid");
    CHECK(player.queue[1].subtune == 1);
    CHECK(player.place == 1);
    CHECK(sources.asked == std::vector<std::string>{"playlist Loading tunes"});
}
#endif

// ---- SPEC-135: carrying on at start ----------------------------------------------------------------------

#if SPZ_PART_RESUME
TEST_CASE("SPEC-135 with resume_on_start on and the recorded tune present, the player is asked to play it after start [parts: resume]") {
    Collection collection;
    FakePlayer player;
    FakeSources sources;
    spz::Settings settings;
    settings.resume_on_start = true;
    const spz::State state = playing_from_the_folder();

    REQUIRE(spz::restore_player(state, &sources, &player));
    CHECK(spz::resume_at_start(settings, state, &collection.files, collection.root, &player));
    CHECK(player.log == std::vector<std::string>{"restore", "play"});   // from its beginning, with its queue
    CHECK(player.current.path == "/MUSICIANS/T/Tester_Tom/Three_Songs.sid");
    CHECK(player.current.subtune == 3);                                 // that subtune
    CHECK(player.queue.size() == 3);
}

// The player is the only thing here that could load or fetch, so "nothing is loaded and no request is made"
// is: the player is not asked to play.
TEST_CASE("SPEC-135 with resume_on_start off, or with an absent tune, the player is not asked to play [parts: resume]") {
    Collection collection;
    const spz::State state = playing_from_the_folder();
    {
        FakePlayer player;
        FakeSources sources;
        spz::Settings off;                      // resume_on_start is off unless the user turns it on
        REQUIRE_FALSE(off.resume_on_start);
        REQUIRE(spz::restore_player(state, &sources, &player));
        CHECK_FALSE(spz::resume_at_start(off, state, &collection.files, collection.root, &player));
        CHECK(player.log == std::vector<std::string>{"restore"});
    }
    {
        FakePlayer player;
        FakeSources sources;
        spz::Settings on;
        on.resume_on_start = true;
        collection.remove("/MUSICIANS/T/Tester_Tom/Three_Songs.sid");     // HVSC lists it; it is not on the device
        REQUIRE(spz::restore_player(state, &sources, &player));
        CHECK_FALSE(spz::resume_at_start(on, state, &collection.files, collection.root, &player));
        CHECK(player.log == std::vector<std::string>{"restore"});         // still the current entry, not played
    }
    {
        FakePlayer player;
        spz::Settings on;
        on.resume_on_start = true;
        CHECK_FALSE(spz::resume_at_start(on, spz::State(), &collection.files, collection.root, &player));   // nothing recorded
        CHECK(player.log.empty());
    }
}
#endif

// ---- SPEC-202: the clean-up at start -----------------------------------------------------------------------

TEST_CASE("SPEC-202 SPEC-132 at start the temporary files go from the app's own folders, and partial downloads past their age from the cache") {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::Folders folders;
    folders.config = folder.file("config/sidplayzero");
    folders.playlists = folder.file("data/sidplayzero/playlists");
    folders.imports = folders.playlists + "/import";
    folders.exports = folders.playlists + "/export";
    folders.state = folder.file("state/sidplayzero");
    folders.cache = folder.file("cache/sidplayzero");

    folder.write("config/sidplayzero/settings.ini", "engine = light\n");
    folder.write("config/sidplayzero/.settings.ini.tmp-sidplayzero", "engine = li");
    folder.write("state/sidplayzero/state.ini", "browser_folder = /\n");
    folder.write("state/sidplayzero/.state.ini.tmp-sidplayzero", "browser_fol");
    folder.write("state/sidplayzero/lock", "");
    folder.write("data/sidplayzero/playlists/Favourites.m3u8", "#EXTM3U\n");
    folder.write("data/sidplayzero/playlists/.Favourites.m3u8.tmp-sidplayzero", "#EXT");
    folder.write("cache/sidplayzero/index.bin", "an index");
    folder.write("cache/sidplayzero/.index.bin.tmp-sidplayzero", "an ind");
    folder.write("cache/sidplayzero/.listing.bin.tmp-sidplayzero", "a lis");
    folder.write("cache/sidplayzero/HVSC_84-all-of-them.7z.part", "half of an old archive");
    folder.write("cache/sidplayzero/HVSC_84-all-of-them.7z.part.meta", "etag = v84\n");
    folder.write("cache/sidplayzero/HVSC_85-all-of-them.7z.part", "half of a new one");
    folder.write("cache/sidplayzero/HVSC_85-all-of-them.7z.part.meta", "etag = v85\n");
    const std::int64_t day = 24 * 60 * 60;
    for (const char* name : {"HVSC_84-all-of-them.7z.part", "HVSC_84-all-of-them.7z.part.meta"}) {
        spz::test::set_modified(folder.file(std::string("cache/sidplayzero/") + name), clock.wall_time_s() - 8 * day);
    }
    for (const char* name : {"HVSC_85-all-of-them.7z.part", "HVSC_85-all-of-them.7z.part.meta"}) {
        spz::test::set_modified(folder.file(std::string("cache/sidplayzero/") + name), clock.wall_time_s() - 6 * day);
    }

    const spz::CleanedUp cleaned = spz::clean_up_at_start(&files, &clock, folders);
    CHECK(cleaned.temp_files == 5);
    CHECK(cleaned.partials == 1);
    CHECK(names_in(folders.config) == std::vector<std::string>{"settings.ini"});
    CHECK(names_in(folders.state) == std::vector<std::string>{"lock", "state.ini"});
    CHECK(names_in(folders.playlists) == std::vector<std::string>{"Favourites.m3u8"});
    CHECK(names_in(folders.cache) ==
          std::vector<std::string>{"HVSC_85-all-of-them.7z.part", "HVSC_85-all-of-them.7z.part.meta", "index.bin"});
    CHECK(spz::test::text_on_disk(folder.file("config/sidplayzero/settings.ini")) == "engine = light\n");
}

TEST_CASE("SPEC-202 SPEC-134 the clean-up at start makes no folder that is not there yet, and writes nothing") {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::Folders folders;
    folders.config = folder.file("config/sidplayzero");
    folders.playlists = folder.file("data/sidplayzero/playlists");
    folders.imports = folders.playlists + "/import";
    folders.exports = folders.playlists + "/export";
    folders.state = folder.file("state/sidplayzero");
    folders.cache = folder.file("cache/sidplayzero");

    const spz::CleanedUp cleaned = spz::clean_up_at_start(&files, &clock, folders);
    CHECK(cleaned.temp_files == 0);
    CHECK(cleaned.partials == 0);
    CHECK(std::filesystem::is_empty(folder.path));
    CHECK(files.count("make_folders") == 0);
    CHECK(files.count("write") == 0);
}
