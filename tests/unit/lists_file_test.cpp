// SPDX-License-Identifier: GPL-3.0-or-later
// A playlist's file (SPEC-170): what the reader makes of a file written by hand, and what the writer
// writes. With it, what other items say of this reader: text files (SPEC-009), paths (SPEC-154) and
// limits (SPEC-204).
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/playlist.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

// Where a line is in a text's lines, or -1.
int line_number(const std::vector<std::string>& lines, const std::string& wanted, int from = 0) {
    for (int i = from; i < static_cast<int>(lines.size()); ++i) {
        if (lines[static_cast<std::size_t>(i)] == wanted) return i;
    }
    return -1;
}

spz::PlaylistEntry tune(const std::string& path, int subtune = 0, const std::string& md5 = "") {
    spz::PlaylistEntry entry;
    entry.entry.path = path;
    entry.entry.subtune = subtune;
    entry.entry.md5 = md5;
    return entry;
}

}  // namespace

TEST_CASE("SPEC-170 a playlist written by hand with the optional lines loads: name, subtunes, MD5s, titles and lengths [parts: playlists]") {
    const spz::PlaylistRead read = spz::read_playlist(fixture("lists/playlists/by_hand.m3u8"));
    CHECK(read.message == "");
    const spz::Playlist& list = read.playlist;
    CHECK(list.name == "Loading tunes");
    REQUIRE(list.entries.size() == 6);

    CHECK(list.entries[0].entry.path == kCommando);
    CHECK(list.entries[0].entry.subtune == 1);
    CHECK(list.entries[0].entry.md5 == kMd5Commando);
    CHECK(list.entries[0].author == "Test Author");
    CHECK(list.entries[0].title == "Commando Test");
    CHECK(list.entries[0].length_s == 3);

    CHECK(list.entries[1].entry.path == kThreeSongs);          // "#SIDTUNE:" with a subtune and no MD5
    CHECK(list.entries[1].entry.subtune == 3);
    CHECK(list.entries[1].entry.md5 == "");
    CHECK(list.entries[1].title == "Three Subtunes");

    CHECK(list.entries[2].entry.path == kThreeSongs);          // with an MD5 and no subtune: the default subtune
    CHECK(list.entries[2].entry.subtune == 0);
    CHECK(list.entries[2].entry.md5 == kMd5ThreeSongs);
    CHECK(list.entries[2].title == "");                        // no "#EXTINF:" line: nothing is known
    CHECK(list.entries[2].author == "");
    CHECK(list.entries[2].length_s == -1);

    CHECK(list.entries[3].entry.path == kClockDemo);
    CHECK(list.entries[3].entry.subtune == 0);
    CHECK(list.entries[4].entry.path == kEitherChip);
    CHECK(list.entries[5].entry.path == "file:///home/u/Music/My Rips/Cool S\xC3\xB6ng.sid");   // outside the collection
    CHECK(list.entries[5].entry.subtune == 0);
}

TEST_CASE("SPEC-170 a playlist written by hand without the optional lines loads, and each entry means its tune's default subtune [parts: playlists]") {
    const spz::PlaylistRead read = spz::read_playlist(fixture("lists/playlists/plain.m3u8"));
    CHECK(read.message == "");
    CHECK(read.playlist.name == "Plain");
    REQUIRE(read.playlist.entries.size() == 2);
    CHECK(read.playlist.entries[0].entry.path == kCommando);
    CHECK(read.playlist.entries[1].entry.path == kThreeSongs);
    for (const spz::PlaylistEntry& entry : read.playlist.entries) {
        CHECK(entry.entry.subtune == 0);
        CHECK(entry.entry.md5 == "");
        CHECK(entry.title == "");
        CHECK(entry.length_s == -1);
        CHECK(entry.other_lines.empty());
    }
}

