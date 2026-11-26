// SPDX-License-Identifier: GPL-3.0-or-later
// The playlists (SPEC-170 to SPEC-173, part `playlists`): the files of the playlists folder, and
// everything the Playlists and Playlist screens and the "Add" key do to them. Every change is saved at
// once, through SPEC-132's safe write.
//
// Nothing here starts a thread or draws. The app calls it where SPEC-022 says files are read and
// written, and shows the messages it returns.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "spz/files.h"
#include "spz/playlist.h"
#include "spz/playlist_resolve.h"
#include "spz/types.h"

namespace spz {

// What an action on the lists came to. `message` is an identifier of SPEC-116, or "" when there is
// nothing to say, and `about` is what message_words() fills in:
//     MSG_ADDED          name: the playlist's name
//     MSG_EXPORTED       path: the file written
//     MSG_FETCHED_SOME   n: tunes fetched; m: absent entries that remain, "0" when none
struct ListsResult {
    bool ok = false;
    std::string message;
    std::map<std::string, std::string> about;
};

// A row of the Playlists screen (SPEC-172).
struct PlaylistSummary {
    std::string name;
    int entries = 0;
    bool favourites = false;   // the one that cannot be renamed or deleted (SPEC-171)
};

// A row of the Playlist screen (SPEC-172).
struct PlaylistRow {
    std::string text;          // the title, or the file's name when the title is not known
    int subtune = 0;           // the subtune number, if the entry has one; else 0
    Found found = Found::Missing;   // the mark: absent or missing (SPEC-173)
};

class Playlists {
public:
    // `folder` is the playlists folder of SPEC-134; it is made when first written to. `resolver` finds
    // each entry's tune (SPEC-173) and says what the file is to hold of it (SPEC-170).
    Playlists(Files* files, const std::string& folder, const EntryResolver* resolver);

    // Reads every ".m3u8" file of the folder. "Favourites" is there afterwards whether a file gave it or
    // not (SPEC-171). Call it once, before anything else.
    void load();

    // The playlists with their numbers of entries: Favourites first.
    std::vector<PlaylistSummary> list() const;
    // The playlist of that name, or null. The pointer holds until the next change.
    const Playlist* find(const std::string& name) const;
    // The full path of a playlist's file (SPEC-170), "" if there is no such playlist.
    std::string file_of(const std::string& name) const;

    // "New": an empty playlist. The name must pass is_playlist_name() and differ from every other
    // playlist's name without regard to case, "Favourites" among them: else nothing is made, and a name
    // that is taken gives MSG_NAME_TAKEN.
    ListsResult create(const std::string& name);
    // A new playlist holding these entries, for an import (SPEC-175). The same rules for the name.
    ListsResult create(const std::string& name, const std::vector<PlaylistEntry>& entries);
    // "Rename": the same rules for the new name. The file's name follows the playlist's. Not Favourites.
    ListsResult rename(const std::string& name, const std::string& new_name);
    // "Delete", once the user has said yes: the playlist and its file. Not Favourites.
    ListsResult remove(const std::string& name);

    // "Add": puts the entry at the end and gives MSG_ADDED. An entry that is already there, the same
    // tune with the same subtune number or the same lack of one, adds nothing and gives
    // MSG_ALREADY_THERE. What the file says of the tune comes from the resolver's facts().
    ListsResult add(const std::string& name, const Entry& entry);
    // "Remove": takes out the entry at `index`, counted from 0.
    ListsResult remove_entry(const std::string& name, std::size_t index);
    // "Up" and "Down": moves the entry at `index` one place, towards the start with `by` -1 and towards
    // the end with +1. At either end it stays, and that is no fault.
    ListsResult move_entry(const std::string& name, std::size_t index, int by);

    // The rows of the Playlist screen, in order.
    std::vector<PlaylistRow> rows(const std::string& name) const;
    // The queue played from this playlist (SPEC-062): its entries in order, with the subtune numbers the
    // playlist has, each at the path its tune is found at now (SPEC-173). A missing entry is in it too.
    std::vector<Entry> queue(const std::string& name) const;

private:
    int index_of(const std::string& name) const;
    // Whether a name is another playlist's, without regard to case, or is "Favourites".
    bool is_taken(const std::string& name, int except) const;
    // The file name SPEC-170 gives a name, with " 2", " 3" and so on added while it is taken.
    std::string free_file_name(const std::string& name, int except);
    // Writes the playlist at `index` to its file through SPEC-132's safe write, its paths corrected.
    FileStatus save(std::size_t index);

    Files* files_;
    std::string folder_;
    const EntryResolver* resolver_;
    std::vector<Playlist> playlists_;
    std::vector<std::string> file_names_;    // the file of each playlist, in the folder
};

namespace lists_files {
// SPEC-132's safe write, for the files of the lists: the target's folder made, a temporary file beside
// the target written and flushed, renamed over it, and the folder flushed.
FileStatus write_safely(Files* files, const std::string& target, const Bytes& bytes);
}  // namespace lists_files

}  // namespace spz
