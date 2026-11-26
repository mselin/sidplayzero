// SPDX-License-Identifier: GPL-3.0-or-later
// What the HVSC area's files share, and nothing outside them uses: a copy through SPEC-132's safe write,
// the clearing of a folder, and SPEC-140's steps 2 to 6, which a set-up and an update (SPEC-148) both run.
// Private to src/core/{hvsc_setup,releases,fetcher,complete_download}.cpp.
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

// Reads a file of at most `limit` bytes and writes it at `to` through SPEC-132's safe write. The area's
// files are kept once renamed into place: a folder that cannot be flushed after it is no failure
// (FolderFlush::Ignored), and neither is it for the area's other safe writes.
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
