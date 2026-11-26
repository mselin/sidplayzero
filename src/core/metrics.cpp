// SPDX-License-Identifier: GPL-3.0-or-later
// The table of the measurements (SPEC-240, SPEC-241): the keys, the form of each value, and which runs
// write it. The keys are the specification's; tests/unit/test_keep_budgets.py compares this table with
// its two. Which keys a run off the device writes, and the form of each value, are as this table has them
// (SPEC-241, FIND-024).
#include "spz/metrics.h"

#include <utility>

namespace spz {

const std::vector<MetricRow>& metric_table() {
    static const std::vector<MetricRow> table = {
    // SPEC-240's table, in its order.
    {"folder_again_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"folder_first_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"rows_per_second_min", MetricForm::Number, MetricWhen::WhenTaken},
    {"row_pause_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"peak_memory_kb", MetricForm::Number, MetricWhen::WhenTaken},
    {"detail_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"key_to_sound_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"underrun_frames", MetricForm::Number, MetricWhen::WhenTaken},
    {"cpu_percent", MetricForm::Number, MetricWhen::WhenTaken},
    {"key_to_screen_ms_p95", MetricForm::Number, MetricWhen::WhenTaken},
    {"start_ms", MetricForm::Number, MetricWhen::EveryRun},
    {"exit_ms", MetricForm::Number, MetricWhen::EveryRun},
    {"search_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"index_step_one_s", MetricForm::Number, MetricWhen::WhenTaken},
    {"index_tunes_per_second", MetricForm::NumberOrNotMeasured, MetricWhen::WhenTaken},
    {"index_underrun_frames", MetricForm::Number, MetricWhen::WhenTaken},
    {"hvsc_setup_s", MetricForm::Number, MetricWhen::WhenTaken},
    // SPEC-241's table, in its order.
    {"app_version", MetricForm::Text, MetricWhen::EveryRun},
    {"engine_library", MetricForm::Text, MetricWhen::EveryRun},
    {"key_to_screen_ms_p50", MetricForm::Number, MetricWhen::WhenTaken},
    {"key_to_screen_ms_max", MetricForm::Number, MetricWhen::WhenTaken},
    {"speed_factor_accurate", MetricForm::Number, MetricWhen::WhenTaken},
    {"speed_factor_light", MetricForm::Number, MetricWhen::WhenTaken},
    {"bench_1_engine", MetricForm::Engine, MetricWhen::WhenTaken},
    {"bench_1_render_load_percent", MetricForm::Number, MetricWhen::WhenTaken},
    {"bench_2_engine", MetricForm::Engine, MetricWhen::WhenTaken},
    {"bench_2_render_load_percent", MetricForm::Number, MetricWhen::WhenTaken},
    {"bench_3_engine", MetricForm::Engine, MetricWhen::WhenTaken},
    {"bench_3_render_load_percent", MetricForm::Number, MetricWhen::WhenTaken},
    {"bench_4_engine", MetricForm::Engine, MetricWhen::WhenTaken},
    {"bench_4_render_load_percent", MetricForm::Number, MetricWhen::WhenTaken},
    {"audio_backend", MetricForm::Text, MetricWhen::DeviceOnly},
    {"audio_rate", MetricForm::Number, MetricWhen::DeviceOnly},
    {"audio_format", MetricForm::Text, MetricWhen::DeviceOnly},
    {"index_tunes", MetricForm::Number, MetricWhen::WhenTaken},
    {"index_step_two_s", MetricForm::Number, MetricWhen::WhenTaken},
    {"render_repeatable", MetricForm::YesNo, MetricWhen::WhenTaken},
    {"render_hash_accurate", MetricForm::Text, MetricWhen::WhenTaken},
    {"render_hash_light", MetricForm::Text, MetricWhen::WhenTaken},
    {"lengths_commando", MetricForm::Text, MetricWhen::DeviceOnly},
    {"key_codes_wrong", MetricForm::Text, MetricWhen::WhenTaken},
    {"memory_check_mib", MetricForm::Number, MetricWhen::EveryRun},
    {"unpacker", MetricForm::YesNo, MetricWhen::EveryRun},
    {"card_free_mib", MetricForm::Number, MetricWhen::EveryRun},
    {"download_everything_offered", MetricForm::YesNo, MetricWhen::EveryRun},
    {"backlight_writable", MetricForm::YesNo, MetricWhen::DeviceOnly},
    {"framebuffer", MetricForm::YesNo, MetricWhen::DeviceOnly},
    {"keyboard", MetricForm::YesNo, MetricWhen::DeviceOnly},
    {"user_groups", MetricForm::Text, MetricWhen::EveryRun},
    };
    return table;
}

bool is_metric(const std::string& key) {
    for (const MetricRow& row : metric_table()) {
        if (key == row.key) return true;
    }
    return false;
}

namespace {

bool only_decimal(const std::string& text) {
    if (text.empty()) return false;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// A number, with or without a decimal part: "812", "14.20".
bool is_number(const std::string& text) {
    const std::size_t dot = text.find('.');
    if (dot == std::string::npos) return only_decimal(text);
    return only_decimal(text.substr(0, dot)) && only_decimal(text.substr(dot + 1));
}

// One line of text: no control character, so no line break.
bool is_one_line(const std::string& text) {
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) return false;
    }
    return true;
}

const char kP50[] = "key_to_screen_ms_p50";
const char kP95[] = "key_to_screen_ms_p95";
const char kMax[] = "key_to_screen_ms_max";

bool is_key_figure(const std::string& key) { return key == kP50 || key == kP95 || key == kMax; }

}  // namespace

bool metric_fits(const std::string& key, const std::string& value) {
    for (const MetricRow& row : metric_table()) {
        if (key != row.key) continue;
        switch (row.form) {
            case MetricForm::Number: return is_number(value);
            case MetricForm::NumberOrNotMeasured: return is_number(value) || value == "not measured";
            case MetricForm::YesNo: return value == "yes" || value == "no";
            case MetricForm::Engine: return value == "accurate" || value == "light";
            case MetricForm::Text: return is_one_line(value);
        }
        return false;
    }
    return false;
}

std::string user_groups_value(const std::string& user, const std::vector<std::string>& groups) {
    std::string value;
    for (const std::string& group : groups) {
        if (group == user) continue;
        if (!value.empty()) value += ' ';
        value += group;
    }
    return value;
}

void Metrics::on_taken(Taken listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    taken_ = std::move(listener);
}

bool Metrics::set(const std::string& key, const std::string& value) {
    if (!metric_fits(key, value)) return false;
    Taken listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] = value;
        listener = taken_;
    }
    if (listener) listener(key, value);
    return true;
}

