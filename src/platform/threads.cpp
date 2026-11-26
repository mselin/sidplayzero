// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/threads.h"

#include <chrono>

namespace spz::platform {

WorkerThread::WorkerThread() : thread_([this] { run(); }) {}

WorkerThread::~WorkerThread() { stop(); }

void WorkerThread::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void WorkerThread::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_one();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void WorkerThread::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

void UiQueue::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    posted_.notify_all();
}

int UiQueue::run_pending() {
    std::deque<std::function<void()>> now;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        now.swap(jobs_);
    }
    int ran = 0;
    for (auto& job : now) {
        job();
        ++ran;
    }
    return ran;
}

bool UiQueue::wait(long ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return posted_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return !jobs_.empty(); });
}

}  // namespace spz::platform
