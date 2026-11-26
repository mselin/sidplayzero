// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-130 and SPEC-022: what a job on a worker reads the settings through. Each read is a copy of the
// settings in force, taken under the settings file's lock; that no thread then reads what the UI thread
// writes is shown under ThreadSanitizer by tests/simulator/keep_settings_threads_test.cpp.
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/settings_file.h"
#include "support.h"

using spz::test::TempFolder;

TEST_CASE("SPEC-130 SPEC-022 a SettingsSource made from the settings file gives a copy of the settings in force, and one made from nothing the defaults") {
    TempFolder folder;
    spz::fake::Files files;
    spz::fake::Executor quick;
    spz::SettingsFile file(&files, &quick, folder.file("settings.ini"));
    file.load();
    const spz::SettingsSource source = &file;
    CHECK(source.get().url_files == spz::Settings().url_files);
    REQUIRE(file.set("url_files", "https://example.org/HVSC/C64Music"));
    CHECK(source.get().url_files == "https://example.org/HVSC/C64Music");   // the value in force, at once
    CHECK(file.copy().url_files == "https://example.org/HVSC/C64Music");
    CHECK(spz::SettingsSource().get().url_files == spz::Settings().url_files);
    spz::Settings fixed;
    fixed.url_archive = "https://example.org/{release}.7z";
    CHECK(spz::SettingsSource(&fixed).get().url_archive == "https://example.org/{release}.7z");
}
