// SPDX-License-Identifier: GPL-3.0-or-later
// Jobs (SPEC-140, SPEC-147): which one runs, where it is, and whether it was asked to stop.
#include "spz/job.h"

#include <mutex>

namespace spz {

struct Jobs::Impl {
    mutable std::mutex mutex;
    JobState state;
    bool stop_asked = false;
};

const char* job_kind_name(JobKind kind) {
    switch (kind) {
        case JobKind::Setup: return "setup";
        case JobKind::Update: return "update";
        case JobKind::Download: return "download";
        case JobKind::Fetch: return "fetch";
        case JobKind::None: break;
    }
    return "";
}

int job_percent(const JobState& state) {
    if (state.total == 0) return 0;
    if (state.done >= state.total) return 100;
    // done < total here, so this is below 100; long double keeps done * 100 from overflowing.
    const long double share = static_cast<long double>(state.done) * 100 / static_cast<long double>(state.total);
    const int percent = static_cast<int>(share);
    return percent > 99 ? 99 : percent;
}

Jobs::Jobs() : impl_(new Impl) {}
Jobs::~Jobs() = default;

bool Jobs::begin(JobKind kind) {
    if (kind == JobKind::None) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.kind != JobKind::None) return false;
    impl_->state = JobState();
    impl_->state.kind = kind;
    impl_->stop_asked = false;
    return true;
}

void Jobs::progress(int step, std::uint64_t done, std::uint64_t total) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.kind == JobKind::None) return;
    impl_->state.step = step;
    impl_->state.done = done;
    impl_->state.total = total;
}

void Jobs::no_stopping() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.kind == JobKind::None) return;
    // A stop asked for just before this still stands: the job looks at stop_asked() once more after
    // calling this, before it moves anything, so a "Stop" that was answered true is never lost.
    impl_->state.can_stop = false;
}

void Jobs::end() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->state = JobState();
    impl_->stop_asked = false;
}

JobState Jobs::state() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}

bool Jobs::running() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state.kind != JobKind::None;
}

bool Jobs::stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.kind == JobKind::None || !impl_->state.can_stop) return false;
    impl_->stop_asked = true;
    return true;
}

bool Jobs::stop_asked() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stop_asked;
}

}  // namespace spz
