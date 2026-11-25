// SPDX-License-Identifier: GPL-3.0-or-later
// "Export" (SPEC-179): the file written for other players, and that importing it gives the playlist back.
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/importers.h"
#include "spz/playlist_export.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS && SPZ_PART_EXPORT

namespace {

// The playlist "Loading tunes": a tune, a tune with a subtune number, and a third.
void fill(ListsWorld* world) {
    world->playlists->load();
    REQUIRE(world->playlists->create("Loading tunes").ok);
    REQUIRE(world->playlists->add("Loading tunes", spz::Entry{kCommando, 0, ""}).ok);
    REQUIRE(world->playlists->add("Loading tunes", spz::Entry{kThreeSongs, 3, ""}).ok);
    REQUIRE(world->playlists->add("Loading tunes", spz::Entry{kClockDemo, 0, ""}).ok);
}

}  // namespace

TEST_CASE("SPEC-179 Export writes #EXTM3U, #PLAYLIST:, and for each entry #EXTINF: and the HVSC path, with ?N for a subtune number, and MSG_EXPORTED gives the path [parts: playlists export]") {
    ListsWorld world;
    fill(&world);
    const spz::ListsResult result = spz::export_playlist(&world.files, *world.playlists, "Loading tunes", world.export_folder);
    CHECK(result.ok);
    CHECK(result.message == "MSG_EXPORTED");
    const std::string path = world.export_folder + "/Loading tunes.m3u8";      // the name SPEC-170 gives it
    REQUIRE(result.about.count("path") == 1);
    CHECK(result.about.at("path") == path);

    const std::string written = file_text(path);
    CHECK(written == text(fixture("lists/expected/export.m3u8")));
    CHECK(is_utf8_text(written));                              // SPEC-009
    CHECK(written.find("#SIDTUNE") == std::string::npos);      // only what other players read
    CHECK(world.files.count("rename") >= 1);                   // saved safely, through the seam (SPEC-132)
    CHECK(join(world.changes_outside(world.temp.file("share"))) == "");
}

TEST_CASE("SPEC-179 an entry whose title is not known is exported as its path alone [parts: playlists export]") {
    spz::Playlist list;
    list.name = "Bare";
    spz::PlaylistEntry known, bare, numbered;
    known.entry.path = kCommando;
    known.author = "Test Author";
    known.title = "Commando Test";
    known.length_s = 3;
    bare.entry.path = kClockDemo;
    numbered.entry.path = kThreeSongs;
    numbered.entry.subtune = 2;
    numbered.entry.md5 = kMd5ThreeSongs;
    list.entries = {known, bare, numbered};
    CHECK(text(spz::exported_playlist(list)) ==
          std::string("#EXTM3U\n#PLAYLIST:Bare\n") +
          "#EXTINF:3,Test Author - Commando Test\n" + kCommando + "\n" +
          kClockDemo + "\n" +
          kThreeSongs + "?2\n");
}

TEST_CASE("SPEC-179 Export replaces a file of that name [parts: playlists export]") {
    ListsWorld world;
    fill(&world);
    const std::string path = world.temp.write("share/sidplayzero/export/Loading tunes.m3u8", "something older\n");
    REQUIRE(spz::export_playlist(&world.files, *world.playlists, "Loading tunes", world.export_folder).ok);
    CHECK(file_text(path) == text(fixture("lists/expected/export.m3u8")));
    // And again: the same one file, no second beside it.
    REQUIRE(spz::export_playlist(&world.files, *world.playlists, "Loading tunes", world.export_folder).ok);
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(world.export_folder)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
}

TEST_CASE("SPEC-179 exporting a playlist and importing the file gives a playlist with the same tunes, subtune numbers and order [parts: playlists export]") {
    ListsWorld world;
    fill(&world);
    const spz::ListsResult result = spz::export_playlist(&world.files, *world.playlists, "Loading tunes", world.export_folder);
    REQUIRE(result.ok);

    spz::Importer importer(&world.files, world.root, world.import_folder, &world.listing, world.playlists.get());
    const spz::ImportReport report = importer.import_file(result.about.at("path"));
    CHECK(report.message == "");
    CHECK(report.playlist == "Loading tunes 2");               // the file's "#PLAYLIST:" name is taken
    CHECK(report.not_found.empty());
    CHECK(join(world.entries_of("Loading tunes 2")) == join(world.entries_of("Loading tunes")));
    CHECK(join(world.entries_of("Loading tunes 2")) ==
          std::string(kCommando) + "\n" + kThreeSongs + "#3\n" + kClockDemo + "\n");
}

#endif  // SPZ_PART_PLAYLISTS && SPZ_PART_EXPORT
