// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-131: what the app remembers between runs, and state.ini; SPEC-133 as it applies to that file.
#include "spz/state_file.h"

#include <set>
#include <utility>

#include "keep_lines.h"
#include "spz/limits.h"
#include "spz/paths.h"
#include "spz/safe_file.h"

namespace spz {

namespace {

// The keys of SPEC-131's table, in its order: the order in which keys the file does not have yet are added.
const std::vector<std::string>& state_keys() {
    static const std::vector<std::string> keys = {
        "browser_folder", "browser_entry", "current_path", "current_subtune", "queue_kind",
        "queue_source", "queue_place", "notice_accepted", "backlight_saved",
    };
    return keys;
}

// A number of at most `digits` decimal digits and nothing else; -1 if it is not one.
long small_number(const std::string& text, std::size_t digits) {
    if (text.empty() || text.size() > digits) return -1;
    long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool is_file_url(const std::string& path) {
    const std::string scheme = "file://";
    return path.size() > scheme.size() + 1 && path.compare(0, scheme.size(), scheme) == 0 &&
           path[scheme.size()] == '/';
}

// Sets one key from the file. False for a value that is not allowed: a fault that costs that key alone.
bool set_key(State* state, const std::string& key, const std::string& value) {
    if (key == "browser_folder") {
        if (value.empty() || value[0] != '/' || !keep::fits_a_line(value)) return false;
        state->browser_folder = value;
        return true;
    }
    if (key == "browser_entry") {
        if (value.find('/') != std::string::npos || !keep::fits_a_line(value)) return false;
        state->browser_entry = value;
        return true;
    }
    if (key == "current_path") {
        // SPEC-154: a path from outside is checked; one that is no collection path is dropped.
        const bool kept = keep::fits_a_line(value) && (is_collection_path(value) || is_file_url(value));
        state->current_path = kept ? value : "";
        return true;
    }
    if (key == "current_subtune") {
        const long subtune = small_number(value, 3);
        if (subtune < 0 || subtune > 256) return false;
        state->current_subtune = static_cast<int>(subtune);
        return true;
    }
    if (key == "queue_kind") {
        if (value != "folder" && value != "search" && value != "playlist") return false;
        state->queue_kind = value;
        return true;
    }
    if (key == "queue_source") {
        if (!keep::fits_a_line(value)) return false;
        state->queue_source = value;
        return true;
    }
    if (key == "queue_place") {
        const long place = small_number(value, 6);
        if (place < 0) return false;
        state->queue_place = static_cast<int>(place);
        return true;
    }
    if (key == "notice_accepted") {
        if (value == "on") state->notice_accepted = true;
        else if (value == "off") state->notice_accepted = false;
        else return false;
        return true;
    }
    if (key == "backlight_saved") {
        const long percent = small_number(value, 3);
        if (percent < 0 || percent > 100) return false;
        state->backlight_saved = static_cast<int>(percent);
        return true;
    }
    return false;
}

// What a key holds, as the file writes it; false when it has nothing to hold and is left out.
bool key_text(const State& state, const std::string& key, std::string* out) {
    const bool queue = !state.queue_kind.empty();
    if (key == "browser_folder") *out = state.browser_folder;
    else if (key == "browser_entry") *out = state.browser_entry;
    else if (key == "current_path") *out = state.current_path;
    else if (key == "current_subtune") {
        if (state.current_path.empty()) return false;
        *out = std::to_string(state.current_subtune);
    } else if (key == "queue_kind") *out = state.queue_kind;
    else if (key == "queue_source") {
        if (!queue) return false;
        *out = state.queue_source;
    } else if (key == "queue_place") {
        if (!queue) return false;
        *out = std::to_string(state.queue_place);
    } else if (key == "notice_accepted") {
        if (!state.notice_accepted) return false;
        *out = "on";
    } else if (key == "backlight_saved") {
        if (state.backlight_saved < 0) return false;
        *out = std::to_string(state.backlight_saved);
    } else {
        return false;
    }
    // A value that cannot be read back as it is (a name that is not UTF-8) is left out: the file stays text.
    return (key == "queue_source" || !out->empty()) && keep::fits_a_line(*out);
}

StateRead read_full(const Bytes& file, keep::Lines* lines) {
    StateRead read;
    *lines = keep::read_lines(file, FileKind::State, std::set<std::string>(state_keys().begin(), state_keys().end()));
    read.used = lines->used;
    read.fault = lines->fault;
    if (!lines->used) {
        lines->kept.clear();
        return read;
    }
    for (const auto& pair : lines->values) {
        if (!set_key(&read.state, pair.first, pair.second)) read.fault = true;
    }
    return read;
}

}  // namespace

StateRead read_state(const Bytes& file) {
    keep::Lines lines;
    return read_full(file, &lines);
}

// ---- the file ----------------------------------------------------------------------------------------

struct StateFile::Saved {
    std::mutex mutex;                  // one write at a time, whichever thread asks
    std::uint64_t written = 0;         // the count of changes the file holds
    FileStatus last = FileStatus::Ok;

