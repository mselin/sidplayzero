// SPDX-License-Identifier: GPL-3.0-or-later
// The fakes of SPEC-021 and SPEC-025 (TF-021): what the unit tests give `core` in place of the outside
// world. Each counts its calls, so a test can show that the code under test went through the seam, and
// each does what the specification's table says of it. They use the C++ library and nothing else.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "spz/audio_sink.h"
#include "spz/clock.h"
#include "spz/engine.h"
#include "spz/executor.h"
#include "spz/files.h"
#include "spz/http.h"
#include "spz/sound_backend.h"
#include "spz/system.h"

namespace spz::fake {

// ---------------------------------------------------------------------------------------------------
// Time that moves only when the test says.
class Clock : public spz::Clock {
public:
    std::int64_t now_ms() const override { ++calls; return now_; }
    std::int64_t wall_time_s() const override { ++calls; return wall_start_s + now_ / 1000; }
    // A sleep is recorded and returns at once: the unit tests have one thread, and time is the test's.
    void sleep_ms(std::int64_t ms) override { ++calls; slept_ms += ms; ++sleeps; }
    TimerId call_after(std::int64_t ms, std::function<void()> fn) override {
        ++calls;
        timers_.push_back({++last_id_, now_ + ms, std::move(fn)});
        return last_id_;
    }
    void cancel(TimerId id) override {
        ++calls;
        timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; }),
                      timers_.end());
    }

    // Moves time on by `ms`, firing each timer that falls due, in the order they fall due.
    void advance(std::int64_t ms) {
        const std::int64_t end = now_ + ms;
        for (;;) {
            auto next = timers_.end();
            for (auto it = timers_.begin(); it != timers_.end(); ++it) {
                if (it->due <= end && (next == timers_.end() || it->due < next->due ||
                                       (it->due == next->due && it->id < next->id))) {
                    next = it;
                }
            }
            if (next == timers_.end()) break;
            Timer timer = std::move(*next);
            timers_.erase(next);
            now_ = std::max(now_, timer.due);
            timer.fn();
        }
        now_ = end;
    }
    std::size_t timers_waiting() const { return timers_.size(); }

    mutable int calls = 0;
    int sleeps = 0;
    std::int64_t slept_ms = 0;
    std::int64_t wall_start_s = 1795564800;  // 25 November 2026, 00:00 UTC

private:
    struct Timer {
        TimerId id;
        std::int64_t due;
        std::function<void()> fn;
    };
    std::int64_t now_ = 0;
    TimerId last_id_ = 0;
    std::vector<Timer> timers_;
};

// ---------------------------------------------------------------------------------------------------
// An executor that runs its jobs when the test says.
class Executor : public spz::Executor {
public:
    void post(std::function<void()> job) override { ++calls; jobs_.push_back(std::move(job)); }
    std::size_t waiting() const { return jobs_.size(); }
    // Runs the oldest job. False if there was none.
    bool run_one() {
        if (jobs_.empty()) return false;
        auto job = std::move(jobs_.front());
        jobs_.pop_front();
        job();
        return true;
    }
    // Runs jobs until none is left, those posted meanwhile included. Returns how many ran.
    int run_all() {
        int ran = 0;
        while (run_one()) ++ran;
        return ran;
    }
    int calls = 0;

private:
    std::deque<std::function<void()>> jobs_;
};

// ---------------------------------------------------------------------------------------------------
// A folder of the test's own, with failures that can be put at any operation.
class Files : public spz::Files {
public:
    struct Operation {
        std::string what;   // "read", "write", "sync", "rename", "remove", "make_folders", "list", "info",
                            // "free_bytes", "can_write"
        std::string path;
        std::string to;     // for rename
    };

    // The n-th operation from now (1 is the next) fails with `status` and does nothing.
    void fail_at(int n, FileStatus status = FileStatus::Failed) { fail_in_ = n; fail_status_ = status; }
    // Every write, rename, remove and make_folders under `prefix` fails with NoAccess.
    void make_read_only(const std::string& prefix) { read_only_.push_back(prefix); }
    // What free_bytes reports, in place of the real figure.
    void set_free_bytes(std::uint64_t bytes) { free_override_ = true; free_bytes_ = bytes; }
    // Called before every operation, for a test that wants to do something in the middle of one.
    std::function<void(const Operation&)> before;

    std::vector<Operation> log;
    int calls = 0;