TEST_CASE("SPEC-170 lines the app does not know are kept with the entry they come before [parts: playlists]") {
    const spz::Playlist list = spz::read_playlist(fixture("lists/playlists/by_hand.m3u8")).playlist;
    REQUIRE(list.entries.size() == 6);
    CHECK(join(list.entries[3].other_lines) == "#EXTVLCOPT:network-caching=1000\n");
    CHECK(join(list.entries[4].other_lines) == "# a note that travels with the tune below\n#EXTGRP:games\n");
    CHECK(list.entries[0].other_lines.empty());                // "#EXTINF:" and "#SIDTUNE:" are the app's own
    CHECK(list.entries[5].other_lines.empty());
}

TEST_CASE("SPEC-170 saved again after its entries are reordered, each unknown line is still before its entry [parts: playlists]") {
    spz::Playlist list = spz::read_playlist(fixture("lists/playlists/by_hand.m3u8")).playlist;
    REQUIRE(list.entries.size() == 6);
    // Clock_Demo (with one unknown line) goes last, Either_Chip (with two) goes first.
    const spz::PlaylistEntry clock = list.entries[3], either = list.entries[4];
    list.entries.erase(list.entries.begin() + 3, list.entries.begin() + 5);
    list.entries.insert(list.entries.begin(), either);
    list.entries.push_back(clock);

    const std::vector<std::string> lines = lines_of(text(spz::write_playlist(list)));
    REQUIRE(lines.size() >= 4);
    CHECK(lines[0] == "#EXTM3U");
    CHECK(lines[1] == "#PLAYLIST:Loading tunes");
    // Either_Chip is the first entry now, and its two lines are right before it, in their order.
    CHECK(lines[2] == "# a note that travels with the tune below");
    CHECK(lines[3] == "#EXTGRP:games");
    CHECK(lines[4] == kEitherChip);
    // Clock_Demo is the last entry, and its line comes after the entry before it and before its own.
    const int option = line_number(lines, "#EXTVLCOPT:network-caching=1000");
    const int clock_line = line_number(lines, kClockDemo);
    const int before = line_number(lines, "file:///home/u/Music/My Rips/Cool S\xC3\xB6ng.sid");
    CHECK(before >= 0);
    CHECK(option > before);
    CHECK(clock_line == option + 1);
    CHECK(clock_line == static_cast<int>(lines.size()) - 1);

    // And read once more, the lines are with the same entries.
    const spz::Playlist again = spz::read_playlist(spz::write_playlist(list)).playlist;
    REQUIRE(again.entries.size() == 6);
    CHECK(again.entries[0].entry.path == kEitherChip);
    CHECK(join(again.entries[0].other_lines) == "# a note that travels with the tune below\n#EXTGRP:games\n");
    CHECK(again.entries[5].entry.path == kClockDemo);
    CHECK(join(again.entries[5].other_lines) == "#EXTVLCOPT:network-caching=1000\n");
}

TEST_CASE("SPEC-170 the app writes the form the specification shows, line for line [parts: playlists]") {
    spz::Playlist list;
    list.name = "Loading tunes";
    spz::PlaylistEntry commando = tune("/MUSICIANS/H/Hubbard_Rob/Commando.sid", 1, "6d019ecba831a9f853675aac29a61c10");
    commando.author = "Rob Hubbard";
    commando.title = "Commando";
    commando.length_s = 235;
    list.entries.push_back(commando);
    CHECK(text(spz::write_playlist(list)) ==
          "#EXTM3U\n"
          "#PLAYLIST:Loading tunes\n"
          "#EXTINF:235,Rob Hubbard - Commando\n"
          "#SIDTUNE:subtune=1 md5=6d019ecba831a9f853675aac29a61c10\n"
          "/MUSICIANS/H/Hubbard_Rob/Commando.sid\n");

    // The optional lines are there only for what is known: a subtune alone, an MD5 alone, neither.
    list.entries.clear();
    list.entries.push_back(tune(kThreeSongs, 3));
    list.entries.push_back(tune(kThreeSongs, 0, kMd5ThreeSongs));
    list.entries.push_back(tune(kClockDemo));
    list.entries.push_back(tune("file:///home/u/Music/a tune.sid"));   // outside the collection: as it is
    CHECK(text(spz::write_playlist(list)) ==
          std::string("#EXTM3U\n#PLAYLIST:Loading tunes\n") +
          "#SIDTUNE:subtune=3\n" + kThreeSongs + "\n" +
          "#SIDTUNE:md5=" + kMd5ThreeSongs + "\n" + kThreeSongs + "\n" +
          kClockDemo + "\n" +
          "file:///home/u/Music/a tune.sid\n");
}

