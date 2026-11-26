// SPDX-License-Identifier: GPL-3.0-or-later
// The composition, keep: the app's folders (SPEC-134), the settings (SPEC-130, SPEC-133), the state
// (SPEC-131), the screen while playing (SPEC-207) and the measurements (SPEC-241).
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "app/composition.h"
#include "spz/keymap.h"
#include "spz/player_roms.h"
#include "spz/version.h"

namespace spz::app {

namespace {

std::string variable(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// SPEC-241: the groups of the user the app runs as.
std::string user_groups() {
    std::string user;
    if (const passwd* entry = getpwuid(getuid())) user = entry->pw_name;
    std::vector<gid_t> ids(static_cast<std::size_t>(getgroups(0, nullptr) > 0 ? getgroups(0, nullptr) : 0));
    if (!ids.empty()) ids.resize(static_cast<std::size_t>(getgroups(static_cast<int>(ids.size()), ids.data())));
    std::vector<std::string> names;
    for (const gid_t id : ids) {
        if (const group* entry = getgrgid(id)) names.push_back(entry->gr_name);
    }
    return user_groups_value(user, names);
}

}  // namespace

void Composition::make_keep() {
    Environment environment;
    environment.home = variable("HOME");
    environment.xdg_config_home = variable("XDG_CONFIG_HOME");
    environment.xdg_data_home = variable("XDG_DATA_HOME");
    environment.xdg_state_home = variable("XDG_STATE_HOME");
    environment.xdg_cache_home = variable("XDG_CACHE_HOME");
    folders_ = app_folders(environment);

    // SPEC-200, step 2: the settings and the state, read once.
    settings_.reset(new SettingsFile(seams_.files, app()->saves(), settings_path(folders_)));
    settings_->load();
    const std::string reset = settings_->take_notice();
    if (!reset.empty()) app()->notice(reset);
    state_.reset(new StateFile(seams_.files, seams_.clock, app()->saves(), state_path(folders_)));
    state_->load();

    // SPEC-207: the brightness a killed run left off is put back now.
    if (seams_.backlight) {
        screen_off_.reset(new ScreenOff(seams_.clock, app()->saves(), seams_.backlight, state_.get()));
        screen_off_->at_start();
        if (part_built("screen-off") && screen_off_->available()) {
            screen_off_->set_after_s(settings_->settings().screen_off_after_s);
        }
        ScreenOff* screen_off = screen_off_.get();
        app()->set_key_filter([screen_off] { return screen_off->key(); });
    }

    ui::Services& s = services();
    s.settings = settings_.get();
    s.state = state_.get();
    s.metrics = &metrics_;
    s.screen_off = screen_off_.get();
    s.setting_changed = [this](const std::string& key) { setting_changed(key); };
    s.home = environment.home.empty() ? "/" : environment.home;

    // SPEC-241: what every run writes.
    metrics_.set("app_version", app_version());
    if (!seams_.libraries.empty()) metrics_.set("engine_library", seams_.libraries.front());
    if (seams_.start_ms >= 0) metrics_.set_number("start_ms", seams_.start_ms);
    metrics_.set("user_groups", user_groups());
    Metrics* metrics = &metrics_;
    app()->set_key_timer([metrics](Millis ms) { metrics->key_answered(ms); });
}

void Composition::keep_at_start() {
    // SPEC-068: the ROMs the settings name, read on the quick worker; one that is not there or has the
    // wrong size gives the notice at start.
    Files* files = seams_.files;
    const Settings settings = settings_->settings();
    app()->quick()->post([this, files, settings] {
        const RomsRead read = read_roms(files, settings);
        on_ui([this, read] {
            set_roms(read.roms);
            if (read.ignored) app()->notice("MSG_ROM_IGNORED");
        });
    });
}

void Composition::setting_changed(const std::string& key) {
    const Settings& now = settings_->settings();
    if (scope_->player) scope_->player->settings_changed();
    if (key == "screen_off_after" && screen_off_ && part_built("screen-off") && screen_off_->available()) {
        screen_off_->set_after_s(now.screen_off_after_s);
    }
    if (key == "rom_kernal" || key == "rom_basic" || key == "rom_chargen") keep_at_start();
    if (key == "collection") collection_changed();
    app()->redraw();
}

}  // namespace spz::app