    // Writes a snapshot, unless the file already holds a newer one.
    FileStatus write(Files* files, const std::string& path, const std::string& contents, std::uint64_t changes) {
        std::lock_guard<std::mutex> lock(mutex);
        if (changes <= written) return FileStatus::Ok;
        last = safe_write(files, path, Bytes(contents.begin(), contents.end()));
        if (last == FileStatus::Ok) written = changes;
        return last;
    }
};

StateFile::StateFile(Files* files, Clock* clock, Executor* quick, std::string path)
    : files_(files), clock_(clock), quick_(quick), path_(std::move(path)), saved_(std::make_shared<Saved>()) {}

StateFile::~StateFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (timer_set_) clock_->cancel(timer_);
    timer_set_ = false;
}

void StateFile::load() {
    Bytes data;
    std::string why;
    const FileStatus status = read_limited(files_, path_, FileKind::State, &data, &why);
    State state;
    bool fault = status == FileStatus::TooLarge;
    std::vector<std::pair<std::string, std::string>> kept;
    if (status == FileStatus::Ok) {
        keep::Lines lines;
        const StateRead read = read_full(data, &lines);
        fault = read.fault;
        state = read.state;
        for (const keep::KeptLine& line : lines.kept) kept.emplace_back(line.key, line.raw);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    fault_ = fault;
    kept_ = std::move(kept);
}

bool StateFile::had_fault() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fault_;
}

State StateFile::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void StateFile::changed() {
    ++changes_;
    if (timer_set_) return;
    timer_set_ = true;
    timer_ = clock_->call_after(kStateSaveAfterMs, [this] { timer_fired(); });
}

void StateFile::timer_fired() {
    std::string contents;
    std::uint64_t changes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timer_set_ = false;
        contents = file_text();
        changes = changes_;
    }
    // The timer fires on the UI thread, which touches no file: the write is the quick worker's (SPEC-022).
    Files* files = files_;
    const std::string path = path_;
    std::shared_ptr<Saved> saved = saved_;
    quick_->post([files, path, contents, changes, saved] { saved->write(files, path, contents, changes); });
}

void StateFile::set_browser(const std::string& folder, const std::string& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.browser_folder == folder && state_.browser_entry == entry) return;
    state_.browser_folder = folder;
    state_.browser_entry = entry;
    changed();
}

void StateFile::set_current(const std::string& path, int subtune) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.current_path == path && state_.current_subtune == subtune) return;
    state_.current_path = path;
    state_.current_subtune = subtune;
    changed();
}

void StateFile::set_queue(const std::string& kind, const std::string& source, int place) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.queue_kind == kind && state_.queue_source == source && state_.queue_place == place) return;
    state_.queue_kind = kind;
    state_.queue_source = source;
    state_.queue_place = place;
    changed();
}

void StateFile::set_notice_accepted(bool accepted) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.notice_accepted == accepted) return;
    state_.notice_accepted = accepted;
    changed();
}

void StateFile::set_backlight_saved(int percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (percent < 0) percent = -1;
    if (state_.backlight_saved == percent) return;
    state_.backlight_saved = percent;
    changed();
}

bool StateFile::unsaved() const {
    std::uint64_t changes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changes = changes_;
    }
    std::lock_guard<std::mutex> lock(saved_->mutex);
    return changes > saved_->written;
}

std::string StateFile::file_text() const {
    std::string text;
    std::set<std::string> written;
    for (const auto& line : kept_) {
        if (line.first.empty()) {
            text += line.second + "\n";
            continue;
        }
        written.insert(line.first);
        std::string value;
        if (key_text(state_, line.first, &value)) text += line.first + " = " + value + "\n";
    }
    for (const std::string& key : state_keys()) {
        if (written.count(key) != 0) continue;
        std::string value;
        if (key_text(state_, key, &value)) text += key + " = " + value + "\n";
    }
    return text;
}

FileStatus StateFile::save_now() {
    std::string contents;
    std::uint64_t changes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        contents = file_text();
        changes = changes_;
    }
    return saved_->write(files_, path_, contents, changes);
}

FileStatus StateFile::last_save() const {
    std::lock_guard<std::mutex> lock(saved_->mutex);
    return saved_->last;
}

}  // namespace spz
