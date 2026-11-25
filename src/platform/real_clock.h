// SPDX-License-Identifier: GPL-3.0-or-later
// The real clock (SPEC-021): the system's monotonic time, and timers that the UI loop runs.
#pragma once

#include <mutex>
#include <vector>

#include "spz/clock.h"

namespace spz::platform {

class RealClock : public Clock {
public:
    std::int64_t now_ms() const override;
    std::int64_t wall_time_s() const override;
    void sleep_ms(std::int64_t ms) override;
    TimerId call_after(std::int64_t ms, std::function<void()> fn) override;
    void cancel(TimerId id) override;

    // Runs the timers that have fallen due. Called by the UI loop, on the UI thread.
    void run_due();

private:
    struct Timer {
        TimerId id;
        std::int64_t due;
        std::function<void()> fn;
    };
    std::mutex mutex_;
    std::vector<Timer> timers_;
    TimerId last_id_ = 0;
};

}  // namespace spz::platform
