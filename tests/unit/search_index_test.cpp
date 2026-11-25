// SPDX-License-Identifier: GPL-3.0-or-later
// The search index: SPEC-160 (what the file holds, and the search text) and SPEC-161 (how it is built
// and kept true), on collections small enough to write down. The collection of 61,157 tunes is in
// search_big_test.cpp.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "search_support.h"
#include "spz/index.h"
#include "spz/md5.h"
#include "spz/search.h"
#include "support.h"

using spz::test::SearchRoot;
using spz::test::TestRoot;
using spz::test::fixture;
using spz::test::has_word;
using spz::test::kHuelsbeck;
using spz::test::kOwnTune;
using spz::test::kRob;
using spz::test::kTom;
using spz::test::move_time_on;
using spz::test::operations;
using spz::test::paths_of;
using spz::test::sorted_words;

namespace {

// What the index must hold for a tune of the fixture collection: its file's header, as tools/make_fixtures.py
// writes it.
struct Expected {
    std::string path;
    const char* fixture_name;
    const char* title;
    const char* author;
    const char* released;
    spz::SidModel model;
    spz::C64Clock clock;
    int year;
};

std::vector<Expected> fixture_collection() {
    const char* const somebody = "Test Author";
    const char* const lately = "2026 SidPlayZero";
    const spz::SidModel old_chip = spz::SidModel::Mos6581;
    const spz::C64Clock pal = spz::C64Clock::Pal;
    const std::string tom = kTom;
    return {
        {"/DEMOS/A-F/Clock_Demo.sid", "tunes/clock_ntsc.sid", "Sixty Hertz", somebody, lately, old_chip, spz::C64Clock::Ntsc, 2026},
        {"/GAMES/A-F/Either_Chip.sid", "tunes/model_either.sid", "Either Chip", somebody, lately, spz::SidModel::FromTune, pal, 2026},
        {std::string(kRob) + "Commando.sid", "tunes/commando_test.sid", "Commando Test", somebody, lately, old_chip, pal, 2026},
        {kHuelsbeck, "tunes/huelsbeck_test.sid", "H\xC3\xBClsbeck T\xC3\xABst", "Thirty-two characters of author!",
         "1988 Thirty-two chars released.", old_chip, pal, 1988},
        {tom + "Changed.sid", "tunes/changed_85.sid", "Changed Between Releases", somebody, "1985 second version", old_chip, pal, 1985},
        {tom + "Needs_BASIC.sid", "tunes/needs_basic.sid", "Needs BASIC", somebody, lately, old_chip, pal, 2026},
        {tom + "New_Chip.sid", "tunes/model_8580.sid", "New Chip", somebody, lately, spz::SidModel::Mos8580, pal, 2026},
        {tom + "Renamed.sid", "tunes/moved.sid", "Moved Between Releases", somebody, lately, old_chip, pal, 2026},
        {tom + "Three_Chips_3SID.sid", "tunes/three_sid.sid", "Three Chips", somebody, lately, old_chip, pal, 2026},
        {tom + "Three_Songs.sid", "tunes/three_subtunes.sid", "Three Subtunes", somebody, lately, old_chip, pal, 2026},
        {tom + "Two_Chips_2SID.sid", "tunes/two_sid.sid", "Two Chips", somebody, lately, old_chip, pal, 2026},
    };
}

// The record of one tune. The test stops if the index has none.
spz::IndexRecord record_of(const spz::Index& index, const std::string& path) {
    spz::IndexRecord record;
    INFO("the record of " << path);
    REQUIRE(index.find(path, &record));
    return record;
}

}  // namespace

