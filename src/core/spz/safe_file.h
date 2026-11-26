// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a file so that it is never left half-written (SPEC-132): settings, state, playlists, the index,
// the listing, fetched tunes and documents all go through this. And the names of what such a write, or a
// download, can leave behind when the app is killed, with the functions that clear them away (SPEC-202).
//
// Everything here works through the Files seam and is called on a worker, never on the UI thread.
#pragma once

#include <cstdint>
#include <string>

#include "spz/clock.h"
#include "spz/files.h"
#include "spz/types.h"

namespace spz {

// A safe write's temporary file is in the target's folder and has the target's name with a dot in front
// and this added: "state.ini" is written as ".state.ini.tmp-sidplayzero".
const char kTempEnding[] = ".tmp-sidplayzero";

// The full path of the temporary file for a target: "/a/b/state.ini" gives "/a/b/.state.ini.tmp-sidplayzero".
std::string temp_path_for(const std::string& target);
// Whether a file's name (not its path) is that of such a temporary file: a dot in front, and the ending.
bool is_temp_name(const std::string& name);

// SPEC-132: writes `bytes` to the temporary file, flushes it (Files::sync), renames it over `target`, and
// flushes the folder, in that order. The target's folder is made first if it is not there (SPEC-134).
// Ok means the target now holds `bytes`. Whatever else is returned, and wherever a failure or a kill came,
// the target is either as it was before or holds all of `bytes`: never a part, never a mixture.
FileStatus safe_write(Files* files, const std::string& target, const Bytes& bytes);

// The second half of a safe write, for a file that something else (a download) has already written at
// temp_path_for(target): flushes it, renames it over `target`, and flushes the folder. The same promise.
FileStatus safe_commit(Files* files, const std::string& target);

// Removes every temporary file (is_temp_name) directly in `folder`, and nothing else. Returns how many it
// removed; 0 for a folder that is not there. The app calls this at start for each of its own folders, and
// for a folder of the collection whenever it lists one (SPEC-132, SPEC-202).
int remove_temp_files(Files* files, const std::string& folder);

// A partial download is not a temporary file: it is NAME.part in the cache folder, with what the server
// said of it in NAME.part.meta beside it (SPEC-132, SPEC-147), and it is kept from run to run.
const char kPartEnding[] = ".part";
const char kPartMetaEnding[] = ".part.meta";
// How old a partial download may be, by its file's modification time: 7 days (SPEC-132 ‡).
const std::int64_t kPartMaxAgeS = 7 * 24 * 60 * 60;

// Removes each NAME.part in `cache_folder` that is kPartMaxAgeS old or older by the clock's wall time, with
// its NAME.part.meta. Returns how many partial downloads it removed (a pair counts once).
int remove_old_partials(Files* files, const Clock* clock, const std::string& cache_folder);

}  // namespace spz
