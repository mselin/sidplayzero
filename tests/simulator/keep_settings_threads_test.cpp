// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-022 and SPEC-130: a setting changed on the UI thread while a job on a worker reads it. The job is
// handed the settings as the composition hands them (src/app/compose_hvsc.cpp: the SettingsFile itself),
// and reads `url_files` on its own thread while the UI thread sets it again and again. Whether two threads
// touch the same bytes without a lock is a question for ThreadSanitizer, so this is one of the simulator
// level's C++ tests: the integration level runs them in its ThreadSanitizer build
// (tests/integration/test_keep_threads.py runs this test there and reads what ThreadSanitizer says).
#include <stdlib.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "doctest.h"
#include "platform/real_files.h"
#include "spz/executor.h"
#include "spz/http.h"
#include "spz/hvsc_setup.h"
#include "spz/job.h"
#include "spz/notice_gate.h"
#include "spz/settings_file.h"

namespace {

// A worker thread of its own, as the app's long worker is (SPEC-022).
class ThreadExecutor : public spz::Executor {
public:
    ThreadExecutor() : thread_([this] { loop(); }) {}
    ~ThreadExecutor() override {
        {
            std::lock_guard<std::mutex> hold(lock_);
            stopping_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    void post(std::function<void()> job) override {
        {
            std::lock_guard<std::mutex> hold(lock_);
            jobs_.push_back(std::move(job));
        }
        wake_.notify_all();
    }

private:
    void loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> hold(lock_);
                wake_.wait(hold, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};

// The UI thread's queue: anything may post to it; the test's own thread runs what is there.
class UiExecutor : public spz::Executor {
public:
    void post(std::function<void()> job) override {
        std::lock_guard<std::mutex> hold(lock_);
        jobs_.push_back(std::move(job));
    }
    int run_all() {
        std::deque<std::function<void()>> now;
        {
            std::lock_guard<std::mutex> hold(lock_);
            now.swap(jobs_);
        }
        for (auto& job : now) job();
        return static_cast<int>(now.size());
    }

private:
    std::mutex lock_;
    std::deque<std::function<void()>> jobs_;
};

// Saves are not what this test is about: they are dropped.
class Dropping : public spz::Executor {
public:
    void post(std::function<void()>) override {}
};

// Every request fails as if there were no network, and says which address it was asked for.
class NoNetwork : public spz::HttpClient {
public:
    spz::HttpResult fetch(const spz::HttpRequest& request, const Progress&) override {
        std::lock_guard<std::mutex> hold(lock_);
        asked.insert(request.url.substr(0, request.url.find("/DOCUMENTS")));
        spz::HttpResult result;
        result.failure = spz::HttpFailure::Network;
        return result;
    }
    std::set<std::string> seen() {
        std::lock_guard<std::mutex> hold(lock_);
        return asked;
    }

private:
    std::mutex lock_;
    std::set<std::string> asked;
};

class Accepted : public spz::NoticeRecord {
public:
    bool notice_accepted() const override { return true; }
    void accept_notice() override {}
};

class NoPanel : public spz::NoticePanel {
public:
    void open_notice_panel(const std::string&) override {}
};

}  // namespace

TEST_CASE("SPEC-022 SPEC-130 SPEC-140 a set-up reads url_files on the long worker while the UI thread changes it, and the two never touch the same setting without a lock") {
    char name[] = "/tmp/spz-settings-threads-XXXXXX";
    REQUIRE(mkdtemp(name) != nullptr);
    const std::string folder = name;
    {
        spz::platform::RealFiles files;
        Dropping saves;
        spz::SettingsFile settings(&files, &saves, folder + "/settings.ini");
        settings.load();
        const std::string first = "https://first.example/C64Music";
        const std::string second = "https://second.example/C64Music";
        REQUIRE(settings.set("url_files", first));

        ThreadExecutor long_worker;
        UiExecutor ui;
        Accepted record;
        NoPanel panel;
        spz::NoticeGate gate(&record, &panel);
        spz::Jobs jobs;
        NoNetwork http;
        spz::HvscSetupParts parts;
        parts.files = &files;
        parts.http = &http;
        parts.long_worker = &long_worker;
        parts.ui = &ui;
        parts.gate = &gate;
        parts.jobs = &jobs;
        parts.settings = &settings;          // as the composition hands it
        parts.root = folder + "/C64Music";
        parts.cache = folder + "/cache";
        spz::HvscSetup setup(parts);

        for (int round = 0; round < 50; ++round) {
            REQUIRE(setup.start());
            // The job is on its way to the long worker: the UI thread changes the setting meanwhile.
            for (int change = 0; change < 20; ++change) {
                REQUIRE(settings.set("url_files", change % 2 == 0 ? second : first));
            }
            for (int wait = 0; wait < 10000 && jobs.running(); ++wait) {
                ui.run_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE_FALSE(jobs.running());
        }
        // Every address the job asked for is one of the two values, whole.
        for (const std::string& asked : http.seen()) {
            CAPTURE(asked);
            CHECK((asked == first || asked == second));
        }
    }
    std::filesystem::remove_all(folder);
}
