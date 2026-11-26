// SPDX-License-Identifier: GPL-3.0-or-later
// Where the collection is (SPEC-040): the first that is set of the environment variable HVSC_BASE, the
// setting `collection`, and "~/C64Music".
#include <filesystem>
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/collection.h"
#include "spz/settings.h"
#include "support.h"

using spz::test::TempFolder;

TEST_CASE("SPEC-040 with HVSC_BASE set, the collection root is the variable's value, whatever the setting says") {
    const spz::CollectionRoot root = spz::find_collection_root("/media/card/HVSC", "/home/u/Music/SIDs", "/home/u");
    CHECK(root.path == "/media/card/HVSC");
    CHECK(root.from_hvsc_base);
}

TEST_CASE("SPEC-040 with HVSC_BASE unset and the setting set, the collection root is the setting's value") {
    const spz::CollectionRoot root = spz::find_collection_root("", "/home/u/Music/SIDs", "/home/u");
    CHECK(root.path == "/home/u/Music/SIDs");
    CHECK_FALSE(root.from_hvsc_base);
}

TEST_CASE("SPEC-040 with neither HVSC_BASE nor the setting set, the collection root is C64Music in the home folder") {
    const spz::CollectionRoot root = spz::find_collection_root("", "", "/home/u");
    CHECK(root.path == "/home/u/C64Music");
    CHECK_FALSE(root.from_hvsc_base);

    // The setting as it is before anyone has changed it (SPEC-130) says the same.
    const spz::Settings untouched;
    CHECK(spz::find_collection_root("", untouched.collection, "/home/u").path == "/home/u/C64Music");
}

TEST_CASE("SPEC-040 a leading ~ is replaced by the home folder, and no other ~ is") {
    CHECK(spz::find_collection_root("", "~/Music/SIDs", "/home/u").path == "/home/u/Music/SIDs");
    const spz::CollectionRoot from_variable = spz::find_collection_root("~/hvsc", "~/Music/SIDs", "/home/u");
    CHECK(from_variable.path == "/home/u/hvsc");
    CHECK(from_variable.from_hvsc_base);
    CHECK(spz::find_collection_root("", "/media/card/~old/SIDs", "/home/u").path == "/media/card/~old/SIDs");
}

TEST_CASE("SPEC-040 SPEC-048 the collection root does not have to exist: it is named all the same, and holds no entry") {
    TempFolder home;
    const spz::CollectionRoot root = spz::find_collection_root("", "", home.path.string());
    CHECK(root.path == home.file("C64Music"));
    CHECK_FALSE(std::filesystem::exists(root.path));

    spz::fake::Files files;
    const spz::Collection collection(&files, root.path, nullptr);
    CHECK_FALSE(collection.has_entries());
    CHECK(files.calls > 0);                                  // it looked, through the seam (SPEC-021)
}
