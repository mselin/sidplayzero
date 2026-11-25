// SPDX-License-Identifier: GPL-3.0-or-later
// Where the app keeps its files (SPEC-134): four folders, worked out from the environment and from nothing
// else. Working them out touches no file: a folder is made, for the user only, when something is first
// written into it (Files::make_folders), and never before.
#pragma once

#include <string>

namespace spz {

// What the environment says. A variable that is not set, or is set to nothing, is "".
struct Environment {
    std::string home;               // HOME
    std::string xdg_config_home;    // XDG_CONFIG_HOME
    std::string xdg_data_home;      // XDG_DATA_HOME
    std::string xdg_state_home;     // XDG_STATE_HOME
    std::string xdg_cache_home;     // XDG_CACHE_HOME
};

// The app's folders, as full paths with no "/" at the end.
struct Folders {
    std::string config;      // settings.ini: $XDG_CONFIG_HOME/sidplayzero, else ~/.config/sidplayzero
    std::string playlists;   // the playlists: $XDG_DATA_HOME/sidplayzero/playlists, else ~/.local/share/...
    std::string imports;     // the "import" folder beside the playlists (SPEC-175): playlists + "/import"
    std::string exports;     // the "export" folder beside the playlists (SPEC-179): playlists + "/export"
    std::string state;       // state.ini, the lock, the self-test's files: $XDG_STATE_HOME/sidplayzero, else
                             // ~/.local/state/sidplayzero
    std::string cache;       // the index, the listing, the speed factors, partial downloads:
                             // $XDG_CACHE_HOME/sidplayzero, else ~/.cache/sidplayzero
};

// SPEC-134: the four folders for this environment.
Folders app_folders(const Environment& environment);

// The files of this header's own area, each in its folder.
std::string settings_path(const Folders& folders);          // config + "/settings.ini"   (SPEC-130)
std::string state_path(const Folders& folders);             // state + "/state.ini"       (SPEC-131)
std::string lock_path(const Folders& folders);              // state + "/lock"            (SPEC-200)
std::string selftest_request_path(const Folders& folders);  // state + "/selftest.request" (SPEC-260)
std::string selftest_results_path(const Folders& folders);  // state + "/selftest.results" (SPEC-241, SPEC-260)

}  // namespace spz
