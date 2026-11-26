// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-241: the measurements the app takes of itself, and the file it writes them to. SPEC-240: every
// measurement a budget is held against is one of them. (tests/unit/test_keep_budgets.py reads the two
// tables out of the specification and compares them with the app's.)
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "fakes.h"
#include "keep_support.h"
#include "spz/metrics.h"
#include "support.h"

using spz::test::TempFolder;

namespace {

// The measurement column of SPEC-240's table, copied from the document.
const std::vector<std::string> kBudgetKeys = {
    "folder_again_ms_max", "folder_first_ms_max", "rows_per_second_min", "row_pause_ms_max", "peak_memory_kb",
    "detail_ms_max", "key_to_sound_ms_max", "underrun_frames", "cpu_percent", "key_to_screen_ms_p95", "start_ms",
    "exit_ms", "search_ms_max", "index_step_one_s", "index_tunes_per_second", "index_underrun_frames", "hvsc_setup_s",
};

// A value of each form.
std::string sample(spz::MetricForm form) {
    switch (form) {
        case spz::MetricForm::Number: return "812";
        case spz::MetricForm::NumberOrNotMeasured: return "not measured";
        case spz::MetricForm::YesNo: return "yes";
        case spz::MetricForm::Engine: return "light";
        case spz::MetricForm::Text: return "some words 1.2.3";
    }
    return "";
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return lines;
}

}  // namespace

TEST_CASE("SPEC-241 SPEC-240 every measurement of the two tables can be recorded, and the file holds one line key = value for each and nothing else") {
    spz::Metrics metrics;
    std::set<std::string> expected;
    for (const spz::MetricRow& row : spz::metric_table()) {
        CAPTURE(row.key);
        const std::string value = sample(row.form);
        CHECK(metrics.set(row.key, value));
        CHECK(metrics.has(row.key));
        CHECK(metrics.value(row.key) == value);
        expected.insert(std::string(row.key) + " = " + value);
    }
    REQUIRE(expected.size() == spz::metric_table().size());

    const std::string text = metrics.text();
    const std::vector<std::string> lines = lines_of(text);
    CHECK(lines.size() == expected.size());
    CHECK(std::set<std::string>(lines.begin(), lines.end()) == expected);
    REQUIRE_FALSE(text.empty());
    CHECK(text.back() == '\n');
}

TEST_CASE("SPEC-240 every measurement that a budget is held against is one the app writes") {
    spz::Metrics metrics;
    for (const std::string& key : kBudgetKeys) {
        CAPTURE(key);
        CHECK(spz::is_metric(key));
        CHECK(metrics.set_number(key, 42));
        CHECK(metrics.value(key) == "42");
    }
    const std::vector<std::string> lines = lines_of(metrics.text());
    REQUIRE(lines.size() == kBudgetKeys.size());
    for (const std::string& key : kBudgetKeys) {
        CAPTURE(key);
        CHECK(std::find(lines.begin(), lines.end(), key + " = 42") != lines.end());
    }
    // Step two of the index is decided only with 1,000 tunes present; else it is recorded as not measured.
    CHECK(metrics.set("index_tunes_per_second", "not measured"));
    CHECK(metrics.value("index_tunes_per_second") == "not measured");
}

TEST_CASE("SPEC-241 nothing else is written: a key that is in neither table is refused") {
    spz::Metrics metrics;
    for (const char* key : {"host_name", "user_name", "bench_0_engine", "bench_5_engine", "start_ms ", "START_MS", ""}) {
        CAPTURE(key);
        CHECK_FALSE(spz::is_metric(key));
        CHECK_FALSE(metrics.set(key, "1"));
        CHECK_FALSE(metrics.set_number(key, 1));
        CHECK_FALSE(metrics.has(key));
    }
    CHECK(metrics.text() == "");
}

