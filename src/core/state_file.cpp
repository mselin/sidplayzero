// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/state_file.h"

#include <utility>

#include "spz/not_built.h"

namespace spz {

StateRead read_state(const Bytes&) { SPZ_NOT_BUILT("SPEC-131"); }

StateFile::StateFile(Files* files, Clock* clock, Executor* quick, std::string path)
    : files_(files), clock_(clock), quick_(quick), path_(std::move(path)) {
    (void)files_;
    (void)clock_;
    (void)quick_;
}

StateFile::~StateFile() {}

void StateFile::load() { SPZ_NOT_BUILT("SPEC-131"); }
bool StateFile::had_fault() const { SPZ_NOT_BUILT("SPEC-133"); }
State StateFile::state() const { SPZ_NOT_BUILT("SPEC-131"); }
void StateFile::set_browser(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-131"); }
void StateFile::set_current(const std::string&, int) { SPZ_NOT_BUILT("SPEC-131"); }
void StateFile::set_queue(const std::string&, const std::string&, int) { SPZ_NOT_BUILT("SPEC-131"); }
void StateFile::set_notice_accepted(bool) { SPZ_NOT_BUILT("SPEC-131"); }
void StateFile::set_backlight_saved(int) { SPZ_NOT_BUILT("SPEC-131"); }
bool StateFile::unsaved() const { SPZ_NOT_BUILT("SPEC-131"); }
FileStatus StateFile::save_now() { SPZ_NOT_BUILT("SPEC-131"); }
FileStatus StateFile::last_save() const { SPZ_NOT_BUILT("SPEC-131"); }

}  // namespace spz