TEST_CASE("SPEC-170 a playlist written by hand in the app's own form comes back byte for byte when saved [parts: playlists]") {
    for (const char* name : {"lists/playlists/by_hand.m3u8", "lists/playlists/plain.m3u8", "lists/playlists/gap.m3u8"}) {
        CAPTURE(name);
        const spz::Bytes file = fixture(name);
        CHECK(text(spz::write_playlist(spz::read_playlist(file).playlist)) == text(file));
    }
}

TEST_CASE("SPEC-170 a playlist's file is named after the playlist, with other characters than letters, digits, space, - and _ replaced by _ [parts: playlists]") {
    CHECK(spz::playlist_file_name("Loading tunes") == "Loading tunes.m3u8");
    CHECK(spz::playlist_file_name("a-b_c 1") == "a-b_c 1.m3u8");
    CHECK(spz::playlist_file_name("Rock/Pop: 80's!") == "Rock_Pop_ 80_s_.m3u8");
    CHECK(spz::playlist_file_name("a.b") == "a_b.m3u8");
    CHECK(spz::playlist_file_name("..") == "__.m3u8");                  // never a way out of the folder
    CHECK(spz::playlist_file_name("x\\y") == "x_y.m3u8");
}

TEST_CASE("SPEC-170 SPEC-062 a playlist read from the playlists folder gives the queue its file describes, subtunes and all [parts: playlists]") {
    ListsWorld world;
    world.put_playlist("Loading tunes.m3u8", text(fixture("lists/playlists/by_hand.m3u8")));
    world.put_playlist("Plain.m3u8", text(fixture("lists/playlists/plain.m3u8")));
    world.playlists->load();
    CHECK(world.files.count("read") >= 2);                          // through the seam (SPEC-021)

    const std::vector<spz::Entry> queue = world.playlists->queue("Loading tunes");
    REQUIRE(queue.size() == 6);
    CHECK(queue[0] == spz::Entry{kCommando, 1, ""});
    CHECK(queue[1] == spz::Entry{kThreeSongs, 3, ""});
    CHECK(queue[2] == spz::Entry{kThreeSongs, 0, ""});              // no number: the default subtune
    CHECK(queue[3] == spz::Entry{kClockDemo, 0, ""});
    CHECK(queue[4] == spz::Entry{kEitherChip, 0, ""});
    CHECK(queue[5].path == "file:///home/u/Music/My Rips/Cool S\xC3\xB6ng.sid");

    const std::vector<spz::Entry> plain = world.playlists->queue("Plain");
    REQUIRE(plain.size() == 2);
    CHECK(plain[0] == spz::Entry{kCommando, 0, ""});
    CHECK(plain[1] == spz::Entry{kThreeSongs, 0, ""});
}

TEST_CASE("SPEC-009 SPEC-170 what the app writes for a playlist is valid UTF-8 text, and it loads again as it was [parts: playlists]") {
    spz::Playlist list;
    list.name = "H\xC3\xBClsbeck & \xE2\x82\xAC 5";                 // "Hülsbeck & € 5"
    spz::PlaylistEntry own = tune(kOwnTune, 0, kMd5OwnTune);
    own.author = "Somebody";
    own.title = "The User's Own \xE2\x80\x9Ctune\xE2\x80\x9D";     // curly quotes, as Windows-1252 has them
    own.length_s = 210;
    list.entries.push_back(own);
    spz::PlaylistEntry huelsbeck = tune(kHuelsbeck, 0, "");
    huelsbeck.author = "Thirty-two characters of author!";
    huelsbeck.title = "H\xC3\xBClsbeck T\xC3\xABst";
    huelsbeck.length_s = 2;
    list.entries.push_back(huelsbeck);

    const spz::Bytes file = spz::write_playlist(list);
    CHECK(is_utf8_text(text(file)));
    CHECK(text(file).back() == '\n');
    CHECK(text(file).find('\r') == std::string::npos);

    const spz::Playlist again = spz::read_playlist(file).playlist;
    CHECK(again.name == list.name);
    REQUIRE(again.entries.size() == 2);
    CHECK(again.entries[0].entry.path == kOwnTune);
    CHECK(again.entries[0].entry.md5 == kMd5OwnTune);
    CHECK(again.entries[0].title == own.title);
    CHECK(again.entries[0].author == "Somebody");
    CHECK(again.entries[0].length_s == 210);
    CHECK(again.entries[1].title == "H\xC3\xBClsbeck T\xC3\xABst");
}

