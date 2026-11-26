// SPDX-License-Identifier: GPL-3.0-or-later
// What the unit tests of the collection, the browser and searching share: a collection root of the
// test's own with a cache folder beside it, a song length database written by the test, and ways to
// read the fake file system's log.
#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/collection.h"
#include "spz/listing.h"
#include "support.h"

namespace spz::test {

// 32 hexadecimal digits that stand for the MD5 of made-up tune number `n`. A test that does not open
// its tunes needs only that each has one.
inline std::string made_up_md5(int n) {
    char text[33];
    std::snprintf(text, sizeof text, "%032x", static_cast<unsigned>(n) + 1);
    return text;
}

// A song length database in HVSC's form (SPEC-142) for these paths, each with one subtune of `length`.
inline std::string database_text(const std::vector<std::string>& paths, const std::string& length = "0:10") {
    std::string text = "[Database]\r\n";
    int n = 0;
    for (const std::string& path : paths) text += "; " + path + "\r\n" + made_up_md5(n++) + "=" + length + "\r\n";
    return text;
}

// Moves a file's or a folder's modification time on by `seconds`, so that a change made within the same
// second as the last look is still a change: the file system keeps whole seconds here (spz/files.h).
inline void move_time_on(const std::filesystem::path& path, int seconds) {
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(seconds));
}

// Whether two spellings of a path name the same place: "a/b", "a//b" and "a/b/" do.
inline bool same_place(const std::string& a, const std::string& b) {
    auto tidy = [](const std::string& path) {
        std::string text = std::filesystem::path(path).lexically_normal().string();
        while (text.size() > 1 && text.back() == '/') text.pop_back();
        return text;
    };
    return tidy(a) == tidy(b);
}

inline bool ends_with(const std::string& text, const std::string& ending) {
    return text.size() >= ending.size() && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

// A list as one line of text, "a | b | c", so that a failed comparison shows what was found.
inline std::string joined(const std::vector<std::string>& items) {
    std::string text;
    for (const std::string& item : items) text += (text.empty() ? "" : " | ") + item;
    return text;
}

// The names of a folder's entries, in the order given, as one line: "DEMOS | GAMES | MUSICIANS".
inline std::string names_of(const spz::FolderListing& listing) {
    std::vector<std::string> names;
    for (const spz::FolderEntry& entry : listing.entries) names.push_back(entry.name);
    return joined(names);
}

// A collection root in a temp folder, with a cache folder beside it and the fakes its code is given.
struct TestRoot {
    TempFolder temp;
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::Listing listing;
    std::string root;     // the collection root: .../C64Music
    std::string cache;    // the cache folder: .../cache

    TestRoot() : root(temp.file("C64Music")), cache(temp.file("cache")) {
        std::filesystem::create_directories(root);
        std::filesystem::create_directories(cache);
    }
    // A root that starts as a copy of a fixture collection, such as "collections/85/C64Music".
    explicit TestRoot(const std::string& fixture_collection) : TestRoot() { copy_from(fixture_collection, ""); }

    std::string database() const { return root + "/DOCUMENTS/Songlengths.md5"; }
    std::string index_file() const { return cache + "/index.bin"; }
    // The place on the device of a path under the root, such as "/MUSICIANS/H".
    std::string at(const std::string& path) const { return root + path; }

    // Puts a fixture, such as "tunes/commando_test.sid", at a path under the root.
    void put(const std::string& fixture_name, const std::string& path) { temp.write("C64Music" + path, fixture(fixture_name)); }
    void put_text(const std::string& path, const std::string& text) { temp.write("C64Music" + path, text); }
    // Copies a fixture folder, such as "collections/85/C64Music/DOCUMENTS", to a path under the root.
    void copy_from(const std::string& fixture_folder, const std::string& path) {
        std::filesystem::create_directories(at(path));
        std::filesystem::copy(fixture_path(fixture_folder), at(path),
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }
    void make_folder(const std::string& path) { std::filesystem::create_directories(at(path)); }
    void remove(const std::string& path) { std::filesystem::remove_all(at(path)); }

    // Builds the listing from the root's song length database and opens it (SPEC-142).
    const spz::Listing* open_listing() {
        const spz::ListingBuild built = spz::Listing::build(&files, database(), cache + "/listing.bin");
        REQUIRE(built.ok);
        REQUIRE(listing.open(&files, cache + "/listing.bin", database()));
        return &listing;
    }
};

// A file system on which one folder cannot be read: listing it fails with NoAccess (SPEC-041). The
// tests may run as a user whom permissions do not stop, so the refusal is made here.
struct FilesWithLockedFolder : spz::fake::Files {
    std::string locked;   // the folder's place on the device

    spz::FileStatus list(const std::string& folder, std::vector<spz::FileInfo>* out) override {
        if (locked.empty() || !same_place(folder, locked)) return spz::fake::Files::list(folder, out);
        ++calls;
        log.push_back({"list", folder, ""});
        return spz::FileStatus::NoAccess;
    }
};

// How many operations of one kind the fake file system has logged from entry `from` of its log on, on
// paths that end in `ending` ("" for any path).
inline int operations(const spz::fake::Files& files, const std::string& what, const std::string& ending = "",
                      std::size_t from = 0) {
    int count = 0;
    for (std::size_t i = from; i < files.log.size(); ++i) {
        if (files.log[i].what == what && ends_with(files.log[i].path, ending)) ++count;
    }
    return count;
}

// How many times a folder has been listed, from entry `from` of the log on.
inline int listings_of(const spz::fake::Files& files, const std::string& folder, std::size_t from = 0) {
    int count = 0;
    for (std::size_t i = from; i < files.log.size(); ++i) {
        if (files.log[i].what == "list" && same_place(files.log[i].path, folder)) ++count;
    }
    return count;
}

// Runs the jobs of the quick worker and the messages they send to the UI thread until none is left.
inline void run_workers(spz::fake::Executor& quick, spz::fake::Executor& ui) {
    while (quick.run_all() + ui.run_all() > 0) {
    }
}

}  // namespace spz::test
