// SPDX-License-Identifier: GPL-3.0-or-later
// What a folder of the collection shows: SPEC-041 (the entries, from the device and from HVSC's listing),
// SPEC-042 (their order), SPEC-047 (a collection that is not HVSC), SPEC-048 (a root with nothing in it).
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "spz/collection.h"
#include "spz/tune.h"
#include "support.h"

using spz::test::TestRoot;
using spz::test::database_text;
using spz::test::fixture;
using spz::test::names_of;

namespace {

const char* const kCommando = "tunes/commando_test.sid";
const char* const kTomsTunes = "Changed.sid | Needs_BASIC.sid | New_Chip.sid | Renamed.sid | Three_Chips_3SID.sid | "
                               "Three_Songs.sid | Two_Chips_2SID.sid";

}  // namespace

TEST_CASE("SPEC-041 a root of three tunes, a text file, a hidden file and DOCUMENTS, with a listing naming two of those tunes and two others, shows five tunes: three present, two absent") {
    TestRoot t;
    t.put(kCommando, "/Alpha.sid");
    t.put("tunes/two_sid.sid", "/Beta.sid");
    t.put("tunes/three_sid.sid", "/Gamma.sid");
    t.put_text("/notes.txt", "not a tune");
    t.put("tunes/model_8580.sid", "/.hidden.sid");
    t.put_text("/DOCUMENTS/Songlengths.md5", database_text({"/Alpha.sid", "/Beta.sid", "/Delta.sid", "/Epsilon.sid"}));
    const spz::Collection collection(&t.files, t.root, t.open_listing());
    CHECK(collection.has_listing());

    const spz::FolderListing root = collection.list("/");
    CHECK(root.notice == "");
    REQUIRE(names_of(root) == "Alpha.sid | Beta.sid | Delta.sid | Epsilon.sid | Gamma.sid");
    const bool present[5] = {true, true, false, false, true};
    for (std::size_t i = 0; i < 5; ++i) {
        CAPTURE(root.entries[i].name);
        CHECK_FALSE(root.entries[i].is_folder);
        CHECK(root.entries[i].present == present[i]);
        CHECK(root.entries[i].path == "/" + root.entries[i].name);
        CHECK(root.entries[i].shown == root.entries[i].name);
    }
    CHECK(t.files.count("list") >= 1);   // the folder was read through the seam (SPEC-021)
}

TEST_CASE("SPEC-041 the folders and tunes HVSC lists show beside what is on the device, and what both name shows once") {
    TestRoot t;
    t.copy_from("collections/85/C64Music/DOCUMENTS", "/DOCUMENTS");
    t.copy_from("collections/85/C64Music/MUSICIANS/H", "/MUSICIANS/H");   // of the tunes, only these are on the device
    const spz::Collection collection(&t.files, t.root, t.open_listing());

    const spz::FolderListing root = collection.list("/");
    REQUIRE(names_of(root) == "DEMOS | GAMES | MUSICIANS");
    for (const spz::FolderEntry& entry : root.entries) CHECK(entry.is_folder);
    CHECK(root.on_device);

    CHECK(names_of(collection.list("/MUSICIANS")) == "H | T");
    CHECK(names_of(collection.list("/MUSICIANS/H")) == "Hubbard_Rob | Huelsbeck_Chris");

    const spz::FolderListing rob = collection.list("/MUSICIANS/H/Hubbard_Rob");
    REQUIRE(names_of(rob) == "Commando.sid");
    CHECK(rob.entries[0].present);
    CHECK_FALSE(rob.entries[0].is_folder);
    CHECK(rob.entries[0].path == "/MUSICIANS/H/Hubbard_Rob/Commando.sid");

    // A folder that only HVSC has: every tune in it is absent, and that is no fault.
    const spz::FolderListing tom = collection.list("/MUSICIANS/T/Tester_Tom");
    REQUIRE(names_of(tom) == kTomsTunes);
    for (const spz::FolderEntry& entry : tom.entries) {
        CAPTURE(entry.name);
        CHECK_FALSE(entry.is_folder);
        CHECK_FALSE(entry.present);
        CHECK(entry.path == "/MUSICIANS/T/Tester_Tom/" + entry.name);
    }
    CHECK(tom.notice == "");
    CHECK_FALSE(tom.on_device);
    CHECK(names_of(collection.list("/DEMOS")) == "A-F");
}