    int count(const std::string& what) const {
        return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const Operation& o) { return o.what == what; }));
    }

    FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) override {
        if (FileStatus s = begin({"read", path, ""}); s != FileStatus::Ok) return s;
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) return FileStatus::NotFound;
        if (size > limit) return FileStatus::TooLarge;
        std::ifstream in(path, std::ios::binary);
        if (!in) return FileStatus::NoAccess;
        out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return FileStatus::Ok;
    }
    FileStatus write(const std::string& path, const std::vector<std::uint8_t>& bytes) override {
        if (FileStatus s = begin({"write", path, ""}, true); s != FileStatus::Ok) return s;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return FileStatus::NoAccess;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return out ? FileStatus::Ok : FileStatus::Failed;
    }
    FileStatus sync(const std::string& path) override { return begin({"sync", path, ""}); }
    FileStatus rename(const std::string& from, const std::string& to) override {
        if (FileStatus s = begin({"rename", from, to}, true); s != FileStatus::Ok) return s;
        if (is_read_only(to)) return FileStatus::NoAccess;
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        return ec ? FileStatus::Failed : FileStatus::Ok;
    }
    FileStatus remove(const std::string& path) override {
        if (FileStatus s = begin({"remove", path, ""}, true); s != FileStatus::Ok) return s;
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return ec ? FileStatus::Failed : FileStatus::Ok;
    }
    FileStatus make_folders(const std::string& path) override {
        if (FileStatus s = begin({"make_folders", path, ""}, true); s != FileStatus::Ok) return s;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return ec ? FileStatus::Failed : FileStatus::Ok;
    }
    FileStatus list(const std::string& folder, std::vector<FileInfo>* out) override {
        if (FileStatus s = begin({"list", folder, ""}); s != FileStatus::Ok) return s;
        std::error_code ec;
        std::filesystem::directory_iterator it(folder, ec);
        if (ec) return std::filesystem::exists(folder) ? FileStatus::NoAccess : FileStatus::NotFound;
        out->clear();
        for (const auto& entry : it) out->push_back(describe(entry.path()));
        return FileStatus::Ok;
    }
    FileStatus info(const std::string& path, FileInfo* out) override {
        if (FileStatus s = begin({"info", path, ""}); s != FileStatus::Ok) return s;
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::symlink_status(path, ec))) return FileStatus::NotFound;
        *out = describe(path);
        return FileStatus::Ok;
    }
    FileStatus free_bytes(const std::string& path, std::uint64_t* out) override {
        if (FileStatus s = begin({"free_bytes", path, ""}); s != FileStatus::Ok) return s;
        if (free_override_) { *out = free_bytes_; return FileStatus::Ok; }
        std::error_code ec;
        std::filesystem::path p(path);
        while (!p.empty() && !std::filesystem::exists(p, ec)) p = p.parent_path();
        const auto space = std::filesystem::space(p.empty() ? std::filesystem::path("/") : p, ec);
        if (ec) return FileStatus::Failed;
        *out = space.available;
        return FileStatus::Ok;
    }
    bool can_write(const std::string& folder) override {
        if (begin({"can_write", folder, ""}) != FileStatus::Ok) return false;
        return std::filesystem::is_directory(folder) && !is_read_only(folder);
    }

private:
    FileStatus begin(Operation op, bool writes = false) {
        ++calls;
        log.push_back(op);
        if (before) before(op);
        if (fail_in_ > 0 && --fail_in_ == 0) return fail_status_;
        if (writes && is_read_only(op.path)) return FileStatus::NoAccess;
        return FileStatus::Ok;
    }
    bool is_read_only(const std::string& path) const {
        return std::any_of(read_only_.begin(), read_only_.end(),
                           [&](const std::string& prefix) { return path.compare(0, prefix.size(), prefix) == 0; });
    }
    static FileInfo describe(const std::filesystem::path& path) {
        FileInfo info;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        info.name = path.filename().string();
        info.is_link = std::filesystem::is_symlink(status);
        info.is_folder = std::filesystem::is_directory(status);
        info.is_special = !info.is_link && !info.is_folder && !std::filesystem::is_regular_file(status);
        if (std::filesystem::is_regular_file(status)) info.size = std::filesystem::file_size(path, ec);
        const auto time = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            const auto system = std::chrono::time_point_cast<std::chrono::seconds>(
                time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            info.modified_s = system.time_since_epoch().count();
        }
        return info;
    }

    int fail_in_ = 0;
    FileStatus fail_status_ = FileStatus::Failed;
    std::vector<std::string> read_only_;
    bool free_override_ = false;
    std::uint64_t free_bytes_ = 0;
};