TEST_CASE("SPEC-160 an index built from the fixture collection and read back has one record per tune, with the fields of its file's header, and a tune of the user's own has its MD5") {
    TestRoot t("collections/85/C64Music");
    t.copy_from("own", "");                                         // "/My Rips/Cool Söng.sid"
    const spz::Listing* listing = t.open_listing();
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());
        REQUIRE(index.build(spz::IndexReason::Start).complete);
    }
    CHECK(std::filesystem::exists(t.index_file()));

    spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());   // read back, as after a restart
    REQUIRE(index.load());
    CHECK(index.count() == 12);
    for (const Expected& expected : fixture_collection()) {
        CAPTURE(expected.path);
        const spz::IndexRecord record = record_of(index, expected.path);
        spz::FileInfo on_device;
        REQUIRE(t.files.info(t.at(expected.path), &on_device) == spz::FileStatus::Ok);
        CHECK(record.path == expected.path);
        CHECK(record.present);
        CHECK(record.size == fixture(expected.fixture_name).size());
        CHECK(record.modified_s == on_device.modified_s);
        CHECK(record.title == expected.title);
        CHECK(record.author == expected.author);
        CHECK(record.released == expected.released);
        CHECK(record.model == expected.model);
        CHECK(record.clock == expected.clock);
        CHECK(record.year == expected.year);
        CHECK(record.md5 == "");                                   // HVSC lists it: the listing has its MD5
    }

    const spz::IndexRecord own = record_of(index, kOwnTune);
    CHECK(own.present);
    CHECK(own.title == "The User's Own");
    CHECK(own.author == "Somebody");
    CHECK(own.year == 2026);
    CHECK(own.md5 == spz::md5_hex(fixture("tunes/own_tune.sid")));
    CHECK(own.md5.size() == 32);
}

TEST_CASE("SPEC-160 the index's header holds its format number, the collection root, and the size and modification time of the song length database it was built from") {
    TestRoot t("collections/85/C64Music");
    const spz::Listing* listing = t.open_listing();
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());
        REQUIRE(index.build(spz::IndexReason::Start).complete);
    }
    spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());
    REQUIRE(index.load());
    spz::FileInfo database;
    REQUIRE(t.files.info(t.database(), &database) == spz::FileStatus::Ok);
    const spz::IndexHeader header = index.header();
    CHECK(header.format == spz::Index::kFormat);
    CHECK(header.root == t.root);
    CHECK(header.database_size == database.size);
    CHECK(header.database_modified_s == database.modified_s);
}