TEST_CASE("SPEC-041 only folders and files ending in .sid in any case are entries; names starting with a dot are left out, and so is DOCUMENTS, but only the root's") {
    TestRoot t;
    t.put(kCommando, "/lower.sid");
    t.put(kCommando, "/UPPER.SID");
    t.put(kCommando, "/Mixed.SiD");
    t.put_text("/readme.txt", "words");
    t.put(kCommando, "/tune.sid.bak");
    t.put(kCommando, "/nosid");
    t.put(kCommando, "/.dot.sid");
    t.put(kCommando, "/.sid");
    t.put(kCommando, "/.dotfolder/Inside.sid");
    t.put(kCommando, "/DOCUMENTS/Stray.sid");
    t.put(kCommando, "/Folder/DOCUMENTS/Kept.sid");
    const spz::Collection collection(&t.files, t.root, nullptr);

    const spz::FolderListing root = collection.list("/");
    REQUIRE(names_of(root) == "Folder | lower.sid | Mixed.SiD | UPPER.SID");
    CHECK(root.entries[0].is_folder);
    for (std::size_t i = 1; i < 4; ++i) {
        CHECK_FALSE(root.entries[i].is_folder);
        CHECK(root.entries[i].present);
    }

    const spz::FolderListing folder = collection.list("/Folder");
    REQUIRE(names_of(folder) == "DOCUMENTS");
    CHECK(folder.entries[0].is_folder);
    CHECK(names_of(collection.list("/Folder/DOCUMENTS")) == "Kept.sid");
}

TEST_CASE("SPEC-041 a name that is not valid UTF-8 is shown with ? for each bad byte, still opens and plays, and has no collection path") {
    TestRoot t;
    t.put(kCommando, "/Bad\xFFName.sid");                 // a byte that is never valid
    t.put("tunes/two_sid.sid", "/Odd\xC3/In\x80side.sid");   // a first byte with nothing after it; a stray second byte
    const spz::Collection collection(&t.files, t.root, nullptr);

    const spz::FolderListing root = collection.list("/");
    REQUIRE(root.entries.size() == 2);
    const spz::FolderEntry& folder = root.entries[0];
    const spz::FolderEntry& tune = root.entries[1];
    CHECK(folder.is_folder);
    CHECK(folder.name == "Odd\xC3");
    CHECK(folder.shown == "Odd?");
    CHECK_FALSE(tune.is_folder);
    CHECK(tune.present);
    CHECK(tune.name == "Bad\xFFName.sid");
    CHECK(tune.shown == "Bad?Name.sid");
    CHECK(tune.path == "");                               // not a collection path (SPEC-154)

    // It still plays: its file is where the collection says it is.
    spz::Bytes bytes;
    REQUIRE(t.files.read(collection.file_path("/", tune.name), spz::kTuneSizeLimit, &bytes) == spz::FileStatus::Ok);
    CHECK(bytes == fixture(kCommando));

    // And the folder still opens.
    const spz::FolderListing inside = collection.list("/Odd\xC3");
    REQUIRE(inside.entries.size() == 1);
    CHECK(inside.entries[0].name == "In\x80side.sid");
    CHECK(inside.entries[0].shown == "In?side.sid");
    CHECK(inside.entries[0].present);
    CHECK(inside.entries[0].path == "");
    REQUIRE(t.files.read(collection.file_path("/Odd\xC3", inside.entries[0].name), spz::kTuneSizeLimit, &bytes) ==
            spz::FileStatus::Ok);
    CHECK(bytes == fixture("tunes/two_sid.sid"));
}

TEST_CASE("SPEC-041 a name is shown as it is when it is valid UTF-8, and with one ? for each byte that is not") {
    CHECK(spz::shown_name("Commando.sid") == "Commando.sid");
    CHECK(spz::shown_name("Cool S\xC3\xB6ng.sid") == "Cool S\xC3\xB6ng.sid");   // two bytes: o with two dots
    CHECK(spz::shown_name("\xE2\x82\xAC.sid") == "\xE2\x82\xAC.sid");           // three bytes: the euro sign
    CHECK(spz::shown_name("a\xFF\xFE" "b") == "a??b");
    CHECK(spz::shown_name("\x80") == "?");                                      // a second byte with no first
    CHECK(spz::shown_name("end\xC3") == "end?");                                // a first byte with no second
    CHECK(spz::shown_name("\xC3(") == "?(");                                    // a first byte, then a plain one
    CHECK(spz::shown_name("") == "");
}

