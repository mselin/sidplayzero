// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/settings_file.h"

#include <utility>

#include "spz/not_built.h"

namespace spz {

const std::vector<std::string>& setting_keys() { SPZ_NOT_BUILT("SPEC-130"); }
std::string setting_text(const Settings&, const std::string&) { SPZ_NOT_BUILT("SPEC-130"); }
bool set_setting(Settings*, const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-130"); }
SettingsRead read_settings(const Bytes&) { SPZ_NOT_BUILT("SPEC-133"); }

SettingsFile::SettingsFile(Files* files, Executor* quick, std::string path)
    : files_(files), quick_(quick), path_(std::move(path)) {
    (void)files_;
    (void)quick_;
}

void SettingsFile::load() { SPZ_NOT_BUILT("SPEC-133"); }
const Settings& SettingsFile::settings() const { SPZ_NOT_BUILT("SPEC-130"); }
bool SettingsFile::had_fault() const { SPZ_NOT_BUILT("SPEC-133"); }
const std::string& SettingsFile::bad_copy() const { SPZ_NOT_BUILT("SPEC-133"); }
std::string SettingsFile::take_notice() { SPZ_NOT_BUILT("SPEC-133"); }
bool SettingsFile::set(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-130"); }
FileStatus SettingsFile::last_save() const { SPZ_NOT_BUILT("SPEC-130"); }

}  // namespace spz