TEST_CASE("SPEC-160 the search text of /MUSICIANS/H/Hubbard_Rob/Commando.sid contains the words hubbard, rob and commando") {
    TestRoot t("collections/85/C64Music");
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    const spz::IndexRecord record = record_of(index, "/MUSICIANS/H/Hubbard_Rob/Commando.sid");
    for (const char* word : {"hubbard", "rob", "commando", "musicians", "test", "author", "2026", "sidplayzero"}) {
        CAPTURE(word);
        CHECK(has_word(record.search_text, word));
    }
    // Nothing but lower-case letters, digits and spaces is left in it.
    CHECK(record.search_text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789 ") == std::string::npos);
}

TEST_CASE("SPEC-160 search text is lower case, with accents taken off the letters and every character that is not a letter or a digit turned into a space") {
    CHECK(sorted_words(spz::search_text("/MUSICIANS/H/Hubbard_Rob/Commando.sid", "Commando Test", "Test Author",
                                        "2026 SidPlayZero")) ==
          "2026 | author | commando | commando | h | hubbard | musicians | rob | sid | sidplayzero | test | test");
    CHECK(sorted_words(spz::search_text("/My Rips/Cool S\xC3\xB6ng.sid", "", "", "")) == "cool | my | rips | sid | song");
    // "Hülsbeck Tëst", "François d'Été", "1988 (c) Rainbow-Arts!"
    CHECK(sorted_words(spz::search_text("/a.sid", "H\xC3\xBClsbeck T\xC3\xABst", "Fran\xC3\xA7ois d'\xC3\x89t\xC3\xA9",
                                        "1988 (c) Rainbow-Arts!")) ==
          "1988 | a | arts | c | d | ete | francois | hulsbeck | rainbow | sid | test");
    // "Škoda Žižka Mañana Ångström"
    CHECK(sorted_words(spz::search_text("/b.sid", "\xC5\xA0koda \xC5\xBDi\xC5\xBEka Ma\xC3\xB1" "ana \xC3\x85ngstr\xC3\xB6m", "", "")) ==
          "angstrom | b | manana | sid | skoda | zizka");
    // Digits stay, and a word is only cut where something is neither a letter nor a digit.
    CHECK(sorted_words(spz::search_text("/Three_Chips_3SID.sid", "", "", "")) == "3sid | chips | sid | three");
}

TEST_CASE("SPEC-160 an absent tune has a record with its path and nothing from a header") {
    TestRoot t("collections/85/C64Music");
    t.remove(kHuelsbeck);
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    CHECK(index.count() == 11);                                    // one record per tune, on the device or not
    const spz::IndexRecord record = record_of(index, kHuelsbeck);
    CHECK_FALSE(record.present);
    CHECK(record.size == 0);
    CHECK(record.modified_s == 0);
    CHECK(record.title == "");
    CHECK(record.author == "");
    CHECK(record.released == "");
    CHECK(record.year == 0);
    CHECK(record.md5 == "");
    CHECK(sorted_words(record.search_text) == "chris | h | huelsbeck | huelsbeck | musicians | sid | test");
}

TEST_CASE("SPEC-160 SPEC-047 with no listing in the root, every tune on the device has a record with its file's MD5") {
    TestRoot t;
    t.put("tunes/commando_test.sid", "/Commando.sid");
    t.put("tunes/three_subtunes.sid", "/More/Three_Songs.sid");
    t.put("tunes/own_tune.sid", "/My Rips/Cool S\xC3\xB6ng.sid");
    spz::Index index(&t.files, &t.clock, nullptr, t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    CHECK(index.count() == 3);
    CHECK(record_of(index, "/Commando.sid").md5 == spz::md5_hex(fixture("tunes/commando_test.sid")));
    CHECK(record_of(index, "/More/Three_Songs.sid").md5 == spz::md5_hex(fixture("tunes/three_subtunes.sid")));
    const spz::IndexRecord own = record_of(index, kOwnTune);
    CHECK(own.md5 == spz::md5_hex(fixture("tunes/own_tune.sid")));
    CHECK(own.present);
    CHECK(own.title == "The User's Own");
    const spz::IndexHeader header = index.header();
    CHECK(header.root == t.root);
    CHECK(header.database_size == 0);
    CHECK(header.database_modified_s == 0);
}

TEST_CASE("SPEC-161 step one gives every tune of the listing a record without opening any tune, which makes every path searchable") {
    TestRoot t;
    t.copy_from("collections/85/C64Music/DOCUMENTS", "/DOCUMENTS");   // HVSC is set up, and no tune is on the device
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    const spz::IndexRun run = index.build(spz::IndexReason::Start);
    REQUIRE(run.complete);
    CHECK(run.rebuilt);
    CHECK(index.complete());
    CHECK(index.percent() == 100);

    CHECK(index.count() == 11);
    CHECK(operations(t.files, "read", ".sid") == 0);
    CHECK(paths_of(spz::search(index, "commando")) == std::string(kRob) + "Commando.sid");
    CHECK(paths_of(spz::search(index, "tester three")) ==
          std::string(kTom) + "Three_Chips_3SID.sid | " + kTom + "Three_Songs.sid");
}

TEST_CASE("SPEC-161 a start with an index that fits reads no tune again; Rebuild the index reads them all; and only a build asked for by the user or made after set-up says MSG_INDEX_READY") {
    TestRoot t("collections/85/C64Music");
    const spz::Listing* listing = t.open_listing();
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());   // the first start: no index yet
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        CHECK(run.complete);
        CHECK(run.rebuilt);
        CHECK(run.notice == "");
        CHECK(operations(t.files, "read", ".sid") == 11);
    }
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());   // the next start: step two only
        const std::size_t from = t.files.log.size();
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        CHECK(run.complete);
        CHECK_FALSE(run.rebuilt);
        CHECK(run.notice == "");
        CHECK(operations(t.files, "read", ".sid", from) == 0);
        CHECK(operations(t.files, "list", "", from) >= 1);                       // it did walk the folders
        CHECK(index.count() == 11);
    }
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());   // "Rebuild the index"
        const std::size_t from = t.files.log.size();
        const spz::IndexRun run = index.build(spz::IndexReason::User);
        CHECK(run.complete);
        CHECK(run.rebuilt);
        CHECK(run.notice == "MSG_INDEX_READY");
        CHECK(operations(t.files, "read", ".sid", from) == 11);
        CHECK(index.count() == 11);
    }
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());   // after HVSC has been set up
        const spz::IndexRun run = index.build(spz::IndexReason::AfterSetup);
        CHECK(run.complete);
        CHECK(run.notice == "MSG_INDEX_READY");
    }
}

