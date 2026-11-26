// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/workers.h"

#include <pthread.h>

#include <utility>

#include "platform/guarded.h"

namespace spz::app {

void UiQueue::post(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
}

void UiQueue::run_all() {
    for (;;) {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        // A message that fails must not take the UI thread with it: the next one still runs.
        platform::run_guarded("a message on the UI thread", job);
    }
}

bool UiQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.empty();
}

Worker::Worker(std::string name) : name_(std::move(name)) {
    thread_ = std::thread([this] { loop(); });
    id_ = thread_.get_id();
}

Worker::~Worker() {
    stop();
    // Destroyed by a job of its own: the thread ends after that job, on its own.
    if (thread_.joinable()) thread_.detach();
}

void Worker::stop() {
    std::deque<Job> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        // SPEC-201: what waits is dropped, but for the kept jobs (the saves), which still run.
        std::deque<Job> kept;
        for (Job& job : jobs_) (job.kept ? kept : dropped).push_back(std::move(job));
        jobs_.swap(kept);
    }
    wake_.notify_all();
    if (std::this_thread::get_id() == id_) return;   // a thread cannot join itself
    if (thread_.joinable()) thread_.join();
    // What the dropped jobs hold goes here, on the thread that stops the worker, not under its lock.
}

void Worker::add(Job job, bool first) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        if (first) jobs_.push_front(std::move(job));
        else jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void Worker::post(std::function<void()> job) { add(Job{std::move(job), false}, false); }

void Worker::post_first(std::function<void()> job) { add(Job{std::move(job), false}, true); }

bool Worker::idle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.empty() && !running_;
}

bool Worker::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void Worker::loop() {
    pthread_setname_np(pthread_self(), name_.substr(0, 15).c_str());
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) return;              // stopping, with nothing left to do
        std::function<void()> job = std::move(jobs_.front().run);
        jobs_.pop_front();
        running_ = true;
        lock.unlock();
        // A job that fails, whatever it throws, must not take the thread with it: the next job still runs.
        const std::string where = "a job on the " + name_ + " worker";
        platform::run_guarded(where.c_str(), job);
        job = nullptr;                          // what it held goes before the next job, not under the lock
        lock.lock();
        running_ = false;
    }
}

}  // namespace spz::app