// ---------------------------------------------------------------------------------------------------
// A table of addresses with canned answers and failures, and a log of what was asked.
class HttpClient : public spz::HttpClient {
public:
    struct Answer {
        int status = 200;
        std::vector<std::uint8_t> body;
        HttpFailure failure = HttpFailure::None;   // fail in this way, writing nothing
        std::string etag;
        std::string last_modified;
        std::uint64_t cut_after = 0;                // if not 0: write this many bytes, then fail as Network
        int steps = 1;                              // how many times progress is reported on the way
    };

    void answer(const std::string& url, Answer a) { answers_[url] = std::move(a); }
    void answer(const std::string& url, const std::string& body) {
        Answer a;
        a.body.assign(body.begin(), body.end());
        answers_[url] = std::move(a);
    }
    // Every request fails as if there were no network, whatever the table says.
    bool down = false;
    // A test whose code must not use the network sets this; any request is then recorded here.
    bool forbidden = false;
    int forbidden_requests = 0;

    std::vector<HttpRequest> log;
    int calls = 0;

    HttpResult fetch(const HttpRequest& request, const Progress& progress) override {
        ++calls;
        log.push_back(request);
        HttpResult result;
        if (forbidden) { ++forbidden_requests; result.failure = HttpFailure::Network; return result; }
        if (down) { result.failure = HttpFailure::Network; return result; }
        if (request.url.compare(0, 8, "https://") != 0) { result.failure = HttpFailure::Tls; return result; }
        const auto found = answers_.find(request.url);
        if (found == answers_.end()) { result.failure = HttpFailure::NotFound; result.status = 404; return result; }
        const Answer& a = found->second;
        result.status = a.status;
        if (a.failure != HttpFailure::None) { result.failure = a.failure; return result; }
        if (a.status == 404 || a.status == 410) { result.failure = HttpFailure::NotFound; return result; }
        if (a.status >= 400) { result.failure = HttpFailure::Network; return result; }
        result.etag = a.etag;
        result.last_modified = a.last_modified;
        result.total = a.body.size();
        // A range is honoured only when the server gave validators and they still match.
        std::uint64_t from = 0;
        if (request.resume_from > 0 && !a.etag.empty() && request.if_etag == a.etag &&
            request.if_last_modified == a.last_modified && request.resume_from <= a.body.size()) {
            from = request.resume_from;
            result.resumed = true;
            result.status = 206;
        }
        std::uint64_t end = a.body.size();
        if (a.cut_after > 0 && a.cut_after < end) end = std::max<std::uint64_t>(a.cut_after, from);
        if (request.limit_bytes > 0 && a.body.size() > request.limit_bytes) { result.failure = HttpFailure::TooLarge; return result; }
        std::ofstream out(request.to_path, std::ios::binary | (from > 0 ? std::ios::app : std::ios::trunc));
        if (!out) { result.failure = HttpFailure::CannotWrite; return result; }
        const int steps = std::max(1, a.steps);
        std::uint64_t done = from;
        for (int step = 1; step <= steps; ++step) {
            const std::uint64_t upto = from + (end - from) * static_cast<std::uint64_t>(step) / static_cast<std::uint64_t>(steps);
            out.write(reinterpret_cast<const char*>(a.body.data()) + done, static_cast<std::streamsize>(upto - done));
            done = upto;
            result.bytes = done - from;
            if (progress && !progress(done, result.total)) { result.failure = HttpFailure::Cancelled; return result; }
        }
        if (end < a.body.size()) result.failure = HttpFailure::Network;
        return result;
    }

private:
    std::map<std::string, Answer> answers_;
};

// ---------------------------------------------------------------------------------------------------
// An engine that produces a known ramp of samples.
class Engine : public spz::Engine {
public:
    // The left sample of frame `index` of `subtune`; the right one is its negative.
    static std::int16_t sample(int subtune, std::uint64_t index) {
        return static_cast<std::int16_t>((static_cast<std::uint64_t>(subtune) * 1000 + index) % 32000);
    }

    bool refuse = false;                 // load() fails
    std::string refuse_why = "MSG_ENGINE_REFUSED";
    std::size_t frames_per_call = 0;     // if not 0: render() gives at most this many, an engine too slow
    int chip_count = 1;
    std::uint8_t register_values[3][kRegisters] = {};

    int loads = 0, renders = 0, calls = 0;
    int loaded_subtune = 0;
    EngineSettings loaded_settings;
    std::uint64_t rendered = 0;
    bool muted[3][3] = {};
    bool filter_on[3] = {true, true, true};

