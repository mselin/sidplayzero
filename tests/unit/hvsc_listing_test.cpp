// SPDX-License-Identifier: GPL-3.0-or-later
// The listing (SPEC-142): HVSC's song length database, read by the app's own reader into listing.bin.
// Also the listing reader's side of SPEC-154: a path that is not an HVSC path never gets into it.
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "hvsc_support.h"
#include "spz/listing.h"
#include "support.h"

using namespace spz::test::hvsc;
using spz::ListedTune;
using spz::Listing;
using spz::ListingBuild;
using spz::Millis;
using spz::test::fixture;
using spz::test::TempFolder;

namespace {

// 32 hexadecimal digits that differ for every number: an MD5 for a line of a database made in a test.
std::string digits(int number) {
    char text[40];
    std::snprintf(text, sizeof text, "%032x", static_cast<unsigned>(number) + 0xabc000u);
    return text;
}

// The two lines of one tune, as HVSC writes them.
std::string tune_lines(const std::string& path, const std::string& md5, const std::string& lengths, const char* end = "\r\n") {
    return "; " + path + end + md5 + "=" + lengths + end;
}

// `count` tunes that are in order already: /GOOD/T0000.sid, /GOOD/T0001.sid, ... each 1:00 long.
std::string good_tunes(int count, const char* end = "\r\n") {
    std::string text;
    for (int i = 0; i < count; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "/GOOD/T%04d.sid", i);
        text += tune_lines(name, digits(i), "1:00", end);
    }
    return text;
}

// A database of a test's own, and where its listing goes.
struct Case {
    TempFolder temp;
    spz::fake::Files files;
    std::string database = temp.file("C64Music/DOCUMENTS/Songlengths.md5");
    std::string listing_file = temp.file("cache/listing.bin");

    Case() { fs::create_directories(temp.path / "cache"); }
    explicit Case(const std::string& text) : Case() { temp.write("C64Music/DOCUMENTS/Songlengths.md5", text); }

    ListingBuild build() { return Listing::build(&files, database, listing_file); }
    bool open(Listing* listing) { return listing->open(&files, listing_file, database); }
};

std::vector<std::string> paths_of(const std::vector<ListedTune>& tunes) {
    std::vector<std::string> paths;
    for (const ListedTune& tune : tunes) paths.push_back(tune.path);
    return paths;
}

std::string fixture_database_text() {
    const spz::Bytes data = fixture("collections/85/C64Music/DOCUMENTS/Songlengths.md5");
    return std::string(data.begin(), data.end());
}

std::string with_lf(std::string text) {
    std::string out;
    for (const char c : text) {
        if (c != '\r') out += c;
    }
    return out;
}

}  // namespace

TEST_CASE("SPEC-142 the listing built from the fixture database has one record for each tune, in order, with every length to the millisecond") {
    Case c(fixture_database_text());
    const ListingBuild built = c.build();
    REQUIRE(built.ok);
    CHECK(built.tunes == 11);
    CHECK(built.lengths == 13);
    CHECK(built.lines_skipped == 0);

    Listing listing;
    REQUIRE(c.open(&listing));
    CHECK(listing.is_open());
    CHECK(listing.tune_count() == 11);

    const std::vector<ListedTune> tunes = listing.tunes(0, 100);
    REQUIRE(tunes.size() == 11);
    CHECK(paths_of(tunes) == kTunes85);
    // The lengths the fixture's database gives, written here from tools/make_fixtures.py.
    const std::vector<std::vector<Millis>> lengths = {
        {2000}, {2000}, {3500}, {2000}, {2000}, {2000}, {2000}, {2000}, {2000}, {2000, 2500, 3000}, {2000},
    };
    const spz::Bytes database = fixture("collections/85/C64Music/DOCUMENTS/Songlengths.md5");
    for (std::size_t i = 0; i < tunes.size(); ++i) {
        CAPTURE(tunes[i].path);
        CHECK(tunes[i].lengths == lengths[i]);
        CHECK(tunes[i].md5 == md5_in_database(database, tunes[i].path));
        CHECK(tunes[i].md5.size() == 32);
    }

    // A window of the same list, for the index (SPEC-161).
    CHECK(paths_of(listing.tunes(3, 2)) == std::vector<std::string>{kTunes85[3], kTunes85[4]});
    CHECK(listing.tunes(11, 5).empty());
}

