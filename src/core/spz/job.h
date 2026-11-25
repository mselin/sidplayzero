// SPDX-License-Identifier: GPL-3.0-or-later
// Jobs (SPEC-140, SPEC-147). A job is one of four things the user starts and can watch and stop: setting
// HVSC up, updating it, the complete download and "Fetch missing". One job runs at a time. `Jobs` is the
// one place that knows which: a job takes it when it begins, reports its step and figures to it while
// it runs, and gives it back when it ends; the HVSC screen and the job mark read it; "Stop" tells it.
//
// begin(), end(), state(), running() and stop() are called on the UI thread; progress(), no_stopping()
// and stop_asked() by the job, on its worker. The class is safe for that.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace spz {

enum class JobKind { None, Setup, Update, Download, Fetch };

// The word the screen description uses for a kind (SPEC-254): "setup", "update", "download", "fetch";
// "" for None.
const char* job_kind_name(JobKind kind);

struct JobState {
    JobKind kind = JobKind::None;   // None: no job runs
    int step = 0;                   // the number of the step in the job's item (SPEC-140, SPEC-144)
    std::uint64_t done = 0;         // bytes; files for "Fetch missing" and while unpacking
    std::uint64_t total = 0;        // 0 while it is not known
    bool can_stop = true;           // false from step 7 of the complete download on (SPEC-144)
};

// The number in the job mark (SPEC-147): `done` as a share of `total`, 0 to 100, rounded down; 0 while
// the total is not known.
int job_percent(const JobState& state);

// Where a job tells what happened: a notice (SPEC-113). The screens own the notices; this is all a job
// needs of them. Called on the UI thread.
class NoticeSink {
public:
    virtual ~NoticeSink() = default;
    // Raises the notice with this identifier of SPEC-116. `about` fills in its words, by the names
    // message_words() takes: "n" and "m" for the numbers the table writes as n and m, "step" for the
    // number of the step that failed.
    virtual void raise(const std::string& id, const std::map<std::string, std::string>& about) = 0;
};

class Jobs {
public:
    Jobs();
    ~Jobs();
    Jobs(const Jobs&) = delete;
    Jobs& operator=(const Jobs&) = delete;

    // A job begins. False, and nothing changes, if one is running already (SPEC-140).
    bool begin(JobKind kind);
    // Where the running job is: its step, and how much is done of how much.
    void progress(int step, std::uint64_t done, std::uint64_t total);
    // From here on the running job cannot be stopped (SPEC-144, step 7).
    void no_stopping();
    // The running job has ended, however it ended. Forgets a stop that was asked for.
    void end();

    // The running job, for the HVSC screen and the job mark. `kind` is None when none runs.
    JobState state() const;
    bool running() const;

    // "Stop", confirmed (SPEC-147): asks the running job to stop. False if no job runs, or if it can no
    // longer be stopped. The job is still running when this returns; it ends once it has cleaned up.
    bool stop();
    // Whether the running job has been asked to stop. A job looks at this between its steps and from
    // its progress callbacks.
    bool stop_asked() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spz
