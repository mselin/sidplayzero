// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/real_clock.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "platform/guarded.h"

namespace spz::platform {

std::int64_t RealClock::now_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t RealClock::wall_time_s() const {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::int64_t RealClock::wall_time_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void RealClock::sleep_ms(std::int64_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

Clock::TimerId RealClock::call_after(std::int64_t ms, std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.push_back({++last_id_, now_ms() + ms, std::move(fn)});
    return last_id_;
}

void RealClock::cancel(TimerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; }), timers_.end());
}

void RealClock::run_due() {
    for (;;) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::int64_t now = now_ms();
            auto next = timers_.end();
            for (auto it = timers_.begin(); it != timers_.end(); ++it) {
                if (it->due <= now && (next == timers_.end() || it->due < next->due)) next = it;
            }
            if (next == timers_.end()) return;
            fn = std::move(next->fn);
            timers_.erase(next);
        }
        // A timer that throws does not take the UI loop with it: the next one still runs (SPEC-022).
        run_guarded("a timer", fn);
    }
}

}  // namespace spz::platform
