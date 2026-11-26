// SPDX-License-Identifier: GPL-3.0-or-later
// The composition's self-test (app/selftest.h): the benchmark of SPEC-260. The set of SPEC-242 fetched if
// it is absent, then played round and round while the key script (selftest_script.cpp) presses keys, then
// the index built again while a tune plays; the figures of SPEC-240 and SPEC-241 it gives.
#include <sys/resource.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "app/selftest.h"
#include "platform/software_sound.h"
#include "spz/path_text.h"

namespace spz::app {

namespace {

// The processor time of the whole process, all threads, in milliseconds.
double cpu_ms() { return 1000.0 * static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

// The process's peak resident size, in KiB.
long peak_memory_kb() {
    rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return usage.ru_maxrss;
}

}  // namespace

void Composition::SelfTest::start_benchmark() {
    // Which tunes of the set are on the device is asked of Files on the quick worker (SPEC-022).
    Composition* p = parts;
    Files* files = p->seams_.files;
    const std::string root = p->scope_->root.path;
    p->app()->quick()->post([p, files, root] {
        std::vector<bool> present;
        for (const char* path : kBenchmarkSet) {
            FileInfo info;
            present.push_back(files->info(root + path, &info) == FileStatus::Ok && !info.is_folder);
        }
        p->on_ui([p, present] {
            SelfTest* self = p->selftest_.get();
            if (!self) return;
            bool all = true;
            for (const bool here : present) all = all && here;
            if (all) {
                for (int n = 0; n < 4; ++n) self->add_to_set(n);
                self->start_playing();
                return;
            }
            self->open_screen();
            self->phase = Phase::Fetching;
            self->fetch_at = 0;
            self->fetch_lines();
        });
    });
}

void Composition::SelfTest::add_to_set(int n) {
    Entry entry;
    entry.path = kBenchmarkSet[n];
    set.push_back(entry);
    set_number.push_back(n + 1);
}

void Composition::SelfTest::fetch_lines() {
    std::vector<std::string> lines = {"Benchmark: fetching the four tunes of the set.",
                                      "Tune " + std::to_string(std::min<std::size_t>(fetch_at + 1, 4)) + "/4: " +
                                          path_text::last_part(kBenchmarkSet[std::min<std::size_t>(fetch_at, 3)])};
    if (!fetch_failed.empty()) lines.push_back("Not fetched: " + std::to_string(fetch_failed.size()));
    say(lines);
}

// One tick while the set is fetched: each tune is played once through the player, which fetches it as
// SPEC-141 does any tune, and stopped as soon as it sounds.
void Composition::SelfTest::fetch_tick() {
    Player* player = parts->scope_->player.get();
    if (!player) {
        start_playing();
        return;
    }
    if (fetch_at >= 4) {
        player->stop();
        if (parts->app()->top_name() == "selftest") parts->app()->close_top();
        start_playing();
        return;
    }
    Entry entry;
    entry.path = kBenchmarkSet[fetch_at];
    if (!fetch_asked) {
        player->play({entry}, 0);
        fetch_asked = true;
        return;
    }
    const PlayerStatus st = player->status();
    if (st.state == PlayerState::Playing || st.state == PlayerState::Paused) {
        add_to_set(static_cast<int>(fetch_at));
    } else if (st.state == PlayerState::Failed || st.state == PlayerState::Idle) {
        if (++fetch_tries < kFetchTries) {
            fetch_asked = false;
            return;
        }
        fetch_failed.push_back(entry.path);
    } else {
        return;     // fetching, waiting or loading
    }
    player->stop();
    ++fetch_at;
    fetch_tries = 0;
    fetch_asked = false;
    fetch_lines();
}

void Composition::SelfTest::start_playing() {
    Composition* p = parts;
    phase = Phase::Playing;
    began = p->seams_.clock->now_ms();
    underruns_before = p->backend_ ? p->backend_->underruns() : 0;
    cpu_before = cpu_ms();
    p->app()->notice("MSG_SELFTEST");
    if (!set.empty()) play(0, began);
    script_next = began;
    p->app()->redraw();
}

void Composition::SelfTest::play(std::size_t index, Millis now) {
    Player* player = parts->scope_->player.get();
    if (!player || set.empty()) return;
    playing = index % set.size();
    player->play(set, playing);
    tune_began = now;
    tune_sounding = false;
    render_ns_before = render_ns();
}

// The tune that played has played as long as it plays: what it rendered is added to its figures.
void Composition::SelfTest::tune_over(Millis now) {
    if (set.empty() || tune_began < 0) return;
    const int n = set_number[playing];
    render_ms[n] += static_cast<double>(render_ns() - render_ns_before) / 1e6;
    played_ms[n] += static_cast<double>(now - tune_began);
}

void Composition::SelfTest::play_tick(Millis now) {
    Player* player = parts->scope_->player.get();
    if (!player || set.empty()) return;
    const PlayerStatus st = player->status();
    if (st.state == PlayerState::Playing && !tune_sounding) {
        // SPEC-240: from the start asked for to the first samples.
        tune_sounding = true;
        key_to_sound_max = std::max(key_to_sound_max, now - tune_began);
        engine_of[set_number[playing]] = st.engine == EngineKind::Accurate ? "accurate" : "light";
    }
    const Millis length = st.length_ms > 0 ? std::min(st.length_ms, kTuneMostMs) : kTuneMostMs;
    const bool gone = st.state == PlayerState::Idle || st.state == PlayerState::Failed;
    if (now - tune_began >= length || (gone && now - tune_began >= 1000)) {
        tune_over(now);
        play(playing + 1, now);
    }
}

void Composition::SelfTest::end_playing(Millis now) {
    script.clear();
    release_held();
    tune_over(now);
    tune_began = now;
    render_ns_before = render_ns();
    Composition* p = parts;
    Metrics& m = p->metrics_;
    const Millis wall = std::max<Millis>(1, now - began);
    m.set_number("underrun_frames", static_cast<std::int64_t>((p->backend_ ? p->backend_->underruns() : 0) - underruns_before));
    m.set("cpu_percent", decimal(100.0 * (cpu_ms() - cpu_before) / static_cast<double>(wall), 1));
    if (key_to_sound_max >= 0) m.set_number("key_to_sound_ms_max", key_to_sound_max);
    if (folder_first_max >= 0) m.set_number("folder_first_ms_max", folder_first_max);
    if (folder_again_max >= 0) m.set_number("folder_again_ms_max", folder_again_max);
    if (rows_per_second_min >= 0) m.set("rows_per_second_min", decimal(rows_per_second_min, 1));
    if (row_pause_max >= 0) m.set_number("row_pause_ms_max", row_pause_max);
    if (detail_max >= 0) m.set_number("detail_ms_max", detail_max);
    if (search_max >= 0) m.set_number("search_ms_max", search_max);
    // SPEC-112, over the key script: Metrics works the three figures out from what it has been told.
    for (const char* key : {"key_to_screen_ms_p95", "key_to_screen_ms_p50", "key_to_screen_ms_max"}) {
        if (m.has(key)) m.set(key, m.value(key));
    }

    // Then the index is built again while a tune plays (SPEC-260, SPEC-161).
    phase = Phase::Indexing;
    indexing_began = now;
    Index* index = p->scope_->index.get();
    if (!index) {
        finish();
        return;
    }
    const std::uint64_t underruns = p->backend_ ? p->backend_->underruns() : 0;
    p->build_index(IndexReason::User, [p, index, underruns](const IndexRun& run) {
        if (!run.complete) return;
        int present = 0;
        const int count = index->count();
        for (int first = 0; first < count; first += kIndexBatch) {
            for (const IndexRecord& record : index->records(first, kIndexBatch)) present += record.present ? 1 : 0;
        }
        p->on_ui([p, run, present, underruns] {
            SelfTest* self = p->selftest_.get();
            if (!self) return;
            Metrics& taken = p->metrics_;
            if (run.step_one_ms >= 0) taken.set("index_step_one_s", decimal(static_cast<double>(run.step_one_ms) / 1000.0, 2));
            taken.set("index_step_two_s", decimal(static_cast<double>(run.step_two_ms) / 1000.0, 2));
            taken.set_number("index_tunes", present);
            if (present >= kStepTunesNeeded && run.step_two_ms > 0) {
                taken.set("index_tunes_per_second", decimal(1000.0 * run.tunes_read / static_cast<double>(run.step_two_ms), 1));
            } else {
                taken.set("index_tunes_per_second", "not measured");
            }
            taken.set_number("index_underrun_frames",
                         static_cast<std::int64_t>((p->backend_ ? p->backend_->underruns() : 0) - underruns));
            self->finish();
        });
    });
}

void Composition::SelfTest::finish() {
    Composition* p = parts;
    const Millis now = p->seams_.clock->now_ms();
    tune_over(now);
    Metrics& m = p->metrics_;
    for (std::size_t i = 0; i < set.size(); ++i) {
        const int n = set_number[i];
        if (!engine_of[n].empty()) m.set("bench_" + std::to_string(n) + "_engine", engine_of[n]);
        const double load = played_ms[n] > 0 ? 100.0 * render_ms[n] / played_ms[n] : 0;
        m.set("bench_" + std::to_string(n) + "_render_load_percent", decimal(load, 1));
    }
    m.set_number("peak_memory_kb", peak_memory_kb());
    p->device_measurements();
    phase = Phase::Done;
    if (p->scope_->player) p->scope_->player->stop();
    std::vector<std::string> lines = {"Benchmark: the set played for " + std::to_string(benchmark_ms / 1000) + " s.",
                                      "Underruns: " + m.value("underrun_frames") + ", processor: " +
                                          m.value("cpu_percent") + " %"};
    if (!fetch_failed.empty()) lines.push_back("Not fetched: " + std::to_string(fetch_failed.size()) + " of 4");
    open_screen();
    done("benchmark", lines);
}

}  // namespace spz::app
