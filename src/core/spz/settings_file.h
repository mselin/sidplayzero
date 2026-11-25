// SPDX-License-Identifier: GPL-3.0-or-later
// The file the settings are kept in (SPEC-130), and what happens when it cannot be read (SPEC-133).
//
// The file is settings.ini in the configuration folder (spz/folders.h). Its form: UTF-8 text, one
// `key = value` to a line; spaces and tabs around the key and around the value are not part of them. An
// empty line and a line starting with `#` mean nothing. The keys and their values are those of SPEC-130's
// table, written exactly as the table writes them: `engine = accurate`, `default_length = 3:30`,
// `repeat_all = on`, `rom_kernal = ` for no file. A setting the file does not name has its default
// (spz/settings.h). In a build without a part (SPEC-115), that part's settings keep their defaults.
#pragma once

#include <string>
#include <vector>

#include "spz/executor.h"
#include "spz/files.h"
#include "spz/settings.h"
#include "spz/types.h"

namespace spz {

// The keys of SPEC-130's table, in its order: "collection", "engine", "sample_rate", "when_song_ends",
// "repeat_all", "shuffle", "default_length", "sid_model", "clock", "rom_kernal", "rom_basic",
// "rom_chargen", "resume_on_start", "screen_off_after", "url_files", "url_archive".
const std::vector<std::string>& setting_keys();

// A setting's value as the file writes it: "auto", "48000", "subtunes", "off", "3:30", "0", "". The
// Settings screen (SPEC-101) shows and edits settings through this form too. "" for a key that is not one.
std::string setting_text(const Settings& settings, const std::string& key);

// Sets one setting from the file's form. False, changing nothing, if the key is not one of the table's or
// the value is not allowed for it: a word the table does not list, a length outside 0:05 to 59:59 that is
// not `0`, a number of seconds that is neither 0 nor 10 to 3600, an address that is not `https://`, an
// archive address without `{release}`.
bool set_setting(Settings* settings, const std::string& key, const std::string& text);

// What reading a settings file's bytes gives (SPEC-133).
struct SettingsRead {
    Settings settings;     // each setting as the file gives it, or its default
    bool fault = false;    // a line that is not empty, a comment or `key = value`; a known key twice (the
                           // last wins); a value not allowed for its key (that setting keeps its default);
                           // bytes that are not valid UTF-8; more than 64 KiB
    bool used = true;      // false for a file that is not valid UTF-8 or is too large: nothing of it is used
};

// Reads a settings file's bytes. A key the app does not know is no fault. Never fails, whatever the bytes
// (this is the function SPEC-205's fuzz target calls).
SettingsRead read_settings(const Bytes& file);

// The settings in force, and their file.
class SettingsFile {
public:
    // `quick` is the quick worker (SPEC-022): every save is handed to it. `path` is spz::settings_path().
    SettingsFile(Files* files, Executor* quick, std::string path);
    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;

    // Reads the file, once, at start, on the calling thread (SPEC-200, step 2). A file that is not there is
    // no fault: every setting has its default, and nothing is made or written. When the file has a fault it
    // is copied, as it was found, to PATH.bad, or to PATH.bad.2, PATH.bad.3 and so on, the first of these
    // names that no file has; the copy is there before this returns and so before anything is written.
    void load();

    const Settings& settings() const;

    // Whether the file read by load() had a fault, and where its copy is ("" if there was none).
    bool had_fault() const;
    const std::string& bad_copy() const;
    // The notice to show at start: "MSG_SETTINGS_RESET" the first time this is called after a load() that
    // found a fault, and "" otherwise.
    std::string take_notice();

    // Changes one setting, at once, and hands the save to the quick worker: the whole file is written again
    // through spz::safe_write(), keeping, as they were, the comments and the lines of keys the app does not
    // know. False, with nothing changed and nothing written, if set_setting() refuses the value.
    bool set(const std::string& key, const std::string& text);

    // How the last save ended; FileStatus::Ok before the first one.
    FileStatus last_save() const;

private:
    Files* files_;
    Executor* quick_;
    std::string path_;
    Settings settings_;
    std::string bad_copy_;
};

}  // namespace spz
