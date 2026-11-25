// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/keep_start.h"

#include "spz/not_built.h"

namespace spz {

BrowserPlace browser_place_at_start(const State&, Files*, const std::string&, const Listing*) { SPZ_NOT_BUILT("SPEC-131"); }
bool restore_player(const State&, QueueSources*, PlayerAtStart*) { SPZ_NOT_BUILT("SPEC-131"); }
bool resume_at_start(const Settings&, const State&, Files*, const std::string&, PlayerAtStart*) { SPZ_NOT_BUILT("SPEC-135"); }
CleanedUp clean_up_at_start(Files*, const Clock*, const Folders&) { SPZ_NOT_BUILT("SPEC-202"); }

}  // namespace spz
