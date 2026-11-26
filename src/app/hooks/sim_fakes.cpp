// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hooks/sim_fakes.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "platform/audio_guard.h"
#include "spz/text.h"

namespace spz::hooks {

namespace {

// A line of a script or of a fakes file, as it is read: spaces, tabs and a CR at either end dropped.
std::string trim(const std::string& line) { return text::trim(line, text::kLineSpaces); }

bool truth(const std::string& value) { return value == "yes" || value == "true" || value == "on" || value == "1"; }

std::vector<std::string> split(const std::string& line, char separator) {
    std::vector<std::string> cells;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, separator)) cells.push_back(cell);
    return cells;
}

}  // namespace

// ---- SimClock ------------------------------------------------------------------------------------------------

std::int64_t SimClock::now_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now_;
}

std::int64_t SimClock::wall_time_s() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wall_start_s_ + now_ / 1000;
}

std::int64_t SimClock::wall_time_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wall_start_s_ * 1000 + now_;
}

void SimClock::sleep_ms(std::int64_t) {}   // a sleep returns at once: time is the script's

Clock::TimerId SimClock::call_after(std::int64_t ms, std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.push_back({++last_id_, now_ + ms, std::move(fn)});
    return last_id_;
}

void SimClock::cancel(TimerId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; }),
                  timers_.end());
}

void SimClock::advance(std::int64_t ms) {
    std::unique_lock<std::mutex> lock(mutex_);
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
        if (timer.due > now_) now_ = timer.due;
        lock.unlock();
        moved_.notify_all();
        timer.fn();
        lock.lock();
    }
    now_ = end;
    lock.unlock();
    moved_.notify_all();
}

bool SimClock::wait_until(std::int64_t when, const std::atomic<bool>& give_up) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (now_ < when) {
        if (give_up) return false;
        moved_.wait_for(lock, std::chrono::milliseconds(5));
    }
    return true;
}

// ---- Holds ---------------------------------------------------------------------------------------------------

void Holds::hold(std::thread::id thread, std::function<bool()> still) {
    std::lock_guard<std::mutex> lock(mutex_);
    held_[thread] = std::move(still);
}

void Holds::release(std::thread::id thread) {
    std::lock_guard<std::mutex> lock(mutex_);
    held_.erase(thread);
}

bool Holds::held(std::thread::id thread) const {
    std::function<bool()> still;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = held_.find(thread);
        if (found == held_.end()) return false;
        still = found->second;
    }
    return still();
}

bool Holds::any() const {
    std::vector<std::function<bool()>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : held_) all.push_back(entry.second);
    }
    for (const auto& still : all) {
        if (still()) return true;
    }
    return false;
}

// ---- SimFiles --------------------------------------------------------------------------------------------------

FileStatus SimFiles::read(const std::string& path, std::size_t limit, std::vector<std::uint8_t>* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->read(path, limit, out);
}
FileStatus SimFiles::write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->write(path, bytes);
}
FileStatus SimFiles::append(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->append(path, bytes);
}
FileStatus SimFiles::map(const std::string& path, std::size_t limit, MappedFile* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->map(path, limit, out);
}
FileStatus SimFiles::sync(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->sync(path);
}
FileStatus SimFiles::rename(const std::string& from, const std::string& to) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->rename(from, to);
}
FileStatus SimFiles::remove(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->remove(path);
}
FileStatus SimFiles::make_folders(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->make_folders(path);
}
FileStatus SimFiles::list(const std::string& folder, std::vector<FileInfo>* out) {
    const std::int64_t delay = list_delay_ms.load();
    if (delay > 0) {
        // SPEC-253: the listing takes this long of the fake clock. Meanwhile the thread counts as settled.
        const std::thread::id me = std::this_thread::get_id();
        const std::int64_t until = clock_->now_ms() + delay;
        SimClock* clock = clock_;
        holds_->hold(me, [clock, until] { return clock->now_ms() < until; });
        clock_->wait_until(until, stopping);
        holds_->release(me);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->list(folder, out);
}
FileStatus SimFiles::info(const std::string& path, FileInfo* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->info(path, out);
}
FileStatus SimFiles::free_bytes(const std::string& path, std::uint64_t* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->free_bytes(path, out);
}
FileStatus SimFiles::block_size(const std::string& path, std::uint64_t* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_->block_size(path, out);
}
bool SimFiles::can_write(const std::string& folder) {
    std::lock_guard<std::mutex> lock(mutex_);
    // `files.root_writable no`: the folder that holds the collection root, the one folder asked, cannot be written.
    return root_writable.load() && files_->can_write(folder);
}

// ---- SimHttp ---------------------------------------------------------------------------------------------------

HttpResult SimHttp::fetch(const HttpRequest& request, const Progress& progress) {
    const std::thread::id me = std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        urls_.push_back(request.url);
    }
    // While the fake holds the answer back, this thread counts as settled (SPEC-253), until the script lets
    // the answer go.
    const std::string url = request.url;
    holds_->hold(me, [this, url] { return http_->stalled() > 0 && !released(url); });
    const HttpResult result = http_->fetch(request, progress);
    holds_->release(me);
    return result;
}

std::vector<std::string> SimHttp::urls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return urls_;
}

void SimHttp::release(const std::string& url) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_.insert(url);
    }
    http_->release(url);
}

bool SimHttp::released(const std::string& url) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return released_.count(url) > 0;
}

