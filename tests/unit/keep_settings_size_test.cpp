// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-133 and SPEC-204: a settings.ini far larger than the 64 KiB limit. It is never read into memory
// (the settings are read on the UI thread at start, and a file larger than memory would end the app at
// every start), and is still kept whole, as it was found, beside where it was.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "spz/settings_file.h"
#include "support.h"

using spz::test::TempFolder;

namespace {

// The fake Files, and the most any read through it asked for. A read that asks for more than a mebibyte
// fails at once without reading: what a file larger than memory would do to the app, without doing it.
class Watched : public spz::fake::Files {
public:
    std::size_t most_asked = 0;
    spz::FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) override {
        if (limit > most_asked) most_asked = limit;
        if (limit > (std::size_t{1} << 20)) return spz::FileStatus::Failed;
        return spz::fake::Files::read(path, limit, out);
    }
};

}  // namespace

TEST_CASE("SPEC-133 SPEC-204 a settings.ini of 64 GiB is not read past the limit: the defaults, the notice, and the file kept whole as settings.ini.bad") {
    TempFolder folder;
    Watched files;
    spz::fake::Executor quick;
    const std::string path = folder.file("settings.ini");
    const std::uintmax_t size = std::uintmax_t{64} << 30;   // sparse: it takes no room on the disk
    folder.write("settings.ini", "engine = light\n");
    std::filesystem::resize_file(path, size);

    spz::SettingsFile file(&files, &quick, path);
    file.load();
    CHECK(files.most_asked <= 64 * 1024);
    CHECK(file.had_fault());
    CHECK(file.settings().engine == spz::Settings().engine);
    CHECK(file.take_notice() == "MSG_SETTINGS_RESET");
    CHECK(file.bad_copy() == path + ".bad");
    std::error_code ec;
    CHECK(std::filesystem::file_size(path + ".bad", ec) == size);   // the whole of it, as it was found

    // The settings work as ever from here on: a change is in force and saved, and then loads with no fault.
    CHECK(file.set("engine", "accurate"));
    quick.run_all();
    CHECK(file.last_save() == spz::FileStatus::Ok);
    spz::SettingsFile again(&files, &quick, path);
    again.load();
    CHECK_FALSE(again.had_fault());
    CHECK(spz::setting_text(again.settings(), "engine") == "accurate");
}
