// SPDX-License-Identifier: GPL-3.0-or-later
// The index and the search on a collection of HVSC's size (SPEC-161, SPEC-162): 61,157 tunes in 1,981
// folders, made by this test in a temp folder (SPEC-252). It is made once for the whole run, and so is
// one complete index of it; each test then works in a cache folder of its own.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

using spz::test::TempFolder;
using spz::test::TestRoot;
using spz::test::ends_with;
using spz::test::fixture;
using spz::test::paths_of;

namespace {

const int kTunes = 61157;

// The collection. Its shape, like HVSC #85's: 61,157 tunes in 1,981 folders, one folder with 1,298 tunes
// and one with 207 folders.
//
//   /DEMOS/UNKNOWN                         1,298 tunes
//   /GAMES/A-F, G-L, M-R, S-Z              500 tunes each
//   /MUSICIANS/A ... /MUSICIANS/Z          26 folders, with 1,947 folders of authors between them: 207 in
//                                          S, 70 in each of the first fifteen other letters and 69 in each
//                                          of the last ten
//   /MUSICIANS/x/Author_x_nnn              30 tunes in each of the first 1,396, and 29 in each of the rest
//
// which is 3 + 1 + 4 + 26 + 1,947 = 1,981 folders, and 1,298 + 2,000 + 41,880 + 15,979 = 61,157 tunes.
// Tune number n, counted from 0 in order of path, is the file S<n>.sid with the title "Title N<n>", each
// number written with five digits: so the word "s00042" finds a tune by its path, and "n00042" only once
// its header has been read. Every file is the fixture "Commando Test" with another title.
struct BigCollection {
    TestRoot t;
    std::vector<std::string> paths;   // every tune, in order of path

    BigCollection() {
        std::vector<std::pair<std::string, int>> folders;   // a folder and how many tunes it has
        folders.push_back({"/DEMOS/UNKNOWN", 1298});
        for (const char* part : {"A-F", "G-L", "M-R", "S-Z"}) folders.push_back({std::string("/GAMES/") + part, 500});
        int author = 0, other_letters = 0;
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            const int authors = letter == 'S' ? 207 : (other_letters++ < 15 ? 70 : 69);
            for (int n = 1; n <= authors; ++n, ++author) {
                char name[64];
                std::snprintf(name, sizeof name, "/MUSICIANS/%c/Author_%c_%03d", letter, letter, n);
                folders.push_back({name, author < 1396 ? 30 : 29});
            }
        }
        REQUIRE(folders.size() == 1 + 4 + 1947);

        // The song length database first, and its listing: the listing does not need the tunes.
        std::string database = "[Database]\n";
        for (const auto& folder : folders) {
            for (int i = 0; i < folder.second; ++i) {
                const int number = static_cast<int>(paths.size());
                char name[16];
                std::snprintf(name, sizeof name, "/S%05d.sid", number);
                paths.push_back(folder.first + name);
                database += "; " + paths.back() + "\n" + spz::test::made_up_md5(number) + "=0:30\n";
            }
        }
        REQUIRE(paths.size() == static_cast<std::size_t>(kTunes));
        t.put_text("/DOCUMENTS/Songlengths.md5", database);
        t.open_listing();

