// SPDX-License-Identifier: GPL-3.0-or-later
// Importing other players' playlists: where from and M3U (SPEC-175), matching a path to a tune
// (SPEC-178), JSidplay2 (SPEC-176) and Sidplay/w (SPEC-177). The files are tools/fixture_makers/lists.py's,
// one for each row of SPEC-175's table and each sample, written as those players write them.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "lists_support.h"
#include "spz/importers.h"
#include "spz/playlists.h"
#include "support.h"

using namespace spz::test;

#if SPZ_PART_PLAYLISTS

namespace {

struct ImportWorld : ListsWorld {
    spz::Importer importer{&files, root, import_folder, &listing, playlists.get()};

    explicit ImportWorld(const std::string& root_name = "C64Music") : ListsWorld(root_name) { playlists->load(); }

    // Puts one of the import fixtures into the import folder, under its own name, and gives its path.
    std::string put(const std::string& fixture_name) {
        const std::string name = std::filesystem::path(fixture_name).filename().string();
        return temp.write("share/sidplayzero/import/" + name, fixture("lists/import/" + fixture_name));
    }
    // Puts a text into the collection root as a file to import.
    std::string put_in_root(const std::string& name, const std::string& contents) {
        return temp.write(std::filesystem::path(root).filename().string() + "/" + name, contents);
    }
    std::string imported(const std::string& fixture_name) {
        const spz::ImportReport report = importer.import_file(put(fixture_name));
        return join(entries_of(report.playlist));
    }
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string three(int subtune) { return std::string(kThreeSongs) + "#" + std::to_string(subtune) + "\n"; }
std::string line(const char* path) { return std::string(path) + "\n"; }

}  // namespace

TEST_CASE("SPEC-175 the import panel lists the files ending in .m3u, .m3u8, .js2, .json or .spl in the import folder and in the collection root [parts: playlists]") {
    ImportWorld world;
    for (const char* name : {"a.m3u", "b.m3u8", "c.js2", "d.json", "e.spl", "notes.txt", "tune.sid", "m3u"}) {
        world.temp.write(std::string("share/sidplayzero/import/") + name, "x\n");
    }
    world.put_in_root("f.m3u", "x\n");
    world.put_in_root("readme.txt", "x\n");

    std::vector<std::string> found = world.importer.files();
    std::sort(found.begin(), found.end());
    std::vector<std::string> wanted = {world.import_folder + "/a.m3u", world.import_folder + "/b.m3u8",
                                       world.import_folder + "/c.js2", world.import_folder + "/d.json",
                                       world.import_folder + "/e.spl", world.root + "/f.m3u"};
    std::sort(wanted.begin(), wanted.end());
    CHECK(join(found) == join(wanted));
    CHECK(world.files.count("list") >= 2);                     // both folders, through the seam (SPEC-021)
}

TEST_CASE("SPEC-175 each player's M3U dialect, written as that player writes it, imports with the expected tunes, subtunes and order [parts: playlists]") {
    struct Case {
        const char* file;
        const char* playlist;
        std::string entries;
    };
    const std::vector<Case> cases = {
        {"vsid.m3u", "vsid", line(kCommando) + line(kThreeSongs)},                              // VICE's VSID
        {"sidplaywx.m3u8", "sidplaywx", line(kClockDemo) + line(kEitherChip) + line(kCommando)},
        {"phosphor.m3u", "phosphor", three(3) + std::string(kCommando) + "#1\n"},               // #PHOSPHOR:song=N
        {"sidplay_mac.m3u", "sidplay_mac", line(kHuelsbeck) + line(kOwnTune)},                  // Latin-1
        {"audacious.m3u", "audacious", three(2) + line(kOwnTune) + line(kCommando)},            // file://, ?N, %20
        {"mpd.m3u", "mpd", three(2) + line(kCommando) + three(3)},                              // /tune_NNN.sid
        {"winamp.m3u", "winamp", three(3) + std::string(kCommando) + "#1\n"},                   // {N}, backslashes
    };
    for (const Case& one : cases) {
        CAPTURE(one.file);
        ImportWorld world;
        world.put_own_tune();
        const std::string path = world.put(one.file);
        const std::string before = file_text(path);

        const spz::ImportReport report = world.importer.import_file(path);
        CHECK(report.message == "");
        CHECK(report.playlist == one.playlist);                // no "#PLAYLIST:" line: the file's name without its ending
        CHECK(join(world.entries_of(report.playlist)) == one.entries);
        CHECK(report.not_found.empty());
        CHECK(report.entries == static_cast<int>(lines_of(one.entries).size()));
        CHECK(world.playlists->list().size() == 2);            // Favourites and the new one

        CHECK(file_text(path) == before);                      // the file is only read
        CHECK(join(world.changes_outside(world.playlists_folder)) == "");
        CHECK(world.files.count("read") >= 1);
    }
}

TEST_CASE("SPEC-175 a file with the same entry twice gives two entries [parts: playlists]") {
    ImportWorld world;
    CHECK(world.imported("twice.m3u") == line(kCommando) + line(kClockDemo) + line(kCommando));
}

TEST_CASE("SPEC-175 mix.m3u holding #PLAYLIST:Favourites becomes the playlist Favourites 2, and one without that line becomes mix [parts: playlists]") {
    ImportWorld world;
    const spz::ImportReport named = world.importer.import_file(world.put("named/mix.m3u"));
    CHECK(named.playlist == "Favourites 2");
    CHECK(world.playlists->find("Favourites 2") != nullptr);
    CHECK(world.playlists->find("Favourites")->entries.empty());           // Favourites itself is untouched

    const spz::ImportReport plain = world.importer.import_file(world.put("plain/mix.m3u"));
    CHECK(plain.playlist == "mix");
    // While the name is taken, " 2", " 3" and so on is added.
    CHECK(world.importer.import_file(world.put("plain/mix.m3u")).playlist == "mix 2");
    CHECK(world.importer.import_file(world.put("plain/mix.m3u")).playlist == "mix 3");
    CHECK(world.importer.import_file(world.put("named/mix.m3u")).playlist == "Favourites 3");
    CHECK(world.playlists->list().size() == 6);
}

TEST_CASE("SPEC-175 an imported name loses its control characters and is cut so that, with the number added, it is at most 40 characters [parts: playlists]") {
    ImportWorld world;
    const std::string full = "A name that is a good deal longer than forty characters allow";
    const spz::ImportReport first = world.importer.import_file(world.put("long_name.m3u"));
    CHECK(first.playlist == full.substr(0, 40));
    const spz::ImportReport second = world.importer.import_file(world.put("long_name.m3u"));
    CHECK(second.playlist == full.substr(0, 38) + " 2");
    CHECK(second.playlist.size() == 40);

    const spz::ImportReport control = world.importer.import_file(world.put("control_name.m3u"));
    CHECK(control.playlist == "Tabhere and a bell there");
}

TEST_CASE("SPEC-175 an entry's subtune comes from the first that applies: #SIDTUNE, #PHOSPHOR song=N, a leading {N}, an ending ?N, an ending /tune_NNN.sid [parts: playlists]") {
    const spz::ImportRead read = spz::read_m3u(fixture("lists/import/subtunes.m3u"));
    CHECK(read.message == "");
    REQUIRE(read.lines.size() == 7);
    const int wanted[] = {3,    // "#SIDTUNE:subtune=3" before it
                          1,    // "#SIDTUNE:" wins over "#PHOSPHOR:", "{3}" and "?2"
                          2,    // "#PHOSPHOR:song=2" wins over "{3}"
                          3,    // "{3}" wins over "?2"
                          2,    // "?2"
                          1,    // "/tune_001.sid"
                          0};   // nothing says: the tune's default subtune
    for (std::size_t i = 0; i < 7; ++i) {
        CAPTURE(i);
        CHECK(read.lines[i].subtune == wanted[i]);
        // Whatever gave the number, the name is the tune's alone.
        CHECK(read.lines[i].text == std::string("/home/u/C64Music") + kThreeSongs);
    }

    ImportWorld world;
    CHECK(world.imported("subtunes.m3u") == three(3) + three(1) + three(2) + three(3) + three(2) + three(1) + line(kThreeSongs));
}

TEST_CASE("SPEC-175 an M3U file is read as UTF-8 if it is valid UTF-8 and as Latin-1 if not, with either kind of line ending [parts: playlists]") {
    // Latin-1, line feeds: SIDPLAY for macOS.
    const spz::ImportRead latin = spz::read_m3u(fixture("lists/import/sidplay_mac.m3u"));
    REQUIRE(latin.lines.size() == 2);
    CHECK(latin.lines[1].text == "/Users/u/Music/C64Music/My Rips/Cool S\xC3\xB6ng.sid");
    // The same text as UTF-8 is read as it is.
    const spz::ImportRead utf8 = spz::read_m3u(bytes("/Users/u/Music/C64Music/My Rips/Cool S\xC3\xB6ng.sid\n"));
    REQUIRE(utf8.lines.size() == 1);
    CHECK(utf8.lines[0].text == latin.lines[1].text);
    // Carriage return and line feed: Winamp's, on Windows.
    const spz::ImportRead crlf = spz::read_m3u(fixture("lists/import/winamp.m3u"));
    REQUIRE(crlf.lines.size() == 2);
    CHECK(crlf.lines[0].text == "C:\\C64Music\\MUSICIANS\\T\\Tester_Tom\\Three_Songs.sid");
    CHECK(crlf.lines[0].subtune == 3);
    CHECK(crlf.lines[1].subtune == 1);
}

TEST_CASE("SPEC-175 a line starting file:// has that removed and its %XX sequences decoded; empty lines and # lines are no entries [parts: playlists]") {
    const spz::ImportRead read = spz::read_m3u(fixture("lists/import/audacious.m3u"));
    REQUIRE(read.lines.size() == 3);
    CHECK(read.lines[0].text == std::string("/home/u/C64Music") + kThreeSongs);
    CHECK(read.lines[0].subtune == 2);
    CHECK(read.lines[1].text == "/home/u/C64Music/My Rips/Cool S\xC3\xB6ng.sid");      // %20 and %C3%B6
    CHECK(read.lines[1].subtune == 0);

    const spz::ImportRead vsid = spz::read_m3u(fixture("lists/import/vsid.m3u"));         // "#EXTM3U", comments, "#"
    CHECK(vsid.name == "");
    CHECK(vsid.lines.size() == 2);
    const spz::ImportRead sparse = spz::read_m3u(bytes("\n#\n\n/a.sid\n\n# x\n/b.sid\n\n"));
    REQUIRE(sparse.lines.size() == 2);
    CHECK(sparse.lines[0].text == "/a.sid");
    CHECK(sparse.lines[1].text == "/b.sid");
    CHECK(spz::read_m3u(fixture("lists/import/named/mix.m3u")).name == "Favourites");
}

TEST_CASE("SPEC-178 one entry for each form of path imports as its tune, and one that matches nothing is reported and kept as a missing entry [parts: playlists]") {
    ImportWorld world("sids");                                 // a root that is not called C64Music
    world.put_own_tune();
    // HVSC gets a second tune of each name the entries use but one, so that the last form, the file's
    // name alone, can find only Renamed.sid: every other entry has to be found by its own form.
    std::string database = file_text(world.root + "/DOCUMENTS/Songlengths.md5");
    int number = 0;
    for (const char* name : {"Commando.sid", "Clock_Demo.sid", "Either_Chip.sid", "Huelsbeck_Test.sid",
                             "Two_Chips_2SID.sid", "Three_Chips_3SID.sid"}) {
        database += std::string("; /DUPLICATES/") + name + "\r\n" + std::string(31, '0') + std::to_string(++number) + "=0:02\r\n";
    }
    world.put_in_root("DOCUMENTS/Songlengths.md5", database);
    world.build_listing();

    // The fixture's eight entries, and the fourth form: the path taken relative to the folder of the
    // file being imported, once as it is and once without regard to case. The tune is the user's own,
    // which HVSC does not list, so that no other form can find it.
    const std::string file = text(fixture("lists/import/forms.m3u")) +
                             world.root + kOwnTune + "\n" +
                             world.root + lower("/My Rips/Cool S") + "\xC3\xB6ng.sid\n";
    const spz::ImportReport report = world.importer.import_file(world.put_in_root("forms.m3u", file));

    const std::string nothing = "/home/u/C64Music/MUSICIANS/N/Nobody/Nothing.sid";
    CHECK(report.message == "");
    CHECK(report.playlist == "forms");
    CHECK(join(world.entries_of("forms")) ==
          line(kCommando) +                                    // the part after /C64Music/
          line(kClockDemo) +                                   // the path as it stands
          line(kEitherChip) +                                  // with a / put in front
          line(kHuelsbeck) +                                   // the same three without regard to case:
          line(kTwoChips) +                                    //   the tune's own path is what the entry gets
          line(kThreeChips) +
          line(kRenamed) +                                     // the file's name alone: one tune has it
          nothing + "\n" +                                     // nothing: a missing entry holding the original text
          line(kOwnTune) +                                     // relative to the folder of the imported file
          line(kOwnTune));                                     //   and that without regard to case
    CHECK(report.entries == 10);
    CHECK(join(report.not_found) == nothing + "\n");

    const std::vector<spz::Entry> queue = world.playlists->queue("forms");
    REQUIRE(queue.size() == 10);
    for (std::size_t i = 0; i < queue.size(); ++i) {
        CAPTURE(queue[i].path);
        CHECK(world.resolver->resolve(queue[i]).found == (i == 7 ? spz::Found::Missing : spz::Found::Present));
    }
}

TEST_CASE("SPEC-178 a path matches a tune that is in the listing or under the root: an absent tune, and one of the user's own [parts: playlists]") {
    ImportWorld world;
    world.put_own_tune();
    world.take_away(kClockDemo);                               // listed, and not on the device
    std::string path;
    CHECK(world.importer.match("/home/u/C64Music/DEMOS/A-F/Clock_Demo.sid", world.import_folder, &path));
    CHECK(path == kClockDemo);
    CHECK(world.importer.match("C:\\Music\\C64Music\\My Rips\\Cool S\xC3\xB6ng.sid", world.import_folder, &path));
    CHECK(path == kOwnTune);                                   // backslashes become slashes
    CHECK_FALSE(world.importer.match("/home/u/C64Music/MUSICIANS/N/Nobody/Nothing.sid", world.import_folder, &path));
}

TEST_CASE("SPEC-178 the file's name alone finds a tune only if exactly one tune in the listing has that name [parts: playlists]") {
    ImportWorld world;
    std::string path;
    CHECK(world.importer.match("D:\\Old\\Stuff\\Renamed.sid", world.import_folder, &path));
    CHECK(path == kRenamed);

    // A second tune of that name in HVSC's database: the name alone no longer says which.
    std::string database = file_text(world.root + "/DOCUMENTS/Songlengths.md5");
    database += "; /DEMOS/A-F/Renamed.sid\r\n0123456789abcdef0123456789abcdef=0:02\r\n";
    world.put_in_root("DOCUMENTS/Songlengths.md5", database);
    world.build_listing();
    CHECK_FALSE(world.importer.match("D:\\Old\\Stuff\\Renamed.sid", world.import_folder, &path));
    CHECK(world.importer.match("/home/u/C64Music/DEMOS/A-F/Renamed.sid", world.import_folder, &path));
    CHECK(path == "/DEMOS/A-F/Renamed.sid");
}

TEST_CASE("SPEC-154 SPEC-178 the importers take no path that is not a collection path: it becomes a missing entry, and nothing is written under the root [parts: playlists]") {
    ImportWorld world;
    world.put_own_tune();
    // A file is put wherever a careless importer would end up.
    const spz::Bytes tune_bytes = fixture("tunes/commando_test.sid");
    world.temp.write("C64Music/Secret.sid", tune_bytes);
    world.temp.write("C64Music/MUSICIANS/Double.sid", tune_bytes);
    world.temp.write("C64Music/MUSICIANS/Ctrl\x01.sid", tune_bytes);
    world.temp.write("C64Music/MUSICIANS/NoEnding.txt", tune_bytes);

    const std::vector<std::string> refused = {
        "/home/u/C64Music/MUSICIANS/../Secret.sid",            // a ".." part
        "/home/u/C64Music/MUSICIANS//Double.sid",              // a doubled slash
        "/home/u/C64Music/MUSICIANS/Ctrl\x01.sid",             // a control character
        "/home/u/C64Music/MUSICIANS/NoEnding.txt",             // no ".sid"
    };
    std::string file;
    for (const std::string& path : refused) file += path + "\n";
    file += "/home/u/C64Music/My Rips/Cool S\xC3\xB6ng.sid\n";     // a space and an accent are allowed
    const spz::ImportReport report = world.importer.import_file(world.temp.write("share/sidplayzero/import/paths.m3u", file));

    CHECK(join(report.not_found) == join(refused));
    std::vector<std::string> wanted = refused;
    wanted.push_back(kOwnTune);
    CHECK(join(world.entries_of("paths")) == join(wanted));
    const std::vector<spz::Entry> queue = world.playlists->queue("paths");
    REQUIRE(queue.size() == 5);
    for (std::size_t i = 0; i < 4; ++i) CHECK(world.resolver->resolve(queue[i]).found == spz::Found::Missing);
    CHECK(world.resolver->resolve(queue[4]).found == spz::Found::Present);
    CHECK(join(world.changes_outside(world.playlists_folder)) == "");
}

TEST_CASE("SPEC-204 SPEC-175 a file to import one byte past 4 MiB is refused with MSG_FILE_TOO_LARGE and makes no playlist; past 20,000 entries the list is cut [parts: playlists]") {
    ImportWorld world;
    std::string big = "/home/u/C64Music" + std::string(kCommando) + "\n";
    big.append(spz::kPlaylistFileLimit + 1 - big.size(), '\n');
    CHECK(spz::read_m3u(bytes(big)).message == "MSG_FILE_TOO_LARGE");
    const spz::ImportReport report = world.importer.import_file(world.temp.write("share/sidplayzero/import/big.m3u", big));
    CHECK(report.message == "MSG_FILE_TOO_LARGE");
    CHECK(report.playlist == "");
    CHECK(world.playlists->list().size() == 1);                // Favourites alone

    std::string many;
    for (int i = 1; i <= 20001; ++i) many += "/T/Tune_" + std::to_string(i) + ".sid\n";
    const spz::ImportRead cut = spz::read_m3u(bytes(many));
    CHECK(cut.message == "MSG_LIST_CUT");
    CHECK(cut.lines.size() == 20000);
}

#if SPZ_PART_IMPORT_MORE

TEST_CASE("SPEC-176 JSidplay2's .js2 and .json files import with the expected tunes in order, and neither carries subtunes [parts: playlists import-more]") {
    ImportWorld world;
    CHECK(world.imported("jsidplay2.js2") == line(kCommando) + line(kThreeSongs) + line(kClockDemo));
    CHECK(world.playlists->find("jsidplay2") != nullptr);
    // The array's objects in order; an object with no "filename", and a "filename" deeper inside, are ignored.
    const spz::ImportReport json = world.importer.import_file(world.put("jsidplay2.json"));
    CHECK(json.message == "");
    CHECK(json.playlist == "jsidplay2 2");
    CHECK(join(world.entries_of(json.playlist)) == line(kCommando) + line(kThreeSongs) + line(kClockDemo));
    CHECK(json.not_found.empty());

    const spz::ImportRead js2 = spz::read_js2(fixture("lists/import/jsidplay2.js2"));
    REQUIRE(js2.lines.size() == 3);
    CHECK(js2.lines[0].text == kCommando);                     // the leading "<HVSC>" is removed
    CHECK(js2.lines[2].text == std::string("/home/u/C64Music") + kClockDemo);
    const spz::ImportRead read = spz::read_jsidplay2_json(fixture("lists/import/jsidplay2.json"));
    REQUIRE(read.lines.size() == 3);
    CHECK(read.lines[1].text == std::string("/C64Music") + kThreeSongs);
    for (const spz::ImportLine& entry : read.lines) CHECK(entry.subtune == 0);
}

TEST_CASE("SPEC-176 a .json file that is not an array of objects imports nothing, makes no playlist, and gives MSG_IMPORT_NOTHING [parts: playlists import-more]") {
    ImportWorld world;
    for (const char* name : {"not_a_list.json", "not_objects.json"}) {
        CAPTURE(name);
        const spz::ImportReport report = world.importer.import_file(world.put(name));
        CHECK(report.message == "MSG_IMPORT_NOTHING");
        CHECK(report.playlist == "");
        CHECK(report.entries == 0);
    }
    CHECK(spz::read_jsidplay2_json(bytes("not JSON at all")).message == "MSG_IMPORT_NOTHING");
    CHECK(spz::read_jsidplay2_json(bytes("")).message == "MSG_IMPORT_NOTHING");
    CHECK(world.playlists->list().size() == 1);                // Favourites alone
    CHECK_FALSE(exists(world.playlists_folder + "/not_a_list.m3u8"));
}

TEST_CASE("SPEC-177 Sidplay/w's two forms of .spl file, written as the specification's samples with the fixture tunes' names, import with the expected tunes and subtunes [parts: playlists import-more]") {
    ImportWorld world;
    // Format 1: numbered sections with Filename= and Subsong=, and a BaseDir= in front of the names.
    const spz::ImportReport one = world.importer.import_file(world.put("format1.spl"));
    CHECK(one.message == "");
    CHECK(one.playlist == "format1");
    CHECK(join(world.entries_of("format1")) == three(3) + std::string(kCommando) + "#1\n");
    CHECK(one.not_found.empty());
    // Format 2: blocks beginning with file=, with subtune= among them.
    const spz::ImportReport two = world.importer.import_file(world.put("format2.spl"));
    CHECK(two.message == "");
    CHECK(join(world.entries_of("format2")) == three(1) + std::string(kNewChip) + "#1\n");
    CHECK(two.not_found.empty());
}

TEST_CASE("SPEC-177 a .spl file's keys are read without regard to case, keys the app does not know are ignored, and BaseDir goes only in front of names that are not full paths [parts: playlists import-more]") {
    const spz::ImportRead sample = spz::read_spl(fixture("lists/import/format1.spl"));
    CHECK(sample.message == "");
    REQUIRE(sample.lines.size() == 2);                         // "Time=" made no entry
    CHECK(sample.lines[0].text == "C:\\c64music\\MUSICIANS\\T\\Tester_Tom\\Three_Songs.sid");
    CHECK(sample.lines[0].subtune == 3);
    CHECK(sample.lines[1].subtune == 1);

    const spz::ImportRead shouted = spz::read_spl(bytes(
        "[SIDPLAY/Windows Playlist]\r\nFORMAT=1\r\nBASEDIR=C:\\c64music\\\r\n"
        "[1]\r\nFILENAME=MUSICIANS\\T\\Tester_Tom\\Three_Songs.sid\r\nSUBSONG=3\r\nCOMMENT=nothing the app knows\r\n"
        "[2]\r\nfilename=D:\\Elsewhere\\C64Music\\MUSICIANS\\H\\Hubbard_Rob\\Commando.sid\r\nsubsong=1\r\n"));
    REQUIRE(shouted.lines.size() == 2);
    CHECK(shouted.lines[0].text == sample.lines[0].text);
    CHECK(shouted.lines[0].subtune == 3);
    CHECK(shouted.lines[1].text == "D:\\Elsewhere\\C64Music\\MUSICIANS\\H\\Hubbard_Rob\\Commando.sid");   // a full path
    CHECK(shouted.lines[1].subtune == 1);

    const spz::ImportRead two = spz::read_spl(fixture("lists/import/format2.spl"));
    REQUIRE(two.lines.size() == 2);                            // "name=" and "time=" made no entry
    CHECK(two.lines[0].subtune == 1);
    CHECK(two.lines[1].subtune == 1);
}

#endif  // SPZ_PART_IMPORT_MORE

#endif  // SPZ_PART_PLAYLISTS
