// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/favourites.h"

#include "spz/not_built.h"

namespace spz {

Favourites::Favourites(Playlists* playlists) : playlists_(playlists) {}

ListsResult Favourites::toggle(const Entry&) { SPZ_NOT_BUILT("SPEC-171"); }
bool Favourites::has_tune(const std::string&) const { SPZ_NOT_BUILT("SPEC-171"); }
bool Favourites::has_playing(const std::string&, int, int) const { SPZ_NOT_BUILT("SPEC-171"); }

}  // namespace spz