TEST_CASE("SPEC-161 the index is built again when the song length database is another one, and when the root is") {
    TestRoot t("collections/85/C64Music");
    {
        spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
        REQUIRE(index.build(spz::IndexReason::Start).complete);
        CHECK(index.count() == 11);
    }

    // HVSC is updated: the database now lists one tune more, at its end, so its size is another.
    const std::string added = std::string(kTom) + "Zz_Brand_New.sid";
    const spz::Bytes old_database = fixture("collections/85/C64Music/DOCUMENTS/Songlengths.md5");
    t.put_text("/DOCUMENTS/Songlengths.md5", std::string(old_database.begin(), old_database.end()) + "; " + added +
                                                 "\r\n" + spz::test::made_up_md5(99) + "=0:05\r\n");
    move_time_on(t.database(), 5);
    spz::Listing updated;
    REQUIRE(spz::Listing::build(&t.files, t.database(), t.cache + "/listing-updated.bin").ok);
    REQUIRE(updated.open(&t.files, t.cache + "/listing-updated.bin", t.database()));
    {
        spz::Index index(&t.files, &t.clock, &updated, t.root, t.index_file());
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        CHECK(run.complete);
        CHECK(run.rebuilt);
        CHECK(index.count() == 12);
        CHECK_FALSE(record_of(index, added).present);
        spz::FileInfo database;
        REQUIRE(t.files.info(t.database(), &database) == spz::FileStatus::Ok);
        CHECK(index.header().database_size == database.size);
    }

    // The collection setting is changed: the same index file, another root.
    TestRoot other;
    other.put("tunes/commando_test.sid", "/Solo.sid");
    {
        spz::Index index(&t.files, &t.clock, nullptr, other.root, t.index_file());
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        CHECK(run.complete);
        CHECK(run.rebuilt);
        CHECK(index.count() == 1);
        CHECK(record_of(index, "/Solo.sid").title == "Commando Test");
        spz::IndexRecord gone;
        CHECK_FALSE(index.find(std::string(kRob) + "Commando.sid", &gone));
        CHECK(index.header().root == other.root);
    }
}

TEST_CASE("SPEC-161 SPEC-202 an index file that is cut short, or is no index at all, is refused, thrown away and built again") {
    TestRoot t("collections/85/C64Music");
    const spz::Listing* listing = t.open_listing();
    {
        spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());
        REQUIRE(index.build(spz::IndexReason::Start).complete);
    }
    spz::Bytes whole;
    REQUIRE(t.files.read(t.index_file(), 64 * 1024 * 1024, &whole) == spz::FileStatus::Ok);
    spz::IndexHeader header;
    std::vector<spz::IndexRecord> records;
    REQUIRE(spz::read_index(whole, &header, &records));
    CHECK(records.size() == 11);
    CHECK(header.root == t.root);

    const spz::Bytes half(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(whole.size() / 2));
    CHECK_FALSE(spz::read_index(half, &header, &records));
    CHECK_FALSE(spz::read_index(spz::Bytes(whole.begin(), whole.end() - 1), &header, &records));   // one byte short
    CHECK_FALSE(spz::read_index(spz::test::bytes("this is not an index"), &header, &records));
    CHECK_FALSE(spz::read_index(spz::Bytes(), &header, &records));

    t.temp.write("cache/index.bin", half);
    spz::Index index(&t.files, &t.clock, listing, t.root, t.index_file());
    CHECK_FALSE(index.load());
    CHECK(index.count() == 0);
    const spz::IndexRun run = index.build(spz::IndexReason::Start);
    CHECK(run.complete);
    CHECK(run.rebuilt);
    CHECK(index.count() == 11);
    CHECK(record_of(index, std::string(kRob) + "Commando.sid").title == "Commando Test");
}

