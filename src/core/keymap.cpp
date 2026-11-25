// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/keymap.h"

#include "spz/not_built.h"

namespace spz {

const std::vector<KeyMapRow>& key_map() { SPZ_NOT_BUILT("SPEC-094"); }
bool part_built(const std::string&) { SPZ_NOT_BUILT("SPEC-115"); }
std::vector<std::string> parts_built() { SPZ_NOT_BUILT("SPEC-115"); }
PlaceKind place_kind(const std::string&) { SPZ_NOT_BUILT("SPEC-092"); }
std::string key_action(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-092"); }
std::array<std::string, 5> action_labels(const std::string&) { SPZ_NOT_BUILT("SPEC-091"); }

}  // namespace spz
