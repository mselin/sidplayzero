// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/playlists.h"

#include "spz/not_built.h"

namespace spz {

Playlists::Playlists(Files* files, const std::string& folder, const EntryResolver* resolver)
    : files_(files), folder_(folder), resolver_(resolver) {}

void Playlists::load() { SPZ_NOT_BUILT("SPEC-170"); }
std::vector<PlaylistSummary> Playlists::list() const { SPZ_NOT_BUILT("SPEC-172"); }
const Playlist* Playlists::find(const std::string&) const { SPZ_NOT_BUILT("SPEC-172"); }
std::string Playlists::file_of(const std::string&) const { SPZ_NOT_BUILT("SPEC-170"); }
ListsResult Playlists::create(const std::string&) { SPZ_NOT_BUILT("SPEC-172"); }
ListsResult Playlists::create(const std::string&, const std::vector<PlaylistEntry>&) { SPZ_NOT_BUILT("SPEC-175"); }
ListsResult Playlists::rename(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-172"); }
ListsResult Playlists::remove(const std::string&) { SPZ_NOT_BUILT("SPEC-172"); }
ListsResult Playlists::add(const std::string&, const Entry&) { SPZ_NOT_BUILT("SPEC-172"); }
ListsResult Playlists::remove_entry(const std::string&, std::size_t) { SPZ_NOT_BUILT("SPEC-172"); }
ListsResult Playlists::move_entry(const std::string&, std::size_t, int) { SPZ_NOT_BUILT("SPEC-172"); }
std::vector<PlaylistRow> Playlists::rows(const std::string&) const { SPZ_NOT_BUILT("SPEC-172"); }
std::vector<Entry> Playlists::queue(const std::string&) const { SPZ_NOT_BUILT("SPEC-173"); }

}  // namespace spz