    bool load(const std::vector<std::uint8_t>&, int subtune, const EngineSettings& settings, std::string* why) override {
        ++calls; ++loads;
        if (refuse) { if (why) *why = refuse_why; return false; }
        loaded_subtune = subtune;
        loaded_settings = settings;
        rendered = 0;
        return true;
    }
    std::size_t render(std::int16_t* out, std::size_t frames) override {
        ++calls; ++renders;
        if (frames_per_call > 0) frames = std::min(frames, frames_per_call);
        for (std::size_t i = 0; i < frames; ++i) {
            const std::int16_t value = sample(loaded_subtune, rendered + i);
            out[2 * i] = value;
            out[2 * i + 1] = static_cast<std::int16_t>(-value);
        }
        rendered += frames;
        return frames;
    }
    int chips() const override { return chip_count; }
    void set_muted(int chip, int voice, bool on) override { ++calls; muted[chip][voice] = on; }
    void set_filter(int chip, bool on) override { ++calls; filter_on[chip] = on; }
    bool registers(int chip, std::uint8_t out[kRegisters]) override {
        ++calls;
        std::memcpy(out, register_values[chip], kRegisters);
        return true;
    }
};

// ---------------------------------------------------------------------------------------------------
// A sound device that pulls when the test says and records what it got.
class AudioSink : public spz::AudioSink {
public:
    bool refuse = false;                     // open() fails
    int opens = 0, closes = 0, calls = 0;
    unsigned sample_rate = 0;
    bool is_open = false;
    std::vector<std::int16_t> received;      // every sample pulled, left and right in turn

    bool open(unsigned rate, Pull pull) override {
        ++calls; ++opens;
        if (refuse) return false;
        sample_rate = rate;
        pull_ = std::move(pull);
        is_open = true;
        return true;
    }
    void close() override { ++calls; ++closes; is_open = false; pull_ = nullptr; }
    void on_device_lost(std::function<void()> fn) override { ++calls; lost_ = std::move(fn); }

    // Pulls `frames` frames, as the device would, and keeps them.
    void pull(std::size_t frames) {
        if (!is_open || !pull_) return;
        std::vector<std::int16_t> buffer(frames * 2);
        pull_(buffer.data(), frames);
        received.insert(received.end(), buffer.begin(), buffer.end());
    }
    // The device goes away, as when headphones are plugged in.
    void drop_device() { if (lost_) lost_(); }

private:
    Pull pull_;
    std::function<void()> lost_;
};

// ---------------------------------------------------------------------------------------------------
// A sound backend that records what it is told and reports what the test tells it to.
class SoundBackend : public spz::SoundBackend {
public:
    explicit SoundBackend(const spz::Clock* clock = nullptr) : clock_(clock) {}

    std::vector<std::string> log;            // "load 2", "start", "pause", "set_length 96000", ...
    int calls = 0;
    bool refuse = false;                     // load() fails
    std::string refuse_why = "MSG_ENGINE_REFUSED";
    BackendAbilities abilities{true, true, true, true};
    int chip_count = 1;
    std::uint64_t underrun_frames = 0;
    std::uint8_t register_values[3][Engine::kRegisters] = {};
    EngineSettings loaded_settings;
    int loaded_subtune = 0;
    std::uint64_t length = 0;
    bool loaded = false, running = false;

    // The events, as the test wants them.
    void emit_song_ended() { if (listener_) listener_->song_ended(); }
    void emit_device_lost() { if (listener_) listener_->device_lost(); }
    void emit_device_back() { if (listener_) listener_->device_back(); }
    void emit_device_gone() { if (listener_) listener_->device_gone(); }
    void emit_failed(const std::string& reason) { if (listener_) listener_->failed(reason); }