TEST_CASE("SPEC-009 SPEC-172 every playlist file the app writes into its folder is valid UTF-8 text [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    world.playlists->load();
    REQUIRE(world.playlists->create("Caf\xC3\xA9 \xE2\x80\x93 sp\xC3\xA4t").ok);     // "Café – spät"
    REQUIRE(world.playlists->add("Caf\xC3\xA9 \xE2\x80\x93 sp\xC3\xA4t", spz::Entry{kHuelsbeck, 0, ""}).ok);
    REQUIRE(world.playlists->add("Caf\xC3\xA9 \xE2\x80\x93 sp\xC3\xA4t", spz::Entry{kOwnTune, 0, ""}).ok);
    int seen = 0;
    for (const auto& file : std::filesystem::directory_iterator(world.playlists_folder)) {
        if (!file.is_regular_file()) continue;
        ++seen;
        CAPTURE(file.path().string());
        const std::string contents = file_text(file.path().string());
        CHECK(is_utf8_text(contents));
        CHECK(contents.compare(0, 8, "#EXTM3U\n") == 0);
    }
    CHECK(seen >= 1);
}

TEST_CASE("SPEC-154 SPEC-170 the playlist reader keeps an entry whose path is no collection path, and it is a missing entry, never a file [parts: playlists]") {
    ListsWorld world;
    world.put_own_tune();
    // A file is put wherever a careless reader would end up, so that only the rule can refuse the path.
    const spz::Bytes tune_bytes = fixture("tunes/commando_test.sid");
    const std::string root_name = std::filesystem::path(world.root).filename().string();
    world.temp.write(root_name + "/Secret.sid", tune_bytes);
    world.temp.write(root_name + "/MUSICIANS/Double.sid", tune_bytes);
    world.temp.write(root_name + "/MUSICIANS\\Back.sid", tune_bytes);
    world.temp.write(root_name + "/MUSICIANS/Ctrl\x01.sid", tune_bytes);
    world.temp.write(root_name + "/MUSICIANS/NoEnding.txt", tune_bytes);
    const std::string long_path = "/" + std::string(251, 'a') + ".sid";            // 256 characters
    world.temp.write(root_name + long_path, tune_bytes);

    const std::vector<std::string> refused = {
        "/MUSICIANS/../Secret.sid",          // a ".." part
        "/MUSICIANS//Double.sid",            // a doubled slash
        "/MUSICIANS\\Back.sid",              // a backslash
        "/MUSICIANS/Ctrl\x01.sid",           // a control character
        "/MUSICIANS/NoEnding.txt",           // no ".sid"
        "MUSICIANS/H/Hubbard_Rob/Commando.sid",   // no "/" at its start
    };
    std::string file = "#EXTM3U\n#PLAYLIST:Paths\n";
    for (const std::string& path : refused) file += path + "\n";
    file += std::string(kOwnTune) + "\n" + long_path + "\n";
    world.put_playlist("Paths.m3u8", file);
    world.playlists->load();

    const spz::Playlist* list = world.playlists->find("Paths");
    REQUIRE(list != nullptr);
    REQUIRE(list->entries.size() == refused.size() + 2);        // every entry stays in the playlist
    for (std::size_t i = 0; i < refused.size(); ++i) {
        CAPTURE(refused[i]);
        CHECK(list->entries[i].entry.path == refused[i]);       // holding its own text
        CHECK(world.resolver->resolve(list->entries[i].entry).found == spz::Found::Missing);
    }
    // A space and an accent are allowed, and so is a path of 256 characters: the length row is the listing's.
    CHECK(world.resolver->resolve(list->entries[refused.size()].entry).found == spz::Found::Present);
    CHECK(world.resolver->resolve(list->entries[refused.size() + 1].entry).found == spz::Found::Present);

    // Saved again, the refused entries are still there, as they were.
    REQUIRE(world.playlists->move_entry("Paths", 0, +1).ok);
    const std::string saved = file_text(world.playlists->file_of("Paths"));
    for (const std::string& path : refused) {
        CAPTURE(path);
        CHECK(line_number(lines_of(saved), path) >= 0);
    }
    // Nothing was written anywhere but the playlists folder: never under the collection root.
    CHECK(join(world.changes_outside(world.playlists_folder)) == "");
}

