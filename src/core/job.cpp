// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/job.h"

#include "spz/not_built.h"

namespace spz {

struct Jobs::Impl {};

const char* job_kind_name(JobKind) { SPZ_NOT_BUILT("SPEC-147"); }
int job_percent(const JobState&) { SPZ_NOT_BUILT("SPEC-147"); }

Jobs::Jobs() {}
Jobs::~Jobs() = default;
bool Jobs::begin(JobKind) { SPZ_NOT_BUILT("SPEC-140"); }
void Jobs::progress(int, std::uint64_t, std::uint64_t) { SPZ_NOT_BUILT("SPEC-147"); }
void Jobs::no_stopping() { SPZ_NOT_BUILT("SPEC-144"); }
void Jobs::end() { SPZ_NOT_BUILT("SPEC-140"); }
JobState Jobs::state() const { SPZ_NOT_BUILT("SPEC-147"); }
bool Jobs::running() const { SPZ_NOT_BUILT("SPEC-140"); }
bool Jobs::stop() { SPZ_NOT_BUILT("SPEC-147"); }
bool Jobs::stop_asked() const { SPZ_NOT_BUILT("SPEC-147"); }

}  // namespace spz
