// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/playlist.h"

#include "spz/not_built.h"

namespace spz {

const char* const kFavouritesName = "Favourites";

PlaylistRead read_playlist(const Bytes&) { SPZ_NOT_BUILT("SPEC-170"); }
Bytes write_playlist(const Playlist&) { SPZ_NOT_BUILT("SPEC-170"); }
std::string playlist_file_name(const std::string&) { SPZ_NOT_BUILT("SPEC-170"); }
bool is_playlist_name(const std::string&) { SPZ_NOT_BUILT("SPEC-172"); }

}  // namespace spz
