// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/workers.h"

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <utility>

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
        job();
    }
}

bool UiQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.empty();
}

Worker::Worker(std::string name, bool finish_on_exit) : name_(std::move(name)), finish_on_exit_(finish_on_exit) {
    thread_ = std::thread([this] { loop(); });
}

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (!finish_on_exit_) jobs_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Worker::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void Worker::post_first(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        jobs_.push_front(std::move(job));
    }
    wake_.notify_one();
}

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
        std::function<void()> job = std::move(jobs_.front());
        jobs_.pop_front();
        running_ = true;
        lock.unlock();
        try {
            job();
        } catch (const std::exception& error) {
            // A job that fails must not take the thread with it: the next job still runs.
            std::fprintf(stderr, "sidplayzero: a job on the %s worker failed: %s\n", name_.c_str(), error.what());
        }
        lock.lock();
        running_ = false;
    }
}

}  // namespace spz::app