TEST_CASE("SPEC-041 a folder that cannot be read shows no entries and the notice MSG_FOLDER_UNREADABLE") {
    TestRoot t;
    t.put(kCommando, "/Locked/Tune.sid");
    t.put(kCommando, "/Open/Tune.sid");
    spz::test::FilesWithLockedFolder files;
    files.locked = t.at("/Locked");
    const spz::Collection collection(&files, t.root, nullptr);

    const spz::FolderListing locked = collection.list("/Locked");
    CHECK(locked.entries.empty());
    CHECK(locked.notice == "MSG_FOLDER_UNREADABLE");

    // The folders around it are as they were.
    const spz::FolderListing root = collection.list("/");
    CHECK(names_of(root) == "Locked | Open");
    CHECK(root.notice == "");
    const spz::FolderListing open = collection.list("/Open");
    CHECK(names_of(open) == "Tune.sid");
    CHECK(open.notice == "");
}

TEST_CASE("SPEC-041 SPEC-154 a tune of the user's own in an HVSC root is listed as present, under a collection path with its space and its accent") {
    TestRoot t("collections/85/C64Music");
    t.copy_from("own", "");                                // "My Rips/Cool Söng.sid", which HVSC does not list
    const spz::Collection collection(&t.files, t.root, t.open_listing());

    CHECK(names_of(collection.list("/")) == "DEMOS | GAMES | MUSICIANS | My Rips");
    const spz::FolderListing rips = collection.list("/My Rips");
    REQUIRE(names_of(rips) == "Cool S\xC3\xB6ng.sid");
    CHECK(rips.entries[0].present);
    CHECK(rips.entries[0].shown == "Cool S\xC3\xB6ng.sid");
    CHECK(rips.entries[0].path == "/My Rips/Cool S\xC3\xB6ng.sid");

    // With everything of the fixture collection on the device, every tune HVSC lists is present.
    const spz::FolderListing tom = collection.list("/MUSICIANS/T/Tester_Tom");
    REQUIRE(names_of(tom) == kTomsTunes);
    for (const spz::FolderEntry& entry : tom.entries) CHECK(entry.present);
    CHECK(tom.on_device);
}

TEST_CASE("SPEC-042 the entries of a folder come out folders first, each group by name without regard to the case of ASCII letters, with ties broken by byte order") {
    TestRoot t;
    for (const char* folder : {"Zebra", "alpha", "Alpha", "beta", "10", "9", "\xC3\x89mile"}) {   // the last: "Émile"
        t.make_folder(std::string("/Here/") + folder);
    }
    for (const char* tune : {"b.sid", "B.sid", "a.sid", "A.SID", "aa.sid", "a-b.sid", "Zz.sid", "0.sid",
                             "\xC3\xBCnder.sid"}) {                                               // the last: "ünder.sid"
        t.put(kCommando, std::string("/Here/") + tune);
    }
    const spz::Collection collection(&t.files, t.root, nullptr);

    // Written down by hand. Digits come before letters; "Alpha" and "alpha" differ only in case, and "A"
    // is the smaller byte; "-" is a smaller byte than "."; a byte of a letter outside ASCII is larger than
    // any ASCII character.
    const spz::FolderListing here = collection.list("/Here");
    CHECK(names_of(here) == "10 | 9 | Alpha | alpha | beta | Zebra | \xC3\x89mile | "
                            "0.sid | a-b.sid | A.SID | a.sid | aa.sid | B.sid | b.sid | Zz.sid | \xC3\xBCnder.sid");
    REQUIRE(here.entries.size() == 16);
    for (std::size_t i = 0; i < 16; ++i) CHECK(here.entries[i].is_folder == (i < 7));
}