TEST_CASE("SPEC-204 SPEC-170 a playlist file of 4 MiB is read, and one a byte longer is refused with MSG_FILE_TOO_LARGE [parts: playlists]") {
    std::string file = std::string("#EXTM3U\n#PLAYLIST:Big\n") + kCommando + "\n";
    file.append(spz::kPlaylistFileLimit - file.size(), '\n');
    REQUIRE(file.size() == 4u * 1024 * 1024);
    const spz::PlaylistRead fits = spz::read_playlist(bytes(file));
    CHECK(fits.message == "");
    CHECK(fits.playlist.name == "Big");
    CHECK(fits.playlist.entries.size() == 1);

    file.push_back('\n');
    const spz::PlaylistRead too_large = spz::read_playlist(bytes(file));
    CHECK(too_large.message == "MSG_FILE_TOO_LARGE");
    CHECK(too_large.playlist.entries.empty());
}

TEST_CASE("SPEC-204 SPEC-170 a playlist holds at most 20,000 entries: the rest is not read, and MSG_LIST_CUT says so [parts: playlists]") {
    std::string file = "#EXTM3U\n#PLAYLIST:Long\n";
    for (int i = 1; i <= 20000; ++i) file += "/T/Tune_" + std::to_string(i) + ".sid\n";
    const spz::PlaylistRead exact = spz::read_playlist(bytes(file));
    CHECK(exact.message == "");
    CHECK(exact.playlist.entries.size() == 20000);

    file += "/T/Tune_20001.sid\n";
    const spz::PlaylistRead cut = spz::read_playlist(bytes(file));
    CHECK(cut.message == "MSG_LIST_CUT");
    REQUIRE(cut.playlist.entries.size() == 20000);
    CHECK(cut.playlist.entries.front().entry.path == "/T/Tune_1.sid");
    CHECK(cut.playlist.entries.back().entry.path == "/T/Tune_20000.sid");
}

TEST_CASE("SPEC-204 SPEC-170 a million-character line is cut at 4,096 bytes and a zero byte ends its line, within two seconds [parts: playlists]") {
    std::string file = "#EXTM3U\n#PLAYLIST:Odd\n";
    file += "/" + std::string(999995, 'a') + ".sid\n";            // a line of a million characters
    file += std::string(kCommando) + '\0' + "junk after a zero byte\n";
    file += std::string(kClockDemo) + "\n";

    const auto begun = std::chrono::steady_clock::now();
    const spz::PlaylistRead read = spz::read_playlist(bytes(file));
    const auto taken = std::chrono::steady_clock::now() - begun;
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(taken).count() < 2000);

    CHECK(read.message == "");
    CHECK(read.playlist.name == "Odd");
    REQUIRE(read.playlist.entries.size() == 3);
    CHECK(read.playlist.entries[0].entry.path == "/" + std::string(4095, 'a'));     // cut there: 4,096 bytes
    CHECK(read.playlist.entries[1].entry.path == kCommando);
    CHECK(read.playlist.entries[2].entry.path == kClockDemo);
}

#endif  // SPZ_PART_PLAYLISTS
