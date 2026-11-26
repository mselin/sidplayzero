// SPDX-License-Identifier: GPL-3.0-or-later
// The app's threads (SPEC-022), each an Executor that `core` hands work to: the UI thread's queue of
// messages, and the quick, fetch and long workers and the render loop, each a thread of its own that runs
// one job at a time, in the order asked.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "spz/executor.h"

namespace spz::app {

// The UI thread's queue: a worker's result reaches the screens as a job posted here, which the UI loop
// runs. post() may be called from any thread.
class UiQueue : public Executor {
public:
    void post(std::function<void()> job) override;
    // Runs the jobs waiting now, and those they post, until none is left. On the UI thread.
    void run_all();
    // Whether a job is waiting (part of "settled", SPEC-253).
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> jobs_;
};

// A worker thread. Jobs run one at a time, in the order posted, except those posted with post_first(),
// which go ahead of everything waiting: on the fetch worker, the tune that is to be played (SPEC-022).
class Worker : public Executor {
public:
    // With `finish_on_exit`, the jobs that wait when it is destroyed still run (the quick worker's saves).
    explicit Worker(std::string name, bool finish_on_exit = false);
    // Lets the job that runs finish, drops those that wait (or runs them, as above), and joins the thread.
    ~Worker() override;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void post(std::function<void()> job) override;
    void post_first(std::function<void()> job);

    // No job waits and none runs.
    bool idle() const;
    // A job runs now.
    bool running() const;
    const std::string& name() const { return name_; }

private:
    void loop();

    std::string name_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool finish_on_exit_ = false;
    bool running_ = false;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace spz::app
