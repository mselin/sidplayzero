// SPDX-License-Identifier: GPL-3.0-or-later
// Seam: time (SPEC-021). `core` reads time, waits and sets timers only through this.
#pragma once

#include <cstdint>
#include <functional>

namespace spz {

class Clock {
public:
    using TimerId = std::uint64_t;

    virtual ~Clock() = default;

    // Milliseconds from an arbitrary start; never goes backwards.
    virtual std::int64_t now_ms() const = 0;
    // Seconds since 1970, for file names and for the shuffle seed.
    virtual std::int64_t wall_time_s() const = 0;
    // Waits. Never called on the UI thread.
    virtual void sleep_ms(std::int64_t ms) = 0;
    // Calls `fn` once, on the UI thread, when `ms` have passed. Returns a handle for cancel().
    virtual TimerId call_after(std::int64_t ms, std::function<void()> fn) = 0;
    // Forgets a timer that has not fired yet; does nothing if it has.
    virtual void cancel(TimerId id) = 0;
};

}  // namespace spz
