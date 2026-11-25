// SPDX-License-Identifier: GPL-3.0-or-later
// Jobs (SPEC-140, SPEC-147): one job runs at a time; its step and figures are what the HVSC screen and
// the job mark show; "Stop" asks it to stop.
#include <string>

#include "doctest.h"
#include "spz/job.h"

using spz::JobKind;
using spz::Jobs;
using spz::JobState;

TEST_CASE("SPEC-140 one job runs at a time: a second one does not begin until the first has ended") {
    Jobs jobs;
    CHECK_FALSE(jobs.running());
    CHECK(jobs.state().kind == JobKind::None);

    REQUIRE(jobs.begin(JobKind::Setup));
    CHECK(jobs.running());
    CHECK(jobs.state().kind == JobKind::Setup);

    for (const JobKind other : {JobKind::Setup, JobKind::Update, JobKind::Download, JobKind::Fetch}) {
        CHECK_FALSE(jobs.begin(other));
        CHECK(jobs.state().kind == JobKind::Setup);
    }

    jobs.end();
    CHECK_FALSE(jobs.running());
    CHECK(jobs.state().kind == JobKind::None);
    CHECK(jobs.begin(JobKind::Fetch));
    CHECK(jobs.state().kind == JobKind::Fetch);
}

TEST_CASE("SPEC-147 a running job's step, bytes done and total are there for the screens to read") {
    Jobs jobs;
    REQUIRE(jobs.begin(JobKind::Setup));
    jobs.progress(3, 100, 400);
    JobState state = jobs.state();
    CHECK(state.kind == JobKind::Setup);
    CHECK(state.step == 3);
    CHECK(state.done == 100);
    CHECK(state.total == 400);
    CHECK(state.can_stop);
    CHECK(spz::job_percent(state) == 25);

    jobs.progress(5, 7, 0);                 // a step whose total is not known yet
    state = jobs.state();
    CHECK(state.step == 5);
    CHECK(state.done == 7);
    CHECK(state.total == 0);
    CHECK(spz::job_percent(state) == 0);

    // A new job starts from nothing.
    jobs.end();
    REQUIRE(jobs.begin(JobKind::Update));
    state = jobs.state();
    CHECK(state.step == 0);
    CHECK(state.done == 0);
    CHECK(state.total == 0);
}

TEST_CASE("SPEC-147 the job mark's percentage is done as a share of the total, from 0 to 100") {
    JobState state;
    state.kind = JobKind::Download;
    state.done = 0; state.total = 1000;
    CHECK(spz::job_percent(state) == 0);
    state.done = 1; state.total = 3;
    CHECK(spz::job_percent(state) == 33);
    state.done = 999; state.total = 1000;
    CHECK(spz::job_percent(state) == 99);
    state.done = 1000; state.total = 1000;
    CHECK(spz::job_percent(state) == 100);
    // The complete archive is some 80 MB: the figures do not overflow.
    state.done = 3ull * 1024 * 1024 * 1024; state.total = 4ull * 1024 * 1024 * 1024;
    CHECK(spz::job_percent(state) == 75);
}

TEST_CASE("SPEC-147 SPEC-254 a job's kind has the screen description's word for it") {
    CHECK(std::string(spz::job_kind_name(JobKind::Setup)) == "setup");
    CHECK(std::string(spz::job_kind_name(JobKind::Update)) == "update");
    CHECK(std::string(spz::job_kind_name(JobKind::Download)) == "download");
    CHECK(std::string(spz::job_kind_name(JobKind::Fetch)) == "fetch");
}

TEST_CASE("SPEC-147 Stop asks the running job to stop, and the job is running until it has ended itself") {
    Jobs jobs;
    CHECK_FALSE(jobs.stop());               // nothing to stop
    CHECK_FALSE(jobs.stop_asked());

    REQUIRE(jobs.begin(JobKind::Setup));
    CHECK_FALSE(jobs.stop_asked());
    CHECK(jobs.stop());
    CHECK(jobs.stop_asked());
    CHECK(jobs.running());

    jobs.end();
    CHECK_FALSE(jobs.running());
    CHECK_FALSE(jobs.stop_asked());

    // The next job is not stopped by the stop of the one before.
    REQUIRE(jobs.begin(JobKind::Setup));
    CHECK_FALSE(jobs.stop_asked());
}

#if SPZ_PART_DOWNLOAD_ALL
TEST_CASE("SPEC-144 SPEC-147 from step 7 on the complete download cannot be stopped [parts: download-all]") {
    Jobs jobs;
    REQUIRE(jobs.begin(JobKind::Download));
    jobs.progress(6, 0, 0);
    CHECK(jobs.state().can_stop);
    jobs.no_stopping();
    jobs.progress(7, 0, 0);
    CHECK_FALSE(jobs.state().can_stop);
    CHECK_FALSE(jobs.stop());
    CHECK_FALSE(jobs.stop_asked());
    CHECK(jobs.running());

    // The job after it can be stopped again.
    jobs.end();
    REQUIRE(jobs.begin(JobKind::Setup));
    CHECK(jobs.state().can_stop);
    CHECK(jobs.stop());
}
#endif
