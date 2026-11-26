// SPDX-License-Identifier: GPL-3.0-or-later
// What the app does at start with what it kept (SPEC-200, steps 4 and 5): where the browser opens and what
// the player gets back (SPEC-131), carrying on with the tune that was playing (SPEC-135), and clearing
// away what a killed run left behind (SPEC-202).
#pragma once

#include <string>
#include <vector>

#include "spz/clock.h"
#include "spz/files.h"
#include "spz/folders.h"
#include "spz/listing.h"
#include "spz/settings.h"
#include "spz/state_file.h"
#include "spz/types.h"

namespace spz {

// ---- the browser's place (SPEC-131) ---------------------------------------------------------------------

struct BrowserPlace {
    std::string folder;   // a path under the root; "/" is the root
    std::string entry;    // the entry to highlight: the remembered one when the remembered folder opens.
                          // The specification does not say what is highlighted when a parent opens instead
};

// Where the browser opens at start: the remembered folder if it is on the device (a folder at `root` +
// folder) or in the listing; else the nearest parent that is one or the other; else the root. `listing`
// may be null, or not open, for a collection that has none (SPEC-047). Called on a worker.
BrowserPlace browser_place_at_start(const State& state, Files* files, const std::string& root, const Listing* listing);

// ---- the player's current entry and queue (SPEC-131, SPEC-135) ------------------------------------------

// Where a restored queue's entries come from. The collection and the playlists provide this; `core`'s
// start-up code needs only these two questions of them.
class QueueSources {
public:
    virtual ~QueueSources() = default;
    // The tunes of a folder of the collection, in the order the browser shows them, as entries without
    // subtune numbers (SPEC-062). False if there is no such folder.
    virtual bool folder_tunes(const std::string& folder, std::vector<Entry>* out) = 0;
    // A playlist's entries, in order (SPEC-062). False if there is no playlist of that name.
    virtual bool playlist_entries(const std::string& name, std::vector<Entry>* out) = 0;
};

// What start-up asks of the player. The player (SPEC-060) provides this.
class PlayerAtStart {
public:
    virtual ~PlayerAtStart() = default;
    // Makes `current` the current entry, with the player Idle, and gives it this queue; `place` is the
    // current entry's place in it, counted from 1. Nothing is loaded and nothing is fetched.
    virtual void restore(const Entry& current, const std::vector<Entry>& queue, int place) = 0;
    // Plays the current entry from its beginning, as the Play/Pause key does in Idle.
    virtual void play_current() = 0;
};

// SPEC-131: gives the player back its current entry, Idle, with its queue made again from its source: the
// folder's tunes, the playlist's entries, or, for `search`, the one current entry alone. False, with the
// player left as it is, when the state has no current entry.
bool restore_player(const State& state, QueueSources* sources, PlayerAtStart* player);

// SPEC-135 (part `resume`): called once the browser is showing, after restore_player(). With the setting
// resume_on_start on, and the restored current entry present (its file is on the device), asks the player
// to play it. An absent entry is not fetched for this: nothing is asked of the player. Returns whether it
// asked. It asks Files whether the tune's file is there, so it is not called on the UI thread.
bool resume_at_start(const Settings& settings, const State& state, Files* files, const std::string& root,
                     PlayerAtStart* player);

// ---- after being killed (SPEC-202, SPEC-132) ------------------------------------------------------------

struct CleanedUp {
    int temp_files = 0;   // temporary files of safe writes removed from the app's own folders
    int partials = 0;     // partial downloads removed because they were past their age
};

// The first thing the workers do at start: removes the temporary files (spz/safe_file.h) from the app's own
// four folders, and from the cache folder the partial downloads that are 7 days old. A folder that is not
// there yet is not made. (Temporary files in the collection's folders go as each folder is listed.)
CleanedUp clean_up_at_start(Files* files, const Clock* clock, const Folders& folders);

}  // namespace spz
