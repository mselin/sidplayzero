// SPDX-License-Identifier: GPL-3.0-or-later
// The fakes a simulator run is given (SPEC-253): set up from the --fakes folder, changed by the script's
// `fake` command, and looked at by its `expect-...` commands. The app's threads use them at once, so each
// is wrapped here in what makes it safe for that: the fake clock, the files and the server, and the
// recording sink, which pulls inside the audio callback's guard (SPEC-022).
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "fakes.h"

namespace spz::hooks {

// Time that moves only when the script says (SPEC-253), safe to read from any thread. Its timers fire on
// the thread that moves it, the UI thread, with no lock held.
class SimClock : public Clock {
public:
    std::int64_t now_ms() const override;
    std::int64_t wall_time_s() const override;
    void sleep_ms(std::int64_t ms) override;
    TimerId call_after(std::int64_t ms, std::function<void()> fn) override;
    void cancel(TimerId id) override;

    // Moves time on by `ms`, firing each timer that falls due, in the order they fall due.
    void advance(std::int64_t ms);
    // Waits, on another thread, until the clock has reached `when`, or `give_up` says stop. True if it has.
    bool wait_until(std::int64_t when, const std::atomic<bool>& give_up);

private:
    struct Timer {
        TimerId id;
        std::int64_t due;
        std::function<void()> fn;
    };
    mutable std::mutex mutex_;
    std::condition_variable moved_;
    std::int64_t now_ = 0;
    TimerId last_id_ = 0;
    std::vector<Timer> timers_;
    std::int64_t wall_start_s_ = 1795564800;   // 25 November 2026, 00:00 UTC, as the unit tests' fake
};

// Which threads wait now for something a fake holds back: the fake clock, or a server's answer. Each wait
// says, when asked, whether it still holds: a listing whose time has come is no longer held, even before
// its thread has woken, so that "settled" waits for what it does next.
class Holds {
public:
    void hold(std::thread::id thread, std::function<bool()> still);
    void release(std::thread::id thread);
    bool held(std::thread::id thread) const;
    // Whether any thread is held now.
    bool any() const;

private:
    mutable std::mutex mutex_;
    std::map<std::thread::id, std::function<bool()>> held_;
};

// The fake files, one operation at a time, with SPEC-253's `files.list_delay_ms`: a folder takes that long
// of the fake clock to list.
class SimFiles : public Files {
public:
    SimFiles(fake::Files* files, SimClock* clock, Holds* holds) : files_(files), clock_(clock), holds_(holds) {}
    FileStatus read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) override;
    FileStatus write(const std::string& path, const std::vector<std::uint8_t>& bytes) override;
    FileStatus append(const std::string& path, const std::vector<std::uint8_t>& bytes) override;
    FileStatus map(const std::string& path, std::size_t limit, MappedFile* out) override;
    FileStatus sync(const std::string& path) override;
    FileStatus rename(const std::string& from, const std::string& to) override;
    FileStatus remove(const std::string& path) override;
    FileStatus make_folders(const std::string& path) override;
    FileStatus list(const std::string& folder, std::vector<FileInfo>* out) override;
    FileStatus info(const std::string& path, FileInfo* out) override;
    FileStatus free_bytes(const std::string& path, std::uint64_t* out) override;
    FileStatus block_size(const std::string& path, std::uint64_t* out) override;
    bool can_write(const std::string& folder) override;

    std::atomic<std::int64_t> list_delay_ms{0};
    std::atomic<bool> root_writable{true};     // SPEC-253: `files.root_writable`
    // Set at exit: whatever waits for the clock gives up.
    std::atomic<bool> stopping{false};
    std::mutex& lock() { return mutex_; }

private:
    fake::Files* files_;
    SimClock* clock_;
    Holds* holds_;
    std::mutex mutex_;
};

// The fake server, with a log of its own that the script may read while a request is held back.
class SimHttp : public HttpClient {
public:
    SimHttp(fake::HttpClient* http, Holds* holds) : http_(http), holds_(holds) {}
    HttpResult fetch(const HttpRequest& request, const Progress& progress) override;
    // The address of every request, in the order made.
    std::vector<std::string> urls() const;
    // `fake http release URL`.
    void release(const std::string& url);

private:
    bool released(const std::string& url) const;

    fake::HttpClient* http_;
    Holds* holds_;
    mutable std::mutex mutex_;
    std::vector<std::string> urls_;
    std::set<std::string> released_;
};

// The recording sink: it pulls when the script moves the clock (or, on the real clock, in real time), always
// inside the audio callback's guard, as the device's own callback is.
class SimSink : public AudioSink {
public:
    explicit SimSink(fake::AudioSink* sink) : sink_(sink) {}
    // The clock that `drop` counts on: the fake one, or the real one.
    void use_clock(const Clock* clock) { clock_ = clock; }
    bool open(unsigned sample_rate, Pull pull) override;
    void close() override;
    void on_device_lost(std::function<void()> fn) override;

    // Pulls this many frames, as the device would, and keeps them.
    void pull(std::size_t frames);
    unsigned sample_rate();
    bool is_open();
    // What the sink has received: every sample, left and right in turn.
    std::vector<std::int16_t> received();
    // `fake sink drop MS`: the device goes away for MS of the clock. A stream that is open is lost, and
    // none opens until the time is over.
    void drop_device(std::int64_t ms);

private:
    fake::AudioSink* sink_;
    const Clock* clock_ = nullptr;
    std::int64_t away_until_ = -1;
    std::recursive_mutex mutex_;
};

struct SimFakes {
    fake::HttpClient http;
    fake::SystemVolume volume;
    fake::Backlight backlight;
    fake::Memory memory;
    fake::Unpacker unpacker;
    fake::AudioSink sink;
    fake::Engine engine;
    fake::Files files;
    fake::SoundBackend backend;
    std::string folder;                  // the --fakes folder, "" if none
    std::int64_t list_delay_ms = 0;
    bool root_writable = true;
    std::size_t samples_saved = 0;       // how much of the received samples `save-samples` has written out

    // Where a file a script names is: "fixtures/..." is under tests/fixtures (the SPZ_FIXTURES variable),
    // anything else under the --fakes folder.
    std::string resolve(const std::string& name) const;
    // Reads fakes.ini and http.tsv from the folder. False, with the reason, if one cannot be read.
    bool load(const std::string& folder, const std::string& home, std::string* why);
    // `fake NAME VALUE` for the values of fakes.ini. False if there is no such value.
    bool set(const std::string& name, const std::string& value, std::string* why);
    // The log `expect-log NAME` looks at: "volume", "backlight" or "unpacker". False for another name.
    bool log(const std::string& name, std::string* text) const;
};

}  // namespace spz::hooks
