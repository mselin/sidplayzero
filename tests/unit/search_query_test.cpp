// SPDX-License-Identifier: GPL-3.0-or-later
// What a search matches (SPEC-162), and what the filters leave of it (SPEC-164, the part `filters`), on
// the fixture collection with three tunes more (search_support.h).
#include <cstdio>
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "search_support.h"
#include "spz/index.h"
#include "spz/search.h"
#include "support.h"

using spz::test::SearchRoot;
using spz::test::TestRoot;
using spz::test::joined;
using spz::test::kHuelsbeck;
using spz::test::kOwnTune;
using spz::test::kRob;
using spz::test::kTom;
using spz::test::paths_of;

namespace {

const std::string rob = kRob;
const std::string tom = kTom;
const char* const kClockDemo = "/DEMOS/A-F/Clock_Demo.sid";
const char* const kEitherChip = "/GAMES/A-F/Either_Chip.sid";
const char* const kNoYear = "/My Rips/No Year.sid";

// The fourteen tunes of SearchRoot, in order of path.
std::vector<std::string> all_fourteen() {
    return {kClockDemo, kEitherChip, rob + "Commando.sid", rob + "Year_Test.sid", kHuelsbeck, tom + "Changed.sid",
            tom + "Needs_BASIC.sid", tom + "New_Chip.sid", tom + "Renamed.sid", tom + "Three_Chips_3SID.sid",
            tom + "Three_Songs.sid", tom + "Two_Chips_2SID.sid", kOwnTune, kNoYear};
}

// `from` without the paths in `out`.
std::vector<std::string> without(std::vector<std::string> from, const std::vector<std::string>& out) {
    for (const std::string& path : out) from.erase(std::remove(from.begin(), from.end(), path), from.end());
    return from;
}

}  // namespace

TEST_CASE("SPEC-162 a query is treated like search text and split at spaces into words") {
    CHECK(joined(spz::query_words("hub 1985")) == "hub | 1985");
    CHECK(joined(spz::query_words("  HUB  ")) == "hub");
    CHECK(joined(spz::query_words("H\xC3\x9CLS")) == "huls");                   // "HÜLS"
    CHECK(joined(spz::query_words("Commando.sid")) == "commando | sid");
    CHECK(joined(spz::query_words("rob_h")) == "rob | h");
    CHECK(spz::query_words("").empty());
    CHECK(spz::query_words("   ").empty());
    CHECK(spz::query_words(" !? - ").empty());
}

TEST_CASE("SPEC-162 each query of a table gives exactly the expected tunes of the fixture collection, in order of path") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    REQUIRE(index.count() == 14);

    struct Row {
        const char* query;
        std::vector<std::string> expected;
    };
    const std::vector<Row> table = {
        {"hub 1985", {rob + "Year_Test.sid"}},                 // "hub" from its path, "1985" from its released line
        {"1985 hub", {rob + "Year_Test.sid"}},                 // the order of the words does not matter
        {"HUB", {rob + "Commando.sid", rob + "Year_Test.sid"}},
        {"huls", {kHuelsbeck}},                                // "Hülsbeck" in its title; its path says "Huelsbeck"
        {"H\xC3\x9CLS", {kHuelsbeck}},                         // "HÜLS"
        {"renamed", {tom + "Renamed.sid"}},                    // a word that only its path has
        {"sixty hertz", {kClockDemo}},                         // words that only its title has
        {"somebody", {kOwnTune}},                              // a word that only its author has
        {"second version", {tom + "Changed.sid"}},             // words that only its released line has
        {"1985", {rob + "Year_Test.sid", tom + "Changed.sid"}},
        {"cool song", {kOwnTune}},                             // "Cool Söng" in its path
        {"Commando.sid", {rob + "Commando.sid"}},
        {"Hubbard_R", {rob + "Commando.sid", rob + "Year_Test.sid"}},
        {"chip", {kEitherChip, tom + "New_Chip.sid", tom + "Three_Chips_3SID.sid", tom + "Two_Chips_2SID.sid"}},
        {"t tom th", {tom + "Three_Chips_3SID.sid", tom + "Three_Songs.sid"}},
        {"test", without(all_fourteen(), {kOwnTune})},         // in a path, a title or "Test Author"
        {"sid", all_fourteen()},
        {"ubbard", {}},                                        // the middle of a word is not its beginning
        {"hubbards", {}},                                      // and a longer word is not found in a shorter
        {"hub 1984", {}},                                      // every word must be found
        {"zzzz", {}},
    };
    for (const Row& row : table) {
        CAPTURE(row.query);
        const spz::SearchResults results = spz::search(index, row.query);
        CHECK(results.searched);
        CHECK(paths_of(results) == joined(row.expected));
        CHECK(results.total == static_cast<int>(row.expected.size()));
        CHECK_FALSE(results.cut_short);
    }
    CHECK(t.clock.sleeps == 0);                               // a search does not wait
}

