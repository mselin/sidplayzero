// SPDX-License-Identifier: GPL-3.0-or-later
// The index where threads meet (SPEC-022): what the screens ask of it on the UI thread is answered at once,
// even while a search on the quick worker holds it (SPEC-162's 2 s).
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "browse_support.h"
#include "doctest.h"
#include "fakes.h"
#include "search_support.h"
#include "spz/index.h"
#include "spz/search.h"
#include "support.h"

using spz::test::SearchRoot;

namespace {

// A clock that holds every thread but the test's in now_ms() while `holding`, until it is let go: a search
// asks it the time at its start, with the index in its hands.
class HoldingClock : public spz::fake::Clock {
public:
    std::int64_t now_ms() const override {
        if (std::this_thread::get_id() != test_thread_) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (holding_) {
                inside_ = true;
                wake_.notify_all();
                wake_.wait(lock, [this] { return !holding_; });
            }
        }
        return spz::fake::Clock::now_ms();
    }
    void hold() {
        std::lock_guard<std::mutex> lock(mutex_);
        holding_ = true;
    }
    void wait_inside() {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return inside_; });
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        holding_ = false;
        wake_.notify_all();
    }

private:
    std::thread::id test_thread_ = std::this_thread::get_id();
    mutable std::mutex mutex_;
    mutable std::condition_variable wake_;
    mutable bool inside_ = false;
    bool holding_ = false;
};

}  // namespace

TEST_CASE("SPEC-022 SPEC-161 SPEC-162 while a search holds the index, whether it is complete and how far its build is are answered at once") {
    SearchRoot t;
    HoldingClock clock;
    spz::Index index(&t.files, &clock, t.open_listing(), t.root, t.index_file());
    REQUIRE(index.build(spz::IndexReason::Start).complete);

    clock.hold();
    std::thread searcher([&] { spz::search(index, "hub", spz::SearchFilter()); });
    clock.wait_inside();                       // the search has the index now, and waits on the clock

    bool complete = false;
    int percent = -1;
    std::future<void> asked = std::async(std::launch::async, [&] {
        complete = index.complete();
        percent = index.percent();
    });
    const bool at_once = asked.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
    clock.release();
    asked.wait();
    searcher.join();
    CHECK(at_once);
    CHECK(complete);
    CHECK(percent == 100);
}