TEST_CASE("SPEC-142 a database with LF line endings gives the same listing as one with CRLF") {
    Case crlf(fixture_database_text());
    Case lf(with_lf(fixture_database_text()));
    REQUIRE(fixture_database_text().find("\r\n") != std::string::npos);
    const ListingBuild built_crlf = crlf.build(), built_lf = lf.build();
    REQUIRE(built_crlf.ok);
    REQUIRE(built_lf.ok);
    CHECK(built_lf.tunes == built_crlf.tunes);
    CHECK(built_lf.lengths == built_crlf.lengths);
    CHECK(built_lf.lines_skipped == 0);

    Listing a, b;
    REQUIRE(crlf.open(&a));
    REQUIRE(lf.open(&b));
    const std::vector<ListedTune> from_crlf = a.tunes(0, 100), from_lf = b.tunes(0, 100);
    REQUIRE(from_lf.size() == from_crlf.size());
    for (std::size_t i = 0; i < from_lf.size(); ++i) {
        CHECK(from_lf[i].path == from_crlf[i].path);
        CHECK(from_lf[i].md5 == from_crlf[i].md5);
        CHECK(from_lf[i].lengths == from_crlf[i].lengths);
    }
}

TEST_CASE("SPEC-142 SPEC-042 the listing is in the browser's order whatever the order of the file: folders before tunes, names without regard to case") {
    // Written down by hand: in the root the folders A and a (a tie without case, so plain byte order)
    // come before the tune b.sid; inside /A the folder B comes before the tunes b.sid and Z.sid.
    Case c("[Database]\r\n" +
           tune_lines("/b.sid", digits(1), "0:01") +
           tune_lines("/A/Z.sid", digits(2), "0:02") +
           tune_lines("/a/y.sid", digits(3), "0:03") +
           tune_lines("/A/B/c.sid", digits(4), "0:04") +
           tune_lines("/A/b.sid", digits(5), "0:05"));
    REQUIRE(c.build().ok);
    Listing listing;
    REQUIRE(c.open(&listing));
    CHECK(paths_of(listing.tunes(0, 10)) ==
          std::vector<std::string>{"/A/B/c.sid", "/A/b.sid", "/A/Z.sid", "/a/y.sid", "/b.sid"});

    std::vector<std::string> folders, tunes;
    REQUIRE(listing.folder("/", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"A", "a"});
    CHECK(tunes == std::vector<std::string>{"b.sid"});
    REQUIRE(listing.folder("/A", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"B"});
    CHECK(tunes == std::vector<std::string>{"b.sid", "Z.sid"});
}

TEST_CASE("SPEC-142 lengths are read as m:ss and as m:ss.SSS, one for each subtune, and held in milliseconds") {
    Case c("[Database]\r\n" +
           tune_lines("/LENGTHS/Forms.sid", digits(1), "3:55.594 1:01.288 0:00.001 59:59 0:05 120:00.250") +
           tune_lines("/LENGTHS/One.sid", "0123456789ABCDEF0123456789ABCDEF", "0:02"));
    const ListingBuild built = c.build();
    REQUIRE(built.ok);
    CHECK(built.tunes == 2);
    CHECK(built.lengths == 7);
    Listing listing;
    REQUIRE(c.open(&listing));
    ListedTune tune;
    REQUIRE(listing.by_path("/LENGTHS/Forms.sid", &tune));
    CHECK(tune.lengths == std::vector<Millis>{235594, 61288, 1, 3599000, 5000, 7200250});
    // 32 hexadecimal digits, in either case in the file; the listing gives them in lower case.
    REQUIRE(listing.by_path("/LENGTHS/One.sid", &tune));
    CHECK(tune.md5 == "0123456789abcdef0123456789abcdef");
    CHECK(tune.lengths == std::vector<Millis>{2000});
}

