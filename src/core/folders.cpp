// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-134: the app's four folders, from the environment alone.
#include "spz/folders.h"

namespace spz {

namespace {

const char kName[] = "sidplayzero";   // SPEC-023

// A folder named by the environment, without the "/" at its end. "" for a variable that is not set, or
// set to a path that is not absolute: the XDG Base Directory Specification has such a value ignored.
std::string base_folder(const std::string& value) {
    if (value.empty() || value[0] != '/') return "";
    std::string folder = value;
    while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
    return folder == "/" ? "" : folder;
}

// $VARIABLE, else ~ followed by `fallback`.
std::string either(const std::string& variable, const std::string& home, const char* fallback) {
    const std::string from_variable = base_folder(variable);
    if (!from_variable.empty()) return from_variable;
    std::string from_home = home;
    while (!from_home.empty() && from_home.back() == '/') from_home.pop_back();
    return from_home + fallback;
}

}  // namespace

Folders app_folders(const Environment& environment) {
    Folders folders;
    folders.config = either(environment.xdg_config_home, environment.home, "/.config") + "/" + kName;
    folders.playlists = either(environment.xdg_data_home, environment.home, "/.local/share") + "/" + kName + "/playlists";
    folders.imports = folders.playlists + "/import";     // FIND-021: inside the playlists folder
    folders.exports = folders.playlists + "/export";
    folders.state = either(environment.xdg_state_home, environment.home, "/.local/state") + "/" + kName;
    folders.cache = either(environment.xdg_cache_home, environment.home, "/.cache") + "/" + kName;
    return folders;
}

std::string settings_path(const Folders& folders) { return folders.config + "/settings.ini"; }
std::string state_path(const Folders& folders) { return folders.state + "/state.ini"; }
std::string lock_path(const Folders& folders) { return folders.state + "/lock"; }
std::string selftest_request_path(const Folders& folders) { return folders.state + "/selftest.request"; }
std::string selftest_results_path(const Folders& folders) { return folders.state + "/selftest.results"; }

}  // namespace spz
