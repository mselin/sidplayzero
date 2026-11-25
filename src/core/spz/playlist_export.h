// SPDX-License-Identifier: GPL-3.0-or-later
// "Export" (SPEC-179, part `export`): a playlist written for other players.
#pragma once

#include <string>

#include "spz/files.h"
#include "spz/playlists.h"

namespace spz {

// The text of an exported playlist: "#EXTM3U", "#PLAYLIST:", then for each entry an "#EXTINF:" line
// when the title is known and the tune's path, with "?N" added when the entry has a subtune number.
Bytes exported_playlist(const Playlist& playlist);

// Writes the playlist of that name into `export_folder`, the `export` folder of SPEC-134, under the file
// name SPEC-170 gives it, replacing a file of that name, through SPEC-132's safe write. Gives
// MSG_EXPORTED with the path written.
ListsResult export_playlist(Files* files, const Playlists& playlists, const std::string& name,
                            const std::string& export_folder);

}  // namespace spz