TEST_CASE("SPEC-142 a folder of the listing gives the folders and the tunes HVSC has there, each in order") {
    Case c(fixture_database_text());
    REQUIRE(c.build().ok);
    Listing listing;
    REQUIRE(c.open(&listing));
    std::vector<std::string> folders, tunes;

    REQUIRE(listing.folder("/", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"DEMOS", "GAMES", "MUSICIANS"});
    CHECK(tunes.empty());

    REQUIRE(listing.folder("/MUSICIANS", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"H", "T"});
    CHECK(tunes.empty());

    REQUIRE(listing.folder("/MUSICIANS/H", &folders, &tunes));
    CHECK(folders == std::vector<std::string>{"Hubbard_Rob", "Huelsbeck_Chris"});

    REQUIRE(listing.folder("/MUSICIANS/T/Tester_Tom", &folders, &tunes));
    CHECK(folders.empty());
    CHECK(tunes == std::vector<std::string>{"Changed.sid", "Needs_BASIC.sid", "New_Chip.sid", "Renamed.sid",
                                            "Three_Chips_3SID.sid", "Three_Songs.sid", "Two_Chips_2SID.sid"});

    CHECK_FALSE(listing.folder("/MUSICIANS/X", &folders, &tunes));
    CHECK_FALSE(listing.folder("/MUSICIANS/H/Hubbard_Rob/Commando.sid", &folders, &tunes));
}

TEST_CASE("SPEC-142 a tune is found by its path and by its MD5, and of two paths with one MD5 the first in order") {
    Case c("[Database]\r\n" +
           tune_lines("/B/Second.sid", digits(7), "0:09") +
           tune_lines("/A/First.sid", digits(7), "0:08") +
           tune_lines("/C/Other.sid", digits(8), "0:07 0:06"));
    REQUIRE(c.build().ok);
    Listing listing;
    REQUIRE(c.open(&listing));
    ListedTune tune;

    REQUIRE(listing.by_path("/C/Other.sid", &tune));
    CHECK(tune.path == "/C/Other.sid");
    CHECK(tune.md5 == digits(8));
    CHECK(tune.lengths == std::vector<Millis>{7000, 6000});
    CHECK_FALSE(listing.by_path("/C/Missing.sid", &tune));
    CHECK_FALSE(listing.by_path("/c/other.sid", &tune));     // a path is compared as it is written

    REQUIRE(listing.by_md5(digits(8), &tune));
    CHECK(tune.path == "/C/Other.sid");
    REQUIRE(listing.by_md5(digits(7), &tune));
    CHECK(tune.path == "/A/First.sid");
    CHECK(tune.lengths == std::vector<Millis>{8000});
    CHECK_FALSE(listing.by_md5(digits(9), &tune));
}

TEST_CASE("SPEC-142 a database with one bad line in 200 is used, and one with two in 100 is refused") {
    // 200 lines: the first line, 99 tunes, and one line that fits neither form.
    Case one_in_200("[Database]\r\n" + good_tunes(50) + "this line is neither a path nor an MD5\r\n" + good_tunes(99).substr(good_tunes(50).size()));
    const ListingBuild used = one_in_200.build();
    CHECK(used.ok);
    CHECK(used.tunes == 99);
    CHECK(used.lines_skipped == 1);
    Listing listing;
    CHECK(one_in_200.open(&listing));
    CHECK(listing.tune_count() == 99);

    // Two bad lines among 99 lines, and among 101: on either side of "two in 100".
    for (const int tunes : {48, 49}) {
        CAPTURE(tunes);
        Case two_in_100("[Database]\r\n" + good_tunes(tunes) + "neither one thing\r\nnor the other\r\n");
        const ListingBuild refused = two_in_100.build();
        CHECK_FALSE(refused.ok);
        CHECK(refused.lines_skipped == 2);
        // There is no listing.
        CHECK_FALSE(exists(two_in_100.listing_file));
        Listing none;
        CHECK_FALSE(two_in_100.open(&none));
        CHECK_FALSE(none.is_open());
    }
}