        // Then the tunes themselves.
        const spz::Bytes tune = fixture("tunes/commando_test.sid");
        for (const auto& folder : folders) std::filesystem::create_directories(t.at(folder.first));
        for (int number = 0; number < kTunes; ++number) {
            spz::Bytes bytes = tune;
            const std::string title = title_of(number);
            std::fill(bytes.begin() + 0x16, bytes.begin() + 0x36, std::uint8_t{0});   // the title's 32 bytes
            std::copy(title.begin(), title.end(), bytes.begin() + 0x16);
            std::ofstream out(t.at(paths[static_cast<std::size_t>(number)]), std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
    }

    static std::string title_of(int number) {
        char title[32];
        std::snprintf(title, sizeof title, "Title N%05d", number);
        return title;
    }
    // The word that only the title of tune `number` has, and the word that its path has.
    static std::string title_word(int number) {
        char word[16];
        std::snprintf(word, sizeof word, "n%05d", number);
        return word;
    }
    static std::string path_word(int number) {
        char word[16];
        std::snprintf(word, sizeof word, "s%05d", number);
        return word;
    }
};

BigCollection& big() {
    static BigCollection collection;
    return collection;
}

// A complete index of the big collection, built in one go: the uninterrupted build.
const std::string& complete_index() {
    static const std::string file = [] {
        BigCollection& all = big();
        const std::string path = all.t.cache + "/complete-index.bin";
        spz::fake::Files files;
        spz::fake::Clock clock;
        spz::Index index(&files, &clock, &all.t.listing, all.t.root, path);
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        REQUIRE(run.complete);
        REQUIRE(index.count() == kTunes);
        return path;
    }();
    return file;
}

// A fake file system that counts the tunes read through it, and remembers which.
struct CountingFiles : spz::fake::Files {
    int tunes_read = 0;
    std::vector<std::string> read_paths;
    CountingFiles() {
        before = [this](const Operation& op) {
            if (op.what == "read" && ends_with(op.path, ".sid")) {
                ++tunes_read;
                read_paths.push_back(op.path);
            }
        };
    }
};

// The number in a tune's file name: 42 for ".../S00042.sid".
int number_of(const std::string& path) {
    return std::stoi(path.substr(path.size() - 9, 5));
}

// The first place where two lists of records differ, or -1 if they are the same.
int first_difference(const std::vector<spz::IndexRecord>& a, const std::vector<spz::IndexRecord>& b) {
    if (a.size() != b.size()) return static_cast<int>(std::min(a.size(), b.size()));
    for (std::size_t i = 0; i < a.size(); ++i) {
        const spz::IndexRecord& x = a[i];
        const spz::IndexRecord& y = b[i];
        if (x.path != y.path || x.present != y.present || x.size != y.size || x.modified_s != y.modified_s ||
            x.title != y.title || x.author != y.author || x.released != y.released || x.model != y.model ||
            x.clock != y.clock || x.year != y.year || x.md5 != y.md5 || x.search_text != y.search_text) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// A clock on which every look at the time finds that `step` ms have passed since the last look.
struct RunningClock : spz::Clock {
    std::int64_t step = 0;
    mutable std::int64_t now = 0;
    std::int64_t now_ms() const override { now += step; return now; }
    std::int64_t wall_time_s() const override { return 1795564800 + now / 1000; }
    void sleep_ms(std::int64_t ms) override { now += ms; }
    TimerId call_after(std::int64_t, std::function<void()>) override { return 0; }
    void cancel(TimerId) override {}
};

}  // namespace

TEST_CASE("SPEC-161 a collection of 61,157 tunes: the build completes; interrupted after 10,000 tunes and started again it reads only the remaining files and equals an uninterrupted build") {
    BigCollection& all = big();
    spz::fake::Clock clock;

    // The uninterrupted build: complete, with a record for every tune and every header read.
    spz::fake::Files plain_files;
    spz::Index whole(&plain_files, &clock, &all.t.listing, all.t.root, complete_index());
    REQUIRE(whole.load());
    REQUIRE(whole.count() == kTunes);
    const std::vector<spz::IndexRecord> whole_records = whole.records(0, kTunes);
    REQUIRE(whole_records.size() == static_cast<std::size_t>(kTunes));
    CHECK(whole_records.front().path == "/DEMOS/UNKNOWN/S00000.sid");
    CHECK(whole_records.front().title == "Title N00000");
    CHECK(whole_records.back().path == "/MUSICIANS/Z/Author_Z_069/S61156.sid");
    CHECK(whole_records.back().title == "Title N61156");
    CHECK(std::all_of(whole_records.begin(), whole_records.end(), [](const spz::IndexRecord& r) { return r.present; }));

    // The interrupted one, in a cache folder of its own.
    TempFolder cache;
    const std::string file = cache.file("index.bin");
    CountingFiles files;
    {
        spz::Index index(&files, &clock, &all.t.listing, all.t.root, file);
        const spz::IndexRun run = index.build(spz::IndexReason::Start, [&] { return files.tunes_read < 10000; });
        CHECK_FALSE(run.complete);
        CHECK(run.notice == "");
        CHECK(files.tunes_read == 10000);
        CHECK_FALSE(index.complete());
        CHECK(index.percent() > 0);
        CHECK(index.percent() < 100);
        CHECK(index.count() == kTunes);                    // step one gave every tune its record

        // A search during the build: a tune whose header has been read is found by its title; one that
        // has not been read yet is found by its path, as every tune is, and not by its title.
        REQUIRE(files.read_paths.size() == 10000);
        std::vector<bool> was_read(static_cast<std::size_t>(kTunes), false);
        for (const std::string& path : files.read_paths) was_read[static_cast<std::size_t>(number_of(path))] = true;
        const int done = number_of(files.read_paths.front());
        const int waiting = static_cast<int>(std::find(was_read.begin(), was_read.end(), false) - was_read.begin());
        REQUIRE(waiting < kTunes);
        CHECK(paths_of(spz::search(index, BigCollection::title_word(done))) == all.paths[static_cast<std::size_t>(done)]);
        CHECK(paths_of(spz::search(index, BigCollection::title_word(waiting))) == "");
        CHECK(paths_of(spz::search(index, BigCollection::path_word(waiting))) == all.paths[static_cast<std::size_t>(waiting)]);
    }

    // Started again, as after the app was stopped: it carries on, and reads only what it had not read.
    files.tunes_read = 0;
    spz::Index index(&files, &clock, &all.t.listing, all.t.root, file);
    const spz::IndexRun run = index.build(spz::IndexReason::Start);
    REQUIRE(run.complete);
    CHECK(files.tunes_read == kTunes - 10000);
    CHECK(index.complete());
    CHECK(index.percent() == 100);
    REQUIRE(index.count() == kTunes);
    const int differs_at = first_difference(index.records(0, kTunes), whole_records);
    CAPTURE(differs_at);
    CHECK(differs_at == -1);
    CHECK(clock.sleeps == 0);                              // no tune was playing: no pauses
}

TEST_CASE("SPEC-161 in the collection of 61,157 tunes, a tune added to the root, changed, and removed is found, updated and gone after the next start's step two, with only that file read") {
    BigCollection& all = big();
    spz::fake::Clock clock;
    TempFolder cache;
    const std::string file = cache.file("index.bin");
    std::filesystem::copy_file(complete_index(), file);

    const std::string path = "/Extra.sid";                 // a tune of the user's own, in the root itself
    struct Tidy {                                          // the collection is shared: leave it as it was
        std::string added;
        ~Tidy() { std::error_code ec; std::filesystem::remove(added, ec); }
    } tidy{all.t.at(path)};

    // Added.
    all.t.put("tunes/own_tune.sid", path);
    {
        CountingFiles files;
        spz::Index index(&files, &clock, &all.t.listing, all.t.root, file);
        const spz::IndexRun run = index.build(spz::IndexReason::Start);
        REQUIRE(run.complete);
        CHECK_FALSE(run.rebuilt);
        CHECK(files.tunes_read == 1);
        REQUIRE(files.read_paths.size() == 1);
        CHECK(spz::test::same_place(files.read_paths[0], all.t.at(path)));
        CHECK(index.count() == kTunes + 1);
        CHECK(paths_of(spz::search(index, "somebody")) == path);          // found, by its author
        spz::IndexRecord record;
        REQUIRE(index.find(path, &record));
        CHECK(record.present);
        CHECK(record.title == "The User's Own");
        CHECK(record.md5 == spz::md5_hex(fixture("tunes/own_tune.sid")));
    }

    // Changed: other bytes of the same size, and a later time.
    all.t.put("tunes/browse_year_1985.sid", path);
    spz::test::move_time_on(all.t.at(path), 5);
    {
        CountingFiles files;
        spz::Index index(&files, &clock, &all.t.listing, all.t.root, file);
        REQUIRE(index.build(spz::IndexReason::Start).complete);
        CHECK(files.tunes_read == 1);
        CHECK(index.count() == kTunes + 1);
        spz::IndexRecord record;
        REQUIRE(index.find(path, &record));
        CHECK(record.title == "Year Test");
        CHECK(record.year == 1985);
        CHECK(record.md5 == spz::md5_hex(fixture("tunes/browse_year_1985.sid")));
        CHECK(paths_of(spz::search(index, "somebody")) == "");
        CHECK(paths_of(spz::search(index, "1985")) == path);
    }

    // Removed.
    all.t.remove(path);
    {
        CountingFiles files;
        spz::Index index(&files, &clock, &all.t.listing, all.t.root, file);
        REQUIRE(index.build(spz::IndexReason::Start).complete);
        CHECK(files.tunes_read == 0);
        CHECK(index.count() == kTunes);
        spz::IndexRecord record;
        CHECK_FALSE(index.find(path, &record));
        CHECK(paths_of(spz::search(index, "1985")) == "");
        CHECK(paths_of(spz::search(index, "extra")) == "");
    }
}

TEST_CASE("SPEC-161 changing the root rebuilds the index: nothing of the collection of 61,157 tunes is left in it") {
    BigCollection& all = big();
    spz::fake::Clock clock;
    TempFolder cache;
    const std::string file = cache.file("index.bin");
    std::filesystem::copy_file(complete_index(), file);

    TestRoot other("collections/85/C64Music");             // the setting `collection` now names this one
    CountingFiles files;
    spz::Index index(&files, &clock, other.open_listing(), other.root, file);
    const spz::IndexRun run = index.build(spz::IndexReason::Start);
    REQUIRE(run.complete);
    CHECK(run.rebuilt);
    CHECK(files.tunes_read == 11);
    CHECK(index.count() == 11);
    CHECK(index.header().root == other.root);
    spz::IndexRecord record;
    CHECK_FALSE(index.find(all.paths.front(), &record));
    CHECK(index.find("/MUSICIANS/H/Hubbard_Rob/Commando.sid", &record));
    CHECK(paths_of(spz::search(index, BigCollection::path_word(0))) == "");
}

TEST_CASE("SPEC-162 a search is one pass over the index, and one that runs past 2 s returns what it has by then") {
    BigCollection& all = big();
    RunningClock clock;
    spz::fake::Files files;
    spz::Index index(&files, &clock, &all.t.listing, all.t.root, complete_index());
    REQUIRE(index.load());

    // In time: every path has the word "sid". The count is the true one, and 500 are returned.
    clock.step = 0;
    const spz::SearchResults in_time = spz::search(index, "sid");
    CHECK(in_time.searched);
    CHECK_FALSE(in_time.cut_short);
    CHECK(in_time.total == kTunes);
    REQUIRE(in_time.hits.size() == 500);
    CHECK(in_time.hits.front().path == all.paths.front());
    CHECK(in_time.hits.back().path == all.paths[499]);
    CHECK(paths_of(spz::search(index, BigCollection::title_word(61156) + " author")) == all.paths.back());

    // Out of time: on this clock three seconds pass between any two looks at it.
    clock.step = 3000;
    const spz::SearchResults late = spz::search(index, "sid");
    CHECK(late.searched);
    CHECK(late.cut_short);
    CHECK(late.total < kTunes);
    CHECK(late.hits.size() <= 500);
    CHECK(static_cast<int>(late.hits.size()) <= late.total);
}
