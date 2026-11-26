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
    explicit Worker(std::string name);
    // stop(), if it has not been called.
    ~Worker() override;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void post(std::function<void()> job) override;
    void post_first(std::function<void()> job);
    // The executor of the jobs that must not be lost at exit: they are posted to this worker, in their
    // order among the others, and stop() still runs those that wait (the quick worker's saves of the
    // settings and the state, SPEC-201). Every other job that waits at exit is dropped.
    Executor* kept() { return &kept_; }

    // Lets the job that runs finish, drops those that wait (but runs the kept ones), and joins the
    // thread. Later posts are dropped. Called on the worker's own thread (by a job of its own), it only
    // asks the thread to end after that job: a thread cannot wait for itself.
    void stop();

    // No job waits and none runs.
    bool idle() const;
    // A job runs now.
    bool running() const;
    const std::string& name() const { return name_; }
    // The worker's thread.
    std::thread::id id() const { return id_; }

private:
    struct Job {
        std::function<void()> run;
        bool kept = false;
    };
    struct Kept : Executor {
        explicit Kept(Worker* of) : worker(of) {}
        void post(std::function<void()> job) override { worker->add(Job{std::move(job), true}, false); }
        Worker* worker;
    };
    void add(Job job, bool first);
    void loop();

    std::string name_;
    Kept kept_{this};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    bool running_ = false;
    bool stopping_ = false;
    std::thread thread_;
    std::thread::id id_;
};

}  // namespace spz::app
