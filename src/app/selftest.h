// SPDX-License-Identifier: GPL-3.0-or-later
// Private to the composition's self-test (src/app/compose_selftest.cpp, selftest_benchmark.cpp and
// selftest_script.cpp): the self-test of SPEC-260 and the measurements of SPEC-240 and SPEC-241, as one
// object of the composition's, Composition::SelfTest. What it does, step by step, is at the top of
// compose_selftest.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/composition.h"
#include "platform/software_sound.h"
#include "spz/keys.h"
#include "ui/selftest_screen.h"

namespace spz::app {

namespace selftest {

const std::size_t kRequestLimit = 4096;
const Millis kTickMs = 10;                       // the self-test looks at what it waits for this often
const Millis kBenchmarkMs = 30 * 60 * 1000;      // SPEC-260: the set plays for 30 minutes
const Millis kTuneMostMs = 2 * 60 * 1000;        // SPEC-242: each tune for its length or 2 minutes
const Millis kRoundEveryMs = 20 * 1000;          // SPEC-260: a round of the key script starts every 20 s
const Millis kHoldMs = 10 * 1000;                // SPEC-240: Down is held for 10 s
const int kHoldEvery = 5;                        // in the first round and every fifth after it
const Millis kKeyGapMs = 120;                    // between two keys of the key script, as a quick hand
const Millis kMoveGapMs = 60;                    // between two keys that move the highlight
const Millis kWaitMostMs = 10 * 1000;            // what the key script waits for at most, before giving up
const Millis kSettingUpCheckMs = 100;            // how often a set-up of HVSC is looked for
const int kFetchTries = 3;
const double kRenderSeconds = 10.0;              // SPEC-241: ten seconds of the built-in tune
const int kStepTunesNeeded = 1000;               // SPEC-240: step two is decided with this many present
const int kIndexBatch = 1000;                    // the index's records counted at a time, each batch a copy

const char* const kBenchmarkSet[4] = {
    "/MUSICIANS/H/Hubbard_Rob/Commando.sid",
    "/MUSICIANS/G/Galway_Martin/Arkanoid.sid",
    "/MUSICIANS/J/Jammer/Antipop_2SID.sid",
    "/DEMOS/A-F/Devils_ReSIDence_3SID.sid",
};
const char* const kFirstFolder = "/MUSICIANS/B/Bayliss_Richard";    // SPEC-240's two folders
const char* const kSecondFolder = "/MUSICIANS/S";
const char* const kQueries[2] = {"hub 1985", "galway ark"};          // SPEC-260's key script
const char* const kCommando = "/MUSICIANS/H/Hubbard_Rob/Commando.sid";

// "12.35": a figure with `places` decimals; 0 for one that is not a number, or below 0.
std::string decimal(double value, int places);

}  // namespace selftest

using namespace selftest;

struct Composition::SelfTest : ui::SelfTestView {
    explicit SelfTest(Composition* owner) : parts(owner) {}

    // ui::SelfTestView
    std::vector<std::string> lines() const override { return shown; }
    std::string place() const override { return key_step ? "key-step" : "selftest"; }

    Composition* parts;
    std::string step;                 // what the request asks for: "none", "keys", "engines", "benchmark"
    bool recording = false;           // the request file is there: measurements go to the results file
    std::string results;
    Millis benchmark_ms = kBenchmarkMs;
    std::mutex write_lock;
    std::vector<std::string> shown;   // the self-test screen's lines
    bool started = false;             // the step has begun
    bool browser_answered = false;    // SPEC-200's start time has been taken
    Millis made_at = 0;               // the clock when the composition was made

    // ---- the key step
    bool key_step = false;
    std::vector<std::string> asked;
    std::map<std::string, bool> answers;   // each name answered, and whether its last answer was right
    std::size_t next_key = 0;
    std::vector<std::string> wrong;

    // ---- a set-up of HVSC, for hvsc_setup_s
    Millis setup_checked = 0;
    Millis setup_began = -1;

    // ---- the benchmark
    enum class Phase { Off, Fetching, Playing, Indexing, Done };
    Phase phase = Phase::Off;
    std::vector<Entry> set;           // the tunes of the set that are on the device, in the set's order
    std::vector<int> set_number;      // each one's N in SPEC-242's table
    // fetching
    std::size_t fetch_at = 0;
    int fetch_tries = 0;
    bool fetch_asked = false;
    std::vector<std::string> fetch_failed;
    // playing
    Millis began = 0;
    std::uint64_t underruns_before = 0;
    double cpu_before = 0;
    std::size_t playing = 0;          // the tune of `set` that plays
    Millis tune_began = -1;
    bool tune_sounding = false;
    std::int64_t render_ns_before = 0;
    Millis key_to_sound_max = -1;
    std::array<double, 5> render_ms{}, played_ms{};
    std::array<std::string, 5> engine_of;
    // the key script
    struct Outcome {
        bool done;
        Millis then;      // how long to wait before the next step, once this one is done
    };
    using Action = std::function<Outcome(Millis now)>;
    std::deque<Action> script;
    Millis script_next = 0;
    Millis round_began = -1;
    int rounds = 0;
    std::set<std::string> listed_before;
    Millis folder_first_max = -1, folder_again_max = -1, detail_max = -1, search_max = -1;
    double rows_per_second_min = -1;
    Millis row_pause_max = -1;
    int held_code = 0;
    Millis moved_at = 0;              // when the key script last moved the highlight
    Millis script_now = 0;            // the time of the step the key script is at
    // indexing
    Millis indexing_began = 0;
    Millis shown_percent = -1;

