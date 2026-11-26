// SPDX-License-Identifier: GPL-3.0-or-later
// Favourites (SPEC-171).
#include "spz/favourites.h"

#include "spz/playlist.h"

namespace spz {

Favourites::Favourites(Playlists* playlists) : playlists_(playlists) {}

ListsResult Favourites::toggle(const Entry& entry) {
    const Playlist* favourites = playlists_->find(kFavouritesName);
    if (favourites != nullptr) {
        for (std::size_t i = 0; i < favourites->entries.size(); ++i) {
            if (favourites->entries[i].entry != entry) continue;
            ListsResult result = playlists_->remove_entry(kFavouritesName, i);
            if (result.ok) result.message = "MSG_FAV_REMOVED";
            return result;
        }
    }
    ListsResult result = playlists_->add(kFavouritesName, entry);
    if (result.ok) {
        result.message = "MSG_FAV_ADDED";
        result.about.clear();
    }
    return result;
}

bool Favourites::has_tune(const std::string& path) const {
    const Playlist* favourites = playlists_->find(kFavouritesName);
    if (favourites == nullptr) return false;
    for (const PlaylistEntry& entry : favourites->entries) {
        if (entry.entry.path == path) return true;
    }
    return false;
}

bool Favourites::has_playing(const std::string& path, int playing_subtune, int default_subtune) const {
    const Playlist* favourites = playlists_->find(kFavouritesName);
    if (favourites == nullptr) return false;
    for (const PlaylistEntry& entry : favourites->entries) {
        if (entry.entry.path != path) continue;
        if (entry.entry.subtune == playing_subtune) return true;
        if (entry.entry.subtune == 0 && playing_subtune == default_subtune) return true;
    }
    return false;
}

}  // namespace spz