TEST_CASE("SPEC-162 an absent tune is found by a word from its path and not by its title") {
    SearchRoot t;
    t.remove(kHuelsbeck);
    t.remove(kClockDemo);
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    REQUIRE(index.count() == 14);

    const spz::SearchResults by_path = spz::search(index, "huelsbeck");
    REQUIRE(paths_of(by_path) == kHuelsbeck);
    CHECK_FALSE(by_path.hits[0].present);
    CHECK(by_path.total == 1);
    CHECK(paths_of(spz::search(index, "huls")) == "");        // its title is not known
    CHECK(paths_of(spz::search(index, "thirty")) == "");      // nor its author
    CHECK(paths_of(spz::search(index, "clock demo")) == kClockDemo);
    CHECK(paths_of(spz::search(index, "sixty")) == "");

    // A present tune beside them is found by its title as before, and says that it is present.
    const spz::SearchResults present = spz::search(index, "commando test");
    REQUIRE(paths_of(present) == rob + "Commando.sid");
    CHECK(present.hits[0].present);
    // And the absent ones are among the results of a word that every path has.
    CHECK(paths_of(spz::search(index, "sid")) == joined(all_fourteen()));
}

TEST_CASE("SPEC-162 a query with no word gives no search") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    for (const char* query : {"", " ", "    ", "!?", " - . / "}) {
        CAPTURE(query);
        const spz::SearchResults results = spz::search(index, query);
        CHECK_FALSE(results.searched);
        CHECK(results.hits.empty());
        CHECK(results.total == 0);
    }
    CHECK(spz::search(index, "zzzz").searched);               // nothing found is not the same as nothing asked
}

TEST_CASE("SPEC-162 at most 500 results are returned, the first in order of path, and the count is the true number of matches") {
    TestRoot t;
    std::vector<std::string> paths;
    for (int n = 1; n <= 620; ++n) {
        char path[40];
        std::snprintf(path, sizeof path, "/MANY/Tune_%04d.sid", n);
        paths.push_back(path);
    }
    paths.push_back("/OTHER/Lone_One.sid");
    t.put_text("/DOCUMENTS/Songlengths.md5", spz::test::database_text(paths));   // HVSC lists them; none is on the device
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    REQUIRE(index.count() == 621);

    const spz::SearchResults many = spz::search(index, "tune");
    CHECK(many.searched);
    CHECK(many.total == 620);
    REQUIRE(many.hits.size() == 500);
    bool first_500_in_order = true;
    for (std::size_t i = 0; i < 500; ++i) first_500_in_order = first_500_in_order && many.hits[i].path == paths[i];
    CHECK(first_500_in_order);
    CHECK_FALSE(many.cut_short);

    const spz::SearchResults exactly = spz::search(index, "tune 05");   // Tune_0500 to Tune_0599
    CHECK(exactly.total == 100);
    REQUIRE(exactly.hits.size() == 100);
    CHECK(exactly.hits.front().path == "/MANY/Tune_0500.sid");
    CHECK(exactly.hits.back().path == "/MANY/Tune_0599.sid");

    const spz::SearchResults all = spz::search(index, "sid");
    CHECK(all.total == 621);
    CHECK(all.hits.size() == 500);
}

#if SPZ_PART_FILTERS

namespace {

// The paths a search for "sid", which every tune's path has, gives with a filter set.
std::vector<std::string> filtered(const spz::Index& index, const spz::SearchFilter& filter) {
    std::vector<std::string> paths;
    for (const spz::SearchHit& hit : spz::search(index, "sid", filter).hits) paths.push_back(hit.path);
    return paths;
}

}  // namespace

TEST_CASE("SPEC-164 with no filter set nothing is left out, and a filter knows whether any of its four lines is set [parts: filters]") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    spz::SearchFilter filter;
    CHECK_FALSE(filter.is_set());
    CHECK(joined(filtered(index, filter)) == joined(all_fourteen()));
    filter.clock = spz::C64Clock::Pal;
    CHECK(filter.is_set());
    filter = spz::SearchFilter();
    filter.model = spz::SidModel::Mos8580;
    CHECK(filter.is_set());
    filter = spz::SearchFilter();
    filter.first_year = 1982;
    CHECK(filter.is_set());
    filter = spz::SearchFilter();
    filter.last_year = 2026;
    CHECK(filter.is_set());
}

TEST_CASE("SPEC-164 the clock filter alone removes exactly the tunes of the other clock [parts: filters]") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    spz::SearchFilter ntsc;
    ntsc.clock = spz::C64Clock::Ntsc;
    CHECK(joined(filtered(index, ntsc)) == kClockDemo);
    CHECK(spz::search(index, "sid", ntsc).total == 1);

    spz::SearchFilter pal;
    pal.clock = spz::C64Clock::Pal;
    CHECK(joined(filtered(index, pal)) == joined(without(all_fourteen(), {kClockDemo})));
    CHECK(spz::search(index, "sid", pal).total == 13);
}

