// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-021: `core` reaches the outside world only through its seams, and a counter in the fake proves
// that the code under test went through one. This is the proof for the file system, with a piece of
// `core` that is always built: the listing. tests/unit/test_structure.py has the rules of the build.
#include <filesystem>
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/listing.h"
#include "support.h"

using spz::test::TempFolder;

TEST_CASE("SPEC-021 building and opening the listing goes through Files: the fake counts each operation, and a fake that refuses leaves core no other way to the file") {
    TempFolder temp;
    spz::test::copy_fixture_tree("collections/85/C64Music", temp.path / "C64Music");
    std::filesystem::create_directories(temp.path / "cache");
    const std::string database = temp.file("C64Music/DOCUMENTS/Songlengths.md5");
    const std::string built = temp.file("cache/listing.bin");

    spz::fake::Files files;
    REQUIRE(spz::Listing::build(&files, database, built).ok);
    CHECK(files.calls > 0);
    CHECK(files.count("read") >= 1);                           // the database, read through the seam
    CHECK(files.count("write") >= 1);                          // the listing, written through it
    CHECK(files.count("rename") >= 1);                         // and put in place by SPEC-132's rename
    CHECK(std::filesystem::exists(built));
    bool database_was_read = false;
    for (const spz::fake::Files::Operation& op : files.log) {
        database_was_read = database_was_read || (op.what == "read" && op.path == database);
    }
    CHECK(database_was_read);

    const int before = files.calls;
    spz::Listing listing;
    REQUIRE(listing.open(&files, built, database));
    CHECK(files.calls > before);                               // opening looks at the files through the seam too

    // A fake that refuses every operation: the same database is on the card, readable by anyone, and
    // core cannot build from it, because the seam is its only way there.
    spz::fake::Files refusing;
    refusing.before = [&refusing](const spz::fake::Files::Operation&) { refusing.fail_at(1, spz::FileStatus::NoAccess); };
    const std::string other = temp.file("cache/other.bin");
    CHECK_FALSE(spz::Listing::build(&refusing, database, other).ok);
    CHECK(refusing.calls > 0);
    CHECK_FALSE(std::filesystem::exists(other));
    spz::Listing unopened;
    CHECK_FALSE(unopened.open(&refusing, built, database));
}