    // ---- writing --------------------------------------------------------------------------------------
    void write(const std::string& key, const std::string& value);

    // The measurements that belong to no step, as Metrics holds them now (SPEC-260: at every exit, and here
    // at the start, so that a device script that looks before the app has left finds them).
    void write_unstepped();
    void say(std::vector<std::string> lines);
    void done(const std::string& what, std::vector<std::string> lines);
    void open_screen();

    // ---- the key step -------------------------------------------------------------------------------------
    void key_lines();
    void start_keys();
    void read_answers();
    bool next_unanswered();
    void keys_done();
    void record_key(const KeyEvent& event);

    // ---- the engines ------------------------------------------------------------------------------------------
    void start_engines();

    // ---- the benchmark: fetching the set ---------------------------------------------------------------------
    void start_benchmark();
    void add_to_set(int n);
    void fetch_lines();

    // One tick while the set is fetched: each tune is played once through the player, which fetches it as
    // SPEC-141 does any tune, and stopped as soon as it sounds.
    void fetch_tick();

    // ---- the benchmark: playing the set ----------------------------------------------------------------------
    std::int64_t render_ns() const { return parts->sound_ ? parts->sound_->render_ns() : 0; }

    void start_playing();
    void play(std::size_t index, Millis now);

    // The tune that played has played as long as it plays: what it rendered is added to its figures.
    void tune_over(Millis now);
    void play_tick(Millis now);

    // ---- the key script --------------------------------------------------------------------------------------
    App* app() const { return parts->app(); }
    BrowserModel* browser() const { return parts->scope_->browser.get(); }

    void press(int code);
    void press(Key key) { press(key_code(key)); }

    void then(Action action) { script.push_back(std::move(action)); }
    void then_press(Key key, Millis gap = kKeyGapMs);

    // Waits until `ready` holds, then gives how long it took to `took`. Gives up, and drops the rest of the
    // round, after kWaitMostMs.
    void then_wait(std::function<bool()> ready, std::function<void(Millis)> took = nullptr);
    bool listed(const std::string& folder) const;
    int row_named(const std::string& name) const;

    // Moves the highlight to the row named `name`, a key at a time; the round ends if there is none.
    void then_highlight(const std::string& name, bool rest = false);

    // Opens the folder `path` from the root, as the keys would: each part highlighted, then Enter. The last
    // one's first screenful is timed (SPEC-240).
    void then_open(const std::string& path, bool timed);
    void then_parent(const std::string& to);

    // SPEC-240: Down held for 10 s; the highlight's moves give the rows a second and the longest pause.
    void then_hold();

    struct Hold {                    // Down held for SPEC-240's figures
        Millis pressed = -1;
        int last_row = -1;
        std::vector<Millis> moves;
        bool at_end = false;
    };
    void hold_figures(const Hold& hold, Millis now);
    void release_held();

    // SPEC-240: the detail line, from when the highlight comes to rest on a tune until it shows.
    void then_rest_on(const std::string& name);
    void then_rest_on_tunes();
    void then_type(const std::string& text);

    // SPEC-240: a search has been answered when its results, or "No results", are shown.
    bool search_answered();
    void then_query(const std::string& query, bool first);

    // One round of the key script (SPEC-260).
    void plan_round();
    void script_tick(Millis now);

    // ---- the benchmark: its end --------------------------------------------------------------------------
    void end_playing(Millis now);
    void finish();

    // SPEC-200's start time when the browser first answers; then the benchmark, once the player has had its
    // entry back (play_at_start(), which the first listing starts on the quick worker): it goes through the
    // quick worker after it.
    void after_first_listing();

    // ---- what changes by itself --------------------------------------------------------------------------
    void tick();

    // SPEC-240: setting HVSC up, from the job's start (Enter on the notice) to its end with a listing.
    void watch_setup(Millis now);
    Jobs& p_jobs() { return parts->jobs_; }

    // SPEC-260: the job mark while the benchmark plays.
    bool marking() const { return phase == Phase::Playing || phase == Phase::Indexing; }
    Millis share_done() const;
};

}  // namespace spz::app