TEST_CASE("SPEC-241 each value has the form of its key: a number, yes or no, or one of its words") {
    CHECK(spz::metric_fits("start_ms", "812"));
    CHECK(spz::metric_fits("start_ms", "0"));
    CHECK(spz::metric_fits("speed_factor_accurate", "1.90"));
    CHECK(spz::metric_fits("cpu_percent", "41"));
    CHECK_FALSE(spz::metric_fits("start_ms", "soon"));
    CHECK_FALSE(spz::metric_fits("start_ms", ""));
    CHECK_FALSE(spz::metric_fits("start_ms", "812 ms"));
    CHECK_FALSE(spz::metric_fits("start_ms", "not measured"));

    CHECK(spz::metric_fits("index_tunes_per_second", "250"));
    CHECK(spz::metric_fits("index_tunes_per_second", "not measured"));
    CHECK_FALSE(spz::metric_fits("index_tunes_per_second", "unknown"));

    for (const char* key : {"render_repeatable", "download_everything_offered", "backlight_writable", "framebuffer", "keyboard"}) {
        CAPTURE(key);
        CHECK(spz::metric_fits(key, "yes"));
        CHECK(spz::metric_fits(key, "no"));
        CHECK_FALSE(spz::metric_fits(key, "true"));
        CHECK_FALSE(spz::metric_fits(key, "1"));
        CHECK_FALSE(spz::metric_fits(key, ""));
    }
    for (const char* key : {"bench_1_engine", "bench_2_engine", "bench_3_engine", "bench_4_engine"}) {
        CAPTURE(key);
        CHECK(spz::metric_fits(key, "accurate"));
        CHECK(spz::metric_fits(key, "light"));
        CHECK_FALSE(spz::metric_fits(key, "auto"));      // the engine used, not the setting
        CHECK_FALSE(spz::metric_fits(key, "reSIDfp"));
    }
    CHECK(spz::metric_fits("key_codes_wrong", "none"));
    CHECK(spz::metric_fits("key_codes_wrong", "Tab PageUp"));
    CHECK(spz::metric_fits("app_version", "SidPlayZero 0.1.0"));
    CHECK(spz::metric_fits("lengths_commando", "3:55.594 1:01.288"));
    CHECK_FALSE(spz::metric_fits("app_version", "0.1.0\nhost_name = cardputer"));   // one line, whatever the text
    CHECK_FALSE(spz::metric_fits("no_such_key", "1"));

    // What does not fit is not recorded.
    spz::Metrics metrics;
    CHECK_FALSE(metrics.set("start_ms", "soon"));
    CHECK_FALSE(metrics.set("render_repeatable", "maybe"));
    CHECK_FALSE(metrics.set("app_version", "0.1.0\nhost_name = cardputer"));
    CHECK_FALSE(metrics.has("start_ms"));
    CHECK(metrics.text() == "");
}

TEST_CASE("SPEC-241 the user_groups line leaves out the group named after the user") {
    CHECK(spz::user_groups_value("mel", {"mel", "audio", "video", "input"}) == "audio video input");
    CHECK(spz::user_groups_value("mel", {"audio", "mel", "video"}) == "audio video");
    CHECK(spz::user_groups_value("mel", {"audio", "video", "mel"}) == "audio video");
    CHECK(spz::user_groups_value("mel", {"melody", "mel", "camel"}) == "melody camel");   // only the group of that very name
    CHECK(spz::user_groups_value("mel", {"audio", "video"}) == "audio video");

    spz::Metrics metrics;
    REQUIRE(metrics.set("user_groups", spz::user_groups_value("mel", {"mel", "audio", "video", "input"})));
    const std::vector<std::string> lines = lines_of(metrics.text());
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "user_groups = audio video input");
}

