// SPDX-License-Identifier: GPL-3.0-or-later
// What the app remembers between runs, and the file it is kept in (SPEC-131).
//
// The file is state.ini in the state folder (spz/folders.h), in the same form as the settings file
// (spz/settings_file.h): `key = value` lines, with the keys of SPEC-131's table. A key that has nothing to
// hold is left out. It is read once at start and written through spz::safe_write(): two seconds after the
// first change not yet saved, and at exit. A file that cannot be read is treated as SPEC-133 treats the
// settings file, without the copy and without the notice.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "spz/clock.h"
#include "spz/executor.h"
#include "spz/files.h"
#include "spz/types.h"

namespace spz {

// How long after the first change not yet saved the file is written (SPEC-131 ‡).
const Millis kStateSaveAfterMs = 2000;

// What is remembered. Each field is one key of the file, of the same name.
struct State {
    std::string browser_folder = "/";   // the folder open in the browser, as a path under the root: "/" is
                                        // the root, "/MUSICIANS/H/Hubbard_Rob" a folder
    std::string browser_entry;          // the highlighted entry's name: "Commando.sid"; "" for none
    std::string current_path;           // the player's current entry: a collection path, or the "file://"
                                        // form (SPEC-170); "" for none
    int current_subtune = 0;            // its subtune, counted from 1; 0 for an entry without a number
    std::string queue_kind;             // "folder", "search" or "playlist"; "" when there is no queue
    std::string queue_source;           // the folder's path, the query, or the playlist's name
    int queue_place = 0;                // the current entry's place in the queue, counted from 1; 0 for none
    bool notice_accepted = false;       // written as `on` once the user has accepted SPEC-152's notice
    int backlight_saved = -1;           // the brightness to restore, in percent, while the app has the
                                        // screen off (SPEC-207); -1, and no line in the file, otherwise

    bool operator==(const State& other) const {
        return browser_folder == other.browser_folder && browser_entry == other.browser_entry &&
               current_path == other.current_path && current_subtune == other.current_subtune &&
               queue_kind == other.queue_kind && queue_source == other.queue_source &&
               queue_place == other.queue_place && notice_accepted == other.notice_accepted &&
               backlight_saved == other.backlight_saved;
    }
    bool operator!=(const State& other) const { return !(*this == other); }
};

// What reading a state file's bytes gives.
struct StateRead {
    State state;           // each key as the file gives it, or as a new State has it
    bool fault = false;    // as SettingsRead::fault; also a value that is not a number where one is needed, a
                           // queue_kind that is not one of the three, a notice_accepted that is not on or off
    bool used = true;      // false for a file that is not valid UTF-8 or is larger than 64 KiB
};

// Reads a state file's bytes. A key the app does not know is no fault. A current_path that is neither a
// collection path (SPEC-154) nor a "file://" path is dropped: the state then has no current entry.
// Never fails, whatever the bytes (SPEC-205's fuzz target calls this beside read_settings()).
StateRead read_state(const Bytes& file);

// The state in force, and its file. Its setters are called on the UI thread; the file is written on the
// quick worker. It may be used from both at once.
class StateFile {
public:
    // `clock` times the two seconds; `quick` is the quick worker (SPEC-022); `path` is spz::state_path().
    StateFile(Files* files, Clock* clock, Executor* quick, std::string path);
    // Forgets its timer. What is unsaved stays unsaved: the exit calls save_now() first.
    ~StateFile();
    StateFile(const StateFile&) = delete;
    StateFile& operator=(const StateFile&) = delete;

    // Reads the file, once, at start, on the calling thread (SPEC-200, step 2). A file that is not there, or
    // that is not used, gives a new State. Nothing is made, copied or written, and nothing is shown.
    void load();
    // Whether the file read by load() had a fault (SPEC-133). The app tells nobody; the tests ask.
    bool had_fault() const;

    State state() const;

    // Each changes what is remembered, at once. A call that changes nothing counts for nothing. The first
    // change not yet saved starts the two seconds; when they are over, what is remembered then is handed to
    // the quick worker and written.
    void set_browser(const std::string& folder, const std::string& entry);
    void set_current(const std::string& path, int subtune);                       // "" and 0 for no entry
    void set_queue(const std::string& kind, const std::string& source, int place);
    void set_notice_accepted(bool accepted);
    void set_backlight_saved(int percent);                                        // -1 clears it

    // Whether something has changed that the file does not hold yet.
    bool unsaved() const;

    // Writes the file now, on the calling thread; lines of keys the app does not know are kept as they
    // were. Used at exit (SPEC-201), and where SPEC-207 says "at once".
    FileStatus save_now();
    // How the last write ended; FileStatus::Ok before the first one.
    FileStatus last_save() const;

private:
    struct Saved;   // the writes of the file, which the quick worker and the caller of save_now() share
    void changed();          // with mutex_ held
    void timer_fired();
    std::string file_text() const;   // with mutex_ held

    Files* files_;
    Clock* clock_;
    Executor* quick_;
    std::string path_;
    State state_;

    mutable std::mutex mutex_;   // guards everything below and state_
    bool fault_ = false;
    std::uint64_t changes_ = 0;          // counts the changes since load(); each write records the count it holds
    bool timer_set_ = false;
    Clock::TimerId timer_ = 0;
    // What the file is written back from: (key, line) for each line kept; a known key is written with
    // what is remembered now, any other line as it was found.
    std::vector<std::pair<std::string, std::string>> kept_;
    std::shared_ptr<Saved> saved_;
};

}  // namespace spz
