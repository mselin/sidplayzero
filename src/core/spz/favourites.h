// SPDX-License-Identifier: GPL-3.0-or-later
// Favourites (SPEC-171, part `playlists`): the playlist named "Favourites", and what the "Fav" key and
// the stars make of it.
#pragma once

#include <string>

#include "spz/playlists.h"
#include "spz/types.h"

namespace spz {

class Favourites {
public:
    explicit Favourites(Playlists* playlists);

    // "Fav": adds the entry if Favourites does not have it, with MSG_FAV_ADDED, and removes it if it
    // does, with MSG_FAV_REMOVED. From the browser and Search the entry is "this tune, no subtune
    // number" (subtune 0); from the now-playing screen it is "this tune, the playing subtune".
    ListsResult toggle(const Entry& entry);

    // The star in the browser and in search results: Favourites has any entry for this tune.
    bool has_tune(const std::string& path) const;
    // The star on the now-playing screen: Favourites has an entry for the playing subtune, or one
    // without a number while the tune's default subtune plays.
    bool has_playing(const std::string& path, int playing_subtune, int default_subtune) const;

private:
    Playlists* playlists_;
};

}  // namespace spz
