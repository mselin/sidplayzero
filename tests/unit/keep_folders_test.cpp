// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-134: the four folders the app keeps its files in, and that each is made only when first needed.
#include <filesystem>
#include <string>

#include "doctest.h"
#include "fakes.h"
#include "spz/folders.h"
#include "spz/settings_file.h"
#include "spz/state_file.h"
#include "support.h"

using spz::test::TempFolder;

namespace {

spz::Environment environment_in(const TempFolder& folder) {
    spz::Environment environment;
    environment.home = folder.file("home");
    environment.xdg_config_home = folder.file("config");
    environment.xdg_data_home = folder.file("data");
    environment.xdg_state_home = folder.file("state");
    environment.xdg_cache_home = folder.file("cache");
    return environment;
}

}  // namespace

TEST_CASE("SPEC-134 with no XDG variable set, the four folders are the ones under the home folder") {
    spz::Environment environment;
    environment.home = "/home/u";
    const spz::Folders folders = spz::app_folders(environment);
    CHECK(folders.config == "/home/u/.config/sidplayzero");
    CHECK(folders.playlists == "/home/u/.local/share/sidplayzero/playlists");
    CHECK(folders.state == "/home/u/.local/state/sidplayzero");
    CHECK(folders.cache == "/home/u/.cache/sidplayzero");
}

TEST_CASE("SPEC-134 each XDG variable moves its own folder and no other") {
    spz::Environment environment;
    environment.home = "/home/u";

    spz::Environment config = environment;
    config.xdg_config_home = "/somewhere/config";
    CHECK(spz::app_folders(config).config == "/somewhere/config/sidplayzero");
    CHECK(spz::app_folders(config).playlists == "/home/u/.local/share/sidplayzero/playlists");
    CHECK(spz::app_folders(config).state == "/home/u/.local/state/sidplayzero");
    CHECK(spz::app_folders(config).cache == "/home/u/.cache/sidplayzero");

    spz::Environment data = environment;
    data.xdg_data_home = "/somewhere/data";
    CHECK(spz::app_folders(data).playlists == "/somewhere/data/sidplayzero/playlists");
    CHECK(spz::app_folders(data).config == "/home/u/.config/sidplayzero");
    CHECK(spz::app_folders(data).state == "/home/u/.local/state/sidplayzero");
    CHECK(spz::app_folders(data).cache == "/home/u/.cache/sidplayzero");

    spz::Environment state = environment;
    state.xdg_state_home = "/somewhere/state";
    CHECK(spz::app_folders(state).state == "/somewhere/state/sidplayzero");
    CHECK(spz::app_folders(state).config == "/home/u/.config/sidplayzero");
    CHECK(spz::app_folders(state).playlists == "/home/u/.local/share/sidplayzero/playlists");
    CHECK(spz::app_folders(state).cache == "/home/u/.cache/sidplayzero");

    spz::Environment cache = environment;
    cache.xdg_cache_home = "/somewhere/cache";
    CHECK(spz::app_folders(cache).cache == "/somewhere/cache/sidplayzero");
    CHECK(spz::app_folders(cache).config == "/home/u/.config/sidplayzero");
    CHECK(spz::app_folders(cache).playlists == "/home/u/.local/share/sidplayzero/playlists");
    CHECK(spz::app_folders(cache).state == "/home/u/.local/state/sidplayzero");
}

TEST_CASE("SPEC-134 the import and export folders are beside the playlists, and each kept file has its folder") {
    spz::Environment environment;
    environment.home = "/home/u";
    environment.xdg_data_home = "/somewhere/data";
    const spz::Folders folders = spz::app_folders(environment);
    CHECK(folders.imports == "/somewhere/data/sidplayzero/playlists/import");
    CHECK(folders.exports == "/somewhere/data/sidplayzero/playlists/export");
    CHECK(spz::settings_path(folders) == "/home/u/.config/sidplayzero/settings.ini");
    CHECK(spz::state_path(folders) == "/home/u/.local/state/sidplayzero/state.ini");
    CHECK(spz::lock_path(folders) == "/home/u/.local/state/sidplayzero/lock");
    CHECK(spz::selftest_request_path(folders) == "/home/u/.local/state/sidplayzero/selftest.request");
    CHECK(spz::selftest_results_path(folders) == "/home/u/.local/state/sidplayzero/selftest.results");
}

TEST_CASE("SPEC-134 with each variable set, the settings and the state appear under it") {
    TempFolder folder;
    const spz::Folders folders = spz::app_folders(environment_in(folder));
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;

    spz::SettingsFile settings(&files, &quick, spz::settings_path(folders));
    settings.load();
    REQUIRE(settings.set("engine", "light"));
    quick.run_all();
    CHECK(std::filesystem::is_regular_file(folder.path / "config" / "sidplayzero" / "settings.ini"));

    spz::StateFile state(&files, &clock, &quick, spz::state_path(folders));
    state.load();
    state.set_browser("/MUSICIANS", "H");
    CHECK(state.save_now() == spz::FileStatus::Ok);
    CHECK(std::filesystem::is_regular_file(folder.path / "state" / "sidplayzero" / "state.ini"));

    CHECK_FALSE(std::filesystem::exists(folder.path / "home"));   // nothing went to the folders under HOME
}

TEST_CASE("SPEC-134 a folder is made when something is first written into it, and not before") {
    TempFolder folder;
    const spz::Folders folders = spz::app_folders(environment_in(folder));
    spz::fake::Files files;
    spz::fake::Clock clock;
    spz::fake::Executor quick;

    spz::SettingsFile settings(&files, &quick, spz::settings_path(folders));
    settings.load();
    spz::StateFile state(&files, &clock, &quick, spz::state_path(folders));
    state.load();
    quick.run_all();
    // Reading what is not there makes nothing: the temporary folder is as empty as it was.
    CHECK(std::filesystem::is_empty(folder.path));
    CHECK(files.count("make_folders") == 0);
    CHECK(files.count("write") == 0);

    REQUIRE(settings.set("engine", "light"));
    quick.run_all();
    CHECK(std::filesystem::is_directory(folders.config));
    CHECK(files.count("make_folders") >= 1);           // made through the seam, which makes it for the user only
    CHECK_FALSE(std::filesystem::exists(folders.state));   // the others are still not needed
    CHECK_FALSE(std::filesystem::exists(folders.cache));
    CHECK_FALSE(std::filesystem::exists(folders.playlists));
}