// ---- SimSink ---------------------------------------------------------------------------------------------------

bool SimSink::open(unsigned sample_rate, Pull pull) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sink_->refuse = away_until_ >= 0 && clock_->now_ms() < away_until_;
    // SPEC-022: the callback runs inside the guard of the audio thread, as the device's does.
    return sink_->open(sample_rate, [pull](std::int16_t* out, std::size_t frames) {
        platform::AudioCallbackScope scope;
        pull(out, frames);
    });
}

void SimSink::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sink_->close();
}

void SimSink::on_device_lost(std::function<void()> fn) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    sink_->on_device_lost(std::move(fn));
}

void SimSink::pull(std::size_t frames) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sink_->is_open) sink_->pull(frames);
}

unsigned SimSink::sample_rate() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sink_->sample_rate;
}

bool SimSink::is_open() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sink_->is_open;
}

std::vector<std::int16_t> SimSink::received() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sink_->received;
}

void SimSink::drop_device(std::int64_t ms) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    away_until_ = clock_->now_ms() + ms;
    if (sink_->is_open) sink_->drop_device();
}

std::string SimFakes::resolve(const std::string& name) const {
    if (name.compare(0, 9, "fixtures/") == 0) {
        const char* fixtures = std::getenv("SPZ_FIXTURES");
        return std::string(fixtures ? fixtures : "tests/fixtures") + name.substr(8);
    }
    return folder.empty() ? name : folder + "/" + name;
}

bool SimFakes::set(const std::string& name, const std::string& value, std::string* why) {
    try {
        if (name == "memory.available_mib") memory.available = std::stoull(value);
        else if (name == "memory.swap_free_mib") memory.swap_free = std::stoull(value);
        else if (name == "files.free_mib") files.set_free_bytes(std::stoull(value) * 1024 * 1024);
        else if (name == "files.list_delay_ms") list_delay_ms = std::stoll(value);
        else if (name == "files.root_writable") root_writable = truth(value);
        else if (name == "volume.level") volume.level = std::stoi(value);
        else if (name == "volume.works") volume.works = truth(value);
        else if (name == "backlight.level") backlight.level = std::stoi(value);
        else if (name == "backlight.writable") backlight.writable = truth(value);
        else if (name == "unpacker.available") unpacker.is_available = truth(value);
        else if (name == "unpacker.tree") unpacker.tree = folder.empty() ? value : folder + "/" + value;
        else if (name == "engine.speed") engine.frames_per_call = static_cast<std::size_t>(std::stoull(value));
        else { *why = "there is no fake value named " + name; return false; }
    } catch (const std::exception&) {
        *why = "'" + value + "' is not a value for " + name;
        return false;
    }
    return true;
}

bool SimFakes::load(const std::string& from, const std::string&, std::string* why) {
    folder = from;
    http.forbidden = true;   // without http.tsv, any request fails the run
    if (folder.empty()) return true;

    std::ifstream ini(folder + "/fakes.ini");
    std::string line;
    int number = 0;
    while (std::getline(ini, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || !set(trim(line.substr(0, equals)), trim(line.substr(equals + 1)), why)) {
            *why = "fakes.ini line " + std::to_string(number) + ": " + (why->empty() ? "not name = value" : *why);
            return false;
        }
    }

    std::ifstream table(folder + "/http.tsv");
    if (!table) return true;
    http.forbidden = false;
    number = 0;
    while (std::getline(table, line)) {
        ++number;
        if (trim(line).empty() || line[0] == '#') continue;
        const std::vector<std::string> cells = split(line, '\t');
        if (cells.size() < 3) { *why = "http.tsv line " + std::to_string(number) + ": needs a URL, a status and a file"; return false; }
        fake::HttpClient::Answer answer;
        try {
            answer.status = std::stoi(cells[1]);
        } catch (const std::exception&) {
            *why = "http.tsv line " + std::to_string(number) + ": the status is not a number";
            return false;
        }
        if (cells[2] != "-") {
            std::ifstream body(resolve(cells[2]), std::ios::binary);
            if (!body) { *why = "http.tsv line " + std::to_string(number) + ": no file " + cells[2]; return false; }
            answer.body.assign(std::istreambuf_iterator<char>(body), std::istreambuf_iterator<char>());
        }
        std::istringstream options(cells.size() > 3 ? cells[3] : "");
        std::string option;
        while (options >> option) {
            if (option == "stall") answer.stall = true;
            else if (option.compare(0, 4, "cut=") == 0) answer.cut_after = std::stoull(option.substr(4));
            else if (option.compare(0, 5, "etag=") == 0) answer.etag = option.substr(5);
            else if (option == "error=connect" || option == "error=timeout") answer.failure = HttpFailure::Network;
            else if (option == "error=tls") answer.failure = HttpFailure::Tls;
            else { *why = "http.tsv line " + std::to_string(number) + ": unknown option " + option; return false; }
        }
        http.answer(cells[0], answer);
    }
    return true;
}

bool SimFakes::log(const std::string& name, std::string* text) const {
    const std::vector<std::string>* lines = nullptr;
    if (name == "volume") lines = &volume.log;
    else if (name == "backlight") lines = &backlight.log;
    else if (name == "unpacker") lines = &unpacker.log;
    else return false;
    text->clear();
    for (const std::string& line : *lines) *text += line + "\n";
    return true;
}

}  // namespace spz::hooks