TEST_CASE("SPEC-161 a tune that is fetched is added at once, from its file alone") {
    TestRoot t("collections/85/C64Music");
    const std::string path = std::string(kRob) + "Commando.sid";
    t.remove(path);
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    CHECK_FALSE(record_of(index, path).present);
    CHECK(paths_of(spz::search(index, "commando")) == path);         // by its path,
    CHECK(paths_of(spz::search(index, "commando author")) == "");    // and not yet by what its header says

    t.put("tunes/commando_test.sid", path);                          // the fetch has put the file in its place
    const std::size_t from = t.files.log.size();
    index.tune_arrived(path);
    const spz::IndexRecord record = record_of(index, path);
    CHECK(record.present);
    CHECK(record.title == "Commando Test");
    CHECK(record.author == "Test Author");
    CHECK(paths_of(spz::search(index, "commando author")) == path);
    CHECK(operations(t.files, "read", ".sid", from) == 1);
    CHECK(operations(t.files, "list", "", from) == 0);
    CHECK(index.count() == 11);
}

TEST_CASE("SPEC-161 when the browser finds a folder changed, that folder alone is brought up to date") {
    SearchRoot t;
    t.put("tunes/model_6581.sid", "/My Rips/Old Chip.sid");
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    REQUIRE(index.count() == 15);

    t.remove("/My Rips/Old Chip.sid");
    t.put("tunes/clock_pal.sid", "/My Rips/Fifty.sid");               // "Fifty Hertz"
    t.put("tunes/cp1252_1.sid", "/DEMOS/A-F/Unseen.sid");             // in another folder: not looked at now
    const std::size_t from = t.files.log.size();
    index.refresh_folder("/My Rips");

    CHECK(record_of(index, "/My Rips/Fifty.sid").title == "Fifty Hertz");
    spz::IndexRecord gone;
    CHECK_FALSE(index.find("/My Rips/Old Chip.sid", &gone));
    CHECK(record_of(index, kOwnTune).present);
    CHECK_FALSE(index.find("/DEMOS/A-F/Unseen.sid", &gone));
    CHECK(index.count() == 15);
    CHECK(operations(t.files, "read", ".sid", from) == 1);            // only the new file
    CHECK(operations(t.files, "list", "", from) == 1);                // only that folder
    CHECK(paths_of(spz::search(index, "fifty hertz")) == "/My Rips/Fifty.sid");
}

TEST_CASE("SPEC-161 a build handles 200 files at a time, and pauses 100 ms between two batches while a tune is playing and not otherwise") {
    TestRoot t;
    for (int n = 1; n <= 450; ++n) {                                  // three batches: 200, 200 and 50
        char path[32];
        std::snprintf(path, sizeof path, "/MANY/Tune_%03d.sid", n);
        t.put("tunes/commando_test.sid", path);
    }
    {
        spz::Index index(&t.files, &t.clock, nullptr, t.root, t.cache + "/quiet.bin");
        REQUIRE(index.build(spz::IndexReason::Start).complete);
        CHECK(index.count() == 450);
        CHECK(t.clock.sleeps == 0);
        CHECK(t.clock.slept_ms == 0);
    }
    {
        spz::Index index(&t.files, &t.clock, nullptr, t.root, t.cache + "/playing.bin");
        index.set_playing(true);
        int asked = 0;
        REQUIRE(index.build(spz::IndexReason::Start, [&] { ++asked; return true; }).complete);
        CHECK(index.count() == 450);
        CHECK(t.clock.sleeps == 2);
        CHECK(t.clock.slept_ms == 200);
        CHECK(asked >= 2);                                            // after each batch that another follows
    }
}
