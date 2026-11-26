// SPDX-License-Identifier: GPL-3.0-or-later
// The real Executors (SPEC-022): a worker with a thread of its own, which runs its jobs one at a time in
// the order they were posted (the quick, fetch and long workers), and the UI thread's queue, which the UI
// loop empties.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "spz/executor.h"

namespace spz::platform {

class WorkerThread : public Executor {
public:
    WorkerThread();
    // Finishes the job that is running, drops those that wait, and joins the thread.
    ~WorkerThread() override;
    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    void post(std::function<void()> job) override;
    // Drops the jobs that wait, lets the running one end, and joins the thread. Later posts are dropped.
    void stop();

private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};

class UiQueue : public Executor {
public:
    void post(std::function<void()> job) override;   // any thread
    // Runs the jobs posted so far, in order, on the calling thread (the UI thread). Returns how many ran.
    int run_pending();
    // Waits up to `ms` for a job to be posted. True if one is waiting.
    bool wait(long ms);

private:
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<std::function<void()>> jobs_;
};

}  // namespace spz::platform