TEST_CASE("SPEC-042 one name comes before another by its letters without regard to their case, then by its bytes") {
    CHECK(spz::name_comes_before("alpha", "Beta"));
    CHECK(spz::name_comes_before("Alpha", "beta"));
    CHECK_FALSE(spz::name_comes_before("Beta", "alpha"));
    CHECK_FALSE(spz::name_comes_before("beta", "Alpha"));
    CHECK(spz::name_comes_before("Alpha", "alpha"));        // a tie but for case: plain byte order
    CHECK_FALSE(spz::name_comes_before("alpha", "Alpha"));
    CHECK(spz::name_comes_before("a.sid", "aa.sid"));
    CHECK(spz::name_comes_before("Tune", "Tune_2"));        // the shorter of two that start alike
    CHECK(spz::name_comes_before("10", "9"));               // by characters, not as numbers
    CHECK(spz::name_comes_before("Zebra", "\xC3\x89mile"));
    CHECK_FALSE(spz::name_comes_before("same", "same"));
}

TEST_CASE("SPEC-047 a root with three tunes and no DOCUMENTS folder has no listing, and lists three present tunes") {
    TestRoot t;
    t.put(kCommando, "/Commando.sid");
    t.put("tunes/three_subtunes.sid", "/Three_Songs.sid");
    t.put("tunes/two_sid.sid", "/Two_Chips.sid");
    const spz::Collection collection(&t.files, t.root, nullptr);

    CHECK_FALSE(collection.has_listing());
    CHECK(collection.has_entries());
    const spz::FolderListing root = collection.list("/");
    REQUIRE(names_of(root) == "Commando.sid | Three_Songs.sid | Two_Chips.sid");
    for (const spz::FolderEntry& entry : root.entries) {
        CAPTURE(entry.name);
        CHECK(entry.present);
        CHECK_FALSE(entry.is_folder);
        CHECK(entry.path == "/" + entry.name);
    }
}

TEST_CASE("SPEC-047 with no listing in the root, the browser shows the folder tree as it is on the device") {
    TestRoot t;
    t.put(kCommando, "/Games/Level_1.sid");
    t.put(kCommando, "/Games/Old/Title.sid");
    t.put(kCommando, "/Top.sid");
    t.make_folder("/Nothing_Yet");
    const spz::Collection collection(&t.files, t.root, nullptr);

    const spz::FolderListing root = collection.list("/");
    REQUIRE(names_of(root) == "Games | Nothing_Yet | Top.sid");
    CHECK(root.entries[0].is_folder);
    CHECK(root.entries[1].is_folder);
    CHECK(root.entries[2].present);
    CHECK(names_of(collection.list("/Games")) == "Old | Level_1.sid");
    const spz::FolderListing old = collection.list("/Games/Old");
    REQUIRE(names_of(old) == "Title.sid");
    CHECK(old.entries[0].present);
    CHECK(old.entries[0].path == "/Games/Old/Title.sid");
    const spz::FolderListing nothing = collection.list("/Nothing_Yet");
    CHECK(nothing.entries.empty());
    CHECK(nothing.notice == "");
    CHECK(nothing.on_device);
}

TEST_CASE("SPEC-048 a root holds no entry when it is empty or has only what the browser leaves out, and holds one as soon as it has a tune or a folder") {
    TestRoot t;
    const spz::Collection collection(&t.files, t.root, nullptr);
    CHECK_FALSE(collection.has_entries());

    t.put_text("/readme.txt", "copy your collection here");
    t.put(kCommando, "/.hidden.sid");
    t.put_text("/DOCUMENTS/STIL.txt", "a document, and no song length database");
    CHECK_FALSE(collection.has_entries());

    t.put(kCommando, "/Tune.sid");
    CHECK(collection.has_entries());
    t.remove("/Tune.sid");
    CHECK_FALSE(collection.has_entries());
    t.make_folder("/Folder");
    CHECK(collection.has_entries());
}

TEST_CASE("SPEC-048 a root that has HVSC's documents and no tune yet holds entries: what HVSC lists") {
    TestRoot t;
    t.copy_from("collections/85/C64Music/DOCUMENTS", "/DOCUMENTS");
    const spz::Collection collection(&t.files, t.root, t.open_listing());
    CHECK(collection.has_entries());
    CHECK(names_of(collection.list("/")) == "DEMOS | GAMES | MUSICIANS");
}
