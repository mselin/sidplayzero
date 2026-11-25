// SPDX-License-Identifier: GPL-3.0-or-later
// The table of the measurements (SPEC-240, SPEC-241): the keys, the form of each value, and which runs
// write it. The keys are the specification's; tests/unit/test_keep_budgets.py compares this table with
// its two. The rest is not built yet (Phase 5).
#include "spz/metrics.h"

#include "spz/not_built.h"

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

bool metric_fits(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-241"); }
std::string user_groups_value(const std::string&, const std::vector<std::string>&) { SPZ_NOT_BUILT("SPEC-241"); }

void Metrics::on_taken(Taken) { SPZ_NOT_BUILT("SPEC-241"); }
bool Metrics::set(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-241"); }
bool Metrics::set_number(const std::string&, std::int64_t) { SPZ_NOT_BUILT("SPEC-241"); }
void Metrics::key_answered(Millis) { SPZ_NOT_BUILT("SPEC-112"); }
bool Metrics::has(const std::string&) const { SPZ_NOT_BUILT("SPEC-241"); }
std::string Metrics::value(const std::string&) const { SPZ_NOT_BUILT("SPEC-241"); }
std::string Metrics::text() const { SPZ_NOT_BUILT("SPEC-241"); }
FileStatus Metrics::write(Files*, const std::string&) const { SPZ_NOT_BUILT("SPEC-241"); }

}  // namespace spz