TEST_CASE("SPEC-142 a database in which no tune is found is refused") {
    for (const char* text : {"[Database]\r\n", "", "[Database]\n; /MUSICIANS/A/Only_A_Path.sid\n"}) {
        CAPTURE(text);
        Case c{std::string(text)};
        const ListingBuild built = c.build();
        CHECK_FALSE(built.ok);
        CHECK(built.tunes == 0);
        CHECK_FALSE(exists(c.listing_file));
    }
    // And when the database is not there at all.
    Case missing;
    CHECK_FALSE(missing.build().ok);
    CHECK_FALSE(exists(missing.listing_file));
}

TEST_CASE("SPEC-142 SPEC-154 a line whose path is not an HVSC path is skipped and counted, and never gets into the listing") {
    // SPEC-154's table of paths, each with an MD5 line of its own, among a thousand good tunes, so that
    // the lines skipped stay far under one in a hundred however they are counted.
    const std::vector<std::string> refused = {
        "/MUSICIANS/../Up.sid",                              // a ".." part
        "/MUSICIANS//Doubled.sid",                           // a doubled slash
        "/MUSICIANS\\Backslash.sid",                         // a backslash
        std::string("/MUSICIANS/Control\x01") + ".sid",      // a control character
        "/" + std::string(251, 'a') + ".sid",                // 256 characters
        "/MUSICIANS/No_Ending.txt",                          // no .sid
        "/My Rips/With Space.sid",                           // a space: fine for a collection path, not for HVSC's
    };
    std::string text = "[Database]\r\n" + good_tunes(500);
    for (std::size_t i = 0; i < refused.size(); ++i) text += tune_lines(refused[i], digits(5000 + static_cast<int>(i)), "0:09");
    text += good_tunes(1000).substr(good_tunes(500).size());

    Case c(text);
    const ListingBuild built = c.build();
    REQUIRE(built.ok);
    CHECK(built.tunes == 1000);
    CHECK(built.lengths == 1000);
    CHECK(built.lines_skipped >= 7);       // each path line; an MD5 line left without a path may count too
    CHECK(built.lines_skipped <= 14);

    Listing listing;
    REQUIRE(c.open(&listing));
    CHECK(listing.tune_count() == 1000);
    const std::vector<ListedTune> tunes = listing.tunes(0, 5000);
    REQUIRE(tunes.size() == 1000);
    for (const ListedTune& tune : tunes) {
        if (!starts_with(tune.path, "/GOOD/T")) FAIL_CHECK("in the listing: " << tune.path);
        if (tune.lengths != std::vector<Millis>{60000}) FAIL_CHECK("lengths of another line at " << tune.path);
    }
    ListedTune tune;
    for (std::size_t i = 0; i < refused.size(); ++i) {
        CAPTURE(refused[i]);
        CHECK_FALSE(listing.by_path(refused[i], &tune));
        CHECK_FALSE(listing.by_md5(digits(5000 + static_cast<int>(i)), &tune));
    }
    std::vector<std::string> folders, names;
    REQUIRE(listing.folder("/", &folders, &names));
    CHECK(folders == std::vector<std::string>{"GOOD"});

    // The longest HVSC path, 255 characters, is listed.
    const std::string longest = "/" + std::string(250, 'a') + ".sid";
    Case fits("[Database]\r\n" + tune_lines(longest, digits(1), "0:01"));
    REQUIRE(fits.build().ok);
    Listing other;
    REQUIRE(fits.open(&other));
    CHECK(other.by_path(longest, &tune));
}

