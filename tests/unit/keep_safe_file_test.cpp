// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-132: a file the app keeps is written so that it is never left half-written; and what such a write,
// or a download, can leave behind is cleared away (SPEC-202).
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/safe_file.h"
#include "support.h"

using spz::test::TempFolder;
using spz::test::bytes;
using spz::test::is_there;
using spz::test::names_in;
using spz::test::on_disk;

namespace {

const spz::Bytes kOld = bytes("engine = accurate\ndefault_length = 3:30\n");
const spz::Bytes kNew = bytes("engine = light\ndefault_length = 59:59\nshuffle = on\n");

// The place in the log of the first operation of a kind on a path, or -1.
int place(const spz::fake::Files& files, const std::string& what, const std::string& path, const std::string& to = "") {
    for (std::size_t i = 0; i < files.log.size(); ++i) {
        if (files.log[i].what == what && files.log[i].path == path && files.log[i].to == to) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

TEST_CASE("SPEC-132 the temporary file is in the target's folder, named as the target with a dot in front and .tmp-sidplayzero added") {
    CHECK(spz::temp_path_for("/a/b/state.ini") == "/a/b/.state.ini.tmp-sidplayzero");
    CHECK(spz::temp_path_for("/C64Music/MUSICIANS/H/Hubbard_Rob/Commando.sid") ==
          "/C64Music/MUSICIANS/H/Hubbard_Rob/.Commando.sid.tmp-sidplayzero");
    CHECK(std::string(spz::kTempEnding) == ".tmp-sidplayzero");
    CHECK(spz::is_temp_name(".state.ini.tmp-sidplayzero"));
    CHECK(spz::is_temp_name(".Commando.sid.tmp-sidplayzero"));
    CHECK_FALSE(spz::is_temp_name("state.ini"));
    CHECK_FALSE(spz::is_temp_name(".state.ini"));
    CHECK_FALSE(spz::is_temp_name("state.ini.tmp-sidplayzero"));    // no dot in front: not one of ours
    CHECK_FALSE(spz::is_temp_name(".tmp-sidplayzero.sid"));
    CHECK_FALSE(spz::is_temp_name("HVSC_85-all-of-them.7z.part"));  // a partial download is not such a file
}

TEST_CASE("SPEC-132 a save writes a new file beside the target, flushes it, renames it over the target and flushes the folder, in that order") {
    TempFolder folder;
    spz::fake::Files files;
    const std::string target = folder.write("kept.ini", kOld);
    const std::string temp = folder.file(".kept.ini.tmp-sidplayzero");

    CHECK(spz::safe_write(&files, target, kNew) == spz::FileStatus::Ok);
    CHECK(on_disk(target) == kNew);

    const int written = place(files, "write", temp);
    const int flushed = place(files, "sync", temp);
    const int renamed = place(files, "rename", temp, target);
    const int folder_flushed = place(files, "sync", folder.path.string());
    CHECK(written >= 0);
    CHECK(flushed > written);
    CHECK(renamed > flushed);
    CHECK(folder_flushed > renamed);
    CHECK(place(files, "write", target) == -1);     // the target itself is never opened for writing
    CHECK_FALSE(is_there(temp));
    CHECK(names_in(folder.path) == std::vector<std::string>{"kept.ini"});
}

TEST_CASE("SPEC-132 with a failure at every operation of a save in turn, the file read back is complete and is the old or the new contents") {
    int operations = 0;
    {
        TempFolder folder;
        spz::fake::Files files;
        const std::string target = folder.write("kept.ini", kOld);
        REQUIRE(spz::safe_write(&files, target, kNew) == spz::FileStatus::Ok);
        REQUIRE(on_disk(target) == kNew);
        operations = files.calls;
    }
    REQUIRE(operations >= 4);   // the write, the flush, the rename, the folder's flush
    for (const spz::FileStatus status : {spz::FileStatus::Failed, spz::FileStatus::NoSpace, spz::FileStatus::NoAccess}) {
        for (int n = 1; n <= operations; ++n) {
            CAPTURE(n);
            CAPTURE(static_cast<int>(status));
            TempFolder folder;
            spz::fake::Files files;
            const std::string target = folder.write("kept.ini", kOld);
            files.fail_at(n, status);
            const spz::FileStatus result = spz::safe_write(&files, target, kNew);
            const spz::Bytes now = on_disk(target);
            CHECK((now == kOld || now == kNew));
            if (result == spz::FileStatus::Ok) CHECK(now == kNew);   // a save that says it worked did

            // Whatever the failure left beside it goes at the next start, and the file is still whole.
            spz::fake::Files later;
            spz::remove_temp_files(&later, folder.path.string());
            CHECK(names_in(folder.path) == std::vector<std::string>{"kept.ini"});
            CHECK(on_disk(target) == now);
        }
    }
}

TEST_CASE("SPEC-132 the same with no file there before: afterwards there is the whole new file, or none") {
    int operations = 0;
    {
        TempFolder folder;
        spz::fake::Files files;
        REQUIRE(spz::safe_write(&files, folder.file("kept.ini"), kNew) == spz::FileStatus::Ok);
        operations = files.calls;
    }
    for (int n = 1; n <= operations; ++n) {
        CAPTURE(n);
        TempFolder folder;
        spz::fake::Files files;
        const std::string target = folder.file("kept.ini");
        files.fail_at(n);
        const spz::FileStatus result = spz::safe_write(&files, target, kNew);
        if (is_there(target)) CHECK(on_disk(target) == kNew);
        if (result == spz::FileStatus::Ok) CHECK(on_disk(target) == kNew);
    }
}

TEST_CASE("SPEC-132 at every moment of a save, what is at the target is the whole old file or the whole new one") {
    TempFolder folder;
    spz::fake::Files files;
    const std::string target = folder.write("kept.ini", kOld);
    int looked = 0;
    // Before each operation the test looks at the card, as a kill at that moment would leave it.
    files.before = [&](const spz::fake::Files::Operation&) {
        const spz::Bytes now = on_disk(target);
        CHECK((now == kOld || now == kNew));
        ++looked;
    };
    CHECK(spz::safe_write(&files, target, kNew) == spz::FileStatus::Ok);
    CHECK(looked >= 4);
    CHECK(on_disk(target) == kNew);
}

TEST_CASE("SPEC-132 SPEC-134 a save makes the target's folder first when it is not there") {
    TempFolder folder;
    spz::fake::Files files;
    const std::string target = folder.file("config/sidplayzero/settings.ini");
    CHECK(spz::safe_write(&files, target, kNew) == spz::FileStatus::Ok);
    CHECK(on_disk(target) == kNew);
    CHECK(files.count("make_folders") >= 1);
    CHECK(names_in(folder.path / "config" / "sidplayzero") == std::vector<std::string>{"settings.ini"});
}

TEST_CASE("SPEC-132 a download written at the temporary name is flushed, renamed over the target, and the folder flushed") {
    TempFolder folder;
    spz::fake::Files files;
    const std::string target = folder.file("Commando.sid");
    const std::string temp = spz::temp_path_for(target);
    REQUIRE(temp == folder.file(".Commando.sid.tmp-sidplayzero"));
    folder.write(".Commando.sid.tmp-sidplayzero", kNew);     // as the fetch left it (SPEC-141)

    CHECK(spz::safe_commit(&files, target) == spz::FileStatus::Ok);
    CHECK(on_disk(target) == kNew);
    CHECK_FALSE(is_there(temp));
    const int flushed = place(files, "sync", temp);
    const int renamed = place(files, "rename", temp, target);
    const int folder_flushed = place(files, "sync", folder.path.string());
    CHECK(flushed >= 0);
    CHECK(renamed > flushed);
    CHECK(folder_flushed > renamed);
}

TEST_CASE("SPEC-132 SPEC-202 the temporary files of a folder are removed, and nothing else in it") {
    TempFolder folder;
    spz::fake::Files files;
    folder.write("settings.ini", "engine = light\n");
    folder.write(".settings.ini.tmp-sidplayzero", "engine = li");
    folder.write(".other.tmp-sidplayzero", "");
    folder.write(".hidden", "the user's own");
    folder.write("notes.tmp-sidplayzero", "no dot in front: the user's own");
    folder.write("deeper/.inside.tmp-sidplayzero", "one folder is cleared at a time");

    CHECK(spz::remove_temp_files(&files, folder.path.string()) == 2);
    CHECK(names_in(folder.path) == std::vector<std::string>{".hidden", "deeper", "notes.tmp-sidplayzero", "settings.ini"});
    CHECK(is_there(folder.file("deeper/.inside.tmp-sidplayzero")));
    CHECK(spz::test::text_on_disk(folder.file("settings.ini")) == "engine = light\n");

    CHECK(spz::remove_temp_files(&files, folder.path.string()) == 0);
    CHECK(spz::remove_temp_files(&files, folder.file("no-such-folder")) == 0);
    CHECK_FALSE(is_there(folder.file("no-such-folder")));
}

TEST_CASE("SPEC-132 SPEC-202 a partial download is kept until it is 7 days old, and then removed with its .meta") {
    TempFolder cache;
    spz::fake::Files files;
    spz::fake::Clock clock;
    const std::int64_t hour = 60 * 60, day = 24 * hour;
    const std::int64_t now = clock.wall_time_s();
    CHECK(spz::kPartMaxAgeS == 7 * day);

    cache.write("HVSC_85-all-of-them.7z.part", "half of an archive");
    cache.write("HVSC_85-all-of-them.7z.part.meta", "etag = v85\n");
    cache.write("HVSC_84-all-of-them.7z.part", "half of an older one");
    cache.write("HVSC_84-all-of-them.7z.part.meta", "etag = v84\n");
    cache.write("listing.bin", "old, and not a download");
    for (const char* name : {"HVSC_85-all-of-them.7z.part", "HVSC_85-all-of-them.7z.part.meta"}) {
        spz::test::set_modified(cache.file(name), now - (7 * day - hour));
    }
    for (const char* name : {"HVSC_84-all-of-them.7z.part", "HVSC_84-all-of-them.7z.part.meta"}) {
        spz::test::set_modified(cache.file(name), now - (7 * day + hour));
    }
    spz::test::set_modified(cache.file("listing.bin"), now - 30 * day);

    CHECK(spz::remove_old_partials(&files, &clock, cache.path.string()) == 1);
    CHECK(names_in(cache.path) ==
          std::vector<std::string>{"HVSC_85-all-of-them.7z.part", "HVSC_85-all-of-them.7z.part.meta", "listing.bin"});

    clock.advance(2 * hour * 1000);    // two hours on, the other is 7 days old too
    CHECK(spz::remove_old_partials(&files, &clock, cache.path.string()) == 1);
    CHECK(names_in(cache.path) == std::vector<std::string>{"listing.bin"});

    CHECK(spz::remove_old_partials(&files, &clock, cache.file("no-such-folder")) == 0);
}
