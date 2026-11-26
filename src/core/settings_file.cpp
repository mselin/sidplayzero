// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-130: the settings file; SPEC-133: a settings file that cannot be read.
#include "spz/settings_file.h"

#include <atomic>
#include <cstdint>
#include <utility>

#include "keep_lines.h"
#include "spz/limits.h"
#include "spz/safe_file.h"

#ifndef SPZ_PART_MODES
#define SPZ_PART_MODES 1
#endif
#ifndef SPZ_PART_SHUFFLE
#define SPZ_PART_SHUFFLE 1
#endif
#ifndef SPZ_PART_RATE
#define SPZ_PART_RATE 1
#endif
#ifndef SPZ_PART_FORCE
#define SPZ_PART_FORCE 1
#endif
#ifndef SPZ_PART_ROMS
#define SPZ_PART_ROMS 1
#endif
#ifndef SPZ_PART_RESUME
#define SPZ_PART_RESUME 1
#endif
#ifndef SPZ_PART_SCREEN_OFF
#define SPZ_PART_SCREEN_OFF 1
#endif
#ifndef SPZ_PART_DOWNLOAD_ALL
#define SPZ_PART_DOWNLOAD_ALL 1
#endif

namespace spz {

namespace {

// A number of at most `digits` decimal digits and nothing else; -1 if it is not one.
int small_number(const std::string& text, std::size_t digits) {
    if (text.empty() || text.size() > digits) return -1;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool on_off(const std::string& text, bool* out) {
    if (text == "on") {
        *out = true;
        return true;
    }
    if (text == "off") {
        *out = false;
        return true;
    }
    return false;
}

const char* on_off_text(bool on) { return on ? "on" : "off"; }

bool is_https(const std::string& text) {
    const std::string scheme = "https://";
    return text.size() > scheme.size() && text.compare(0, scheme.size(), scheme) == 0 &&
           text.find(' ') == std::string::npos && keep::fits_a_line(text);
}

// "m:ss" from 0:05 to 59:59, or "0"; in seconds, or -1.
int length_seconds(const std::string& text) {
    if (text == "0") return 0;
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return -1;
    const int minutes = small_number(text.substr(0, colon), 2);
    const std::string second_text = text.substr(colon + 1);
    const int seconds = second_text.size() == 2 ? small_number(second_text, 2) : -1;
    if (minutes < 0 || seconds < 0 || seconds > 59) return -1;
    const int total = minutes * 60 + seconds;
    return total >= 5 && total <= 59 * 60 + 59 ? total : -1;
}

// Whether the part a setting's value belongs to is built (SPEC-115). A setting's default belongs to no part.
bool part_built(const std::string& key, const std::string& text) {
    if (text == setting_text(Settings(), key)) return true;
    if (key == "when_song_ends" || key == "repeat_all") return SPZ_PART_MODES != 0;
    if (key == "shuffle") return SPZ_PART_SHUFFLE != 0;
    if (key == "sample_rate") return SPZ_PART_RATE != 0;
    if (key == "sid_model" || key == "clock") return SPZ_PART_FORCE != 0;
    if (key == "rom_kernal" || key == "rom_basic" || key == "rom_chargen") return SPZ_PART_ROMS != 0;
    if (key == "resume_on_start") return SPZ_PART_RESUME != 0;
    if (key == "screen_off_after") return SPZ_PART_SCREEN_OFF != 0;
    if (key == "url_archive") return SPZ_PART_DOWNLOAD_ALL != 0;
    return true;
}

// Sets a setting from the file's form if SPEC-130's table allows the value, whatever the parts built.
bool set_by_table(Settings* settings, const std::string& key, const std::string& text) {
    if (key == "collection") {
        if (text.empty() || !keep::fits_a_line(text)) return false;
        settings->collection = text;
        return true;
    }
    if (key == "engine") {
        if (text == "auto") settings->engine = EngineChoice::Auto;
        else if (text == "accurate") settings->engine = EngineChoice::Accurate;
        else if (text == "light") settings->engine = EngineChoice::Light;
        else return false;
        return true;
    }
    if (key == "sample_rate") {
        if (text == "44100") settings->sample_rate = 44100;
        else if (text == "48000") settings->sample_rate = 48000;
        else return false;
        return true;
    }
    if (key == "when_song_ends") {
        if (text == "subtunes") settings->when_song_ends = WhenSongEnds::Subtunes;
        else if (text == "next_tune") settings->when_song_ends = WhenSongEnds::NextTune;
        else if (text == "repeat") settings->when_song_ends = WhenSongEnds::Repeat;
        else if (text == "stop") settings->when_song_ends = WhenSongEnds::Stop;
        else return false;
        return true;
    }
    if (key == "repeat_all") return on_off(text, &settings->repeat_all);
    if (key == "shuffle") return on_off(text, &settings->shuffle);
    if (key == "default_length") {
        const int seconds = length_seconds(text);
        if (seconds < 0) return false;
        settings->default_length_s = seconds;
        return true;
    }
    if (key == "sid_model") {
        if (text == "auto") settings->sid_model = SidModel::FromTune;
        else if (text == "6581") settings->sid_model = SidModel::Mos6581;
        else if (text == "8580") settings->sid_model = SidModel::Mos8580;
        else return false;
        return true;
    }
    if (key == "clock") {
        if (text == "auto") settings->clock = C64Clock::FromTune;
        else if (text == "pal") settings->clock = C64Clock::Pal;
        else if (text == "ntsc") settings->clock = C64Clock::Ntsc;
        else return false;
        return true;
    }
    if (key == "rom_kernal" || key == "rom_basic" || key == "rom_chargen") {
        if (!text.empty() && !keep::fits_a_line(text)) return false;
        std::string* rom = key == "rom_kernal" ? &settings->rom_kernal
                           : key == "rom_basic" ? &settings->rom_basic
                                                : &settings->rom_chargen;
        *rom = text;
        return true;
    }
    if (key == "resume_on_start") return on_off(text, &settings->resume_on_start);
    if (key == "screen_off_after") {
        const int seconds = small_number(text, 4);
        if (seconds != 0 && (seconds < 10 || seconds > 3600)) return false;
        settings->screen_off_after_s = seconds;
        return true;
    }
    if (key == "url_files") {
        if (!is_https(text)) return false;
        settings->url_files = text;
        return true;
    }
    if (key == "url_archive") {
        if (!is_https(text) || text.find("{release}") == std::string::npos) return false;
        settings->url_archive = text;
        return true;
    }
    return false;
}

std::set<std::string> known_keys() { return std::set<std::string>(setting_keys().begin(), setting_keys().end()); }

// read_settings(), and what SettingsFile keeps to write the file again.
SettingsRead read_full(const Bytes& file, keep::Lines* lines, std::set<std::string>* as_found) {
    SettingsRead read;
    *lines = keep::read_lines(file, FileKind::Settings, known_keys());
    read.used = lines->used;
    read.fault = lines->fault;
    if (!lines->used) {
        lines->kept.clear();
        return read;
    }
    for (const auto& pair : lines->values) {
        Settings tried = read.settings;
        if (!set_by_table(&tried, pair.first, pair.second)) {
            read.fault = true;             // the setting keeps its default
        } else if (!part_built(pair.first, pair.second)) {
            as_found->insert(pair.first);  // kept for a build with the part; here, the default
        } else {
            read.settings = tried;
        }
    }
    return read;
}

}  // namespace

const std::vector<std::string>& setting_keys() {
    static const std::vector<std::string> keys = {
        "collection", "engine", "sample_rate", "when_song_ends", "repeat_all", "shuffle",
        "default_length", "sid_model", "clock", "rom_kernal", "rom_basic", "rom_chargen",
        "resume_on_start", "screen_off_after", "url_files", "url_archive",
    };
    return keys;
}

std::string setting_text(const Settings& settings, const std::string& key) {
    if (key == "collection") return settings.collection;
    if (key == "engine") {
        switch (settings.engine) {
            case EngineChoice::Auto: return "auto";
            case EngineChoice::Accurate: return "accurate";
            case EngineChoice::Light: return "light";
        }
        return "auto";
    }
    if (key == "sample_rate") return std::to_string(settings.sample_rate);
    if (key == "when_song_ends") {
        switch (settings.when_song_ends) {
            case WhenSongEnds::Subtunes: return "subtunes";
            case WhenSongEnds::NextTune: return "next_tune";
            case WhenSongEnds::Repeat: return "repeat";
            case WhenSongEnds::Stop: return "stop";
        }
        return "subtunes";
    }
    if (key == "repeat_all") return on_off_text(settings.repeat_all);
    if (key == "shuffle") return on_off_text(settings.shuffle);
    if (key == "default_length") {
        const int total = settings.default_length_s;
        if (total <= 0) return "0";
        const int seconds = total % 60;
        return std::to_string(total / 60) + (seconds < 10 ? ":0" : ":") + std::to_string(seconds);
    }
    if (key == "sid_model") {
        switch (settings.sid_model) {
            case SidModel::FromTune: return "auto";
            case SidModel::Mos6581: return "6581";
            case SidModel::Mos8580: return "8580";
        }
        return "auto";
    }
    if (key == "clock") {
        switch (settings.clock) {
            case C64Clock::FromTune: return "auto";
            case C64Clock::Pal: return "pal";
            case C64Clock::Ntsc: return "ntsc";
        }
        return "auto";
    }
    if (key == "rom_kernal") return settings.rom_kernal;
    if (key == "rom_basic") return settings.rom_basic;
    if (key == "rom_chargen") return settings.rom_chargen;
    if (key == "resume_on_start") return on_off_text(settings.resume_on_start);
    if (key == "screen_off_after") return std::to_string(settings.screen_off_after_s);
    if (key == "url_files") return settings.url_files;
    if (key == "url_archive") return settings.url_archive;
    return "";
}

bool set_setting(Settings* settings, const std::string& key, const std::string& text) {
    Settings tried = *settings;
    if (!set_by_table(&tried, key, text) || !part_built(key, text)) return false;
    *settings = tried;
    return true;
}

SettingsRead read_settings(const Bytes& file) {
    keep::Lines lines;
    std::set<std::string> as_found;
    return read_full(file, &lines, &as_found);
}

// ---- the file ----------------------------------------------------------------------------------------

struct SettingsFile::Saved {
    std::atomic<FileStatus> last{FileStatus::Ok};
};

SettingsFile::SettingsFile(Files* files, Executor* quick, std::string path)
    : files_(files), quick_(quick), path_(std::move(path)), saved_(std::make_shared<Saved>()) {}

void SettingsFile::load() {
    {
        std::lock_guard<std::mutex> hold(lock_);
        settings_ = Settings();
    }
    bad_copy_.clear();
    fault_ = false;
    notice_ = false;
    kept_.clear();
    as_found_.clear();

    Bytes data;
    std::string why;
    const FileStatus status = read_limited(files_, path_, FileKind::Settings, &data, &why);
    const bool too_large = status == FileStatus::TooLarge;
    if (too_large) {
        // Not used at all (SPEC-133; FIND-021: MSG_SETTINGS_RESET), and not read: it is renamed aside
        // below, as it was found, whatever its size (SPEC-204).
        fault_ = true;
        data.clear();
    } else if (status != FileStatus::Ok) {
        return;   // not there, or not readable: every setting has its default, and nothing is written
    } else {
        keep::Lines lines;
        const SettingsRead read = read_full(data, &lines, &as_found_);
        fault_ = read.fault;
        {
            std::lock_guard<std::mutex> hold(lock_);
            settings_ = read.settings;
        }
        for (const keep::KeptLine& line : lines.kept) kept_.emplace_back(line.key, line.raw);
    }
    if (!fault_) return;

    notice_ = true;
    // The copy: PATH.bad, else the first of PATH.bad.2, PATH.bad.3 ... that no file has.
    std::string copy = path_ + ".bad";
    for (int n = 2; n < 10000; ++n) {
        FileInfo info;
        if (files_->info(copy, &info) == FileStatus::NotFound) break;
        copy = path_ + ".bad." + std::to_string(n);
    }
    const FileStatus kept = too_large ? files_->rename(path_, copy) : safe_write(files_, copy, data);
    if (kept == FileStatus::Ok) bad_copy_ = copy;
}

const Settings& SettingsFile::settings() const { return settings_; }

Settings SettingsFile::copy() const {
    std::lock_guard<std::mutex> hold(lock_);
    return settings_;
}

Settings SettingsSource::get() const {
    if (file_ != nullptr) return file_->copy();
    if (fixed_ != nullptr) return *fixed_;
    return Settings();
}

bool SettingsFile::had_fault() const { return fault_; }
const std::string& SettingsFile::bad_copy() const { return bad_copy_; }

std::string SettingsFile::take_notice() {
    if (!notice_) return "";
    notice_ = false;
    return "MSG_SETTINGS_RESET";
}

std::string SettingsFile::file_text() const {
    std::string text;
    for (const auto& line : kept_) {
        if (line.first.empty() || as_found_.count(line.first) != 0) {
            text += line.second;
        } else {
            text += line.first + " = " + setting_text(settings_, line.first);
        }
        text += '\n';
    }
    return text;
}

bool SettingsFile::set(const std::string& key, const std::string& text) {
    Settings changed = settings_;
    if (!set_setting(&changed, key, text)) return false;
    {
        std::lock_guard<std::mutex> hold(lock_);
        settings_ = changed;
    }
    as_found_.erase(key);
    bool listed = false;
    for (const auto& line : kept_) listed = listed || line.first == key;
    if (!listed) kept_.emplace_back(key, "");
    const std::string contents = file_text();
    Files* files = files_;
    const std::string path = path_;
    std::shared_ptr<Saved> saved = saved_;
    quick_->post([files, path, contents, saved] {
        saved->last = safe_write(files, path, Bytes(contents.begin(), contents.end()));
    });
    return true;
}

FileStatus SettingsFile::last_save() const { return saved_->last; }

}  // namespace spz
