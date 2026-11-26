// SPDX-License-Identifier: GPL-3.0-or-later
// What the HVSC area's files share, and nothing outside them uses: the file moves of SPEC-132 done
// through the Files seam, the clearing of a folder, and SPEC-140's steps 2 to 6, which a set-up and an
// update (SPEC-148) both run. Private to src/core/{hvsc_setup,releases,fetcher,complete_download}.cpp.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "spz/executor.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/types.h"

namespace spz {
namespace hvsc_detail {

// The documents, under the root and under `url_files`, and the song length file among them.
extern const char* const kDocumentNames[5];   // in the order SPEC-140 names them
const char kSongLengths[] = "DOCUMENTS/Songlengths.md5";
const char kHvSids[] = "DOCUMENTS/hv_sids.txt";
// hv_sids.txt is two lines; what is read of it at most (SPEC-204).
const std::size_t kHvSidsReadLimit = 64 * 1024;

// The folder a path is in: "/a/b/c" gives "/a/b", "/c" gives "/", "c" gives ".".
std::string parent_folder(const std::string& path);
// An address under `url_files` or another base address: `base` with any slash at its end dropped, then
// `path`, which starts with "/".
std::string join_url(const std::string& base, const std::string& path);
// SPEC-132's name for the temporary file of a target: ".NAME.tmp-sidplayzero" beside it.
std::string temp_beside(const std::string& target);

// The second half of SPEC-132's safe write, for a file already written at `temporary` in the target's
// folder: flushes it, renames it over `target`, flushes the folder. On failure the temporary file is
// removed and the target is as it was.
FileStatus commit_temporary(Files* files, const std::string& temporary, const std::string& target);
// SPEC-132's safe write: the temporary file, then commit_temporary(). The target's folder must exist.
FileStatus write_safely(Files* files, const std::string& target, const Bytes& bytes);
// Reads a file of at most `limit` bytes and writes it safely at `to`.
FileStatus copy_safely(Files* files, const std::string& from, const std::string& to, std::size_t limit);

// Removes a folder and everything in it, without following links. Ok if it was not there.
FileStatus remove_tree(Files* files, const std::string& folder);

// How SPEC-140's steps 2 to 6 ended.
struct SetupOutcome {
    int failed_step = 0;     // 0: they ended well
    bool stopped = false;    // "Stop": failed_step is the step it was in
    int release = 0;         // the release the new hv_sids.txt names
    Room room;               // for a failure in step 2
};

// Runs SPEC-140's steps 2 to 6 on the worker it is called on, reporting each step to `jobs` and looking
// at its stop between steps and in the downloads. Downloads into a new folder in `cache`, which is gone
// again when this returns. What was in the root is untouched unless it ends well.
SetupOutcome run_setup_steps(Files* files, HttpClient* http, Jobs* jobs, const std::string& url_files,
                             const std::string& root, const std::string& cache);

// On the UI thread: ends the job and tells how it ended, by the notices and the listener of SPEC-140.
void tell_setup_outcome(const SetupOutcome& outcome, Jobs* jobs, NoticeSink* notices, HvscListener* listener);

// The words of a notice, for NoticeSink::raise.
std::map<std::string, std::string> words(const std::string& name, std::uint64_t value);

}  // namespace hvsc_detail
}  // namespace spz