    int count(const std::string& what) const {
        return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const std::string& entry) {
            return entry == what || entry.compare(0, what.size() + 1, what + " ") == 0;
        }));
    }

    void set_listener(SoundBackendListener* listener) override { listener_ = listener; }
    bool load(const std::vector<std::uint8_t>&, int subtune, const EngineSettings& settings, std::string* why) override {
        note("load " + std::to_string(subtune));
        if (refuse) { if (why) *why = refuse_why; return false; }
        settle();
        loaded = true; running = false;
        loaded_subtune = subtune; loaded_settings = settings;
        position_ = 0; length = 0;
        return true;
    }
    void start() override { note("start"); settle(); running = true; }
    void pause() override { note("pause"); settle(); running = false; }
    void resume() override { note("resume"); settle(); running = true; }
    void stop() override { note("stop"); settle(); running = false; loaded = false; position_ = 0; }
    void set_length(std::uint64_t samples) override { note("set_length " + std::to_string(samples)); length = samples; }
    // Moves on with the fake clock, at the sample rate, while started and not paused (SPEC-025).
    std::uint64_t position() const override { const_cast<SoundBackend*>(this)->settle(); return position_; }
    void set_muted(int chip, int voice, bool on) override {
        note("set_muted " + std::to_string(chip) + " " + std::to_string(voice) + (on ? " on" : " off"));
    }
    void set_filter(int chip, bool on) override { note("set_filter " + std::to_string(chip) + (on ? " on" : " off")); }
    void set_skip(bool on) override { note(std::string("set_skip ") + (on ? "on" : "off")); }
    bool registers(int chip, std::uint8_t out[Engine::kRegisters]) override {
        ++calls;
        if (!abilities.registers) return false;
        std::memcpy(out, register_values[chip], Engine::kRegisters);
        return true;
    }
    int chips() const override { return chip_count; }
    BackendAbilities can() const override { return abilities; }
    std::uint64_t underruns() const override { return underrun_frames; }

private:
    void note(const std::string& entry) { ++calls; log.push_back(entry); }
    void settle() {
        if (!clock_) return;
        const std::int64_t now = clock_->now_ms();
        if (running) position_ += static_cast<std::uint64_t>(now - last_ms_) * loaded_settings.sample_rate / 1000;
        last_ms_ = now;
    }
    const spz::Clock* clock_;
    SoundBackendListener* listener_ = nullptr;
    std::uint64_t position_ = 0;
    std::int64_t last_ms_ = 0;
};

// ---------------------------------------------------------------------------------------------------
// A scripted stand-in for 7z.
class Unpacker : public spz::Unpacker {
public:
    bool is_available = true;
    std::vector<ArchiveEntry> entries;       // what list() reports
    std::string tree;                         // a folder whose contents unpack() copies into place
    int fail_after_files = -1;                // unpack() fails when it has written this many
    std::vector<std::string> log;
    int calls = 0;

    bool available() override { ++calls; return is_available; }
    bool list(const std::string& archive, std::vector<ArchiveEntry>* out) override {
        ++calls; log.push_back("list " + archive);
        if (!is_available) return false;
        *out = entries;
        return true;
    }
    bool unpack(const std::string& archive, const std::string& into, const Progress& progress) override {
        ++calls; log.push_back("unpack " + archive + " " + into);
        if (!is_available) return false;
        std::uint64_t files = 0;
        std::error_code ec;
        if (tree.empty()) return true;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(tree)) {
            const auto target = std::filesystem::path(into) / std::filesystem::relative(entry.path(), tree);
            if (entry.is_directory()) { std::filesystem::create_directories(target, ec); continue; }
            if (fail_after_files >= 0 && files == static_cast<std::uint64_t>(fail_after_files)) return false;
            std::filesystem::create_directories(target.parent_path(), ec);
            std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
            ++files;
            if (progress && !progress(files)) return false;
        }
        return true;
    }
};

// A recorder holding a volume level.
class SystemVolume : public spz::SystemVolume {
public:
    int level = 50;
    bool muted = false;
    bool works = true;
    std::vector<std::string> log;
    int calls = 0;

    bool get(int* percent, bool* is_muted) override {
        ++calls; log.push_back("get");
        if (!works) return false;
        *percent = level; *is_muted = muted;
        return true;
    }
    bool set(int percent) override {
        ++calls; log.push_back("set " + std::to_string(percent));
        if (!works) return false;
        level = percent;
        return true;
    }
    bool toggle_mute() override {
        ++calls; log.push_back("toggle_mute");
        if (!works) return false;
        muted = !muted;
        return true;
    }
};

// A recorder holding a brightness level.
class Backlight : public spz::Backlight {
public:
    int level = 70;
    bool writable = true;
    std::vector<std::string> log;
    int calls = 0;

    bool can_change() override { ++calls; return writable; }
    bool get(int* percent) override { ++calls; log.push_back("get"); *percent = level; return true; }
    bool set(int percent) override {
        ++calls; log.push_back("set " + std::to_string(percent));
        if (!writable) return false;
        level = percent;
        return true;
    }
};

// Memory figures set by the test.
class Memory : public spz::Memory {
public:
    std::uint64_t available = 300;
    std::uint64_t swap_free = 200;
    int calls = 0;
    std::uint64_t available_mib() override { ++calls; return available; }
    std::uint64_t swap_free_mib() override { ++calls; return swap_free; }
};

}  // namespace spz::fake