bool Metrics::set_number(const std::string& key, std::int64_t value) { return set(key, std::to_string(value)); }

void Metrics::key_answered(Millis ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++answered_[ms < 0 ? 0 : ms];
    ++answered_count_;
}

std::string Metrics::key_figure(const std::string& key) const {
    if (answered_count_ == 0) return "";
    if (key == kMax) return std::to_string(answered_.rbegin()->first);
    // The smallest time within which at least `percent` of every 100 were answered.
    const std::uint64_t percent = key == kP95 ? 95 : 50;
    const std::uint64_t needed = (answered_count_ * percent + 99) / 100;
    std::uint64_t within = 0;
    for (const auto& pair : answered_) {
        within += pair.second;
        if (within >= needed) return std::to_string(pair.first);
    }
    return std::to_string(answered_.rbegin()->first);
}

std::string Metrics::value_held(const std::string& key) const {
    if (is_key_figure(key)) {
        const std::string figure = key_figure(key);
        if (!figure.empty()) return figure;
    }
    const auto found = values_.find(key);
    return found == values_.end() ? "" : found->second;
}

bool Metrics::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_key_figure(key) && answered_count_ > 0) return true;
    return values_.count(key) != 0;
}

std::string Metrics::value(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_held(key);
}

std::string Metrics::text() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string text;
    for (const MetricRow& row : metric_table()) {
        const std::string key = row.key;
        const bool held = values_.count(key) != 0 || (is_key_figure(key) && answered_count_ > 0);
        if (held) text += key + " = " + value_held(key) + "\n";
    }
    return text;
}

FileStatus Metrics::write(Files* files, const std::string& path) const {
    const std::string contents = text();
    return files->write(path, Bytes(contents.begin(), contents.end()));
}

}  // namespace spz