TEST_CASE("SPEC-142 an MD5 line with no path before it is skipped and counted") {
    Case c("[Database]\r\n" + digits(900) + "=0:09\r\n" + good_tunes(200));
    const ListingBuild built = c.build();
    REQUIRE(built.ok);
    CHECK(built.tunes == 200);
    CHECK(built.lines_skipped == 1);
    Listing listing;
    REQUIRE(c.open(&listing));
    ListedTune tune;
    CHECK_FALSE(listing.by_md5(digits(900), &tune));
}

TEST_CASE("SPEC-142 the listing is built again when the database's size or modification time differs from what it records") {
    Case c("[Database]\r\n" + good_tunes(3));
    REQUIRE(c.build().ok);
    Listing listing;
    REQUIRE(c.open(&listing));
    CHECK(listing.tune_count() == 3);

    // Another size: the listing no longer fits its database, until it is built again.
    c.temp.write("C64Music/DOCUMENTS/Songlengths.md5", "[Database]\r\n" + good_tunes(4));
    Listing stale;
    CHECK_FALSE(c.open(&stale));
    REQUIRE(c.build().ok);
    Listing fresh;
    REQUIRE(c.open(&fresh));
    CHECK(fresh.tune_count() == 4);

    // The same size at another time.
    fs::last_write_time(c.database, fs::last_write_time(c.database) + std::chrono::seconds(10));
    Listing later;
    CHECK_FALSE(c.open(&later));
    REQUIRE(c.build().ok);
    CHECK(c.open(&later));
}

TEST_CASE("SPEC-142 SPEC-202 a listing file that is cut short, or is not a listing, does not open") {
    Case c("[Database]\r\n" + good_tunes(20));
    REQUIRE(c.build().ok);
    const spz::Bytes whole = read_file(c.listing_file);
    REQUIRE(whole.size() > 8);

    write_file(c.listing_file, spz::Bytes(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(whole.size() / 2)));
    Listing cut;
    CHECK_FALSE(c.open(&cut));

    write_file(c.listing_file, spz::test::bytes("this is not a listing at all, only some words in a file"));
    Listing words;
    CHECK_FALSE(c.open(&words));

    fs::remove(c.listing_file);
    Listing gone;
    CHECK_FALSE(c.open(&gone));

    write_file(c.listing_file, whole);
    Listing again;
    CHECK(c.open(&again));
}

TEST_CASE("SPEC-142 SPEC-132 SPEC-154 the listing is written through a temporary file beside it, and nothing is written anywhere else") {
    Case c(fixture_database_text());
    REQUIRE(c.build().ok);
    CHECK(c.files.calls > 0);                                  // it went through the seam (SPEC-021)

    const std::string temporary = temporary_of(c.listing_file);
    int written = -1, synced = -1, renamed = -1, folder_synced = -1;
    for (std::size_t i = 0; i < c.files.log.size(); ++i) {
        const auto& op = c.files.log[i];
        const int at = static_cast<int>(i);
        if (op.what == "write" && op.path == temporary && written < 0) written = at;
        if (op.what == "sync" && op.path == temporary) synced = at;
        if (op.what == "rename" && op.path == temporary && op.to == c.listing_file) renamed = at;
        if (op.what == "sync" && op.path == c.temp.file("cache")) folder_synced = at;
    }
    CHECK(written >= 0);
    CHECK(synced > written);
    CHECK(renamed > synced);
    CHECK(folder_synced > renamed);
    CHECK_FALSE(exists(temporary));

    // Nothing is created, changed or removed but the listing and its temporary file, in the cache folder.
    for (const auto& op : changes(c.files)) {
        CAPTURE(op.what);
        CAPTURE(op.path);
        const bool makes_the_cache = op.what == "make_folders" && op.path == c.temp.file("cache");
        CHECK((op.path == temporary || op.path == c.listing_file || makes_the_cache));
    }
}