TEST_CASE("SPEC-241 SPEC-112 from the key events answered come the middle, the 95-in-100 and the longest time") {
    {
        spz::Metrics metrics;             // 95 of 100 within 10 ms, five slow ones
        for (int i = 0; i < 95; ++i) metrics.key_answered(10);
        for (int i = 0; i < 5; ++i) metrics.key_answered(200);
        CHECK(metrics.value("key_to_screen_ms_p50") == "10");
        CHECK(metrics.value("key_to_screen_ms_p95") == "10");
        CHECK(metrics.value("key_to_screen_ms_max") == "200");
    }
    {
        spz::Metrics metrics;             // only 94 of 100 within 10 ms: the budget's figure is the slow one
        for (int i = 0; i < 6; ++i) metrics.key_answered(200);
        for (int i = 0; i < 94; ++i) metrics.key_answered(10);
        CHECK(metrics.value("key_to_screen_ms_p50") == "10");
        CHECK(metrics.value("key_to_screen_ms_p95") == "200");
        CHECK(metrics.value("key_to_screen_ms_max") == "200");
    }
    {
        spz::Metrics metrics;             // 1, 2, ... 100 ms, in no order
        for (int i = 0; i < 100; ++i) metrics.key_answered((i * 37) % 100 + 1);
        CHECK(metrics.value("key_to_screen_ms_p50") == "50");
        CHECK(metrics.value("key_to_screen_ms_p95") == "95");
        CHECK(metrics.value("key_to_screen_ms_max") == "100");
        const std::vector<std::string> lines = lines_of(metrics.text());
        CHECK(std::set<std::string>(lines.begin(), lines.end()) ==
              std::set<std::string>{"key_to_screen_ms_p50 = 50", "key_to_screen_ms_p95 = 95", "key_to_screen_ms_max = 100"});
    }
    {
        spz::Metrics metrics;             // one key
        metrics.key_answered(37);
        CHECK(metrics.value("key_to_screen_ms_p50") == "37");
        CHECK(metrics.value("key_to_screen_ms_p95") == "37");
        CHECK(metrics.value("key_to_screen_ms_max") == "37");
    }
    {
        spz::Metrics metrics;             // no key at all: nothing to say
        CHECK_FALSE(metrics.has("key_to_screen_ms_p95"));
        CHECK(metrics.text() == "");
    }
}

TEST_CASE("SPEC-241 a key recorded again holds the newer value, and each measurement is told as it is taken") {
    spz::Metrics metrics;
    std::vector<std::string> told;
    metrics.on_taken([&](const std::string& key, const std::string& value) { told.push_back(key + " = " + value); });

    CHECK(metrics.set_number("start_ms", 812));
    CHECK(metrics.set("app_version", "SidPlayZero 0.1.0"));
    CHECK(metrics.set_number("start_ms", 790));
    CHECK_FALSE(metrics.set("start_ms", "soon"));
    CHECK_FALSE(metrics.set("host_name", "cardputer"));

    CHECK(metrics.value("start_ms") == "790");
    const std::vector<std::string> lines = lines_of(metrics.text());
    CHECK(std::set<std::string>(lines.begin(), lines.end()) ==
          std::set<std::string>{"start_ms = 790", "app_version = SidPlayZero 0.1.0"});
    CHECK(lines.size() == 2);
    // The self-test's results file keeps every start_ms line (SPEC-260), so each one is told.
    CHECK(told == std::vector<std::string>{"start_ms = 812", "app_version = SidPlayZero 0.1.0", "start_ms = 790"});
}

TEST_CASE("SPEC-241 the measurements are written to the file that was asked for, through the Files seam") {
    TempFolder folder;
    spz::fake::Files files;
    spz::Metrics metrics;
    REQUIRE(metrics.set_number("start_ms", 812));
    REQUIRE(metrics.set_number("exit_ms", 20));
    REQUIRE(metrics.set("app_version", "SidPlayZero 0.1.0"));
    const std::string path = folder.file("metrics.txt");
    CHECK(metrics.write(&files, path) == spz::FileStatus::Ok);
    CHECK(files.calls >= 1);
    const std::string written = spz::test::text_on_disk(path);
    CHECK(written == metrics.text());
    CHECK(spz::test::has_line(written, "start_ms = 812"));
    CHECK(spz::test::has_line(written, "exit_ms = 20"));
    CHECK(spz::test::has_line(written, "app_version = SidPlayZero 0.1.0"));
}