TEST_CASE("SPEC-164 the chip model filter alone removes exactly the tunes of the other chip model [parts: filters]") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    // One fixture tune is marked for either chip. The specification does not say which chip filter such
    // a tune fits, so it is left out of both comparisons.
    spz::SearchFilter new_chip;
    new_chip.model = spz::SidModel::Mos8580;
    CHECK(joined(without(filtered(index, new_chip), {kEitherChip})) == tom + "New_Chip.sid");

    spz::SearchFilter old_chip;
    old_chip.model = spz::SidModel::Mos6581;
    CHECK(joined(without(filtered(index, old_chip), {kEitherChip})) ==
          joined(without(all_fourteen(), {kEitherChip, tom + "New_Chip.sid"})));
}

TEST_CASE("SPEC-164 the year filters alone remove exactly the tunes outside the years, and a tune with no year fits only when both say any [parts: filters]") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);
    const std::vector<std::string> of_1985 = {rob + "Year_Test.sid", tom + "Changed.sid"};

    spz::SearchFilter from_1986;
    from_1986.first_year = 1986;
    CHECK(joined(filtered(index, from_1986)) == joined(without(all_fourteen(), {rob + "Year_Test.sid", tom + "Changed.sid", kNoYear})));

    spz::SearchFilter until_1986;
    until_1986.last_year = 1986;
    CHECK(joined(filtered(index, until_1986)) == joined(of_1985));

    spz::SearchFilter only_1985;
    only_1985.first_year = 1985;                                   // the ends of a range belong to it
    only_1985.last_year = 1985;
    CHECK(joined(filtered(index, only_1985)) == joined(of_1985));

    spz::SearchFilter only_1988;
    only_1988.first_year = 1988;
    only_1988.last_year = 1988;
    CHECK(joined(filtered(index, only_1988)) == kHuelsbeck);

    // The widest years there are still leave out the tune that has no year.
    spz::SearchFilter from_the_start;
    from_the_start.first_year = 1982;
    CHECK(joined(filtered(index, from_the_start)) == joined(without(all_fourteen(), {kNoYear})));
    spz::SearchFilter until_now;
    until_now.last_year = 2026;
    CHECK(joined(filtered(index, until_now)) == joined(without(all_fourteen(), {kNoYear})));
    // A filter on something else lets it through: its years are both "any".
    spz::SearchFilter pal;
    pal.clock = spz::C64Clock::Pal;
    const std::vector<std::string> with_pal = filtered(index, pal);
    CHECK(std::find(with_pal.begin(), with_pal.end(), kNoYear) != with_pal.end());
}

TEST_CASE("SPEC-164 all four filters together remove exactly the tunes that do not fit every one [parts: filters]") {
    SearchRoot t;
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    spz::SearchFilter filter;
    filter.model = spz::SidModel::Mos6581;
    filter.clock = spz::C64Clock::Pal;
    filter.first_year = 1984;
    filter.last_year = 1990;
    CHECK(joined(filtered(index, filter)) == joined({rob + "Year_Test.sid", kHuelsbeck, tom + "Changed.sid"}));
    CHECK(spz::search(index, "sid", filter).total == 3);

    // The filters and the query both count: of those three, the ones with a word that begins "hu".
    CHECK(paths_of(spz::search(index, "hu", filter)) == rob + "Year_Test.sid | " + kHuelsbeck);

    filter.clock = spz::C64Clock::Ntsc;                            // no tune of those years is an NTSC one
    CHECK(filtered(index, filter).empty());
    CHECK(spz::search(index, "sid", filter).searched);
}

TEST_CASE("SPEC-164 absent tunes, whose headers are not known, are left out while any filter is set [parts: filters]") {
    SearchRoot t;
    t.remove(tom + "Renamed.sid");
    spz::Index index(&t.files, &t.clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    CHECK(paths_of(spz::search(index, "renamed")) == tom + "Renamed.sid");       // with no filter it is found
    for (int line = 0; line < 4; ++line) {
        CAPTURE(line);
        spz::SearchFilter filter;
        if (line == 0) filter.model = spz::SidModel::Mos6581;
        if (line == 1) filter.clock = spz::C64Clock::Pal;
        if (line == 2) filter.first_year = 1982;
        if (line == 3) filter.last_year = 2026;
        CHECK(paths_of(spz::search(index, "renamed", filter)) == "");
        const std::vector<std::string> left = filtered(index, filter);
        CHECK(std::find(left.begin(), left.end(), tom + "Renamed.sid") == left.end());
        CHECK(std::find(left.begin(), left.end(), tom + "Changed.sid") != left.end());   // a present one stays
    }
}

#endif  // SPZ_PART_FILTERS
