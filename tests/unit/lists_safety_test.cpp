// SPDX-License-Identifier: GPL-3.0-or-later
// Playlists against what could break them: a playlists folder that cannot be looked at (SPEC-172), and an
// entry whose text would break the file into more lines than it has (SPEC-170, SPEC-154, SPEC-176).
#include <stdexcept>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/importers.h"
#include "spz/playlist.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

// The fake Files, with info() failing as NoAccess for any path in one folder: what a folder the app may
// not search gives (EACCES).
class NoLooking : public spz::fake::Files {
public:
    std::string folder;
    int infos = 0;
    spz::FileStatus info(const std::string& path, spz::FileInfo* out) override {
        if (++infos > 100000) throw std::runtime_error("info() asked for ever");
        if (path.compare(0, folder.size() + 1, folder + "/") == 0) return spz::FileStatus::NoAccess;
        return spz::fake::Files::info(path, out);
    }
};

int lines_equal_to(const std::string& text, const std::string& wanted) {
    int found = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (line == wanted) ++found;
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return found;
}

}  // namespace

TEST_CASE("SPEC-172 a playlist cannot be made or renamed while the playlists folder cannot be looked at: it fails at once, and nothing is written [parts: playlists]") {
    TempFolder temp;
    NoLooking files;
    files.folder = temp.file("playlists");
    std::filesystem::create_directories(files.folder);
    spz::Playlists lists(&files, files.folder, nullptr);
    lists.load();

    const spz::ListsResult made = lists.create("Loading");
    CHECK_FALSE(made.ok);
    CHECK(made.message == "");                                 // SPEC-116 has no message but for a full card
    CHECK(files.infos < 10);
    CHECK(files.count("write") == 0);
    REQUIRE(lists.list().size() == 1);                         // Favourites only
    CHECK(lists.find("Loading") == nullptr);

    // A playlist made while the folder could be looked at, renamed when it cannot.
    spz::fake::Files plain;
    spz::Playlists before(&plain, files.folder, nullptr);
    before.load();
    REQUIRE(before.create("Loading").ok);
    spz::Playlists after(&files, files.folder, nullptr);
    after.load();
    files.infos = 0;
    const spz::ListsResult renamed = after.rename("Loading", "Saving");
    CHECK_FALSE(renamed.ok);
    CHECK(files.infos < 10);
    CHECK(after.find("Loading") != nullptr);
    CHECK(after.find("Saving") == nullptr);
}

TEST_CASE("SPEC-170 SPEC-154 an entry whose text holds a line feed, a carriage return or a zero byte is written as one line: it cannot add lines to the playlist's file [parts: playlists]") {
    spz::Playlist list;
    list.name = "Mixed";
    spz::PlaylistEntry entry;
    entry.entry.path = std::string("/no/such.sid\n#EXTINF:1,Injected\n/MUSICIANS/H/Hubbard_Rob/Commando.sid\r/x.sid") +
                       std::string(1, '\0') + "/y.sid\x01";
    list.entries.push_back(entry);
    const spz::Bytes written = spz::write_playlist(list);
    const std::string text(written.begin(), written.end());
    CHECK(lines_equal_to(text, "/MUSICIANS/H/Hubbard_Rob/Commando.sid") == 0);
    CHECK(text.find('\r') == std::string::npos);
    CHECK(text.find('\0') == std::string::npos);
    CHECK(text.find('\x01') != std::string::npos);              // any other control character stays (SPEC-154)
    const spz::PlaylistRead read = spz::read_playlist(written);
    REQUIRE(read.playlist.entries.size() == 1);
    CHECK(read.playlist.entries[0].title.empty());             // no #EXTINF line came with it
    CHECK(read.playlist.entries[0].entry.path.find("Commando.sid") != std::string::npos);   // the text, on one line
}

TEST_CASE("SPEC-176 SPEC-170 a JSidplay2 .json entry whose name holds line feeds is kept as one missing entry, and the playlist's file gains no tune from it [parts: playlists import-more]") {
    ListsWorld world;
    world.playlists->load();
    spz::Importer importer{&world.files, world.root, world.import_folder, &world.listing, world.playlists.get()};
    const std::string file = world.temp.write(
        "share/sidplayzero/import/inject.json",
        std::string(R"([{"filename":"/C64Music/no/such.sid\n#EXTINF:1,Injected\n/MUSICIANS/X/Nobody/Injected.sid"}])"));
    const spz::ImportReport report = importer.import_file(file);
    REQUIRE(report.playlist == "inject");
    CHECK(report.entries == 1);
    REQUIRE(report.not_found.size() == 1);
    CHECK(report.not_found[0].find('\n') == std::string::npos);
    CHECK(world.entries_of("inject").size() == 1);

    const std::string saved = file_text(world.playlists_folder + "/inject.m3u8");
    CHECK(lines_equal_to(saved, "/MUSICIANS/X/Nobody/Injected.sid") == 0);
    CHECK(saved.find("\n#EXTINF") == std::string::npos);     // no line of its own
    world.restart();
    CHECK(world.entries_of("inject").size() == 1);           // read back: still the one entry
}

#endif  // SPZ_PART_PLAYLISTS
