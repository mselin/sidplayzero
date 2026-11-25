// SPDX-License-Identifier: GPL-3.0-or-later
// What the detail line says of an entry (SPEC-046, the part `detail`): a present tune's title, author and
// the length of its default subtune; an absent tune's number of subtunes and the length of the first;
// nothing for a folder. The words and the m:ss form are the screen's, and the simulator's tests.
#include <string>
#include <vector>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "spz/collection.h"
#include "support.h"

#if SPZ_PART_DETAIL

using spz::test::TestRoot;
using spz::test::operations;

namespace {

const spz::Millis kDefaultLength = 210000;   // 3:30, as the setting `default_length` starts (SPEC-063)

// The entry of a folder with this name. The test stops if there is none.
spz::FolderEntry entry_named(const spz::Collection& collection, const std::string& folder, const std::string& name) {
    for (const spz::FolderEntry& entry : collection.list(folder).entries) {
        if (entry.name == name) return entry;
    }
    FAIL("no entry " << name << " in " << folder);
    return spz::FolderEntry();
}

}  // namespace

TEST_CASE("SPEC-046 a present tune's detail is its title, its author and the length of its default subtune [parts: detail]") {
    TestRoot t("collections/85/C64Music");
    const spz::Collection collection(&t.files, t.root, t.open_listing());

    const std::string rob = "/MUSICIANS/H/Hubbard_Rob";
    const spz::FolderEntry commando = entry_named(collection, rob, "Commando.sid");
    const std::size_t before = t.files.log.size();
    const spz::EntryDetail detail = collection.detail(rob, commando, kDefaultLength);
    CHECK_FALSE(detail.is_folder);
    CHECK(detail.present);
    CHECK(detail.title == "Commando Test");
    CHECK(detail.author == "Test Author");
    CHECK(detail.subtunes == 1);
    CHECK(detail.length_ms == 3500);                                    // 0:03.500 in the database
    CHECK(operations(t.files, "read", "Commando.sid", before) >= 1);    // read from the file, through the seam

    // This one's default subtune is its second, and the database gives that one 0:02.500.
    const std::string tom = "/MUSICIANS/T/Tester_Tom";
    const spz::EntryDetail three = collection.detail(tom, entry_named(collection, tom, "Three_Songs.sid"), kDefaultLength);
    CHECK(three.present);
    CHECK(three.title == "Three Subtunes");
    CHECK(three.subtunes == 3);
    CHECK(three.length_ms == 2500);
}

TEST_CASE("SPEC-046 SPEC-063 a present tune's length in the detail goes by its file's MD5, not by its path [parts: detail]") {
    TestRoot t("collections/85/C64Music");
    const std::string rob = "/MUSICIANS/H/Hubbard_Rob";
    t.put("tunes/own_tune.sid", rob + "/Commando.sid");       // another tune's bytes at a listed path
    t.put("tunes/commando_test.sid", "/My Rips/Copy.sid");    // the listed tune's bytes at another path
    const spz::Collection collection(&t.files, t.root, t.open_listing());

    const spz::EntryDetail other = collection.detail(rob, entry_named(collection, rob, "Commando.sid"), kDefaultLength);
    CHECK(other.present);
    CHECK(other.title == "The User's Own");
    CHECK(other.author == "Somebody");
    CHECK(other.length_ms == kDefaultLength);

    const spz::EntryDetail copy = collection.detail("/My Rips", entry_named(collection, "/My Rips", "Copy.sid"), kDefaultLength);
    CHECK(copy.present);
    CHECK(copy.title == "Commando Test");
    CHECK(copy.length_ms == 3500);
}

TEST_CASE("SPEC-046 an absent tune's detail is the number of its subtunes and the length of the first, from the listing, and no file is read for it [parts: detail]") {
    TestRoot t("collections/85/C64Music");
    const std::string tom = "/MUSICIANS/T/Tester_Tom";
    t.remove(tom + "/Three_Songs.sid");
    const spz::Collection collection(&t.files, t.root, t.open_listing());

    const spz::FolderEntry entry = entry_named(collection, tom, "Three_Songs.sid");
    REQUIRE_FALSE(entry.present);
    const std::size_t before = t.files.log.size();
    const spz::EntryDetail detail = collection.detail(tom, entry, kDefaultLength);
    CHECK_FALSE(detail.present);
    CHECK_FALSE(detail.is_folder);
    CHECK(detail.title == "");                    // its header is not known
    CHECK(detail.author == "");
    CHECK(detail.subtunes == 3);
    CHECK(detail.length_ms == 2000);              // 0:02, the first of "0:02 0:02.500 0:03"
    CHECK(operations(t.files, "read", ".sid", before) == 0);
}

TEST_CASE("SPEC-046 SPEC-047 with no listing in the root, a present tune's length is the default length [parts: detail]") {
    TestRoot t;
    t.put("tunes/commando_test.sid", "/Commando.sid");
    t.put("tunes/three_subtunes.sid", "/Three_Songs.sid");
    t.put("tunes/two_sid.sid", "/Two_Chips.sid");
    const spz::Collection collection(&t.files, t.root, nullptr);

    const spz::FolderListing root = collection.list("/");
    REQUIRE(root.entries.size() == 3);
    for (const spz::FolderEntry& entry : root.entries) {
        CAPTURE(entry.name);
        CHECK(collection.detail("/", entry, kDefaultLength).length_ms == kDefaultLength);
        CHECK(collection.detail("/", entry, 65000).length_ms == 65000);     // whatever the setting says: 1:05
    }
    CHECK(collection.detail("/", root.entries[0], kDefaultLength).title == "Commando Test");
}

TEST_CASE("SPEC-046 a folder has no detail, and nothing is read to find that out [parts: detail]") {
    TestRoot t("collections/85/C64Music");
    const spz::Collection collection(&t.files, t.root, t.open_listing());
    const spz::FolderEntry musicians = entry_named(collection, "/", "MUSICIANS");
    REQUIRE(musicians.is_folder);

    const std::size_t before = t.files.log.size();
    const spz::EntryDetail detail = collection.detail("/", musicians, kDefaultLength);
    CHECK(detail.is_folder);
    CHECK(detail.title == "");
    CHECK(detail.author == "");
    CHECK(operations(t.files, "read", "", before) == 0);
}

#endif  // SPZ_PART_DETAIL
